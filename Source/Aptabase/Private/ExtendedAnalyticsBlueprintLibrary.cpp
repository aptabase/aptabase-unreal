#include "ExtendedAnalyticsBlueprintLibrary.h"

#include <Analytics.h>
#include <Interfaces/IAnalyticsProvider.h>

#include "AptabaseAnalyticsProvider.h"
#include "AptabaseLog.h"
#include "ExtendedAnalyticsEventAttribute.h"

FExtendedAnalyticsEventAttribute UExtendedAnalyticsBlueprintLibrary::MakeExtendedAnalyticsEventStringAttribute(const FString& Name, const FString& Value)
{
	FExtendedAnalyticsEventAttribute Attribute;
	Attribute.Key = Name;
	Attribute.Value.Set<FString>(Value);
	return Attribute;
}

FExtendedAnalyticsEventAttribute UExtendedAnalyticsBlueprintLibrary::MakeExtendedAnalyticsEventNumberAttribute(const FString& Name, const float Value)
{
	FExtendedAnalyticsEventAttribute Attribute;
	Attribute.Key = Name;
	Attribute.Value.Set<float>(Value);
	return Attribute;
}

void UExtendedAnalyticsBlueprintLibrary::RecordEventWithAttributes(const FString& EventName, const TArray<FExtendedAnalyticsEventAttribute>& Attributes)
{
	const TSharedPtr<IAnalyticsProvider> Provider = FAnalytics::Get().GetDefaultConfiguredProvider();
	if (!Provider.IsValid())
	{
		UE_LOG(LogAptabase, Warning, TEXT("RecordEventWithAttributes: Failed to get the default analytics provider. Double check your [Analytics] configuration in your INI"));
		return;
	}

	const TSharedPtr<FAptabaseAnalyticsProvider> AptabaseProvider = StaticCastSharedPtr<FAptabaseAnalyticsProvider>(Provider);
	if (!AptabaseProvider.IsValid())
	{
		UE_LOG(LogAptabase, Warning, TEXT("RecordEventWithAttributes: Attributes of type FExtendedAnalyticsEventAttribute only works with Aptabase analytics"));
		return;
	}

	AptabaseProvider->RecordExtendedEvent(EventName, Attributes);
}