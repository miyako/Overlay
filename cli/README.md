# overlay-cli

Standalone C++17 helper, using AppKit/ImageIO on macOS and Win32/WIC on Windows.
No native 4D plug-in, third-party library, accessibility permission, event tap,
outside-click hook, downloaded executable or runtime trust bypass is used.

## Protocol 1

All commands emit exactly one compact UTF-8 JSON object plus LF on stdout on
success, without logging. Failure emits only
`{"protocol":1,"error":{"code":"CODE","message":"DETAIL"}}` plus LF on stderr
and exits 2 (validation/native failure) or 3 (unexpected native exception).

```
overlay-cli show|validate --image PATH|--stdin --x N --y N
  [--mode passive|clickable] [--timeout-ms N]
  [--fade-in-ms N] [--fade-out-ms N] [--opacity N] [--scale N]
  [--screen INDEX] [--modal false] [--host-pid PID] [--cancel-file PATH]
overlay-cli screens
overlay-cli capabilities
overlay-cli arguments --value TEXT
overlay-cli inspect --image PATH --x N --y N [same show options except --stdin]
overlay-cli --version
overlay-cli --help
```

Arguments are separate literal argv entries. Unknown/repeated options, invalid
UTF-8/NUL, non-decimal/nonfinite/overflow numbers, missing required values,
multiple image sources and **`--modal true`** are errors. No positional operands,
equals-style options, environment expansion, shell commands or abbreviations.
`--stdin` takes no value; all other show/validate options take one.

| Option | Default | Range / meaning |
|---|---|---|
| `--x`, `--y` | required | -1000000..1000000, image top-left in global desktop units |
| `--mode` | `passive` | passive never accepts clicks; clickable accepts left-button down |
| `--timeout-ms` | 0 | integer 0..3600000; 0 means no automatic display timeout; positive starts at presentation and includes fade-in |
| `--fade-in-ms`, `--fade-out-ms` | 0 | integer 0..60000 each |
| `--opacity` | 1 | decimal 0..1; clickable requires >0 and nonzero effective rendered alpha |
| `--scale` | 1 | decimal 0.01..16, nearest-neighbor pixels to native units |
| `--screen` | all | integer 0..255, available screen index for intersection validation |
| `--modal` | false | only literal `false` accepted |
| `--host-pid` | original parent | integer 1..2147483647, overrides automatically observed original parent lifetime |
| `--cancel-file` | none | caller-owned UTF-8 path; existence cancels |

`--screen` does not change the global origin, clip the image or relocate it.
Use `screens` to obtain actual display bounds. Both encoded and decoded images
are bounded: PNG only, one nonanimated frame, encoded bytes <=33554432, each
dimension <=8192, total pixels <=16777216. The scaled surface has those same
dimension/pixel limits. Fully transparent images fail; at least one actual
nonzero-alpha rendered pixel rectangle must intersect an eligible current screen
(not merely the image bounding box). Clickable checks the actual nearest-neighbor
raster after opacity is baked into 8-bit premultiplied channels; opacity or
downsampling that eliminates every visible pixel is rejected on **both OSes**.
Scaled dimensions round upward to whole native units (points on macOS, physical
pixels on Windows); every rendered pixel occupies one such unit. Clickable images are limited
to 512 vertically merged horizontal nonzero-alpha coverage rectangles on both OSes.
This keeps AppKit native-window resources bounded; simplify highly fragmented
artwork or use passive mode.

`--stdin` reads binary PNG until EOF, with a **5000 ms total deadline** and byte
limit; pipes and regular files are supported, interactive terminals are not.
Close input after writing. Image files are read as binary, never as commands.
Metadata is not used to resize or rotate the PNG.

Results:

```json
{"protocol":1,"outcome":"accepted","reason":"click","x":123,"y":456}
{"protocol":1,"outcome":"dismissed","reason":"timeout"}
{"protocol":1,"valid":true,"width":128,"height":64}
{"protocol":1,"screens":[{"index":0,"x":0,"y":0,"width":1920,"height":1080,"scale":1}]}
{"protocol":1,"version":"0.1.1"}
{"protocol":1,"modal":false,"format":"png","modes":["passive","clickable"]}
```

`show` returns accepted only for a visible-pixel left click; coordinates are
**whole-window-relative top-left native units**, not global desktop coordinates
or original image pixels: `x = eventGlobalX - options.x`,
`y = eventGlobalTopDownY - options.y`. macOS coverage-panel offsets are mapped
back into the whole overlay before this subtraction. Dismissal reasons are `timeout`,
`escape`, `cancel`, `host-exit`, `screen-disconnect`, **without coordinates**.
`validate` performs parsing, full native PNG decoding, alpha/size/coverage and
current-screen placement checks, **without creating any window**; it does not
watch host lifetime. `--help` returns a protocol object with `usage` Text.
`capabilities` takes no options and reports the supported PNG modes and refusal
of modal operation. It is deterministic, performs no image or display access,
and creates no GUI, making it suitable for native build contract smoke tests.
`arguments --value TEXT` is an explicit parser-only diagnostic for generic
SystemWorker quoting tests. It returns `{"protocol":1,"value":"TEXT"}` with the
exact decoded UTF-8 argument, including empty Text, quotes, Unicode and literal
File/Folder path strings. It performs no filesystem lookup or normalization.
Exactly one `--value` option is required, with no extras; NUL and malformed UTF-8
are rejected. This command does not claim that a path exists or contains a PNG.
`inspect` is the corresponding parser-only image-path diagnostic: it uses the
strict show option parser but requires `--image`, not `--stdin`, and returns
`{"protocol":1,"image":"PATH"}` without opening, decoding or normalizing the path.
It performs no display access or placement validation. Use `validate` for actual
PNG and current-screen validation, not either parser diagnostic.

Error codes: `invalid_arguments`, `unsupported_modal`, `image_read`, `invalid_image`, `image_limit`,
`image_decode`, `image_complexity`, `offscreen`, `screen_unavailable`,
`stdin_timeout`, `cancelled`, `control_error`, `host_watch`, `native_error`.
Errors before presentation are diagnostics, not successful dismissal records.

## Native behavior and limitations

Both platforms use borderless modeless topmost/floating tool windows, no taskbar
or Dock activation, no `makeKey`, foreground activation or focus capture. There
is no global mouse listener or outside-click dismissal. Images render
premultiplied BGRA, with nearest-neighbor scaling to keep alpha coverage exact.

**macOS:** coordinates are points, global x increases right, global y increases
down from the **top of the main display**; other displays can have negative
coordinates. The PNG is resampled by `scale` into whole-point raster pixels,
independently of Retina backing scale. A passive panel sets `ignoresMouseEvents=YES`. Clickable mode uses
**separate nonactivating NSPanel windows for each nonzero-alpha coverage
rectangle**, drawing clipped parts of the full PNG. There is literally no window
at alpha-zero holes; this is the cross-application passthrough mechanism, not
`NSView.hitTest:nil` or an assumption that nonopaque NSWindow pixels pass clicks.
Subpixel window/backing-store edge rounding across mixed-DPI displays and
cross-application click/focus behavior still require interactive validation on
each supported macOS version; source implementation is not such evidence.

**Windows:** coordinates are physical desktop pixels, primary-display top-left
origin, x right/y down, other displays may be negative. Per-monitor-v2 awareness
is required (Windows 10 1703+); failure is explicit, never virtualized units.
Positions round to nearest integer; scaled dimensions round upward. Screen
`scale` is 1 because all reported coordinates are already physical pixels.
Top-down 32-bit DIBs feed `UpdateLayeredWindow` with premultiplied alpha.
Clickable uses layered-window alpha-zero cross-application passthrough, **without
`WS_EX_TRANSPARENT`**. Passive adds that flag for whole-window passthrough.
`WS_EX_NOACTIVATE`, `MA_NOACTIVATE` and `SW_SHOWNOACTIVATE` avoid activation.
Runtime WIC decoding, focus and layered hit-testing need Windows native tests.

Escape dismisses only a **clickable overlay that actually has keyboard focus**.
There is no global Escape sampling, interception or injection. These windows
deliberately do not activate or take focus, so Escape normally remains entirely
with the foreground app and is not an available cancellation route. Passive
overlays never dismiss because Escape was pressed in another app. Use the
control file, a signal, host lifetime, or a positive timeout to dismiss.

Normal timeout/click/Escape runs the configured fade-out, then emits its single
record. Both fades use the shared bounded smoothstep ease-in-out curve
`t*t*(3-2*t)`, with progress clamped to 0..1. Input is disabled on zero-alpha
frames. Ordinary key/click events cannot replace an outcome during fade-out.
**Explicit cancellation wins until the terminal record is written**: signal,
control-file, `WM_CLOSE` or `WM_QUIT` closes immediately and returns `cancel`,
even if a click or timeout was already selected. Windows uses a separate abort
flag, so closing never waits for a pending fade-out.
**Signal, control-file, host exit and display change cancel immediately,
even during fades** (approximately one 10–20 ms event-loop interval, excluding
native drawing/OS scheduling). No animation restarts or unbounded timers.
Screen topology changes dismiss rather than silently repositioning.
With timeout 0 (the default), there is no display deadline; the process remains
alive until another dismissal/cancellation event. Fade and stdin deadlines
remain bounded. A calling facade must not describe a finite worker safety
ceiling as unlimited display lifetime.

SIGINT/SIGTERM set a cancellation flag on macOS; Windows C-runtime SIGINT/SIGTERM
are handled when delivered by that runtime. For reliable cross-platform control,
pass a unique caller-owned `--cancel-file` that does not yet exist, then create
it to cancel. The helper never creates/removes that path; existing paths cancel
immediately. Caller must remove its own control path after process completion.
Cancellation while reading stdin yields `cancelled` error, not a show result.
4D/SystemWorker hard termination, `TerminateProcess`, SIGKILL, process crashes
or disconnected stdout **cannot promise a terminal record**: caller must
distinguish process termination/missing record from normal dismissal.

Every `show` observes its **original parent process by default**, including when
timeout is 0; `--host-pid` explicitly overrides that host. The helper captures
its parent PID at native startup (`getppid` / Toolhelp process snapshot), then
binds macOS `kqueue`/`EVFILT_PROC NOTE_EXIT` or a Windows
`OpenProcess(SYNCHRONIZE)` handle immediately after parsing, **before reading
stdin or decoding PNG**. It passes the same kernel watcher into the display
loop, never switches to a reparented PID, and latches observed exit. Subsequent
PID reuse after binding cannot keep it alive.
An already nonexistent host yields `host-exit` when presentation is reached;
inaccessible/undiscoverable hosts fail closed. Metadata/`validate` commands do
not create lifetime watchers. Like any PID-based handoff there is a small race
*before* initial capture/registration; callers must launch from a still-running
host or pass its own current PID, not a stale arbitrary PID. A host exit during
pre-presentation input/decode is observed by the already bound watcher; stdin's
independent 5-second deadline still bounds incomplete input.

## Development

```
cmake -S cli -B build/native-development -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native-development --config Debug
ctest --test-dir build/native-development -C Debug --output-on-failure
```

CTest validates strict arguments, UTF-8, numeric and image bounds, real native
PNG decode/premultiplication/orientation, exact alpha coverage, transparent-hole
placement, malformed PNG, binary stdin/EOF/deadline, control-file cancellation,
kernel lifetime watching (including a controlled child exit on macOS), and
compact protocol framing. Tests **never present GUI**. Windows uses static
MSVC CRT and OS DLLs only; AppKit/ImageIO are OS frameworks. There are no
downloaded third-party feature dependencies. The repository MIT license does
not replace applicable compiler, SDK or runtime licenses: Windows static-CRT
redistribution must follow the relevant MSVC redistribution terms and any
required notices. Review the actual toolchain/runtime distribution before release.
Run native CTest separately on Windows; a macOS build does not verify Win32.
Manual acceptance must cover both modes, holes over another app, no focus
change, mixed DPI/multiple displays, fades/cancellation/host exit/disconnection.
