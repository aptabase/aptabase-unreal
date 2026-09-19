#if WITH_DEV_AUTOMATION_TESTS

#include <Async/Async.h>
#include <Dom/JsonObject.h>
#include <HAL/FileManager.h>
#include <Misc/AutomationTest.h>
#include <Misc/FileHelper.h>
#include <Misc/Paths.h>
#include <Serialization/JsonSerializer.h>

#include "AptabaseCrashReporter.h"
#include "AptabaseErrorQueue.h"

namespace
{
	FAptabaseErrorContext TestContext()
	{
		FAptabaseErrorContext Context;
		Context.SessionId = TEXT("123456789012345678");
		Context.OsName = TEXT("Mac");
		Context.OsVersion = TEXT("15.0");
		Context.AppVersion = TEXT("1.2.3");
		Context.SdkVersion = TEXT("aptabase-unreal@0.3.0");
		Context.bIsDebug = true;
		Context.ApiUrl = TEXT("http://localhost:3000");
		Context.AppKey = TEXT("A-DEV-1234567890");
		return Context;
	}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAptabaseErrorPayloadTest, "Aptabase.Errors.Payload", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAptabaseErrorPayloadTest::RunTest(const FString& Parameters)
{
	const FAptabaseErrorReport Report = FAptabaseErrorReport::Create(TEXT("Disk full"), TEXT("SaveError"), TEXT("SaveGame\nMain"), false, TestContext());
	const auto Json = Report.ToJsonObject();
	TestEqual(TEXT("Error type"), Json->GetStringField(TEXT("errorType")), FString(TEXT("SaveError")));
	TestEqual(TEXT("Message"), Json->GetStringField(TEXT("errorMessage")), FString(TEXT("SaveError: Disk full")));
	TestEqual(TEXT("Original stack"), Json->GetStringField(TEXT("stackTrace")), FString(TEXT("SaveGame\nMain")));
	TestEqual(TEXT("SDK platform"), Json->GetStringField(TEXT("platform")), FString(TEXT("Unreal")));
	TestEqual(TEXT("Session"), Json->GetStringField(TEXT("sessionId")), TestContext().SessionId);
	TestEqual(TEXT("OS"), Json->GetStringField(TEXT("osName")), TestContext().OsName);
	TestEqual(TEXT("OS version"), Json->GetStringField(TEXT("osVersion")), TestContext().OsVersion);
	TestEqual(TEXT("App version"), Json->GetStringField(TEXT("appVersion")), TestContext().AppVersion);
	TestEqual(TEXT("SDK version"), Json->GetStringField(TEXT("sdkVersion")), TestContext().SdkVersion);
	TestTrue(TEXT("Debug flag"), Json->GetBoolField(TEXT("isDebug")));
	TestEqual(TEXT("Handled severity"), Json->GetStringField(TEXT("severity")), FString(TEXT("error")));
	TestEqual(TEXT("Handled kind"), Json->GetStringField(TEXT("kind")), FString(TEXT("handled")));
	TestFalse(TEXT("Credentials are not serialized"), Json->HasField(TEXT("appKey")));
	TestFalse(TEXT("No event properties envelope"), Json->HasField(TEXT("systemProps")));
	FDateTime Timestamp;
	TestTrue(TEXT("ISO timestamp"), FDateTime::ParseIso8601(*Json->GetStringField(TEXT("timestamp")), Timestamp));

	const auto Fatal = FAptabaseErrorReport::Create(TEXT("Missing assets"), TEXT("LoadError"), TEXT(""), true, TestContext()).ToJsonObject();
	TestEqual(TEXT("Fatal severity"), Fatal->GetStringField(TEXT("severity")), FString(TEXT("fatal")));
	TestEqual(TEXT("Fatal kind"), Fatal->GetStringField(TEXT("kind")), FString(TEXT("crash")));
	TestEqual(TEXT("Fatal prefix"), Fatal->GetStringField(TEXT("errorMessage")), FString(TEXT("Fatal LoadError: Missing assets")));
	TestFalse(TEXT("Missing stack omitted"), Fatal->HasField(TEXT("stackTrace")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAptabaseErrorLimitsTest, "Aptabase.Errors.FieldLimits", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAptabaseErrorLimitsTest::RunTest(const FString& Parameters)
{
	const FString LongValue = FString::ChrN(11000, TCHAR('x'));
	auto Context = TestContext();
	Context.SessionId = Context.OsName = Context.OsVersion = Context.AppVersion = Context.SdkVersion = LongValue;
	const auto Json = FAptabaseErrorReport::Create(LongValue, LongValue, LongValue, true, Context).ToJsonObject();
	TestEqual(TEXT("Message limit"), Json->GetStringField(TEXT("errorMessage")).Len(), 5000);
	TestEqual(TEXT("Type limit"), Json->GetStringField(TEXT("errorType")).Len(), 100);
	TestEqual(TEXT("Stack limit"), Json->GetStringField(TEXT("stackTrace")).Len(), 10000);
	TestEqual(TEXT("Session limit"), Json->GetStringField(TEXT("sessionId")).Len(), 100);
	TestEqual(TEXT("OS name limit"), Json->GetStringField(TEXT("osName")).Len(), 30);
	TestEqual(TEXT("OS version limit"), Json->GetStringField(TEXT("osVersion")).Len(), 100);
	TestEqual(TEXT("App version limit"), Json->GetStringField(TEXT("appVersion")).Len(), 50);
	TestEqual(TEXT("SDK version limit"), Json->GetStringField(TEXT("sdkVersion")).Len(), 40);
	const auto Empty = FAptabaseErrorReport::Create(TEXT(""), TEXT(" \t\n"), TEXT(""), false, Context);
	TestEqual(TEXT("Whitespace type fallback"), Empty.ErrorType, FString(TEXT("Error")));
	TestEqual(TEXT("Empty message is still a valid report"), Empty.ErrorMessage, FString(TEXT("Error: ")));
	const auto Padded = FAptabaseErrorReport::Create(TEXT("test"), FString::ChrN(101, TCHAR(' ')) + TEXT("Error"), TEXT(""), false, Context);
	TestEqual(TEXT("Trim before truncating so the type remains valid"), Padded.ErrorType, FString(TEXT("Error")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAptabaseErrorRetryTest, "Aptabase.Errors.Retry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAptabaseErrorRetryTest::RunTest(const FString& Parameters)
{
	for (int32 Code : {0, 408, 429, 500, 503})
	{
		FAptabaseErrorQueue Queue;
		Queue.StartSession(TestContext());
		Queue.Enqueue(TEXT("Offline"), TEXT("NetworkError"), TEXT("original stack"), false);
		const auto Batch = Queue.TakeBatch();
		if (!TestEqual(TEXT("One initial report"), Batch.Num(), 1))
		{
			return false;
		}
		Queue.Complete(Batch[0], Code != 0, Code);
		const auto Retry = Queue.TakeBatch();
		if (!TestEqual(FString::Printf(TEXT("HTTP %d retried"), Code), Retry.Num(), 1))
		{
			return false;
		}
		TestEqual(TEXT("Retry preserves timestamp"), Retry[0].Timestamp, Batch[0].Timestamp);
		TestEqual(TEXT("Retry preserves stack"), Retry[0].StackTrace, Batch[0].StackTrace);
		Queue.Complete(Retry[0], true, 202);
		TestEqual(TEXT("Delivered report removed"), Queue.TakeBatch().Num(), 0);
	}
	for (int32 Code : {200, 202, 204, 301, 400, 401, 403, 404, 422})
	{
		TestFalse(FString::Printf(TEXT("HTTP %d is terminal"), Code), FAptabaseErrorQueue::ShouldRetry(true, Code));
	}
	TestFalse(TEXT("403 is terminal even if transport reports failure"), FAptabaseErrorQueue::ShouldRetry(false, 403));
	TestTrue(TEXT("Failed transport retried"), FAptabaseErrorQueue::ShouldRetry(false, 200));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAptabaseErrorCapacityTest, "Aptabase.Errors.CapacityAndDeduplication", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAptabaseErrorCapacityTest::RunTest(const FString& Parameters)
{
	FAptabaseErrorQueue Queue;
	Queue.StartSession(TestContext());
	TestTrue(TEXT("First occurrence accepted"), Queue.Enqueue(TEXT("Repeated"), TEXT("Error"), TEXT("stack"), false));
	TestFalse(TEXT("Duplicate suppressed"), Queue.Enqueue(TEXT("Repeated"), TEXT("Error"), TEXT("stack"), false));
	TestTrue(TEXT("Fatal has separate identity"), Queue.Enqueue(TEXT("Repeated"), TEXT("Error"), TEXT("stack"), true));
	TestTrue(TEXT("Different type has separate identity"), Queue.Enqueue(TEXT("Repeated"), TEXT("OtherError"), TEXT("stack"), false));
	TestTrue(TEXT("Different stack has separate identity"), Queue.Enqueue(TEXT("Repeated"), TEXT("Error"), TEXT("other stack"), false));
	for (int32 Index = 4; Index < FAptabaseErrorQueue::MaxPendingReports; ++Index)
	{
		TestTrue(TEXT("Queue space available"), Queue.Enqueue(FString::FromInt(Index), TEXT("Error"), TEXT(""), false));
	}
	const auto Batch = Queue.TakeBatch();
	TestEqual(TEXT("Full batch"), Batch.Num(), 25);
	TestFalse(TEXT("In-flight reports count towards capacity"), Queue.Enqueue(TEXT("Overflow"), TEXT("Error"), TEXT(""), false));
	for (const auto& Report : Batch)
	{
		Queue.Complete(Report, false, 0);
	}
	const auto Retries = Queue.TakeBatch();
	TestEqual(TEXT("All original reports retained after failure"), Retries.Num(), 25);
	for (const auto& Report : Retries)
	{
		Queue.Complete(Report, true, 202);
	}
	TestTrue(TEXT("Overflow can be reported once capacity is available"), Queue.Enqueue(TEXT("Overflow"), TEXT("Error"), TEXT(""), false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAptabaseErrorSessionTest, "Aptabase.Errors.SessionLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAptabaseErrorSessionTest::RunTest(const FString& Parameters)
{
	FAptabaseErrorQueue Queue;
	TestFalse(TEXT("No capture before session"), Queue.Enqueue(TEXT("test"), TEXT("Error"), TEXT(""), false));
	Queue.StartSession(TestContext());
	for (int32 Index = 0; Index < FAptabaseErrorQueue::MaxUniqueErrorsPerSession; ++Index)
	{
		TestTrue(TEXT("Unique report accepted"), Queue.Enqueue(FString::FromInt(Index), TEXT("Error"), TEXT(""), false));
		const auto Batch = Queue.TakeBatch();
		if (Batch.Num() != 1)
		{
			return false;
		}
		Queue.Complete(Batch[0], true, 202);
	}
	TestFalse(TEXT("Session cap applies after delivery"), Queue.Enqueue(TEXT("Over cap"), TEXT("Error"), TEXT(""), false));
	Queue.EndSession();
	TestFalse(TEXT("No capture after session"), Queue.Enqueue(TEXT("Stopped"), TEXT("Error"), TEXT(""), false));
	Queue.StartSession(TestContext());
	TestTrue(TEXT("New session resets deduplication"), Queue.Enqueue(TEXT("0"), TEXT("Error"), TEXT(""), false));
	const auto OldBatch = Queue.TakeBatch();
	if (OldBatch.Num() != 1)
	{
		return false;
	}
	Queue.EndSession();
	auto NewContext = TestContext();
	NewContext.SessionId = TEXT("new session");
	NewContext.AppKey = TEXT("A-DEV-9999999999");
	NewContext.ApiUrl = TEXT("https://another-host.example");
	NewContext.AppVersion = TEXT("2.0");
	Queue.StartSession(NewContext);
	Queue.Complete(OldBatch[0], true, 429);
	const auto Retry = Queue.TakeBatch();
	if (!TestEqual(TEXT("Previous session report retried"), Retry.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Original session retained"), Retry[0].Context.SessionId, TestContext().SessionId);
	TestEqual(TEXT("Original app key retained"), Retry[0].Context.AppKey, TestContext().AppKey);
	TestEqual(TEXT("Original host retained"), Retry[0].Context.ApiUrl, TestContext().ApiUrl);
	TestEqual(TEXT("Original app version retained"), Retry[0].Context.AppVersion, TestContext().AppVersion);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAptabaseErrorThreadTest, "Aptabase.Errors.ConcurrentCapture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAptabaseErrorThreadTest::RunTest(const FString& Parameters)
{
	FAptabaseErrorQueue Queue;
	Queue.StartSession(TestContext());
	TArray<TFuture<void>> Workers;
	for (int32 Worker = 0; Worker < 4; ++Worker)
	{
		Workers.Add(Async(
			EAsyncExecution::Thread,
			[&Queue]()
			{
				for (int32 Index = 0; Index < 100; ++Index)
				{
					Queue.Enqueue(FString::FromInt(Index), TEXT("WorkerError"), TEXT(""), false);
				}
			}
		));
	}
	for (auto& Worker : Workers)
	{
		Worker.Wait();
	}
	const auto Batch = Queue.TakeBatch();
	TestEqual(TEXT("Concurrent capture is bounded"), Batch.Num(), 25);
	TSet<uint64> Fingerprints;
	for (const auto& Report : Batch)
	{
		Fingerprints.Add(Report.GetFingerprint());
	}
	TestEqual(TEXT("Concurrent duplicates suppressed"), Fingerprints.Num(), Batch.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAptabaseCrashReportTest, "Aptabase.Errors.CrashReport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAptabaseCrashReportTest::RunTest(const FString& Parameters)
{
	// Fatal log / assert as written to GErrorHist: heading, location, message, blank line, call stack.
	const auto Assert = FAptabaseCrashReporter::CreateReport(
		TEXT(
			"Assertion failed: Ptr != nullptr [File:D:/Game/Source/Player.cpp] [Line: 42] \r\nPlayer has no pawn\r\n\r\n0x00007ff6 Game.exe!APlayer::Tick()\r\n0x00007ff7 Game.exe!AActor::TickActor()"
		),
		TEXT(""),
		TEXT(""),
		TestContext()
	);
	TestEqual(TEXT("Assert type"), Assert.ErrorType, FString(TEXT("AssertionFailed")));
	TestEqual(TEXT("Assert message"), Assert.ErrorMessage, FString(TEXT("Fatal AssertionFailed: Ptr != nullptr [File:D:/Game/Source/Player.cpp] [Line: 42] \nPlayer has no pawn")));
	TestEqual(TEXT("Assert stack"), Assert.StackTrace, FString(TEXT("0x00007ff6 Game.exe!APlayer::Tick()\n0x00007ff7 Game.exe!AActor::TickActor()")));
	TestEqual(TEXT("Crash severity"), Assert.Severity, FString(TEXT("fatal")));
	TestEqual(TEXT("Crash kind"), Assert.Kind, FString(TEXT("crash")));
	TestEqual(TEXT("Session context"), Assert.Context.SessionId, TestContext().SessionId);

	const auto Fatal = FAptabaseCrashReporter::CreateReport(TEXT("Fatal error: [File:Main.cpp] [Line: 7] \nOut of memory\n\n[Callstack] 0x1 Game!Main()"), TEXT(""), TEXT(""), TestContext());
	TestEqual(TEXT("Fatal type"), Fatal.ErrorType, FString(TEXT("FatalError")));
	TestEqual(TEXT("Fatal message"), Fatal.ErrorMessage, FString(TEXT("Fatal FatalError: [File:Main.cpp] [Line: 7] \nOut of memory")));
	TestEqual(TEXT("Fatal stack"), Fatal.StackTrace, FString(TEXT("[Callstack] 0x1 Game!Main()")));

	// Crash description without the blank line still splits at the first frame.
	const auto Crash = FAptabaseCrashReporter::CreateReport(TEXT("Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0x0\n0x00007ff6 Game.exe!Foo()"), TEXT(""), TEXT(""), TestContext());
	TestEqual(TEXT("Exception type"), Crash.ErrorType, FString(TEXT("UnhandledException")));
	TestEqual(TEXT("Exception message"), Crash.ErrorMessage, FString(TEXT("Fatal UnhandledException: EXCEPTION_ACCESS_VIOLATION reading address 0x0")));
	TestEqual(TEXT("Exception stack"), Crash.StackTrace, FString(TEXT("0x00007ff6 Game.exe!Foo()")));

	// Unknown formats are reported verbatim under a generic type; a location-only heading is not a type.
	const auto Plain = FAptabaseCrashReporter::CreateReport(TEXT("[File:X.cpp] [Line: 1] something broke"), TEXT(""), TEXT(""), TestContext());
	TestEqual(TEXT("Generic type"), Plain.ErrorType, FString(TEXT("FatalError")));
	TestEqual(TEXT("Verbatim message"), Plain.ErrorMessage, FString(TEXT("Fatal FatalError: [File:X.cpp] [Line: 1] something broke")));
	TestTrue(TEXT("No stack"), Plain.StackTrace.IsEmpty());

	const auto Empty = FAptabaseCrashReporter::CreateReport(TEXT(""), TEXT(""), TEXT(""), TestContext());
	TestEqual(TEXT("Empty history still yields a valid report"), Empty.ErrorMessage, FString(TEXT("Fatal FatalError: Fatal error without details")));
	TestEqual(TEXT("Empty history keeps the generic type"), Empty.ErrorType, FString(TEXT("FatalError")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAptabaseCrashReportApplePlatformTest, "Aptabase.Errors.CrashReportApplePlatform", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAptabaseCrashReportApplePlatformTest::RunTest(const FString& Parameters)
{
	// On Apple platforms the crash context overwrites GErrorHist with the call stack before the error
	// handler runs; the description survives only as the message the error device logged.
	const TCHAR* Stack =
		TEXT("0x15c18e30 libUnrealEditor-Engine.dylib!FTimerUnifiedDelegate::Execute() const   [UnknownFile]) \n0x15c1c514 libUnrealEditor-Engine.dylib!FTimerManager::Tick(float)   [UnknownFile]) ");
	const auto Fatal = FAptabaseCrashReporter::CreateReport(
		Stack, TEXT("Fatal error: [File:Game.cpp] [Line: 77] \r\nIntentional test crash\r\n"), TEXT("SIGSEGV: invalid attempt to access memory at address 0x3"), TestContext()
	);
	TestEqual(TEXT("Type from logged message"), Fatal.ErrorType, FString(TEXT("FatalError")));
	TestEqual(TEXT("Message from logged message"), Fatal.ErrorMessage, FString(TEXT("Fatal FatalError: [File:Game.cpp] [Line: 77] \nIntentional test crash")));
	TestEqual(TEXT("Whole history is the stack"), Fatal.StackTrace, FString(Stack).TrimStartAndEnd());

	const auto Assert = FAptabaseCrashReporter::CreateReport(
		Stack, TEXT("Assertion failed: false [File:Game.cpp] [Line: 3] \nMust not happen"), TEXT("SIGSEGV: invalid attempt to access memory at address 0x3"), TestContext()
	);
	TestEqual(TEXT("Assert type from logged message"), Assert.ErrorType, FString(TEXT("AssertionFailed")));
	TestEqual(TEXT("Assert message from logged message"), Assert.ErrorMessage, FString(TEXT("Fatal AssertionFailed: false [File:Game.cpp] [Line: 3] \nMust not happen")));

	// A real crash logs nothing before the handler: the signal description is all that is known.
	const auto Signal = FAptabaseCrashReporter::CreateReport(Stack, TEXT(""), TEXT("SIGSEGV: invalid attempt to access memory at address 0x0"), TestContext());
	TestEqual(TEXT("Signal type"), Signal.ErrorType, FString(TEXT("SIGSEGV")));
	TestEqual(TEXT("Signal message"), Signal.ErrorMessage, FString(TEXT("Fatal SIGSEGV: invalid attempt to access memory at address 0x0")));
	TestEqual(TEXT("Signal stack"), Signal.StackTrace, FString(Stack).TrimStartAndEnd());

	// A history with its own description takes precedence over the logged message.
	const auto Windows = FAptabaseCrashReporter::CreateReport(
		TEXT("Fatal error: [File:Main.cpp] [Line: 7] \nOut of memory\n\n0x1 Game!Main()"), TEXT("Fatal error: something else"), TEXT("EXCEPTION_ACCESS_VIOLATION"), TestContext()
	);
	TestEqual(TEXT("History description wins"), Windows.ErrorMessage, FString(TEXT("Fatal FatalError: [File:Main.cpp] [Line: 7] \nOut of memory")));

	const auto Nothing = FAptabaseCrashReporter::CreateReport(Stack, TEXT(""), TEXT(""), TestContext());
	TestEqual(TEXT("Stack without any description"), Nothing.ErrorMessage, FString(TEXT("Fatal FatalError: Fatal error without details")));
	TestEqual(TEXT("Stack without any description keeps the stack"), Nothing.StackTrace, FString(Stack).TrimStartAndEnd());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAptabaseCrashReportPersistenceTest, "Aptabase.Errors.CrashReportPersistence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAptabaseCrashReportPersistenceTest::RunTest(const FString& Parameters)
{
	const FAptabaseErrorReport Original = FAptabaseCrashReporter::CreateReport(TEXT("Fatal error: [File:Main.cpp] [Line: 7] \nOut of memory\n\n0x1 Game!Main()"), TEXT(""), TEXT(""), TestContext());
	FAptabaseErrorReport Restored;
	TestTrue(TEXT("Round trip parses"), FAptabaseErrorReport::FromJsonObject(*Original.ToJsonObject(), Restored));
	TestEqual(TEXT("Round trip type"), Restored.ErrorType, Original.ErrorType);
	TestEqual(TEXT("Round trip message"), Restored.ErrorMessage, Original.ErrorMessage);
	TestEqual(TEXT("Round trip stack"), Restored.StackTrace, Original.StackTrace);
	TestEqual(TEXT("Round trip timestamp"), Restored.Timestamp, Original.Timestamp);
	TestEqual(TEXT("Round trip severity"), Restored.Severity, Original.Severity);
	TestEqual(TEXT("Round trip kind"), Restored.Kind, Original.Kind);
	TestEqual(TEXT("Round trip session"), Restored.Context.SessionId, Original.Context.SessionId);
	TestEqual(TEXT("Round trip OS"), Restored.Context.OsName, Original.Context.OsName);
	TestEqual(TEXT("Round trip app version"), Restored.Context.AppVersion, Original.Context.AppVersion);
	TestEqual(TEXT("Round trip SDK version"), Restored.Context.SdkVersion, Original.Context.SdkVersion);
	TestTrue(TEXT("Round trip debug flag"), Restored.Context.bIsDebug);
	TestTrue(TEXT("Credentials are not restored from the body"), Restored.Context.AppKey.IsEmpty() && Restored.Context.ApiUrl.IsEmpty());
	FJsonObject Incomplete;
	Incomplete.SetStringField(TEXT("errorType"), TEXT("X"));
	TestFalse(TEXT("Incomplete body rejected"), FAptabaseErrorReport::FromJsonObject(Incomplete, Restored));

	const FString Directory = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("AptabaseTests"), FGuid::NewGuid().ToString());
	IFileManager& FileManager = IFileManager::Get();
	FileManager.MakeDirectory(*Directory, true);
	FString Body;
	FJsonSerializer::Serialize(Original.ToJsonObject(), TJsonWriterFactory<>::Create(&Body));
	for (int32 Index = 0; Index < FAptabaseCrashReporter::MaxPersistedReports + 2; ++Index)
	{
		FFileHelper::SaveStringToFile(Body, *FPaths::Combine(Directory, FString::Printf(TEXT("%d-session.json"), 1000 + Index)));
	}
	const FString Corrupt = FPaths::Combine(Directory, TEXT("2000-session.json"));
	FFileHelper::SaveStringToFile(TEXT("{not json"), *Corrupt);
	const FString Unrelated = FPaths::Combine(Directory, TEXT("notes.txt"));
	FFileHelper::SaveStringToFile(TEXT("keep"), *Unrelated);

	TArray<FAptabaseErrorReport> Loaded = FAptabaseCrashReporter::LoadPendingReports(Directory);
	TestEqual(TEXT("Newest reports loaded up to the cap"), Loaded.Num(), FAptabaseCrashReporter::MaxPersistedReports);
	TestFalse(TEXT("Corrupt file deleted"), FileManager.FileExists(*Corrupt));
	TestFalse(TEXT("Oldest file beyond the cap deleted"), FileManager.FileExists(*FPaths::Combine(Directory, TEXT("1000-session.json"))));
	TestTrue(TEXT("Unrelated files untouched"), FileManager.FileExists(*Unrelated));
	TestEqual(TEXT("Nothing loaded from an empty path"), FAptabaseCrashReporter::LoadPendingReports(FString()).Num(), 0);
	if (Loaded.Num() > 1)
	{
		TestTrue(TEXT("Loaded file recorded"), FileManager.FileExists(*Loaded[0].PersistedPath));
		TestEqual(TEXT("Loaded report keeps its session"), Loaded[0].Context.SessionId, TestContext().SessionId);

		auto NewContext = TestContext();
		NewContext.SessionId = TEXT("new session");
		NewContext.AppKey = TEXT("A-DEV-9999999999");
		NewContext.ApiUrl = TEXT("https://another-host.example");
		FAptabaseErrorQueue Queue;
		TestFalse(TEXT("No pending reports before session"), Queue.EnqueueReport(Loaded[0]));
		Queue.StartSession(NewContext);
		TestTrue(TEXT("Pending report accepted"), Queue.EnqueueReport(Loaded[0]));
		TestTrue(TEXT("Identical pending report from another file is not deduplicated"), Queue.EnqueueReport(Loaded[1]));
		const auto Batch = Queue.TakeBatch();
		if (!TestEqual(TEXT("Both pending reports queued"), Batch.Num(), 2))
		{
			return false;
		}
		TestEqual(TEXT("Crashed session retained"), Batch[0].Context.SessionId, TestContext().SessionId);
		TestEqual(TEXT("Current key used"), Batch[0].Context.AppKey, NewContext.AppKey);
		TestEqual(TEXT("Current host used"), Batch[0].Context.ApiUrl, NewContext.ApiUrl);
		Queue.Complete(Batch[0], false, 503);
		TestTrue(TEXT("File kept while retrying"), FileManager.FileExists(*Batch[0].PersistedPath));
		Queue.Complete(Batch[0], true, 202);
		TestFalse(TEXT("File deleted after delivery"), FileManager.FileExists(*Batch[0].PersistedPath));
		Queue.Complete(Batch[1], true, 400);
		TestFalse(TEXT("File deleted after permanent rejection"), FileManager.FileExists(*Batch[1].PersistedPath));
	}
	FileManager.DeleteDirectory(*Directory, false, true);
	return true;
}
#endif
