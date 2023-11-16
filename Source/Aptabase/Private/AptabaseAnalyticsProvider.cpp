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

		return !UE_BUILD_SHIPPING;
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
	if (Settings->bBatchEvents)
	{
		const float SendInterval = IsInReleaseMode() ? Settings->SendInterval : Settings->DebugSendInterval;
		GameInstance->GetTimerManager().SetTimer(BatchEventTimerHandle, FTimerDelegate::CreateRaw(this, &FAptabaseAnalyticsProvider::FlushEvents), SendInterval, true);
	}

	SessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);

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

	for (const auto& Event : BatchedEvents)
	{
		SendEventNow(Event);
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

	const UAptabaseSettings* Settings = GetDefault<UAptabaseSettings>();
	if (Settings->bBatchEvents)
	{
		UE_LOG(LogAptabase, Verbose, TEXT("Batch event (%s) for later."), *EventName);
		BatchedEvents.Emplace(EventPayload);
	}
	else
	{
		UE_LOG(LogAptabase, Verbose, TEXT("Sending event (%s) now."), *EventName);
		SendEventNow(EventPayload);
	}
}

void FAptabaseAnalyticsProvider::SendEventNow(const FAptabaseEventPayload& EventPayload)
{
	const UAptabaseSettings* Settings = GetDefault<UAptabaseSettings>();

	const FString RequestUrl = FString::Printf(TEXT("%s/api/v0/event"), *Settings->GetApiUrl());

	FString RequestJsonPayload;
	const TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&RequestJsonPayload, 0);
	FJsonSerializer::Serialize(EventPayload.ToJsonObject(), JsonWriter);

	UE_LOG(LogAptabase, VeryVerbose, TEXT("Sending event: %s to %s Payload: %s"), *EventPayload.EventName, *RequestUrl, *RequestJsonPayload);

	const FHttpRequestRef HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetVerb("POST");
	HttpRequest->SetContentAsString(RequestJsonPayload);
	HttpRequest->SetHeader(TEXT("App-Key"), Settings->AppKey);
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetURL(RequestUrl);
	HttpRequest->OnProcessRequestComplete().BindRaw(this, &FAptabaseAnalyticsProvider::OnEventRecoded, EventPayload);
	HttpRequest->ProcessRequest();
}

void FAptabaseAnalyticsProvider::OnEventRecoded(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, FAptabaseEventPayload OriginalEvent)
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
			BatchedEvents.Emplace(OriginalEvent);
		}

		return;
	}

	UE_LOG(LogAptabase, VeryVerbose, TEXT("Event recorded successfully."));
}
