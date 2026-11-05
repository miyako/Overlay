# Overlay

A **CLI-backed 4D component** for modeless PNG tooltips and image overlays outside
4D form boundaries. Host API: `cs.Overlay.Overlay`. The bundled `overlay-cli`
runs a native event loop in a separate process: AppKit/ImageIO on macOS and
Win32 layered windows/WIC on Windows. C++17 and Objective-C++; no browser, GUI
toolkit dependency, 4D SDK plug-in, runtime download or third-party image decoder.

**Development preview, not release ready.** A native build is not proof of
cross-application click-through, native Windows execution or compiled component
loading. See [local evidence](#local-evidence-and-remaining-gates).

## Behavior

| Mode | Input and focus |
| --- | --- |
| `passive` | All pixels pass input through; never activates or takes keyboard focus. Suitable for a tooltip. |
| `clickable` | Visible image pixels accept a click, returning top-left window-local coordinates. Fully transparent pixels must pass through to underlying applications; this behavior requires native desktop verification. Escape dismisses only when the overlay has keyboard focus. |

Both modes are **modeless**. `modal:true` / `--modal true` fails with
`unsupported_modal`; waiting for a helper is not application or 4D modality.
Outside-click dismissal, global event hooks, accessibility permissions, event
injection and native SVG are not implemented. Rasterize SVG to PNG in 4D first.

One helper owns one overlay. A display timeout is optional, not a fixed 30-second
process timeout. Cancellation and host lifetime monitoring prevent intentionally
unbounded windows from being the only lifecycle mechanism. No GUI thread calls
4D: events travel through the process protocol and 4D callbacks.

## 4D API

The public feature and asynchronous handle contracts, including worker/callback
lifetime and Blob input, are documented in
[Overlay](component/Overlay/Documentation/Classes/Overlay.md).
The generic synchronous [_Process](component/Overlay/Documentation/Classes/_Process.md)
adapter remains available for bounded metadata commands; displaying an overlay
must use the asynchronous feature API, not `.execute().wait()`.

The source host fixture includes `Overlay_demo`, an explicitly invoked manual
example. Do not start interactive fixtures on an unattended/shared desktop.
The host namespace `Overlay` has no conflicting namespace in this repository;
uniqueness among a host's other components is the integrator's responsibility.

## Native CLI protocol

UTF-8 JSON, protocol 1, compact JSON plus LF. A normal completed `show` emits
**exactly one** terminal stdout record and exits zero:

```json
{"protocol":1,"outcome":"accepted","reason":"click","x":42,"y":18}
```

Dismissal is `{protocol:1,outcome:"dismissed",reason:"timeout"}` (or `escape`,
`cancel`, `host-exit`, and the documented screen-change reason), with **no**
fabricated coordinates. OS-enforced termination cannot promise a terminal
record: the 4D facade distinguishes cancellation from successful protocol
completion and preserves output/status. Stderr is separate from stdout;
nonempty stderr alone is not failure. Validation/runtime failures have nonzero
status, empty stdout and structured stderr:

```json
{"protocol":1,"error":{"code":"unsupported_modal","message":"..."}}
```

No stdout diagnostic logging. Completion is serialized by the native event loop;
once finished, subsequent timeout/click/cancel events cannot emit another record.

| Command | Result |
| --- | --- |
| `overlay-cli --version` | `{"protocol":1,"version":"0.1.0"}` + LF |
| `overlay-cli capabilities` | `{"protocol":1,"modal":false,"format":"png","modes":["passive","clickable"]}` + LF |
| `overlay-cli screens` | Current native displays, coordinate rectangles and units |
| `overlay-cli validate --image PATH ...` | Decode/check the actual PNG and placement without displaying a window |
| `overlay-cli show --image PATH ...` | Display PNG and emit terminal outcome after completion |
| `overlay-cli show --stdin ...` | Read binary PNG on stdin to EOF, then display; not UTF-8 image conversion |

Common options: `--x`, `--y`, `--mode passive|clickable`, `--screen`,
`--scale`, `--opacity`, `--timeout-ms`, `--fade-in-ms`, `--fade-out-ms`,
`--host-pid`, `--modal true|false`. Options are separate tokens, not
`--option=value`; unknown, duplicate, unsupported, missing and conflicting
options fail. Binary PNG stdin is an intentional alternative to `--image`,
never inferred from an arbitrary `-` argument. Input has encoded-byte and read
deadline bounds; dimensions/pixel allocation are checked before full decode.
Empty, malformed and fully transparent images fail; interactive opacity zero
fails. Timings, numeric ranges and scaling are bounded; NaN/Infinity and
out-of-range values fail before any window is shown.

macOS uses points, with the origin at the top-left of the main display and Y
increasing down. Windows uses physical virtual-desktop pixels with explicit
per-monitor DPI awareness and the primary-display top-left origin. Negative
coordinates are valid for secondary displays. `--screen` restricts placement
validation to that display; it does **not** change the global coordinate origin.
Use `screens` to convert a screen-local location to global coordinates.
Exact limits and cancellation details are in the
[native protocol reference](cli/README.md). Placement validates actual nonzero-alpha image
coverage against connected displays, not just an invisible bounding rectangle.
Scaling preserves image aspect ratio. Click coordinates refer to the displayed
window in the platform units, with top-left origin, not source PNG pixels.

On macOS clickable mode creates up to 512 native coverage-rectangle panels, so
alpha-zero holes have **no window** over them; it does not rely on a view
returning `nil` from `hitTest`. Highly fragmented clickable images fail
`image_complexity` rather than creating unbounded native windows. Windows uses
per-pixel layered-window alpha without blanket clickable `WS_EX_TRANSPARENT`.
Mixed-DPI edge rounding and real cross-process delivery remain manual gates.

## Source layout and dependencies

```text
cli/                              shared C++17 protocol, platform GUI backends
component/Overlay/Project/         real Overlay.4DProject
component/Overlay/Documentation/   API and execution contracts
component/Overlay/Resources/bin/   assembled universal macOS / Windows x64 helper
tests/host/                       namespaced source/compiled host and manual demo
tests/cli-contract.json            deterministic native smoke vectors
scripts/                          provenance, build, real 4D compiler and packer
```

Uses system AppKit/CoreGraphics/ImageIO and Windows User32/GDI/OLE/WIC.
No bundled tool4d. Preserve [MIT](LICENSE) and
[redistribution notices](component/Overlay/Resources/licenses/).
The C++ runtime/compiler are supplied by Xcode/MSVC; Windows uses the static
MSVC CRT, whose redistribution terms must be reviewed for the release toolchain.
Hosted runner/toolchain patch versions are not immutable pins. CI uses Python
3.12 and platform Xcode/MSVC; local doctor records the actual available tools.

## Build and verification

Read [AGENTS.md](AGENTS.md) before modifying identity or the acceptance pipeline.
The repository is already initialized for exact basename `Overlay`, namespace
`Overlay`, CLI `overlay-cli`, language `cpp`; do not initialize the original
`miyako/4d-component-template` or reconfigure this initialized destination.

```sh
python3 scripts/doctor.py --root .
python3 -m unittest discover -s tests -v
python3 scripts/generate_icons.py --check
python3 scripts/build_cli.py --root . --target macos-arm64
```

Native targets: `macos-arm64`, `macos-x64`, `windows-x64`. The two workflows
remain **CI** and manual main-only **Publish**. CI runs noninteractive CLI
regressions on each native target, registers CTest tests through CMake, and
assembles both macOS slices with `lipo` plus the Windows x64 resource. Headless
green CI does **not** establish interactive GUI input behavior.

Every native binary requires its same-revision `build-manifest.json`.
C++ cross mode is unsupported by the acceptance helper; do not manufacture a
Windows binary or relabel translated Intel execution as native Intel evidence.
On a native ARM Mac without all three artifacts, source-host/compilation checks
may be possible, but the full cross-platform packer correctly remains blocked.

Free pinned tool4d for unsigned local checks needs no serial or signing secrets:

```sh
python3 scripts/ci_config.py download-tool4d --root .
python3 scripts/test_4d.py --root . --tool4d .tools/tool4d.app/Contents/MacOS/tool4d
python3 scripts/compile_component.py --root . --tool4d .tools/tool4d.app/Contents/MacOS/tool4d
```

Once all three same-source native artifacts are available:

```sh
python3 scripts/accept_local.py --root . --repository miyako/Overlay \
  --tool4d .tools/tool4d.app/Contents/MacOS/tool4d
```

An accepted local pair is `dist/Overlay.zip` and
`dist/Overlay.acceptance.json`. The ZIP must strip `.4dm` sources, contain
compiler-generated metadata/native libraries, and be unpacked and loaded by a
real host with `Is compiled mode=True`. A successful compiler alone is not that
gate. No signing, release, tag, publication or credential setup is part of this
development task. Optional signing policy defaults to unsigned.

## Local evidence and remaining gates

Use actual generated reports, not this checklist, as evidence:
`build/compile-status.json`, `build/source-host-status.json`,
`build/compiled-host-status.json`, and `build/acceptance.json` when present.
`passed` means executed successfully; `not_run` is not a waiver.

Required manual desktop checks: passive focus preservation and all-pixel input
passthrough; clickable visible-pixel acceptance and alpha-zero holes delivering
real clicks to a **different application**; keyboard focus/Escape; negative
secondary-display coordinates and mixed DPI; screen removal; cancellation/host
exit during fades; no orphan panel; overlapping timeout/click exactly-once
completion. Test both native macOS architectures and Windows x64. No global
click injection or accessibility bypass should be added to automate these checks.

An opt-in ten-second alpha-hole probe generates its own PNG and bounds the
helper lifetime (never run by CI):

```sh
python3 scripts/manual_overlay.py --executable build/macos-arm64/overlay-cli \
  --x 100 --y 100 --mode clickable
```

Place a control in another application underneath the hole. Repeat with
`--mode passive` and `--stdin`. The script reports process output, **not** a
success claim about whether another application actually received the click.
The [cross-OS desktop checklist](docs/manual-desktop.md) covers local coordinates,
focus, bounded cancellation/owned-host exit, mixed DPI and real 4D teardown.
The probe supports `--cancel-after-ms 500` and `--host-exit-after-ms 500`
without terminating a real 4D instance.

Dependency-manager download/install, signed/quarantined downloads and
notarization are separate unexecuted gates until explicitly tested.
