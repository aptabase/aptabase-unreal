#include "AptabaseErrorReport.h"

#include <Dom/JsonObject.h>

FAptabaseErrorReport FAptabaseErrorReport::Create(const FString& ErrorType, const FString& Message, const FString& StackTrace, bool bFatal, bool bAutomatic, const FAptabaseErrorContext& Context)
{
	FAptabaseErrorReport Report;
	const FString NormalizedType = ErrorType.TrimStartAndEnd();
	Report.ErrorType = NormalizedType.IsEmpty() ? TEXT("Error") : NormalizedType.Left(100);
	Report.ErrorMessage = (FString(bFatal ? TEXT("Fatal ") : TEXT("")) + Report.ErrorType + TEXT(": ") + Message).Left(5000);
	Report.StackTrace = StackTrace.Left(10000);
	Report.Timestamp = FDateTime::UtcNow().ToIso8601();
	Report.Severity = bFatal ? TEXT("fatal") : TEXT("error");
	Report.Kind = bFatal ? TEXT("crash") : (bAutomatic ? TEXT("unhandled") : TEXT("handled"));
	Report.Context = Context;
	Report.Context.SessionId = Context.SessionId.Left(100);
	Report.Context.OsName = Context.OsName.Left(30);
	Report.Context.OsVersion = Context.OsVersion.Left(100);
	Report.Context.AppVersion = Context.AppVersion.Left(50);
	Report.Context.SdkVersion = Context.SdkVersion.Left(40);
	return Report;
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
