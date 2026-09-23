#include "AptabaseErrorDispatcher.h"

#include <Async/Async.h>
#include <HttpModule.h>
#include <Interfaces/IHttpResponse.h>
#include <Serialization/JsonSerializer.h>

#include "AptabaseLog.h"

FAptabaseErrorDispatcher::~FAptabaseErrorDispatcher()
{
	if (ActiveRequest.IsValid())
	{
		ActiveRequest->OnProcessRequestComplete().Unbind();
		ActiveRequest->CancelRequest();
	}
}

void FAptabaseErrorDispatcher::StartSession(const FAptabaseErrorContext& Context)
{
	Queue.StartSession(Context);
}

void FAptabaseErrorDispatcher::EndSession()
{
	Queue.EndSession();
}

void FAptabaseErrorDispatcher::TrackError(const FString& Message, const FString& ErrorType, const FString& StackTrace, bool bFatal)
{
	if (!Queue.Enqueue(Message, ErrorType, StackTrace, bFatal) || bFlushScheduled.exchange(true))
	{
		return;
	}

	// Coalesce bursts from any thread into one game-thread task; HTTP requests are only started there.
	TWeakPtr<FAptabaseErrorDispatcher, ESPMode::ThreadSafe> WeakThis = AsShared();
	AsyncTask(
		ENamedThreads::GameThread,
		[WeakThis]()
		{
			if (const auto Dispatcher = WeakThis.Pin())
			{
				Dispatcher->bFlushScheduled = false;
				Dispatcher->Flush();
			}
		}
	);
}

bool FAptabaseErrorDispatcher::EnqueueReport(FAptabaseErrorReport Report)
{
	return Queue.EnqueueReport(MoveTemp(Report));
}

void FAptabaseErrorDispatcher::Flush()
{
	check(IsInGameThread());
	if (bFlushing)
	{
		bFlushAgain = true;
		return;
	}
	Batch = Queue.TakeBatch();
	BatchIndex = 0;
	bFlushing = !Batch.IsEmpty();
	if (bFlushing)
	{
		SendNext();
	}
}

void FAptabaseErrorDispatcher::SendNext()
{
	if (!Batch.IsValidIndex(BatchIndex))
	{
		Batch.Empty();
		bFlushing = false;
		if (bFlushAgain)
		{
			bFlushAgain = false;
			Flush();
		}
		return;
	}

	const FAptabaseErrorReport& Report = Batch[BatchIndex];
	FString Body;
	FJsonSerializer::Serialize(Report.ToJsonObject(), TJsonWriterFactory<>::Create(&Body));
	const FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Report.Context.ApiUrl + TEXT("/api/v0/error"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("App-Key"), Report.Context.AppKey);
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(Body);
	Request->OnProcessRequestComplete().BindSP(AsShared(), &FAptabaseErrorDispatcher::OnComplete);
	ActiveRequest = Request;
	if (!Request->ProcessRequest() && ActiveRequest == Request)
	{
		// Some HTTP implementations fail to start without invoking the delegate.
		Request->OnProcessRequestComplete().Unbind();
		OnComplete(Request, nullptr, false);
	}
}

void FAptabaseErrorDispatcher::OnComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (Request != ActiveRequest)
	{
		return;
	}
	const int32 ResponseCode = Response.IsValid() ? Response->GetResponseCode() : 0;
	Queue.Complete(Batch[BatchIndex], bWasSuccessful, ResponseCode);
	if (!bWasSuccessful || ResponseCode < 200 || ResponseCode >= 300)
	{
		UE_LOG(
			LogAptabase,
			Warning,
			TEXT("Error report request failed (HTTP %d). %s"),
			ResponseCode,
			FAptabaseErrorQueue::ShouldRetry(bWasSuccessful, ResponseCode) ? TEXT("Will retry on a later flush.") : TEXT("Report discarded.")
		);
	}
	ActiveRequest.Reset();
	++BatchIndex;
	SendNext();
}
