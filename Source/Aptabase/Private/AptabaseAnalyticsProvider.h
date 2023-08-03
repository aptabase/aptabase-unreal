#pragma once

#include <Interfaces/IAnalyticsProvider.h>
#include <Interfaces/IHttpRequest.h>

struct FExtendedAnalyticsEventAttribute;

/**
 *  Implementation of Aptabase Analytics provider
 */
class FAptabaseAnalyticsProvider : public IAnalyticsProvider
{
public:
	/**
	 * Overload for RecordEvent that takes an array of ExtendedAttributes
	 */
	void RecordExtendedEvent(const FString& EventName, const TArray<FExtendedAnalyticsEventAttribute>& Attributes);

private:
	// Being IAnalyticsProvider Interface
	virtual bool StartSession(const TArray<FAnalyticsEventAttribute>& Attributes) override;
	virtual void EndSession() override;
	virtual FString GetSessionID() const override;
	virtual bool SetSessionID(const FString& InSessionID) override;
	virtual void FlushEvents() override;
	virtual void SetUserID(const FString& InUserID) override;
	virtual FString GetUserID() const override;
	virtual void RecordEvent(const FString& EventName, const TArray<FAnalyticsEventAttribute>& Attributes) override;
	// End IAnalyticsProvider Interface
	/**
	 * Internal function for common code in recording events
	 */
	void RecordEventInternal(const FString& EventName, const TArray<FExtendedAnalyticsEventAttribute>& Attributes);
	/**
	 * @brief Callback executed when an event is successfully recoded by the analytics backend.
	 */
	void OnEventRecoded(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
	/**
	 * @brief Current Id of the user, required by the IAnalyticsProvider interface
	 * @warning Aptabase is a privacy-first solution and will NOT send the UserId to the backend.
	 */
	FString UserId;
	/**
	 * @brief Current Id of the user's session
	 */
	FString SessionId;
	/**
	 * @brief Indicates if the user has an active session running.
	 */
	bool bHasActiveSession = false;
};
