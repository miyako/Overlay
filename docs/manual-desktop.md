# Manual desktop acceptance

These are **unexecuted manual gates**, not conclusions from native CI or tool4d.
Run on macOS Intel, macOS Apple Silicon and Windows x64 separately. Use the same
revision's native executable or unpacked compiled component. Record OS version,
CPU, displays/scales, revision, artifact SHA256, command, actual stdout/stderr,
and observations for each row below. Mark `passed`, `failed`, or `not_run`.

Never inject clicks, install global keyboard/mouse hooks, request accessibility
permissions, kill another session's application, or disable Gatekeeper to make
a test pass. Use an ordinary separate application as the click recipient.

## Bounded standalone probe

Place a control in another application underneath the selected desktop location.
The probe makes a 180 by 120 image with a transparent hole at local
`50 <= x < 130`, `30 <= y < 90`. It stops after ten seconds by default, with a
maximum configured display duration of thirty seconds and an outer process
deadline. It never certifies human observations.

macOS shell:

```sh
python3 scripts/manual_overlay.py --executable build/macos-arm64/overlay-cli \
  --x 100 --y 100 --mode clickable
```

For native Intel use `build/macos-x64/overlay-cli`. Coordinates are macOS points,
top-left of the main display, Y down. Retina backing scale does not multiply
these coordinates.

Windows PowerShell:

```powershell
python scripts/manual_overlay.py --executable build/windows-x64/overlay-cli.exe `
  --x 100 --y 100 --mode clickable
```

Windows coordinates are physical virtual-desktop pixels, primary-display
top-left, Y down, with per-monitor-v2 awareness. Both OSes allow negative
coordinates on secondary displays. Run `overlay-cli screens` first and choose
a visible location; `--screen` restricts validation rather than changing origin.

Repeat with `--mode passive` and `--stdin`. For an untimed native overlay the
production API supports timeout zero, but this manual harness deliberately
keeps its display and process deadlines bounded.

| Gate | Action and required observation |
|---|---|
| Transparent-hole passthrough | Click a separate application's control through the hole. That application must receive the real click; the overlay must remain, with no accepted result. A view-level hit-test or a screenshot is not evidence. |
| Visible click/local coordinates | Click the blue border. Clickable mode must close with one LF-terminated `accepted/click` JSON object and local top-left x/y in points or physical pixels, not global position or source-image coordinates. Click near several edges. |
| Passive passthrough/focus | Repeat through both blue border and hole. All clicks reach the other app; typing stays in its focused field; no `accepted` outcome occurs. |
| Nonactivation/Escape | Showing either mode must not activate it. Escape in another app must not dismiss an overlay. These windows intentionally do not normally acquire focus, so focused-only Escape is not a generally available cancellation route. |
| Timeout/fades | Do not click. Expect one `dismissed/timeout` record, no x/y, then no residual window or helper. Repeat with `--fade-in-ms 2000 --fade-out-ms 2000`; inspect smooth easing and no invisible frame intercepting clicks. |
| Cancel during fade | Add `--cancel-after-ms 500 --fade-in-ms 2000`. Expect prompt `dismissed/cancel`, no x/y, without waiting for the fade or display timeout. Repeat cancellation during fade-out by clicking before its scheduled cancellation. |
| Host exit during fade | Add `--host-exit-after-ms 500 --fade-in-ms 2000`. The probe creates and exits **only its own disposable Python host**; expect prompt `dismissed/host-exit`, no x/y, no residual window. This does not substitute for the real 4D host-exit gate. |
| Race/exactly once | Click near the display timeout or scheduled cancel. Exactly one terminal record is allowed; explicit cancellation wins if observed before serialization. No duplicate callback or leftover panel. |
| Mixed DPI/negative coordinates | Repeat on displays left/above the primary, then across a mixed-scale boundary. Check local coordinates, window edges and the hole at backing-store boundaries. |
| Display disconnect | On a machine where safe, disconnect only a test display. Expect `screen-disconnect` and teardown, not a stranded window or silent relocation. Otherwise record `not_run`. |

Clickable artwork is limited to 512 vertically merged nonzero-alpha coverage
rectangles. Highly fragmented artwork must fail `image_complexity`; do not
silently test a different or simplified image and call the original passed.

## Real 4D desktop host

Install the exact unpacked compiled `Overlay.4dbase` into a disposable host's
`Components/`. Put an original PNG at `Desktop/overlay.png` and copy the methods
`Overlay_demo` and `Overlay_demo_done` from `tests/host/Project/Sources/Methods/`
into that host. Its `Project/Sources/dependencies.json` must exist before first
open. In a form, store the returned handle:

```4d
Form.overlayHandle:=Overlay_demo
```

Move/resize the form and edit a field while the overlay is visible: the host
must remain responsive. Repeat the other-app hole and visible-click checks.
Use a form button to call `Form.overlayHandle.cancel()`; verify prompt teardown,
one callback, preserved actual output/status and a local `cancelled` result,
not a fabricated native terminal record after hard termination. Check `.done`
and `.result`; calling `.cancel()` again after completion must return False.
Run two overlapping invocations and cancel only one: the other must remain.

For the real host-exit gate, use only this **owned disposable host**. Display an
untimed overlay and quit that host; the helper's original-parent watcher must
remove it. Do not terminate the user's normal 4D instance. Repeat on both OSes.
Test `showBlob` with the same PNG's Blob to confirm it draws identically, not
only that invalid bytes produce an error. Verify `modal:true` fails explicitly
and never blocks unrelated forms as if true modality had been implemented.

No signing, notarization, published Dependency-manager download, native
Intel/Windows 4D loading or interactive behavior is established merely by a
successful unsigned Apple Silicon compiled-host test.
