#pragma once

#include <Misc/TVariant.h>

#include "ExtendedAnalyticsEventAttribute.generated.h"

/**
 * Struct to hold type specific data for analytics events.
 * @note similar to FAnalyticsEventAttribute::AttrTypeEnum leveraging the TVariant type
 * @note properties are not blueprint exposed because TVariant is not blueprint friendly
 */
USTRUCT(BlueprintType)
struct FExtendedAnalyticsEventAttribute
{
	GENERATED_BODY();

	/**
	 * Name of the Attribute
	 */
	FString Key;

	/**
	 * Value of the Attribute
	 * @note we can support more types but they need to be converted to the correct JSON type before sending
	 */
	TVariant<FString, double> Value;
};
