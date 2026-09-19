#pragma once

#include <Interfaces/IHttpRequest.h>

#include "AptabaseErrorQueue.h"
#include <atomic>

/** Queues manual reports from any thread; all HTTP work and lifecycle operations run on the game thread. */
class FAptabaseErrorDispatcher : public TSharedFromThis<FAptabaseErrorDispatcher, ESPMode::ThreadSafe>
{
public:
	~FAptabaseErrorDispatcher();
	void StartSession(const FAptabaseErrorContext& Context);
	void EndSession();
	void TrackError(const FString& Message, const FString& ErrorType, const FString& StackTrace, bool bFatal);
	/** Queues a previously captured report (see FAptabaseErrorQueue::EnqueueReport); call Flush to send it. */
	bool EnqueueReport(FAptabaseErrorReport Report);
	/** Queues a previously captured report (see FAptabaseErrorQueue::EnqueueReport); call Flush to send it. */
	bool EnqueueReport(FAptabaseErrorReport Report);
	void Flush();

private:
	void SendNext();
	void OnComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	FAptabaseErrorQueue Queue;
	TArray<FAptabaseErrorReport> Batch;
	int32 BatchIndex = 0;
	FHttpRequestPtr ActiveRequest;
	std::atomic<bool> bFlushScheduled{false};
	bool bFlushing = false;
	bool bFlushAgain = false;
};
