#pragma once

#include <Misc/OutputDevice.h>

#include "AptabaseErrorDispatcher.h"

/** Opt-in capture of engine error logs. Does not install native crash/signal handlers. */
class FAptabaseErrorLog final : public FOutputDevice
{
public:
	explicit FAptabaseErrorLog(TSharedRef<FAptabaseErrorDispatcher, ESPMode::ThreadSafe> InDispatcher);
	virtual ~FAptabaseErrorLog() override;
	virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override;
	virtual bool CanBeUsedOnAnyThread() const override { return true; }
	virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

private:
	TSharedRef<FAptabaseErrorDispatcher, ESPMode::ThreadSafe> Dispatcher;
};
