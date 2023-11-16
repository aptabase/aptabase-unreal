#include "Aptabase.h"

#include <Editor.h>
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

#ifdef WITH_EDITOR
	FEditorDelegates::EndPIE.AddRaw(this, &FAptabaseModule::OnEndPIE);
#endif
}

void FAptabaseModule::ShutdownModule()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication& Application = FSlateApplication::Get();
		Application.OnPreShutdown().RemoveAll(this);
	}

#ifdef WITH_EDITOR
	FEditorDelegates::EndPIE.RemoveAll(this);
#endif
}

void FAptabaseModule::OnApplicationShutdown()
{
	if (AnalyticsProvider.IsValid())
	{
		AnalyticsProvider->EndSession();
	}

	AnalyticsProvider.Reset();
}

#ifdef WITH_EDITOR
void FAptabaseModule::OnEndPIE(bool bIsSimulating)
{
	if (AnalyticsProvider.IsValid())
	{
		AnalyticsProvider->EndSession();
	}
}
#endif

IMPLEMENT_MODULE(FAptabaseModule, Aptabase);