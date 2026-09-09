#include "AptabaseAnalyticsProvider.h"

#include <Dom/JsonValue.h>
#include <Engine/Engine.h>
#include <Engine/GameInstance.h>
#include <GeneralProjectSettings.h>
#include <HttpModule.h>
#include <Interfaces/IHttpResponse.h>
#include <Interfaces/IPluginManager.h>
#include <Kismet/GameplayStatics.h>
#include <Kismet/KismetInternationalizationLibrary.h>
#include <Serialization/JsonSerializer.h>
#include <TimerManager.h>

#include "AptabaseData.h"
#include "AptabaseErrorDispatcher.h"
#include "AptabaseErrorLog.h"
#include "AptabaseLog.h"
#include "AptabaseSettings.h"
#include "ExtendedAnalyticsEventAttribute.h"

namespace
{
	UGameInstance* GetCurrentGameInstance()
	{
		if (!GEngine)
		{
			return nullptr;
		}
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

FAptabaseAnalyticsProvider::FAptabaseAnalyticsProvider() : ErrorDispatcher(MakeShared<FAptabaseErrorDispatcher, ESPMode::ThreadSafe>())
{
}

FAptabaseAnalyticsProvider::~FAptabaseAnalyticsProvider()
{
	ErrorLog.Reset();
	if (const UGameInstance* GameInstance = GetCurrentGameInstance())
	{
		GameInstance->GetTimerManager().ClearTimer(BatchEventTimerHandle);
	}
	for (const FHttpRequestPtr& Request : EventRequests)
	{
		Request->OnProcessRequestComplete().Unbind();
		Request->CancelRequest();
	}
}

void FAptabaseAnalyticsProvider::TrackError(const FString& ErrorType, const FString& Message, const FString& StackTrace, bool bFatal)
{
	ErrorDispatcher->TrackError(ErrorType, Message, StackTrace, bFatal);
}

void FAptabaseAnalyticsProvider::RecordExtendedEvent(const FString& EventName, const TArray<FExtendedAnalyticsEventAttribute>& Attributes)
{
	RecordEventInternal(EventName, Attributes);
}

bool FAptabaseAnalyticsProvider::StartSession(const TArray<FAnalyticsEventAttribute>& Attributes)
{
	if (bHasActiveSession)
	{
		return true;
	}
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

	FAptabaseErrorContext ErrorContext;
	ErrorContext.SessionId = SessionId;
	ErrorContext.OsName = UGameplayStatics::GetPlatformName();
	ErrorContext.OsVersion = FPlatformMisc::GetOSVersion();
	ErrorContext.AppVersion = GetDefault<UGeneralProjectSettings>()->ProjectVersion;
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Aptabase"));
	ErrorContext.SdkVersion = FString::Printf(TEXT("aptabase-unreal@%s"), Plugin.IsValid() ? *Plugin->GetDescriptor().VersionName : TEXT("unknown"));
	ErrorContext.bIsDebug = !IsInReleaseMode();
	ErrorContext.ApiUrl = Settings->GetApiUrl();
	ErrorContext.ApiUrl.RemoveFromEnd(TEXT("/"));
	ErrorContext.AppKey = Settings->AppKey;
	ErrorDispatcher->StartSession(ErrorContext);
	if (Settings->bEnableErrorLogging)
	{
		ErrorLog = MakeUnique<FAptabaseErrorLog>(ErrorDispatcher.ToSharedRef());
	}
	return true;
}

void FAptabaseAnalyticsProvider::EndSession()
{
	ErrorLog.Reset();
	ErrorDispatcher->EndSession();
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

		EventsToProcess.RightChopInline(FMath::Min(NumEventsPerRequest, EventsToProcess.Num()));
		SendEventsNow(CurrentBatch);
	}

	BatchedEvents.Empty();
	ErrorDispatcher->Flush();
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
	EventRequests.Add(HttpRequest);
	if (!HttpRequest->ProcessRequest())
	{
		HttpRequest->OnProcessRequestComplete().Unbind();
		EventRequests.Remove(HttpRequest);
	}
}

void FAptabaseAnalyticsProvider::OnEventsRecoded(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, TArray<FAptabaseEventPayload> OriginalEvents)
{
	EventRequests.Remove(Request);
	if (!bWasSuccessful || !Response.IsValid())
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

void FAptabaseAnalyticsProvider::SetDefaultEventAttributes(TArray<FAnalyticsEventAttribute>&& Attributes)
{
	DefaultEventAttributes = MoveTemp(Attributes);
}

TArray<FAnalyticsEventAttribute> FAptabaseAnalyticsProvider::GetDefaultEventAttributesSafe() const
{
	return DefaultEventAttributes;
}

int32 FAptabaseAnalyticsProvider::GetDefaultEventAttributeCount() const
{
	return DefaultEventAttributes.Num();
}

FAnalyticsEventAttribute FAptabaseAnalyticsProvider::GetDefaultEventAttribute(int AttributeIndex) const
{
	if (DefaultEventAttributes.IsValidIndex(AttributeIndex))
	{
		return DefaultEventAttributes[AttributeIndex];
	}
	
	UE_LOG(LogAptabase, Warning, TEXT("Requested default event attribute index %d is out of bounds (count: %d)"), AttributeIndex, DefaultEventAttributes.Num());
	return FAnalyticsEventAttribute();
}
