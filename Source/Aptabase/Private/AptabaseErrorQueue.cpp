#include "AptabaseErrorQueue.h"

#include <HAL/FileManager.h>
#include <Misc/ScopeLock.h>

void FAptabaseErrorQueue::StartSession(const FAptabaseErrorContext& InContext)
{
	FScopeLock Lock(&Mutex);
	Context = InContext;
	Seen.Empty();
	bActive = true;
}

void FAptabaseErrorQueue::EndSession()
{
	FScopeLock Lock(&Mutex);
	bActive = false;
}

bool FAptabaseErrorQueue::Enqueue(const FString& Message, const FString& ErrorType, const FString& StackTrace, bool bFatal)
{
	FScopeLock Lock(&Mutex);
	if (!bActive || Outstanding >= MaxPendingReports || Seen.Num() >= MaxUniqueErrorsPerSession)
	{
		return false;
	}

	FAptabaseErrorReport Report = FAptabaseErrorReport::Create(Message, ErrorType, StackTrace, bFatal, Context);
	const uint64 Fingerprint = Report.GetFingerprint();
	if (Seen.Contains(Fingerprint))
	{
		return false;
	}
	Seen.Add(Fingerprint);
	Pending.Add(MoveTemp(Report));
	++Outstanding;
	return true;
}

bool FAptabaseErrorQueue::EnqueueReport(FAptabaseErrorReport Report)
{
	FScopeLock Lock(&Mutex);
	if (!bActive || Outstanding >= MaxPendingReports)
	{
		return false;
	}
	Report.Context.ApiUrl = Context.ApiUrl;
	Report.Context.AppKey = Context.AppKey;
	Pending.Add(MoveTemp(Report));
	++Outstanding;
	return true;
}

TArray<FAptabaseErrorReport> FAptabaseErrorQueue::TakeBatch()
{
	FScopeLock Lock(&Mutex);
	TArray<FAptabaseErrorReport> Batch = MoveTemp(Pending);
	Pending.Empty();
	return Batch;
}

void FAptabaseErrorQueue::Complete(const FAptabaseErrorReport& Report, bool bWasSuccessful, int32 ResponseCode)
{
	FScopeLock Lock(&Mutex);
	if (ShouldRetry(bWasSuccessful, ResponseCode))
	{
		Pending.Add(Report);
		return;
	}
	--Outstanding;
	if (!Report.PersistedPath.IsEmpty())
	{
		// Delivered or permanently rejected: either way the file must not be sent again.
		IFileManager::Get().Delete(*Report.PersistedPath, false, false, true);
	}
}

bool FAptabaseErrorQueue::ShouldRetry(bool bWasSuccessful, int32 ResponseCode)
{
	// The server deliberately returns 403 for an exhausted monthly quota.
	if (ResponseCode >= 400 && ResponseCode < 500 && ResponseCode != 408 && ResponseCode != 429)
	{
		return false;
	}
	return !bWasSuccessful || ResponseCode <= 0 || ResponseCode == 408 || ResponseCode == 429 || ResponseCode >= 500;
}
