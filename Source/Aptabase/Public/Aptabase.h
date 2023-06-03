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
	 * @brief Reference to the Aptabase provider instance
	 */
	TSharedPtr<IAnalyticsProvider> AnalyticsProvider;
};
