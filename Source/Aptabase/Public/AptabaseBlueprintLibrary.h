#pragma once

#include <CoreMinimal.h>
#include <Kismet/BlueprintFunctionLibrary.h>

#include "AptabaseBlueprintLibrary.generated.h"

/** Aptabase-specific APIs available to both C++ and Blueprints. */
UCLASS()
class APTABASE_API UAptabaseBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Reports a handled error, or a fatal error when the application cannot recover.
	 * Requires an active Aptabase session. Call on the game thread.
	 * Supply the original stack trace if available; an empty stack is omitted.
	 */
	UFUNCTION(BlueprintCallable, Category = "Analytics|Aptabase", meta = (AdvancedDisplay = "StackTrace,bFatal"))
	static void TrackError(const FString& ErrorMessage, const FString& ErrorType = TEXT("Error"), const FString& StackTrace = TEXT(""), bool bFatal = false);
};
