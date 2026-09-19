#pragma once

#include <CoreMinimal.h>
#include <Delegates/IDelegateInstance.h>
#include <Misc/OutputDevice.h>

#include "AptabaseErrorReport.h"
#include <atomic>

/**
 * Opt-in reporting of fatal engine errors: asserts, Fatal logs and crashes that reach the engine's
 * error handler. Unreal never delivers these through GLog at Fatal verbosity and terminates the
 * process right after, so the report is built inside the error handler from GErrorHist, the
 * exception description and the message the error device logged, then written to disk. Pending
 * reports are sent at the next session start. On Windows the handler also tries to send the report
 * right away: the crash is handled on a separate thread while the HTTP thread keeps running. On
 * Apple and Unix platforms the handler switches to a crash allocator that suspends every other
 * thread on its next allocation, including the HTTP thread, so nothing can be sent until relaunch.
 */
class FAptabaseCrashReporter final
{
public:
	/** How long the crashing thread waits for an immediate delivery before the engine continues shutting down. */
	static constexpr float SendTimeoutSeconds = 3.0f;
	/** Newest pending reports kept on disk; older files are deleted when reports are loaded. */
	static constexpr int32 MaxPersistedReports = 10;

	/** PendingDirectory receives one JSON file per crash; empty disables persistence. */
	FAptabaseCrashReporter(const FAptabaseErrorContext& InContext, const FString& PendingDirectory);
	~FAptabaseCrashReporter();

	FAptabaseCrashReporter(const FAptabaseCrashReporter&) = delete;
	FAptabaseCrashReporter& operator=(const FAptabaseCrashReporter&) = delete;

	/**
	 * Builds a crash report from the engine's error state. ErrorHistory is GErrorHist: on Windows the
	 * description, a blank line and the call stack; on Apple platforms the call stack alone because the
	 * crash context overwrites the history before the error handler runs. LoggedMessage is the text the
	 * error device logged when the fatal error was raised, used when the history has no description, and
	 * ExceptionDescription (GErrorExceptionDescription) is the last resort. The heading before the first
	 * colon ("Fatal error", "Assertion failed", "SIGSEGV") becomes the error type.
	 */
	static FAptabaseErrorReport CreateReport(const TCHAR* ErrorHistory, const TCHAR* LoggedMessage, const TCHAR* ExceptionDescription, const FAptabaseErrorContext& Context);

	/**
	 * Loads reports persisted by earlier runs, newest first, deleting unreadable files and anything
	 * beyond the newest MaxPersistedReports readable ones. Each report carries the file in PersistedPath so the queue can delete
	 * it once the outcome is final.
	 */
	static TArray<FAptabaseErrorReport> LoadPendingReports(const FString& PendingDirectory);

private:
	/** Records the message the platform error device logs as "appError called: ..." before the crash handler runs. */
	class FLoggedMessageCapture final : public FOutputDevice
	{
	public:
		virtual void Serialize(const TCHAR* Data, ELogVerbosity::Type Verbosity, const FName& Category) override;
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }
		virtual bool CanBeUsedOnPanicThread() const override { return true; }
		const TCHAR* Get() const { return bCaptured ? Message : TEXT(""); }

	private:
		std::atomic<bool> bCaptured{false};
		TCHAR Message[4096] = {0};
	};

	void OnFatalError();
	FString Persist(const FString& Body) const;

	FAptabaseErrorContext Context;
	FString PendingDirectory;
	FLoggedMessageCapture LoggedMessage;
	std::atomic<bool> bReported{false};
	FDelegateHandle SystemErrorHandle;
	FDelegateHandle ShutdownAfterErrorHandle;
};
