#include "AptabaseErrorReport.h"

#include <Dom/JsonObject.h>

FAptabaseErrorReport FAptabaseErrorReport::Create(const FString& Message, const FString& ErrorType, const FString& StackTrace, bool bFatal, const FAptabaseErrorContext& Context)
{
	FAptabaseErrorReport Report;
	const FString NormalizedType = ErrorType.TrimStartAndEnd();
	Report.ErrorType = NormalizedType.IsEmpty() ? TEXT("Error") : NormalizedType.Left(100);
	Report.ErrorMessage = (FString(bFatal ? TEXT("Fatal ") : TEXT("")) + Report.ErrorType + TEXT(": ") + Message).Left(5000);
	Report.StackTrace = StackTrace.Left(10000);
	Report.Timestamp = FDateTime::UtcNow().ToIso8601();
	Report.Severity = bFatal ? TEXT("fatal") : TEXT("error");
	Report.Kind = bFatal ? TEXT("crash") : TEXT("handled");
	Report.Context = Context;
	Report.Context.SessionId = Context.SessionId.Left(100);
	Report.Context.OsName = Context.OsName.Left(30);
	Report.Context.OsVersion = Context.OsVersion.Left(100);
	Report.Context.AppVersion = Context.AppVersion.Left(50);
	Report.Context.SdkVersion = Context.SdkVersion.Left(40);
	return Report;
}

bool FAptabaseErrorReport::FromJsonObject(const FJsonObject& Json, FAptabaseErrorReport& OutReport)
{
	FAptabaseErrorReport Report;
	if (!Json.TryGetStringField(TEXT("errorType"), Report.ErrorType) || !Json.TryGetStringField(TEXT("errorMessage"), Report.ErrorMessage) ||
		!Json.TryGetStringField(TEXT("timestamp"), Report.Timestamp) || !Json.TryGetStringField(TEXT("severity"), Report.Severity) || !Json.TryGetStringField(TEXT("kind"), Report.Kind) ||
		Report.ErrorType.IsEmpty() || Report.Timestamp.IsEmpty())
	{
		return false;
	}
	Json.TryGetStringField(TEXT("stackTrace"), Report.StackTrace);
	Json.TryGetStringField(TEXT("sessionId"), Report.Context.SessionId);
	Json.TryGetStringField(TEXT("osName"), Report.Context.OsName);
	Json.TryGetStringField(TEXT("osVersion"), Report.Context.OsVersion);
	Json.TryGetStringField(TEXT("appVersion"), Report.Context.AppVersion);
	Json.TryGetStringField(TEXT("sdkVersion"), Report.Context.SdkVersion);
	Json.TryGetBoolField(TEXT("isDebug"), Report.Context.bIsDebug);
	Report.ErrorType = Report.ErrorType.Left(100);
	Report.ErrorMessage = Report.ErrorMessage.Left(5000);
	Report.StackTrace = Report.StackTrace.Left(10000);
	Report.Context.SessionId = Report.Context.SessionId.Left(100);
	Report.Context.OsName = Report.Context.OsName.Left(30);
	Report.Context.OsVersion = Report.Context.OsVersion.Left(100);
	Report.Context.AppVersion = Report.Context.AppVersion.Left(50);
	Report.Context.SdkVersion = Report.Context.SdkVersion.Left(40);
	OutReport = MoveTemp(Report);
	return true;
}

TSharedRef<FJsonObject> FAptabaseErrorReport::ToJsonObject() const
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("errorType"), ErrorType);
	Json->SetStringField(TEXT("errorMessage"), ErrorMessage);
	if (!StackTrace.IsEmpty())
	{
		Json->SetStringField(TEXT("stackTrace"), StackTrace);
	}
	Json->SetStringField(TEXT("timestamp"), Timestamp);
	Json->SetStringField(TEXT("platform"), TEXT("Unreal"));
	Json->SetStringField(TEXT("sessionId"), Context.SessionId);
	Json->SetStringField(TEXT("osName"), Context.OsName);
	Json->SetStringField(TEXT("osVersion"), Context.OsVersion);
	Json->SetStringField(TEXT("appVersion"), Context.AppVersion);
	Json->SetStringField(TEXT("sdkVersion"), Context.SdkVersion);
	Json->SetBoolField(TEXT("isDebug"), Context.bIsDebug);
	Json->SetStringField(TEXT("severity"), Severity);
	Json->SetStringField(TEXT("kind"), Kind);
	return Json;
}

uint64 FAptabaseErrorReport::GetFingerprint() const
{
	// FNV-1a keeps the deduplication set small even when reports carry large stacks.
	uint64 Hash = 14695981039346656037ull;
	for (const FString* Field : {&Kind, &ErrorType, &ErrorMessage, &StackTrace})
	{
		for (TCHAR Character : *Field)
		{
			Hash = (Hash ^ static_cast<uint64>(Character)) * 1099511628211ull;
		}
		Hash = (Hash ^ 0xffull) * 1099511628211ull;
	}
	return Hash;
}
