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

	/**
	 * Rebuilds a report from a body produced by ToJsonObject, for reports persisted by the crash
	 * reporter. Credentials are not part of the body; the caller supplies the current host and key.
	 */
	static bool FromJsonObject(const FJsonObject& Json, FAptabaseErrorReport& OutReport);

	FString ErrorType;
	FString ErrorMessage;
	FString StackTrace;
	FString Timestamp;
	FString Severity;
	FString Kind;
	FAptabaseErrorContext Context;
	/** File this report was loaded from, deleted once the report is delivered or rejected. Not serialized. */
	FString PersistedPath;

	TSharedRef<FJsonObject> ToJsonObject() const;
	uint64 GetFingerprint() const;
};
