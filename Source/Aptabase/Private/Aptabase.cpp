#include "Aptabase.h"

#include <Framework/Application/SlateApplication.h>
#include <Modules/ModuleManager.h>

#include "AptabaseAnalyticsProvider.h"

TSharedPtr<IAnalyticsProvider> FAptabaseModule::CreateAnalyticsProvider(const FAnalyticsProviderConfigurationDelegate& GetConfigValue) const
{
	return AnalyticsProvider;
}

void FAptabaseModule::StartupModule()
{
	AnalyticsProvider = MakeShared<FAptabaseAnalyticsProvider>();

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication& Application = FSlateApplication::Get();
		Application.OnPreShutdown().AddRaw(this, &FAptabaseModule::OnApplicationShutdown);
	}
}

void FAptabaseModule::ShutdownModule()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication& Application = FSlateApplication::Get();
		Application.OnPreShutdown().RemoveAll(this);
	}
}

void FAptabaseModule::OnApplicationShutdown()
{
	if (AnalyticsProvider.IsValid())
	{
		AnalyticsProvider->EndSession();
	}

	AnalyticsProvider.Reset();
}

IMPLEMENT_MODULE(FAptabaseModule, Aptabase);