# Testing error reporting

Install the plugin in a C++ Unreal Engine project and build the Development Editor target. Open **Tools → Test Automation** (or **Session Frontend → Automation**) and run **Aptabase.Errors**.

For a command-line run, substitute your engine executable and project path:

```sh
UnrealEditor-Cmd YourProject.uproject -unattended -NullRHI \
  -ExecCmds="Automation RunTests Aptabase.Errors" \
  -TestExit="Automation Test Queue Empty" -log
```

The tests cover the JSON contract and field limits, retry status codes, capture-time context across sessions, deduplication, in-flight queue capacity, session limits, and concurrent capture. They do not send data to Aptabase.

For an end-to-end check, configure a local HTTP server as the SH host and start an analytics session in Play-in-Editor. Record a normal event and call **Track Error**. Verify that the event goes to `/api/v0/events` as an array and the error goes to `/api/v0/error` as one JSON object, with `App-Key` and `Content-Type: application/json` headers. Return 202 to accept a report; repeat with 400/403 (no retry), 408/429/500 (retry on the next flush), and a disconnected server. End/start PIE and verify that pending retries retain the old session and that new reports carry the new session.

Enable **Enable Error Logging** before starting the session. Emit the same `UE_LOG(LogTemp, Error, TEXT("Test error"))` several times: only one report should arrive. Emit a Warning and verify that no report arrives. End the session and verify that subsequent error logs are not reported. Fatal logging actually terminates Unreal and is not required to test classification: use **Track Error** with **Fatal** enabled instead.
