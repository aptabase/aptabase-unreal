#include "AptabaseCrashReporter.h"

#include <CoreGlobals.h>
#include <Dom/JsonObject.h>
#include <HAL/FileManager.h>
#include <HAL/PlatformProcess.h>
#include <HAL/PlatformTime.h>
#include <HttpModule.h>
#include <Interfaces/IHttpRequest.h>
#include <Interfaces/IHttpResponse.h>
#include <Misc/CoreDelegates.h>
#include <Misc/FileHelper.h>
#include <Misc/OutputDeviceRedirector.h>
#include <Misc/Paths.h>
#include <Modules/ModuleManager.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>

namespace
{
	bool LooksLikeStackFrame(const FString& Text)
	{
		return Text.StartsWith(TEXT("0x")) || Text.StartsWith(TEXT("[Callstack]"));
	}
} // namespace

void FAptabaseCrashReporter::FLoggedMessageCapture::Serialize(const TCHAR* Data, ELogVerbosity::Type Verbosity, const FName& Category)
{
	// FMacErrorOutputDevice, FWindowsErrorOutputDevice and FUnixErrorOutputDevice all log the fatal
	// message this way on the first appError, before raising the exception that runs the crash handler.
	static const TCHAR Prefix[] = TEXT("appError called: ");
	constexpr int32 PrefixLength = UE_ARRAY_COUNT(Prefix) - 1;
	if (Verbosity != ELogVerbosity::Error || !Data || FCString::Strncmp(Data, Prefix, PrefixLength) != 0 || bCaptured.exchange(true))
	{
		return;
	}
	FCString::Strncpy(Message, Data + PrefixLength, UE_ARRAY_COUNT(Message));
}

FAptabaseCrashReporter::FAptabaseCrashReporter(const FAptabaseErrorContext& InContext, const FString& InPendingDirectory) : Context(InContext), PendingDirectory(InPendingDirectory)
{
	if (!PendingDirectory.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*PendingDirectory, true);
	}
	// Loading a module from inside the error handler is not safe, so make sure HTTP is loaded now.
	// The events path loads it lazily on the first flush, which may not have happened yet.
	FHttpModule::Get();
	if (GLog)
	{
		GLog->AddOutputDevice(&LoggedMessage);
	}
	// Both delegates can fire for the same failure; whichever comes first sends the report.
	SystemErrorHandle = FCoreDelegates::OnHandleSystemError.AddRaw(this, &FAptabaseCrashReporter::OnFatalError);
	ShutdownAfterErrorHandle = FCoreDelegates::OnShutdownAfterError.AddRaw(this, &FAptabaseCrashReporter::OnFatalError);
}

FAptabaseCrashReporter::~FAptabaseCrashReporter()
{
	FCoreDelegates::OnHandleSystemError.Remove(SystemErrorHandle);
	FCoreDelegates::OnShutdownAfterError.Remove(ShutdownAfterErrorHandle);
	if (GLog)
	{
		GLog->RemoveOutputDevice(&LoggedMessage);
	}
}

FAptabaseErrorReport FAptabaseCrashReporter::CreateReport(const TCHAR* ErrorHistory, const TCHAR* LoggedMessage, const TCHAR* ExceptionDescription, const FAptabaseErrorContext& InContext)
{
	FString History = FString(ErrorHistory ? ErrorHistory : TEXT("")).Replace(TEXT("\r\n"), TEXT("\n")).TrimStartAndEnd();

	FString Message;
	FString StackTrace;
	if (LooksLikeStackFrame(History))
	{
		StackTrace = History;
	}
	else
	{
		// The engine writes the description, a blank line, then the call stack. Fall back to the first
		// line that looks like a stack frame when the blank line is missing.
		Message = History;
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
	}
	if (Message.IsEmpty() && LoggedMessage && LoggedMessage[0] != 0)
	{
		Message = FString(LoggedMessage).Replace(TEXT("\r\n"), TEXT("\n")).TrimStartAndEnd();
	}
	if (Message.IsEmpty() && ExceptionDescription && ExceptionDescription[0] != 0)
	{
		Message = FString(ExceptionDescription).TrimStartAndEnd();
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

TArray<FAptabaseErrorReport> FAptabaseCrashReporter::LoadPendingReports(const FString& PendingDirectory)
{
	TArray<FAptabaseErrorReport> Reports;
	if (PendingDirectory.IsEmpty())
	{
		return Reports;
	}
	IFileManager& FileManager = IFileManager::Get();
	TArray<FString> FileNames;
	FileManager.FindFiles(FileNames, *PendingDirectory, TEXT("json"));
	// File names start with the capture time, so the lexical order is the chronological order.
	FileNames.Sort(
		[](const FString& A, const FString& B)
		{
			return A > B;
		}
	);
	for (int32 Index = 0; Index < FileNames.Num(); ++Index)
	{
		const FString Path = FPaths::Combine(PendingDirectory, FileNames[Index]);
		FString Body;
		TSharedPtr<FJsonObject> Json;
		FAptabaseErrorReport Report;
		if (Reports.Num() < MaxPersistedReports && FFileHelper::LoadFileToString(Body, *Path) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Json) && Json.IsValid() &&
			FAptabaseErrorReport::FromJsonObject(*Json, Report))
		{
			Report.PersistedPath = Path;
			Reports.Add(MoveTemp(Report));
		}
		else
		{
			FileManager.Delete(*Path, false, false, true);
		}
	}
	return Reports;
}

FString FAptabaseCrashReporter::Persist(const FString& Body) const
{
	if (PendingDirectory.IsEmpty())
	{
		return FString();
	}
	const FString Path = FPaths::Combine(PendingDirectory, FString::Printf(TEXT("%lld-%s.json"), FDateTime::UtcNow().ToUnixTimestamp(), *Context.SessionId));
	return FFileHelper::SaveStringToFile(Body, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ? Path : FString();
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
	const FAptabaseErrorReport Report = CreateReport(GErrorHist, LoggedMessage.Get(), GErrorExceptionDescription, Context);
	FString Body;
	FJsonSerializer::Serialize(Report.ToJsonObject(), TJsonWriterFactory<>::Create(&Body));
	const FString PersistedPath = Persist(Body);

#if PLATFORM_WINDOWS
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("HTTP")))
	{
		return;
	}

	const FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Context.ApiUrl + TEXT("/api/v0/error"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("App-Key"), Context.AppKey);
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(Body);
	Request->SetTimeout(SendTimeoutSeconds);
	// The game thread is suspended, so let the HTTP thread finish the request and update its status.
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
	if (!Request->ProcessRequest())
	{
		return;
	}

	// Requests are processed on the HTTP thread, which keeps running while this thread waits.
	const double Deadline = FPlatformTime::Seconds() + SendTimeoutSeconds;
	while (Request->GetStatus() == EHttpRequestStatus::Processing && FPlatformTime::Seconds() < Deadline)
	{
		FPlatformProcess::Sleep(0.01f);
	}
	if (Request->GetStatus() == EHttpRequestStatus::Processing)
	{
		Request->CancelRequest();
		return;
	}
	const FHttpResponsePtr Response = Request->GetResponse();
	if (!PersistedPath.IsEmpty() && Request->GetStatus() == EHttpRequestStatus::Succeeded && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
	{
		// Delivered now; the next launch must not send it again.
		IFileManager::Get().Delete(*PersistedPath, false, false, true);
	}
#endif
}
