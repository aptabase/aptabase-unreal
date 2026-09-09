#include "AptabaseBlueprintLibrary.h"

#include <Modules/ModuleManager.h>

#include "Aptabase.h"
#include "AptabaseAnalyticsProvider.h"

void UAptabaseBlueprintLibrary::TrackError(const FString& ErrorMessage, const FString& ErrorType, const FString& StackTrace, bool bFatal)
{
	// The default provider may be a Multicast provider or another SDK. Resolve our
	// own module's instance instead of casting an arbitrary IAnalyticsProvider.
	if (const FAptabaseModule* Module = FModuleManager::GetModulePtr<FAptabaseModule>(TEXT("Aptabase")))
	{
		if (Module->AnalyticsProvider.IsValid())
		{
			StaticCastSharedPtr<FAptabaseAnalyticsProvider>(Module->AnalyticsProvider)->TrackError(ErrorType, ErrorMessage, StackTrace, bFatal);
		}
	}
}
