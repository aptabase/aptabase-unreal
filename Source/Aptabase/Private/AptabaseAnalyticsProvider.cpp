#include "AptabaseAnalyticsProvider.h"

#include <GeneralProjectSettings.h>
#include <HttpModule.h>
#include <Interfaces/IHttpResponse.h>
#include <Interfaces/IPluginManager.h>
#include <Kismet/GameplayStatics.h>
#include <Kismet/KismetInternationalizationLibrary.h>

#include "AptabaseData.h"
#include "AptabaseLog.h"
#include "AptabaseSettings.h"
#include "ExtendedAnalyticsEventAttribute.h"

namespace
{
	UGameInstance* GetCurrentGameInstance()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game)
			{
				if (UGameInstance* GameInstance = Context.OwningGameInstance)
				{
					return GameInstance;
				}
			}
		}

		return nullptr;
	}

	bool IsInReleaseMode()
	{
		// TODO: This should be something more extensible/customizable.
		//  Developers should be able to decide on the fly if they are running Debug/Release

		return UE_BUILD_SHIPPING;
	}
} // namespace

void FAptabaseAnalyticsProvider::RecordExtendedEvent(const FString& EventName, const TArray<FExtendedAnalyticsEventAttribute>& Attributes)
{
	RecordEventInternal(EventName, Attributes);
}

bool FAptabaseAnalyticsProvider::StartSession(const TArray<FAnalyticsEventAttribute>& Attributes)
{
	const UGameInstance* GameInstance = GetCurrentGameInstance();
	if (!ensure(GameInstance))
	{
		UE_LOG(LogAptabase, Warning, TEXT("Cannot start session: Invalid game instance."));
		return false;
	}

	const UAptabaseSettings* Settings = GetDefault<UAptabaseSettings>();
	const float SendInterval = IsInReleaseMode() ? Settings->SendInterval : Settings->DebugSendInterval;
	GameInstance->GetTimerManager().SetTimer(BatchEventTimerHandle, FTimerDelegate::CreateRaw(this, &FAptabaseAnalyticsProvider::FlushEvents), SendInterval, true);

	const int64 EpochInSeconds = FDateTime::UtcNow().ToUnixTimestamp();
	const int Random = FMath::RandRange(0, 99999999);
	const FString RandomString = FString::Printf(TEXT("%08d"), Random);
	SessionId = FString::Printf(TEXT("%lld%s"), EpochInSeconds, *RandomString);

	bHasActiveSession = true;
	return true;
}

void FAptabaseAnalyticsProvider::EndSession()
{
	if (BatchEventTimerHandle.IsValid())
	{
		if (const UGameInstance* GameInstance = GetCurrentGameInstance())
		{
			GameInstance->GetTimerManager().ClearTimer(BatchEventTimerHandle);
		}
	}

	// Send any leftover events if any before closing the active session
	FlushEvents();

	bHasActiveSession = false;
}

FString FAptabaseAnalyticsProvider::GetSessionID() const
{
	return SessionId;
}

bool FAptabaseAnalyticsProvider::SetSessionID(const FString& InSessionID)
{
	UE_LOG(LogAptabase, Log, TEXT("Aptabase automatically generates and manage sessions. Discarding session id set request."));
	return false;
}

void FAptabaseAnalyticsProvider::FlushEvents()
{
	UE_LOG(LogAptabase, Verbose, TEXT("Flushing %s batched events."), *LexToString(BatchedEvents.Num()));

	TArrayView<FAptabaseEventPayload> EventsToProcess = BatchedEvents;

	while (!EventsToProcess.IsEmpty())
	{
		constexpr int32 NumEventsPerRequest = 25;

		TArray<FAptabaseEventPayload> CurrentBatch;
		CurrentBatch.Append(EventsToProcess.Left(NumEventsPerRequest));

		EventsToProcess.RightChopInline(NumEventsPerRequest);
		SendEventsNow(CurrentBatch);
	}

	BatchedEvents.Empty();
}

void FAptabaseAnalyticsProvider::SetUserID(const FString& InUserID)
{
	UE_LOG(LogAptabase, Log, TEXT("Aptabase is a privacy-first solution and will NOT send the UserId to the backend. Discarding user id set request."));

	UserId = InUserID;
}

FString FAptabaseAnalyticsProvider::GetUserID() const
{
	return UserId;
}

void FAptabaseAnalyticsProvider::RecordEvent(const FString& EventName, const TArray<FAnalyticsEventAttribute>& Attributes)
{
	TArray<FExtendedAnalyticsEventAttribute> ExtendedAttributes;
	for (const FAnalyticsEventAttribute& Attribute : Attributes)
	{
		FExtendedAnalyticsEventAttribute& NewAttribute = ExtendedAttributes.Emplace_GetRef();
		NewAttribute.Key = Attribute.GetName();
		NewAttribute.Value.Set<FString>(Attribute.GetValue());
	}

	RecordEventInternal(EventName, ExtendedAttributes);
}

void FAptabaseAnalyticsProvider::RecordEventInternal(const FString& EventName, const TArray<FExtendedAnalyticsEventAttribute>& Attributes)
{
	if (!bHasActiveSession)
	{
		UE_LOG(LogAptabase, Warning, TEXT("No session is currently active. Discarding event."));
		return;
	}

	const TSharedPtr<IPlugin> AptabasePlugin = IPluginManager::Get().FindPlugin("Aptabase");

	FAptabaseEventPayload EventPayload;
	EventPayload.EventName = EventName;
	EventPayload.SessionId = SessionId;
	EventPayload.EventAttributes = Attributes;
	EventPayload.TimeStamp = FDateTime::UtcNow().ToIso8601();
	EventPayload.SystemProps.Locale = UKismetInternationalizationLibrary::GetCurrentLocale();
	EventPayload.SystemProps.AppVersion = GetDefault<UGeneralProjectSettings>()->ProjectVersion;
	EventPayload.SystemProps.SdkVersion = FString::Printf(TEXT("aptabase-unreal@%s"), *AptabasePlugin->GetDescriptor().VersionName);
	EventPayload.SystemProps.OsName = UGameplayStatics::GetPlatformName();
	EventPayload.SystemProps.OsVersion = FPlatformMisc::GetOSVersion();
	EventPayload.SystemProps.IsDebug = !IsInReleaseMode();

	UE_LOG(LogAptabase, Verbose, TEXT("Batching event (%s) for next flush."), *EventName);
	BatchedEvents.Emplace(EventPayload);
}

void FAptabaseAnalyticsProvider::SendEventsNow(const TArray<FAptabaseEventPayload>& EventPayloads)
{
	TArray<TSharedPtr<FJsonValue>> Events;

	UE_LOG(LogAptabase, VeryVerbose, TEXT("Sending batch containing:"));
	for (const FAptabaseEventPayload& EventPayload : EventPayloads)
	{
		UE_LOG(LogAptabase, VeryVerbose, TEXT("Event: %s"), *EventPayload.EventName);
		const TSharedPtr<FJsonObject>& JsonPayload = EventPayload.ToJsonObject();

		Events.Add(MakeShared<FJsonValueObject>(JsonPayload));
	}

	const UAptabaseSettings* Settings = GetDefault<UAptabaseSettings>();

	const FString RequestUrl = FString::Printf(TEXT("%s/api/v0/events"), *Settings->GetApiUrl());

	FString RequestJsonPayload;

	const TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&RequestJsonPayload, 0);
	FJsonSerializer::Serialize(Events, JsonWriter);

	const FHttpRequestRef HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetVerb("POST");
	HttpRequest->SetContentAsString(RequestJsonPayload);
	HttpRequest->SetHeader(TEXT("App-Key"), Settings->AppKey);
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetURL(RequestUrl);
	HttpRequest->OnProcessRequestComplete().BindRaw(this, &FAptabaseAnalyticsProvider::OnEventsRecoded, EventPayloads);
	HttpRequest->ProcessRequest();
}

void FAptabaseAnalyticsProvider::OnEventsRecoded(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, TArray<FAptabaseEventPayload> OriginalEvents)
{
	if (!bWasSuccessful)
	{
		UE_LOG(LogAptabase, Error, TEXT("Request to record the event was unsuccessful."));
		return;
	}

	const auto ResponseCode = Response->GetResponseCode();
	if (!EHttpResponseCodes::IsOk(ResponseCode))
	{
		UE_LOG(LogAptabase, Error, TEXT("Request to record the event received unexpected code: %s"), *LexToString(Response->GetResponseCode()));

		if (ResponseCode >= 400 && ResponseCode < 500)
		{
			UE_LOG(LogAptabase, Error, TEXT("Data was sent in the wrong format. Event will be skipped."))
		}
		else if (ResponseCode >= 500)
		{
			UE_LOG(LogAptabase, Error, TEXT("Server-side issue. Event will be re-queued."))
			BatchedEvents.Append(OriginalEvents);
		}

		return;
	}

	UE_LOG(LogAptabase, VeryVerbose, TEXT("Event recorded successfully."));
}
