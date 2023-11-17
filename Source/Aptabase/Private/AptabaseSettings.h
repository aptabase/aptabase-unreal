#pragma once

#include <Engine/DeveloperSettings.h>

#include "AptabaseSettings.generated.h"

/**
 * @brief Possible hosts for ingesting data. Some might require additional data (e.g.: SH - self hosted).
 */
UENUM()
enum class EAptabaseHost : uint8
{
	EU,
	US,
	DEV,
	SH
};


/**
 * Holds configuration for integrating the Aptabase Analytics tracker
 */
UCLASS(defaultconfig, Config = Aptabase)
class APTABASE_API UAptabaseSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	/**
	 * @brief Returns the base Url for all requests we will be sending out based on the Host
	 */
	FString GetApiUrl() const;
	/**
	 * @brief Key used to identify your app when making requests
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Aptabase Analytics")
	FString AppKey = TEXT("");
	/**
	 * @brief Automatically determined based on the AppKey. Will decide the
	 */
	UPROPERTY(Config, VisibleAnywhere, Category = "Aptabase Analytics")
	EAptabaseHost Host;
	/**
	 * @brief Url to be used for self-hosted instances (Host = SH)
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Aptabase Analytics", meta = (EditCondition = "Host == EAptabaseHost::SH", EditConditionHides))
	FString CustomHost;
	/**
	 * @brief How often the analytics provider will send the currently batched events to the backend
	 * @note in seconds
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Aptabase Analytics", meta = (Unit = "s"))
	float SendInterval = 60.0f;
	/**
	 * @brief **DEBUG MODE**: How often the analytics provider will send the currently batched events to the backend
	 * @note in seconds
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Aptabase Analytics", meta = (Unit = "s"))
	float DebugSendInterval = 2.0f;

private:
	// Begin UDeveloperSettings interface
	virtual FName GetContainerName() const override;
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;
#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	// End UDeveloperSettings interface
};
