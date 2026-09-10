#include "AptabaseCrashReporter.h"

#include <CoreGlobals.h>
#include <HAL/PlatformProcess.h>
#include <HAL/PlatformTime.h>
#include <HttpModule.h>
#include <Interfaces/IHttpRequest.h>
#include <Misc/CoreDelegates.h>
#include <Modules/ModuleManager.h>
#include <Serialization/JsonSerializer.h>

FAptabaseCrashReporter::FAptabaseCrashReporter(const FAptabaseErrorContext& InContext) : Context(InContext)
{
	// Loading a module from inside the error handler is not safe, so make sure HTTP is loaded now.
	// The events path loads it lazily on the first flush, which may not have happened yet.
	FHttpModule::Get();
	// Both delegates can fire for the same failure; whichever comes first sends the report.
	SystemErrorHandle = FCoreDelegates::OnHandleSystemError.AddRaw(this, &FAptabaseCrashReporter::OnFatalError);
	ShutdownAfterErrorHandle = FCoreDelegates::OnShutdownAfterError.AddRaw(this, &FAptabaseCrashReporter::OnFatalError);
}

FAptabaseCrashReporter::~FAptabaseCrashReporter()
{
	FCoreDelegates::OnHandleSystemError.Remove(SystemErrorHandle);
	FCoreDelegates::OnShutdownAfterError.Remove(ShutdownAfterErrorHandle);
}

FAptabaseErrorReport FAptabaseCrashReporter::CreateReport(const TCHAR* ErrorHistory, const FAptabaseErrorContext& InContext)
{
	FString History = FString(ErrorHistory ? ErrorHistory : TEXT("")).Replace(TEXT("\r\n"), TEXT("\n")).TrimStartAndEnd();

	// The engine writes the description, a blank line, then the call stack. Fall back to the first
	// line that looks like a stack frame when the blank line is missing.
	FString Message = History;
	FString StackTrace;
	int32 Split = History.Find(TEXT("\n\n"));
	if (Split == INDEX_NONE)
	{
		static const TCHAR* const FrameMarkers[] = {TEXT("\n0x"), TEXT("\n[Callstack]")};
		for (const TCHAR* Marker : FrameMarkers)
		{
			const int32 MarkerIndex = History.Find(Marker);
			if (MarkerIndex != INDEX_NONE && (Split == INDEX_NONE || MarkerIndex < Split))
			{
				Split = MarkerIndex;
			}
		}
	}
	if (Split != INDEX_NONE)
	{
		Message = History.Left(Split).TrimEnd();
		StackTrace = History.Mid(Split).TrimStart();
	}

	// "Fatal error: [File:...] [Line: 12] \nMessage" -> type "FatalError", message "[File:...] [Line: 12] \nMessage".
	FString ErrorType = TEXT("FatalError");
	int32 Colon = INDEX_NONE;
	if (Message.FindChar(TEXT(':'), Colon) && Colon > 0 && Colon <= 60)
	{
		const FString Heading = Message.Left(Colon);
		if (!Heading.Contains(TEXT("\n")) && !Heading.Contains(TEXT("[")))
		{
			TArray<FString> Words;
			Heading.ParseIntoArrayWS(Words);
			ErrorType.Reset();
			for (FString& Word : Words)
			{
				Word[0] = FChar::ToUpper(Word[0]);
				ErrorType += Word;
			}
			Message = Message.Mid(Colon + 1).TrimStart();
		}
	}
	if (Message.IsEmpty())
	{
		Message = TEXT("Fatal error without details");
	}

	return FAptabaseErrorReport::Create(Message, ErrorType, StackTrace, true, InContext);
}

void FAptabaseCrashReporter::OnFatalError()
{
	// OnShutdownAfterError can run before the error text exists; leave the flag clear so a later
	// delegate with details still reports.
	if (GErrorHist[0] == 0 || bReported.exchange(true))
	{
		return;
	}

	// Do not log, assert or touch the game thread from here: the engine is inside its error handler.
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("HTTP")))
	{
		return;
	}

	const FAptabaseErrorReport Report = CreateReport(GErrorHist, Context);
	FString Body;
	FJsonSerializer::Serialize(Report.ToJsonObject(), TJsonWriterFactory<>::Create(&Body));

	const FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Context.ApiUrl + TEXT("/api/v0/error"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("App-Key"), Context.AppKey);
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(Body);
	Request->SetTimeout(SendTimeoutSeconds);
	if (!Request->ProcessRequest())
	{
		return;
	}

	// Requests are processed on the HTTP thread, which keeps running while this thread waits.
	// The completion delegate would only run on the game thread, so poll the status instead.
	const double Deadline = FPlatformTime::Seconds() + SendTimeoutSeconds;
	while (Request->GetStatus() == EHttpRequestStatus::Processing && FPlatformTime::Seconds() < Deadline)
	{
		FPlatformProcess::Sleep(0.01f);
	}
	if (Request->GetStatus() == EHttpRequestStatus::Processing)
	{
		Request->CancelRequest();
	}
}
