# OverlayHandle

Returned by `cs.Overlay.Overlay.show()` or `showBlob()`. Retain the handle in a
form variable if a Cancel button needs it; the invocation remains alive even if
the caller drops its reference.

- `done : Boolean` — true only after a complete result has been stored.
- `cancelled : Boolean` — whether cancellation has been requested.
- `result : Object` — Null until done; then a fresh decoded copy of the complete
  process envelope. Reading does not wait or decode native output again.
- `cancel() : Boolean` — true only for the first accepted cancellation request
  while active; false for repeated requests or an already completed invocation.

Cancellation records intent under a shared-object lock. Before launch it
prevents native process creation. Once launched it queues `terminate()` to the
owning **4D worker process number**, not an operating-system PID. Completion
clears that number under the same lock before killing the worker and discarding
pending messages. A completed handle cannot resurrect a named worker or cancel
another invocation.

Cancellation races natural termination: whichever completion already occurred
wins. A cancellation accepted before terminal processing produces an error
envelope with `cancelled:true` and `error.code:"cancelled"`. The worker remains
alive until its SystemWorker termination callback has run. Actual exit status
and available diagnostics are preserved rather than reporting fake success.

`_state` is an implementation detail shared by this handle and its single owned
worker; do not mutate it. Underscore convention is not access control.
