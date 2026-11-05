# Overlay

`cs.Overlay.Overlay.new()` selects the bundled universal macOS helper without
a target argument. The optional `target : Text` accepts `"macOS-universal"`
on macOS; the released 0.1.0 API requires `.new("Windows-x64")` on Windows.
An omitted Windows target returns `unsupported_architecture` on execution;
native Windows ARM64 is unsupported. This is a CLI-backed component launching
the standalone `overlay-cli` executable, not a 4D plug-in.
The host namespace is `Overlay`; component-internal code uses `cs.Overlay`.

## Metadata

`version() : Object`, `screens() : Object` and `capabilities() : Object` use the synchronous generic
adapter (30-second maximum). Successful envelopes contain `ok:true`, `stdout`,
`stderr`, `exitCode:0`, and decoded `value`. Protocol 1 and each payload's field
types are checked. `version()` returns `{"protocol":1,"version":"0.1.0"}`;
the host fixture checks that exact synchronized component/CLI version.
`capabilities()` returns deterministic
`{"protocol":1,"modal":false,"format":"png","modes":["passive","clickable"]}`.
Screen entries have numeric `index`, `x`, `y`, `width`, `height`,
and `scale`. Empty screen lists are allowed in a headless environment.
Screen coordinates follow the native CLI's desktop coordinate system, not
4D form coordinates. Read the native screen metadata before choosing coordinates.

## Nonblocking display

```4d
var $overlay : cs.Overlay.Overlay
var $handle : cs.Overlay.OverlayHandle
$overlay:=cs.Overlay.Overlay.new()
$handle:=$overlay.show(New object("image"; File("/DATA/overlay.png"); \
    "x"; 100; "y"; 100; "mode"; "clickable"; "timeoutMs"; 5000); \
    Formula(My_overlay_completed($1)))
```

The example uses the macOS default; on Windows construct with
`cs.Overlay.Overlay.new("Windows-x64")` instead.

`show(options : Object; callback : Object) : cs.Overlay.OverlayHandle` returns
immediately after queueing an isolated worker invocation. It does **not** call
`execute()` or `wait()`. The caller's UI remains available. A callback is an
optional Formula (`Null` is accepted), invoked once with the complete result
on the invocation's owned worker, including validation/launch errors. Use `$1`,
not captures of local variables from an expired caller. For explicit callback
state, pass `{formula:Formula(...), context:sharedObject}` instead; `This` is
that context when the formula runs. Mutate shared context only inside `Use`.
A Formula object itself is not writable. For UI work, marshal back using `CALL FORM` to a live window or
`CALL WORKER(1; Formula(My_overlay_completed($1)); $1)`; the demo does the latter.
The component does not retain forms or block the caller's message loop.

Options are:

| Key | Type | CLI flag / behavior |
| --- | --- | --- |
| `image` | Text or `4D.File` | `--image`; required for `show` |
| `x`, `y` | Number | `--x`, `--y`; default zero |
| `mode` | Text | `--mode passive` or `clickable` |
| `timeoutMs` | Number | `--timeout-ms`; zero disables automatic display timeout |
| `fadeInMs`, `fadeOutMs` | Number | `--fade-in-ms`, `--fade-out-ms` |
| `opacity`, `scale` | Number | `--opacity`, `--scale` |
| `screen` | Number | `--screen` |
| `modal` | Boolean | `--modal`; true returns structured unsupported error |
| `hostPid` | Number | `--host-pid`; optional override of native original-parent-process lifetime tracking |

Unknown keys, nonnumeric scalar options, invalid image types and embedded NUL
are rejected before launch. The native CLI validates option ranges and image
contents. Numeric options use JSON numeric formatting, independent of host
locale. Arguments are individually quoted using the same `_Process` helper as
synchronous execution; there is no shell or PATH fallback. The async system
worker omits the `timeout` option entirely when `timeoutMs` is zero or absent:
the documented SystemWorker default is undefined, not an implicit 30-second cap.
An untimed invocation remains alive until native dismissal, host exit, or
explicit cancellation; callers must retain a handle or another dismissal path.
Native host tracking defaults to the original parent process; `hostPid` is an
override, not an opt-in needed to prevent orphaned overlays when 4D exits.
For a positive display timeout, its finite safety timeout is the duration plus
both fades and ten seconds of cleanup allowance (30 seconds minimum, 3,730
seconds maximum). This accommodates the native one-hour positive timeout and
up to 60 seconds per fade without truncating valid long-lived invocations.

### Binary PNG input

`showBlob(png : Blob; options : Object; callback : Object) : OverlayHandle`
uses `show --stdin`; `options.image` must be absent. Pass a **Blob**, not a Text
conversion or base64. The worker calls the supported
`SystemWorker.postMessage(Blob)` API and then `closeInput()`. This writes the
original binary bytes without UTF-8 conversion. Output remains deliberately
`dataType:"text", encoding:"UTF-8"` so JSON stdout/stderr are Text.
File PNG input is preferable for very large images: Blob arguments are copied
into the invocation's worker message.

## Completion and cancellation

See [OverlayHandle](OverlayHandle.md). Each invocation owns a unique live
`CALL WORKER` process, retained process-local `_OverlayOperation`, and retained
`4D.SystemWorker`. Its callbacks concatenate stdout/stderr independently.
Only `onTerminate` decodes normal native output; the exact-once finish guard
prevents duplicate completion. The native terminal payload must contain
`protocol:1` and Text `outcome/reason`. `accepted` requires reason `click` and
numeric window-local, top-left `x/y` (macOS points, Windows physical pixels);
these are relative to the rendered overlay, not global desktop coordinates.
Both coordinates must be finite and in the inclusive range 0–8192.
`dismissed` requires reason `timeout`, `escape`, `cancel`,
`host-exit`, or `screen-disconnect` and no coordinates. Each type is checked
before type-sensitive comparison. Malformed JSON,
protocol/version mismatches or malformed terminal fields fail `invalid_protocol`.
Terminal stdout must be exactly one compact native-schema JSON object followed
by one LF. Missing/extra LF, CRLF, embedded raw newlines, concatenated objects
and trailing junk are rejected independently of `JSON Parse` behavior. Decimal
and exponent coordinate notation are supported. The host fixture labels its
positive decoder-unit examples separately from actual native execution; they
are not evidence of GUI clicks.

Results preserve actual stdout/stderr and any available exit status. Nonzero
status is `cli_error`; system errors are `process_error`; abnormal termination
is `process_terminated`. Available `.errors` diagnostics are retained.
Cancellation is `cancelled`, not a fabricated successful native terminal event.
Prelaunch failures never invent an exit code. Nonempty stderr alone is not
failure when exit status is zero.

Escape dismissal is a native event for an actually focused clickable overlay,
not global keyboard monitoring. Passive overlays do not acquire keyboard focus.

The worker reference and callback are released on completion, its shared
live-worker ID is cleared, and `KILL WORKER` clears pending cancellation messages
and ends the owned worker. There is no global OS PID map, worker reuse, blocking
poll loop, permanent error handler, shell, or OS-wide process kill.
Local error handlers around creation/parsing are restored immediately.

`tests/host/Project/Sources/Methods/Overlay_demo.4dm` is a real desktop-host
example using `Desktop/overlay.png`. `Smoke` uses exact native `capabilities`
as its deterministic feature-success case, validates an original opaque 2×2 PNG
with native `validate` without showing a window, and runs native metadata, invalid async
input, unsupported modal input, binary-invalid input and cancellation without
opening an interactive overlay, and records the actual compiled mode.

## Verified 4D API references

- [SystemWorker callbacks, Blob postMessage, terminate](https://developer.4d.com/docs/API/SystemWorkerClass)
- [CALL WORKER: copy semantics and persistent lifetime](https://developer.4d.com/docs/commands/call-worker)
- [KILL WORKER: queue clearing and termination](https://developer.4d.com/docs/commands/kill-worker)
- [Function.call](https://developer.4d.com/docs/API/FunctionClass#call)
