#pragma once

#include <CoreMinimal.h>
#include <Delegates/IDelegateInstance.h>

#include "AptabaseErrorReport.h"
#include <atomic>

/**
 * Opt-in reporting of fatal engine errors: asserts, Fatal logs and crashes that reach the engine's
 * error handler. Unreal never delivers these through GLog at Fatal verbosity and terminates the
 * process right after, so the report is built from GErrorHist and sent synchronously from the
 * error handler with a short timeout. Best effort only: the request is processed by the HTTP
 * thread while the crashing thread waits, and nothing is sent if that thread is not running.
 */
class FAptabaseCrashReporter final
{
public:
	/** How long the crashing thread waits for the report to be delivered before the engine continues shutting down. */
	static constexpr float SendTimeoutSeconds = 3.0f;

	explicit FAptabaseCrashReporter(const FAptabaseErrorContext& InContext);
	~FAptabaseCrashReporter();

	FAptabaseCrashReporter(const FAptabaseCrashReporter&) = delete;
	FAptabaseCrashReporter& operator=(const FAptabaseCrashReporter&) = delete;

	/**
	 * Builds a crash report from the engine's error history (GErrorHist). The heading before the first
	 * colon ("Fatal error", "Assertion failed", "Unhandled Exception") becomes the error type; the text
	 * up to the first blank line is the message and the remainder is the call stack.
	 */
	static FAptabaseErrorReport CreateReport(const TCHAR* ErrorHistory, const FAptabaseErrorContext& Context);

private:
	void OnFatalError();

	FAptabaseErrorContext Context;
	std::atomic<bool> bReported{false};
	FDelegateHandle SystemErrorHandle;
	FDelegateHandle ShutdownAfterErrorHandle;
};
