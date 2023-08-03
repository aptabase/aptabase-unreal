#pragma once

#include <CoreMinimal.h>
#include <Kismet/BlueprintFunctionLibrary.h>

#include "ExtendedAnalyticsEventAttribute.h"

#include "ExtendedAnalyticsBlueprintLibrary.generated.h"

/**
 * A C++ and Blueprint accessible library of utility functions for extended analytics tracking
 */
UCLASS()
class APTABASE_API UExtendedAnalyticsBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Creates and ExtendedAnalyticsEventAttribute with a name and a string value
	 */
	UFUNCTION(BlueprintPure, Category = "Analytics")
	static FExtendedAnalyticsEventAttribute MakeExtendedAnalyticsEventStringAttribute(const FString& Name, const FString& Value);
	/**
	 * Creates and ExtendedAnalyticsEventAttribute with a name and a number (double) value
	 */
	UFUNCTION(BlueprintPure, Category = "Analytics")
	static FExtendedAnalyticsEventAttribute MakeExtendedAnalyticsEventNumberAttribute(const FString& Name, const float Value);
	/**
	 * Records an event has happened by name with an array of ExtendedAttributes (preserve native type)
	 */
	UFUNCTION(BlueprintCallable, Category = "Analytics")
	static void RecordEventWithAttributes(const FString& EventName, const TArray<FExtendedAnalyticsEventAttribute>& Attributes);
};
