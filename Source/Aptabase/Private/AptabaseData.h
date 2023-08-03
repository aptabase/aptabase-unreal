#pragma once

#include "AptabaseData.generated.h"

/**
 * @brief Properties of the user's system
 */
USTRUCT()
struct FAptabaseSystemProperties
{
	GENERATED_BODY()

	/**
	 * @brief Whether this event should show up inside the "Release" or "Debug" dashboard inside the Web view
	 */
	UPROPERTY()
	bool IsDebug = false;

	/**
	 * @brief Localization language code currently used by the user
	 */
	UPROPERTY()
	FString Locale;

	/**
	 * @brief Version of the project
	 * @note This is automatically grabbed from GeneralProjectSettings->ProjectVersion
	 */
	UPROPERTY()
	FString AppVersion;

	/**
	 * @brief Version of the plugin
	 * @note This is automatically grabbed from Aptabase's plugin file -> VersionName
	 */
	UPROPERTY()
	FString SdkVersion;

	/**
	 * @brief Name of the user's operating system
	 */
	UPROPERTY()
	FString OsName;

	/**
	 * @brief Version of the user's operating system
	 */
	UPROPERTY()
	FString OsVersion;
};

/**
 * @brief Payload for HTTP requests to record an event
 */
USTRUCT()
struct FAptabaseEventPayload
{
	GENERATED_BODY()

	/**
	 * @brief Time the event happened
	 */
	UPROPERTY()
	FString TimeStamp;

	/**
	 * @brief Id of the current session
	 */
	UPROPERTY()
	FString SessionId;

	/**
	 * @brief Name of the event
	 */
	UPROPERTY()
	FString EventName;

	/**
	 * @brief Information about the user's system
	 */
	UPROPERTY()
	FAptabaseSystemProperties SystemProps;
};
