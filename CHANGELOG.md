# Changelog

## 0.3.0

- Add `UAptabaseBlueprintLibrary::TrackError` and the **Track Error** Blueprint node for structured handled and fatal error reports.
- Add opt-in **Enable Error Logging** to report Unreal Error/Fatal log messages during active sessions.
- Include capture-time session, timestamp, OS, app version, SDK version, severity, kind, and debug mode; truncate fields to the server limits.
- Send errors individually to `/api/v0/error`, retry network failures and HTTP 408/429/5xx on later flushes, and discard permanent rejections including HTTP 403 quota exhaustion.
- Bound outstanding reports to 25 and deduplicate up to 100 unique errors per session.
- Clean up log subscriptions and HTTP callbacks at shutdown; fix flushing event batches with fewer than 25 events.
