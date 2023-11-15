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
#include "ExtendedAnalyticsEventAttribute.h"

void FAptabaseAnalyticsProvider::RecordExtendedEvent(const FString& EventName, const TArray<FExtendedAnalyticsEventAttribute>& Attributes)
{
	RecordEventInternal(EventName, Attributes);
}

bool FAptabaseAnalyticsProvider::StartSession(const TArray<FAnalyticsEventAttribute>& Attributes)
{
	SessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);

	bHasActiveSession = true;
	return true;
}

void FAptabaseAnalyticsProvider::EndSession()
{
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

	const TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();

	for (const FExtendedAnalyticsEventAttribute& Attribute : Attributes)
	{
		const auto& AttributeValue = Attribute.Value;

		if (AttributeValue.IsType<double>())
		{
			Props->SetField(Attribute.Key, MakeShared<FJsonValueNumber>(AttributeValue.Get<double>()));
		}
		else if (AttributeValue.IsType<float>())
		{
			Props->SetField(Attribute.Key, MakeShared<FJsonValueNumber>(AttributeValue.Get<float>()));
		}
		else
		{
			Props->SetField(Attribute.Key, MakeShared<FJsonValueString>(AttributeValue.Get<FString>()));
		}
	}

	const FString RequestUrl = FString::Printf(TEXT("%s/api/v0/event"), *Settings->GetApiUrl());

	FString RequestJsonPayload;
	const TSharedPtr<FJsonObject> Payload = FJsonObjectConverter::UStructToJsonObject(EventPayload);
	Payload->SetObjectField("props", Props);

	const TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&RequestJsonPayload, 0);
	FJsonSerializer::Serialize(Payload.ToSharedRef(), JsonWriter);

	UE_LOG(LogAptabase, Verbose, TEXT("Sending event: %s to %s Payload: %s"), *EventName, *RequestUrl, *RequestJsonPayload);

	const FHttpRequestRef HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetVerb("POST");
	HttpRequest->SetContentAsString(RequestJsonPayload);
	HttpRequest->SetHeader(TEXT("App-Key"), Settings->AppKey);
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
