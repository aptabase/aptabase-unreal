#pragma once

#include <HAL/CriticalSection.h>

#include "AptabaseErrorReport.h"

/** Thread-safe capture and retry state. In-flight reports count towards the capacity. */
class FAptabaseErrorQueue
{
public:
	static constexpr int32 MaxPendingReports = 25;
	static constexpr int32 MaxUniqueErrorsPerSession = 100;

	void StartSession(const FAptabaseErrorContext& InContext);
	void EndSession();
	bool Enqueue(const FString& Message, const FString& ErrorType, const FString& StackTrace, bool bFatal);
	/**
	 * Queues a report captured earlier, such as a crash persisted by a previous run. The report keeps
	 * its own session and capture-time context; only the host and key come from the current session.
	 * Not deduplicated: reports from different sessions are distinct even when their text matches.
	 */
	bool EnqueueReport(FAptabaseErrorReport Report);
	TArray<FAptabaseErrorReport> TakeBatch();
	void Complete(const FAptabaseErrorReport& Report, bool bWasSuccessful, int32 ResponseCode);
	static bool ShouldRetry(bool bWasSuccessful, int32 ResponseCode);

private:
	FCriticalSection Mutex;
	FAptabaseErrorContext Context;
	TArray<FAptabaseErrorReport> Pending;
	TSet<uint64> Seen;
	int32 Outstanding = 0;
	bool bActive = false;
};
