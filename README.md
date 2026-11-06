# Overlay

A 4D component for **modeless PNG tooltips and clickable image overlays outside
form boundaries**. The public API is `cs.Overlay.Overlay`. A bundled native
helper draws the image in a separate process; no 4D plug-in or browser is needed.
Display calls are asynchronous and return a cancellable handle.

**Unsigned and unnotarized early release.** See the
[latest release](https://github.com/miyako/Overlay/releases/latest) for its exact
checks and limitations. Transparent-hole delivery to other applications and
several platform/GUI behaviors still require verification.

## Install

1. Download [Overlay.zip](https://github.com/miyako/Overlay/releases/latest/download/Overlay.zip),
   not GitHub's source archive. The release includes a checksum and public
   acceptance summary.
2. Quit the host database. Extract the ZIP and place the entire
   `Overlay.4dbase` folder in the host database's `Components/` folder, creating
   that folder if necessary.
3. Reopen the host database. Keep `Overlay.4DZ`, `Resources/`, and `Libraries/`
   together inside `Overlay.4dbase`; do not install the helper separately.

```text
YourDatabase/
  Components/
    Overlay.4dbase/
      Overlay.4DZ
      Resources/
      Libraries/
  Project/
    YourDatabase.4DProject
```

Use an interactive desktop 4D host with component classes, `SystemWorker` and
persistent `CALL WORKER` support. **Source and exact compiled-package host
contracts have been verified with 4D/tool4d 20.7 on Apple Silicon only.**
That is the tested baseline, not a claim that every newer host/platform works.
The host must also meet its own 4D operating-system requirements.

| Platform | Bundled helper | 4D component host evidence |
|---|---|---|
| macOS Apple Silicon | Universal macOS executable; native CI passed | Source and compiled headless contracts passed on 20.7 |
| macOS Intel | Same universal executable; native Intel CI passed | Real Intel 4D loading not_run |
| Windows x64 | Native layered-window executable; native CI passed | Real Windows 4D loading not_run |

Linux and native Windows ARM64 are unsupported. The macOS helper's deployment
target is macOS 11; this does not lower the host 4D version's OS requirements.
Unsigned downloads may be blocked by Gatekeeper/quarantine; no trust bypass is
provided. Dependency-manager installation is **not verified**; the instructions
above describe manual installation, not a tested dependency-manager workflow.

## Show a PNG without blocking the form

Put a suitable `overlay.png` on the desktop. Run this from a form method or
button in a desktop host. Keep the API and returned handle in `Form` so another
button can cancel the invocation. This example uses the released macOS default:

```4d
var $png : 4D.File
var $options; $callback : Object

Form.overlay:=cs.Overlay.Overlay.new()
$png:=Folder(fk desktop folder).file("overlay.png")
$options:=New object("image"; $png; "x"; 100; "y"; 100; "mode"; "clickable")
$options.timeoutMs:=5000
$options.fadeInMs:=150
$options.fadeOutMs:=150
$callback:=Formula(CALL WORKER(1; Formula(Overlay_completed($1)); $1))
Form.overlayHandle:=Form.overlay.show($options; $callback)
```

On macOS no target argument is needed: the component selects its bundled
universal helper. In the released 0.1.0 API, Windows still requires the explicit
x64 selection; replace only the constructor line with:

```4d
Form.overlay:=cs.Overlay.Overlay.new("Windows-x64")
```

Create the host project method **`Overlay_completed`**:

```4d
#DECLARE($result : Object)
var $diagnostic : Object
If ($result.ok)
    If ($result.value.outcome="accepted")
        ALERT("Clicked at "+String($result.value.x)+", "+String($result.value.y))
    Else
        ALERT("Dismissed: "+$result.value.reason)
    End if
Else
    $diagnostic:=Overlay_error_details($result)
    If (Not($diagnostic.cancelled))
        ALERT($diagnostic.message)
    End if
End if
```

Also add the host project methods
[`Overlay_error_details`](tests/host/Project/Sources/Methods/Overlay_error_details.4dm)
and [`Overlay_error_capture`](tests/host/Project/Sources/Methods/Overlay_error_capture.4dm).
These are sample host methods, not new component APIs, so they also work with
the existing 0.1.0 package. The formatter shows the native error code/message,
wrapper code and exit status; for example:

```text
image_complexity: clickable PNG exceeds 512 alpha-coverage rectangles
Wrapper: cli_error
Exit code: 2
```

Parsing is locally guarded and restores the previous error handler. Malformed
or unexpected stderr falls back to the wrapper message and raw stderr rather
than throwing or hiding the failure. The original envelope remains available as
`diagnostic.result`. Only `ok:false`, `cancelled:true` with wrapper code
`cancelled` is ignored as an acknowledged cancellation.

`show(options : Object; callback : Object)` returns an `OverlayHandle`
immediately. The callback receives **one complete result object**, including
validation/launch failures, on the invocation's isolated worker. The formula
above forwards that result to the main worker for UI handling; it does not
access `Form` from the component worker. Use `CALL FORM` to a live window for
form-specific updates instead. Do not capture expired caller-local variables
in the callback. `Null` is accepted when no callback is needed.

On success, `result.value` contains `outcome` and `reason`; only an accepted
click has `x` and `y`. Dismissal reasons include `timeout`, `escape`, `cancel`,
`host-exit`, and `screen-disconnect`. On failure, inspect `result.error.code`;
available `stdout`, `stderr`, `exitCode` and system diagnostics are preserved.
Nonempty stderr alone is not failure. A native validation error such as
`unsupported_modal` is reported in structured stderr under a wrapper
`cli_error`; prelaunch errors need not have an exit code.

## Cancel and retain the handle

In a Cancel button, or the form's unload handling:

```4d
var $requested : Boolean
If (Value type(Form.overlayHandle)=Is object)
    $requested:=Form.overlayHandle.cancel()
End if
```

`cancel()` returns true only for the first accepted request while active.
Completion is asynchronous; use the callback or inspect `handle.done` and then
`handle.result` without waiting. Cancellation produces `ok:false`,
`cancelled:true`, `error.code:"cancelled"`, not a fabricated accepted/dismissed
success. A completed handle cannot cancel another invocation.

Dropping the handle or closing the form does **not** automatically cancel an
overlay: cancel explicitly on form unload if it belongs to that form.
The component retains its worker and system process until completion.
The helper watches its original parent process by default to close on host exit.

## PNG bytes instead of a file

`showBlob(png : Blob; options : Object; callback : Object)` accepts binary PNG
bytes. Do not pass Text or base64, and omit `options.image`. After the form
initialization above, this is an alternative to the `show` call, not an
additional call to make while the first overlay is still active:

```4d
var $pngBytes : Blob
var $blobOptions; $blobCallback : Object
DOCUMENT TO BLOB(Folder(fk desktop folder).file("overlay.png").platformPath; $pngBytes)
$blobOptions:=New object("x"; 100; "y"; 100; "mode"; "passive"; "timeoutMs"; 3000)
$blobCallback:=Formula(CALL WORKER(1; Formula(Overlay_completed($1)); $1))
Form.overlayHandle:=Form.overlay.showBlob($pngBytes; $blobOptions; $blobCallback)
```

This also forwards completion to the main worker for UI handling. File input is
preferable for large images because Blob bytes are copied into the worker message.

## Modes, placement and timing

| Option | Meaning |
|---|---|
| `mode` | `"passive"` (default): all pixels pass input through, no activation. `"clickable"`: visible pixels accept a click; alpha-zero holes are intended to pass through. Cross-application hole delivery remains unverified. |
| `x`, `y` | Native desktop coordinates, default 0. macOS: points, main-display top-left, Y down. Windows: physical pixels, primary-display top-left, per-monitor DPI aware. Negative secondary-display positions are valid. Not 4D form coordinates. |
| `screen` | Optional screen index restricting placement validation; does not change the coordinate origin. Use `overlay.screens()` to inspect current displays. |
| `scale`, `opacity` | Default 1. Scale 0.01-16, aspect-preserving nearest-neighbor rendering; opacity 0-1. Permanently invisible clickable content fails. |
| `timeoutMs` | Default 0: no automatic display timeout. Positive maximum 3600000 ms. Use a finite timeout or retain a cancellation path. |
| `fadeInMs`, `fadeOutMs` | Default 0; each 0-60000 ms, smoothstep easing. Cancellation/host exit can stop a fade promptly. |
| `modal` | Default false. `true` is explicitly unsupported; waiting for a helper is not application or 4D modality. |

Returned click coordinates have a **top-left origin relative to the whole
rendered overlay**, in macOS points or Windows physical pixels, not source PNG
pixels. The image must have visible content on a connected/selected display,
not merely an intersecting transparent bounding box.

Only PNG is supported; rasterize SVG in 4D first. Limits include 32 MiB encoded
input, dimensions up to 8192, 16777216 decoded/scaled pixels, and a 5-second binary
stdin deadline. Malformed, empty, fully transparent or invalidly placed images
fail. Unknown options and invalid values are rejected.

## Troubleshooting

`cli_error` is the wrapper classification, not the underlying cause. Use the
callback formatter above before changing options or artwork.

| Reported code | What to check |
|---|---|
| `image_complexity` | Clickable alpha coverage exceeds 512 merged rectangles. Simplify the alpha mask: use a contiguous visible shape or a simple opaque blue rectangular frame with one transparent hole. Many separate alpha islands or changing row boundaries increase coverage complexity; texture/color variation alone does not. |
| `invalid_image`, `image_decode` | Check that the input is a valid, nonempty, single-frame PNG, not renamed SVG/JPEG or fully transparent content. Check effective visibility after scaling/opacity too. |
| `image_limit` | Reduce encoded bytes, dimensions or scaled pixels to the documented limits. |
| `offscreen`, `screen_unavailable` | Query `screens()`. Visible pixels, not just transparent image bounds, must intersect a connected/selected screen. Coordinates are native desktop coordinates, not form coordinates. |
| `unsupported_modal` | Set `modal:false` or omit it; true application/4D modality is not supported. |
| `invalid_arguments`, `invalid_argument` | Check option spelling, types, ranges and File/Blob input. Unknown options fail explicitly. |
| `unsupported_architecture`, `unsupported_platform` | Use a supported host. Released Windows 0.1.0 needs `.new("Windows-x64")`; native Windows ARM64 and Linux are unsupported. |
| `missing_executable`, `launch_failed`, `process_error`, `process_terminated` | Keep the entire component folder intact; inspect the retained stderr/status/system diagnostics and OS trust restrictions. Do not repair by falling back to PATH or disabling trust checks. |

The native helper's `validate` command checks the PNG, clickable complexity and
placement **without showing a window**. For macOS, substitute your installed
component and PNG paths:

```sh
"/path/to/Overlay.4dbase/Resources/bin/macOS/overlay-cli" validate \
  --image "/path/to/overlay.png" --x 100 --y 100 --mode clickable
```

On Windows use `Resources/bin/Windows/overlay-cli.exe` with the same arguments.
Match the display call's mode, position, scale and opacity. Validation success
does not establish real cross-application click-through.

## Limitations and evidence

- Clickable artwork is capped at **512 coverage rectangles**. Highly fragmented
  images fail explicitly instead of creating unbounded native windows.
- Windows are intentionally nonactivating. Escape applies only when a clickable
  overlay actually has focus; ordinarily use timeout/cancel/host lifetime.
  There is no global Escape or outside-click interception.
- One user-confirmed Apple Silicon helper probe accepted a visible-frame click,
  returned local coordinates and closed. It did **not** verify numerical
  coordinate accuracy, hole passthrough or 4D GUI integration.
- The user subsequently reported successful source-component use in real desktop
  4D on macOS with a simple blue frame after diagnosing the 512-rectangle error.
  This is scoped user-reported manual success, not separate attestation of hole
  delivery, coordinate accuracy, focus or other platforms.
- Native Intel/Windows 4D loading, real cross-application hole delivery, real 4D
  GUI edge cases, focus, mixed DPI, display disconnect, interactive teardown,
  dependency-manager installation and signing/notarization remain `not_run`.

The package was assembled from same-revision native CI binaries, freshly compiled
and tested as an unpacked source-stripped ZIP on Apple Silicon. Its acceptance
status remains **`partial`, `finished:true`, `release_ready:false`**. The
[release notes and assets](https://github.com/miyako/Overlay/releases/latest)
contain exact provenance and the checksum.
The earlier [0.1.0 preview](https://github.com/miyako/Overlay/releases/tag/0.1.0)
remains available separately; its original package and evidence are unchanged.

## Reference

- [Overlay API and callback contracts](component/Overlay/Documentation/Classes/Overlay.md)
- [OverlayHandle lifecycle](component/Overlay/Documentation/Classes/OverlayHandle.md)
- [Native protocol, limits and errors](cli/README.md)
- [Bounded desktop checklist and real 4D demo](docs/manual-desktop.md)
- [Contributor build/acceptance playbook](AGENTS.md)
- [MIT license](LICENSE) and [bundled notices](component/Overlay/Resources/licenses/)

`version()`, `screens()` and `capabilities()` are bounded synchronous metadata
calls returning `{ok, value, ...}` envelopes; only `show`/`showBlob` display
asynchronously. For native diagnostics:

| Command | Version response |
|---|---|
| `overlay-cli --version` | `{"protocol":1,"version":"0.1.1"}` + LF |
