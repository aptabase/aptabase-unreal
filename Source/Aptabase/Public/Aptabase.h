#pragma once

#include <Interfaces/IAnalyticsProviderModule.h>

/**
 * @brief Implementation of the Aptabase analytics provider.
 */
class FAptabaseModule : public IAnalyticsProviderModule
{
public:
	// Being IAnalyticsProviderModule Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual TSharedPtr<IAnalyticsProvider> CreateAnalyticsProvider(const FAnalyticsProviderConfigurationDelegate& GetConfigValue) const override;
	// End IAnalyticsProviderModule Interface
	/**
	 * @brief Callback executed before the application is shutdown.
	 * @note We will clean up the analytics provider and end the session if it's left running.
	 */
	void OnApplicationShutdown();
	/**
	 * @brief Reference to the Aptabase provider instance
	 */
	TSharedPtr<IAnalyticsProvider> AnalyticsProvider;
};
