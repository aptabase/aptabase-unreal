# Changelog

## 0.3.0

- Add `UAptabaseBlueprintLibrary::TrackError` and the **Track Error** Blueprint node for structured handled and fatal error reports.
- Add opt-in **Enable Crash Reporting** to report fatal errors (assertion failures, `Fatal` logs, crashes routed through the engine's error handler) during active sessions. The error handler writes the report to `Saved/Aptabase/Crashes` and the next session start sends it; on Windows the handler also attempts an immediate send.
- Include capture-time session, timestamp, OS, app version, SDK version, severity, kind, and debug mode; truncate fields to the server limits.
- Send errors individually to `/api/v0/error`, retry network failures and HTTP 408/429/5xx on later flushes, and discard permanent rejections including HTTP 403 quota exhaustion.
- Bound outstanding reports to 25 and deduplicate up to 100 unique errors per session.
- `StartSession()` now returns `true` without starting a new session when one is already active; previously it replaced the session ID and restarted the flush timer.
- Cancel in-flight event requests and unbind their callbacks when the provider is destroyed; guard against responses without a body.
