# Testing error reporting

Install the plugin in a C++ Unreal Engine project and build the Development Editor target. Open **Tools → Test Automation** (or **Session Frontend → Automation**) and run **Aptabase.Errors**.

For a command-line run, substitute your engine executable and project path:

```sh
UnrealEditor-Cmd YourProject.uproject -unattended -NullRHI \
  -ExecCmds="Automation RunTests Aptabase.Errors" \
  -TestExit="Automation Test Queue Empty" -log
```

The tests cover the JSON contract and field limits, retry status codes, capture-time context across sessions, deduplication, in-flight queue capacity, session limits, concurrent capture, and parsing the engine's error history into a crash report. They do not send data to Aptabase.

For an end-to-end check, configure a local HTTP server as the SH host and start an analytics session in Play-in-Editor. Record a normal event and call **Track Error**. Verify that the event goes to `/api/v0/events` as an array and the error goes to `/api/v0/error` as one JSON object, with `App-Key` and `Content-Type: application/json` headers. Return 202 to accept a report; repeat with 400/403 (no retry), 408/429/500 (retry on the next flush), and a disconnected server. End/start PIE and verify that pending retries retain the old session and that new reports carry the new session.

To exercise crash reporting, enable **Enable Crash Reporting**, start a session, then trigger a fatal error from a packaged or `-game` build rather than Play-in-Editor, since a fatal error terminates the whole editor: for example a Blueprint-callable test function containing `UE_LOG(LogTemp, Fatal, TEXT("Test crash"))` or `check(false)`. Verify that one request arrives at `/api/v0/error` with `kind: "crash"`, `severity: "fatal"`, the engine's description as `errorMessage` and the call stack as `stackTrace`, before the engine's crash reporter appears. Repeat with the server stopped to confirm the game still terminates normally after the 3 second timeout. Non-fatal `UE_LOG(..., Error, ...)` messages must not produce a report.
