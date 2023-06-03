#include "Aptabase.h"

#include <Modules/ModuleManager.h>

#include "AptabaseAnalyticsProvider.h"

TSharedPtr<IAnalyticsProvider> FAptabaseModule::CreateAnalyticsProvider(const FAnalyticsProviderConfigurationDelegate& GetConfigValue) const
{
	return AnalyticsProvider;
}

void FAptabaseModule::StartupModule()
{
	AnalyticsProvider = MakeShared<FAptabaseAnalyticsProvider>();
}

void FAptabaseModule::ShutdownModule()
{
	if (AnalyticsProvider.IsValid())
	{
		// TODO: Adjust the end session calls to be more in line with what other modules do: 1) end session on destruction & 2) end session if a new one is started
		// AnalyticsProvider->EndSession();
	}
}

IMPLEMENT_MODULE(FAptabaseModule, Aptabase);