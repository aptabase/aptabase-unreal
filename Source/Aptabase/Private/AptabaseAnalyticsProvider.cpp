#include "AptabaseAnalyticsProvider.h"

#include <GeneralProjectSettings.h>
#include <HttpModule.h>
#include <Interfaces/IHttpResponse.h>
#include <Interfaces/IPluginManager.h>
#include <JsonObjectConverter.h>
#include <Kismet/GameplayStatics.h>
#include <Kismet/KismetInternationalizationLibrary.h>

#include "AptabaseData.h"
#include "AptabaseLog.h"
#include "AptabaseSettings.h"

bool FAptabaseAnalyticsProvider::StartSession(const TArray<FAnalyticsEventAttribute>& Attributes)
{
	if (bHasActiveSession)
	{
		UE_LOG(LogAptabase, Warning, TEXT("Session start requested while a session was running. Please manually end the session before starting a new one."));
		EndSession();
	}

	bHasActiveSession = true;
	RecordEvent(TEXT("SessionStart"), Attributes);
	return true;
}

void FAptabaseAnalyticsProvider::EndSession()
{
	if (!bHasActiveSession)
	{
		UE_LOG(LogAptabase, Log, TEXT("No session is currently active. Discarding session end request."));
		return;
	}

	RecordEvent(TEXT("SessionEnd"), {});

	bHasActiveSession = false;
}

FString FAptabaseAnalyticsProvider::GetSessionID() const
{
	return SessionId;
}

bool FAptabaseAnalyticsProvider::SetSessionID(const FString& InSessionID)
{
	if (bHasActiveSession)
	{
		UE_LOG(LogAptabase, Warning, TEXT("Cannot change the Session Id while session is running. Discarding session id set request."));
		return false;
	}

	SessionId = InSessionID;
	return true;
}

void FAptabaseAnalyticsProvider::FlushEvents()
{
	UE_LOG(LogAptabase, Log, TEXT("Aptabase implementation doesn't cache events"));
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
	const UAptabaseSettings* Settings = GetDefault<UAptabaseSettings>();
	const TSharedPtr<IPlugin> AptabasePlugin = IPluginManager::Get().FindPlugin("Aptabase");

	FAptabaseEventPayload EventPayload;
	EventPayload.EventName = EventName;
	EventPayload.SessionId = SessionId;
	EventPayload.TimeStamp = FDateTime::UtcNow().ToIso8601();
	EventPayload.SystemProps.Locale = UKismetInternationalizationLibrary::GetCurrentLocale();
	EventPayload.SystemProps.AppVersion = GetDefault<UGeneralProjectSettings>()->ProjectVersion;
	EventPayload.SystemProps.SdkVersion = FString::Printf(TEXT("aptabase-unreal@%s"), *AptabasePlugin->GetDescriptor().VersionName);
	EventPayload.SystemProps.OsName = UGameplayStatics::GetPlatformName();
	EventPayload.SystemProps.OsVersion = FPlatformMisc::GetOSVersion();
	EventPayload.SystemProps.IsDebug = !UE_BUILD_SHIPPING;

	for (const FAnalyticsEventAttribute& Attribute : Attributes)
	{
		EventPayload.Props.Emplace(Attribute.GetName(), Attribute.GetValue());
	}

	const FString RequestUrl = FString::Printf(TEXT("%s/api/v0/event"), *Settings->GetApiUrl());

	FString RequestJsonPayload;
	FJsonObjectConverter::UStructToJsonObjectString(EventPayload, RequestJsonPayload);

	UE_LOG(LogAptabase, Verbose, TEXT("Sending event: %s to %s Payload: %s"), *EventName, *RequestUrl, *RequestJsonPayload);

	const TSharedRef<IHttpRequest> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetVerb("POST");
	HttpRequest->SetContentAsString(RequestJsonPayload);
	HttpRequest->SetHeader(TEXT("App-Key"), Settings->ApiKey);
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetURL(RequestUrl);
	HttpRequest->OnProcessRequestComplete().BindRaw(this, &FAptabaseAnalyticsProvider::OnEventRecoded);
	HttpRequest->ProcessRequest();
}

void FAptabaseAnalyticsProvider::OnEventRecoded(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (!bWasSuccessful)
	{
		UE_LOG(LogAptabase, Error, TEXT("Request to record the event was unsuccessful."));
		return;
	}

	if (!EHttpResponseCodes::IsOk(Response->GetResponseCode()))
	{
		UE_LOG(LogAptabase, Error, TEXT("Request to record the event received unexpected code: %s"), *LexToString(Response->GetResponseCode()));
		return;
	}

	UE_LOG(LogAptabase, Verbose, TEXT("Event recorded successfully."));
}