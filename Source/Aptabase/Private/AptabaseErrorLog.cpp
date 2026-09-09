#include "AptabaseErrorLog.h"

#include <Misc/OutputDeviceRedirector.h>

FAptabaseErrorLog::FAptabaseErrorLog(TSharedRef<FAptabaseErrorDispatcher, ESPMode::ThreadSafe> InDispatcher) : Dispatcher(InDispatcher)
{
	if (GLog)
	{
		GLog->AddOutputDevice(this);
	}
}

FAptabaseErrorLog::~FAptabaseErrorLog()
{
	if (GLog)
	{
		GLog->RemoveOutputDevice(this);
	}
}

void FAptabaseErrorLog::Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category)
{
	// Ignore transport diagnostics as well as our own logs to avoid reporting a
	// failure to send an error as another error. Warnings and normal logs are noise.
	if ((Verbosity != ELogVerbosity::Error && Verbosity != ELogVerbosity::Fatal) || Category == FName(TEXT("LogAptabase")) || Category == FName(TEXT("LogHttp")))
	{
		return;
	}
	static thread_local bool bReporting = false;
	if (bReporting)
	{
		return;
	}
	TGuardValue<bool> Guard(bReporting, true);
	Dispatcher->TrackError(Category.ToString(), Message ? Message : TEXT(""), TEXT(""), Verbosity == ELogVerbosity::Fatal, true);
}
