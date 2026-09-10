#pragma once

#include <CoreMinimal.h>

class FJsonObject;

/** Context copied on the game thread at session start; safe to use from any thread, including the error handler. */
struct FAptabaseErrorContext
{
	FString SessionId;
	FString OsName;
	FString OsVersion;
	FString AppVersion;
	FString SdkVersion;
	bool bIsDebug = false;
	FString ApiUrl;
	FString AppKey;
};

/** One immutable POST /api/v0/error body, enriched and limited at capture time. */
struct FAptabaseErrorReport
{
	static FAptabaseErrorReport Create(const FString& Message, const FString& ErrorType, const FString& StackTrace, bool bFatal, const FAptabaseErrorContext& Context);

	FString ErrorType;
	FString ErrorMessage;
	FString StackTrace;
	FString Timestamp;
	FString Severity;
	FString Kind;
	FAptabaseErrorContext Context;

	TSharedRef<FJsonObject> ToJsonObject() const;
	uint64 GetFingerprint() const;
};
