---
status: todo
---
# Thread-safe queued Signal writes

This is the implementation specification for adding `Signal<T>::setThreadSafe(T)`.
The declarations and behavior below are chosen target contracts, not optional
sketches or claims that the current API implements them. Preserve synchronous
`set()`, owner-thread reads, and the existing reactive/publication runtime.

**Execution status:** planning only. No framework implementation is part of writing
this task. All implementation checkpoints and evidence gates start pending.

## Intent, boundaries, and committed decisions

### Intended authoring improvement

A button handler can start app-owned background work and let the worker submit its
result directly to a signal, without manually calling `Application::postAfter()`.
The framework transports an owning value to the signal's executor; the worker
never reads or mutates the reactive graph.

```cpp
// Illustrative callback fragment, not a new worker/executor API.
// workers is app-owned std::vector<std::jthread>, alive through Application::run().
// Include the required standard headers before importing stana.
const Signal result{std::string{"Ready"}};

Text{.text = result};

Button{
	.text = "Run",
	.onClick = [result, &workers] -> void {
		result.set("Working...");
		workers.emplace_back([result] -> void {
			std::string output;
			try {
				output = doSlowWork();
			} catch (const std::exception &error) {
				output = error.what();
			}
			try {
				result.setThreadSafe(std::move(output));
			} catch (const std::exception &error) {
				// App-specific worker error reporting; do not touch UI state here.
				reportWorkerFailure(error.what());
			}
		});
	}
};
```

The implementation example must define its worker ownership and failure reporting;
the fragment above does not introduce `doSlowWork()` or `reportWorkerFailure()` as
Stana symbols. Do not teach detached threads or a local `jthread` whose destruction
joins inside the click handler. Frequent jobs should use an app-owned pool; building
that pool is not this task.

### Decisions already made

| Decision | Committed behavior |
| --- | --- |
| Public name | `setThreadSafe`, using repository lowerCamelCase naming. |
| Return/failure style | `void`; failure to enqueue throws on the calling thread. |
| Existing setter | `set()` remains synchronous and owner-thread-only. |
| Queued setter | Always deferred, including calls from the owning thread. Never silently falls back to `set()`. |
| Read semantics | `get()`, computed evaluation, bindings, and graph traversal remain owner-thread-only. |
| Execution target | The signal's creating executor, not the producer's thread-local runtime or the currently focused window. |
| Ordering | FIFO admission per executor; no latest-value coalescing. |
| Lifetime | Queued commands own payloads and weakly reference signal state. No document, component, or signal resurrection. |
| Threading model | One owner of mutable reactive/document state; synchronize only the cross-thread mailbox and lifecycle metadata. |
| Native support | Current macOS backend plus portable deterministic headless coverage. No Windows/Linux native host implementation. |

The name was explicitly selected instead of `post()` and the initially requested
`set__thread_safe()`. Do not export aliases under either spelling. Double-underscore
identifiers are reserved by C++; this is not merely a formatter preference.

### Non-goals

- Do not make `get()`, `update()`, `operator++`, `Binding::set()`, observations,
  scopes, node handles, or `Transaction::perform()` cross-thread APIs.
- Do not add blocking reads, futures, synchronous cross-thread dispatch, worker
  spawning, cancellation tokens, a public executor module, or a thread-pool service.
- Do not change `Task`/`sleepFor()` into background execution. They remain
  UI-executor coroutines/timers.
- Do not make `T` or objects referenced by `T` intrinsically thread-safe. Enqueueing
  a raw pointer or `string_view` does not copy or protect its referent.
- Do not infer freshness, component liveness, or latest-request-wins semantics from
  a signal write. Request-generation checks remain a separate concern.
- Do not add last-value-wins coalescing, cross-signal atomic batches, priority
  queues, a public queue configuration surface, or unrelated renderer changes.

## Current implementation and boundaries

### Grounded starting points

| Existing source | Current behavior | Required change |
| --- | --- | --- |
| `modules/reactive/reactive.cppm` | `Source::checkThread()` guards creating-thread identity; `Signal::get()`/`set()` use it. `update()` reads then calls `set()`. | Preserve checks and synchronous semantics; add the typed queued-write adapter and an owning executor endpoint reference. |
| `modules/reactive/reactive.cppm` | `ReactiveRuntime::current()` is thread-local; `flush()` prepares/delivers observations, publishes commits, then runs effects. | Attach a mailbox endpoint without turning synchronous settlement into an implicit mailbox pump. |
| `modules/tasks/tasks.cppm` | `TaskScheduler::current()` and its timers/dispatcher are thread-local and unsynchronized. | Do not call or mutate the producer's scheduler to route a signal write. Preserve current task semantics. |
| `modules/application/application.cppm` | `run()` prepares windows; `prepare()` installs the native task dispatcher before mounting. Preparation can throw before `runWindows()` is entered. | Acquire an executor-session guard before any window preparation and release it on every exit. |
| `src/application/application.cpp` | Native adapter setup, `runWindows()`, shutdown, and public delayed dispatch. | Install owning mailbox wakeup transport and contain queued-application failures. |
| `src/platform/native_contracts.hpp` | Private module-free owning native transport. | Add only the native mailbox wakeup/lifetime transport required by the application bridge. |
| `src/platform/macos/native_host.mm` | Native `postAfter()` targets the main queue; native `run()` requires the main thread. | Provide a thread-safe asynchronous wakeup with explicit lifetime and an exception boundary. |
| `src/platform/unsupported.cpp` | Native services report unsupported platform. | Keep native execution unsupported; headless mailbox operation must not call these services. |
| `modules/testing/testing.cppm` | `TestHost` mounts real documents; `flush()` settles the runtime and commits; `advanceTime()` drives deterministic timers. | Lease the owner endpoint and add one deterministic mailbox turn to explicit `flush()`. |
| `tests/reactive/contracts.cpp`, `tests/tasks/contracts.cpp`, `tests/ui/contracts.cpp` | Affinity, settlement, coroutine/lifetime, and retained UI behavior. | Preserve existing cases and add focused mailbox/concurrency integration suites. |

### Source organization

Keep typed `Signal<T>` work in `modules/reactive/reactive.cppm`. Put non-generic
mailbox/lifecycle algorithms in `src/reactive/executor.cpp`, belonging to the
existing `stana.reactive` module, and register it in the existing library target.
Important internal endpoint declarations may remain in that module interface as
needed by the application/testing modules; they are not a new supported public API.

Dependency direction stays inward: reactive defines the portable mailbox and
executor-session contracts; application/testing install and drive them. Reactive
must not import application, UI, AppKit, libdispatch, or private native headers.
The application bridge translates between module values and module-free native
callbacks. Do not create a second reactive scheduler or document commit pipeline.

## Canonical public API and semantics

Add this member to the existing `Signal<T>` template in namespace `stana`:

```cpp
/// @brief Queues an owning value for application on this signal's executor.
/// @param value Value transferred into the executor's bounded mailbox.
/// @note Always deferred, even when called on the owning thread. Returning means
/// admission, not application, observer delivery, or native display completion.
/// @throws std::logic_error No active owning executor session, or session closed.
/// @throws std::overflow_error The owning executor's pending-write limit is full.
/// @throws Other exceptions from value construction or mailbox allocation.
void setThreadSafe(T value) const;
```

This is deliberately not `noexcept`. Keep the existing `set(T) const` signature,
equality comparator, transaction behavior, and owner checks unchanged. Do not add
the queued setter to `Binding` in this task. Existing public module re-exports must
make the addition available through `import stana;` and `import stana.reactive;`.

### Admission and application

1. The producer builds an owning command carrying `T` and a weak signal-state
   reference. Value construction may occur before endpoint validation.
2. Synchronize endpoint state/capacity validation and queue insertion. Successful
   insertion is the admission linearization point. Serialize it against closure.
3. Request an asynchronous owner wakeup. Neither admission nor wakeup may execute
   the command inline, invoke equality, enter a transaction, or notify observers.
4. On an explicit owner mailbox turn, acquire the weak signal state. If it has
   expired, discard that command without mutation or notification.
5. Otherwise use the same owner-side mutation path as `set()`: check affinity,
   compare using the signal's comparator, assign the value if different, invalidate
   dependents, and settle through `Transaction::perform()`.

Factor a shared typed mutation helper if needed; do not implement a second setter
whose equality, exception, or publication semantics diverge from `set()`.

A normal return guarantees only that the command was admitted. It can later be
discarded because the signal expired, the endpoint shut down, or its session failed.
There is no completion acknowledgement or promise that a frame was displayed.

### Ordering, transactions, and reentrancy

- Commands admitted sequentially by one producer preserve order. Concurrent
  producers are ordered by mailbox admission, not wall-clock start/completion.
  All signals sharing an endpoint share that admission order.
- Do not drop intermediate writes. Equal values still use ordinary equality
  suppression at application time. There is no extra enqueue-time comparison.
- Each applied command uses normal synchronous setter settlement; independent
  commands are not automatically wrapped in one cross-signal transaction.
- A producer's surrounding transaction does not travel with its command. An
  owner-thread call to `setThreadSafe()` inside a transaction remains deferred.
- Synchronous `set()` can overtake a queued write. For example, on the owner,
  `value.setThreadSafe(1); value.set(2);` reads as `2` immediately and can become
  `1` after the queued command runs. Do not silently cancel the older command.
- Take a finite FIFO snapshot at the beginning of a mailbox turn. Commands posted
  by comparator/observer/effect callbacks belong to a later turn, never recursively
  to the current snapshot. Nested pumps cannot steal or reorder an active batch.
- Never execute mailbox commands while the owner runtime is inside a synchronous
  transaction, computed evaluation, or reactive flush. A nested native run loop or
  reentrant `TestHost::flush()` must defer mailbox draining until a safe turn.
  It may still perform the existing safe/no-op synchronous settlement behavior.
- Reject owner-thread `setThreadSafe()` during a computed evaluation using the
  existing mutation-contract `std::logic_error`; do not introduce a new loophole
  for computed side effects. Workers must not inspect the owner's collector.

## Owner endpoint, session activation, and routing

### Stable endpoint and lifecycle

Use one stable shared endpoint object per owner executor epoch. Capture it when
constructing signal state on that owner. Its owner identity is immutable; its
mailbox, mode, admission count, and wakeup bookkeeping are synchronized.

| State | `setThreadSafe()` behavior | Transition |
| --- | --- | --- |
| Dormant | Throw `std::logic_error`; no loop/provider exists to execute the command. | Native session or first headless host activates this endpoint. |
| Active native | Admit and wake the native owner asynchronously. | Normal/failed run exit closes the endpoint. |
| Active headless | Admit; explicit owner `TestHost::flush()` drives delivery. | Last live host lease closes the endpoint. |
| Closed | Throw `std::logic_error`; never reopen this endpoint. | Terminal. New signals can belong to a new epoch. |

Signal construction, imports, and dormant endpoint allocation must not create
`NSApplication`, a native source, a window, or a worker. Creating signals before
`Application::run()` or before `TestHost` remains supported: activation adopts
that owner's current dormant endpoint, so these existing signals gain delivery.
Calls to the queued setter before activation fail rather than silently pile up.

The owner runtime creates a fresh dormant endpoint for subsequent signal
construction after its previous endpoint closes. Existing signals keep their old
closed endpoint and must not be rebound implicitly to a later session. Their
same-thread `get()`/`set()` remain usable; only queued admission stays closed.
This is an explicit queued-write lifetime rule, not a change to signal storage.

Destroying the owner thread closes its endpoint even if worker-held signal handles
survive. An endpoint outliving that thread contains no dereferenceable pointer to
the destroyed thread-local runtime. A recycled OS thread ID cannot reactivate it.
Use endpoint object identity for epochs, not a newly allocated identity on workers
or an assumed globally unique `std::thread::id`.

### Native sessions

- Acquire a single RAII native session before preparing the first window in either
  `Application::run()` overload. Do not activate/replace the endpoint per window.
- Install wakeup transport before mounting, so lifecycle effects can enqueue safely.
  A queued write during mount cannot run until synchronous mounting settles and a
  safe native turn occurs. Do not pump AppKit merely to run the setter.
- Cover preparation exceptions, unsupported platforms, missing native resources,
  empty window tuples, native failures, `Application::stop()`, and normal final
  window closure. Every path closes admission and cancels pending commands.
- All windows in the run share the owner endpoint. Closing one window does not
  close it while the application session remains active.
- Acquire native sessions only on the native UI/main thread. Reject concurrent or
  nested native sessions and mixing native/headless modes on one active endpoint;
  never silently replace another provider.

### Headless sessions and multiple hosts

- `TestHost` acquires an owner-thread headless lease before document setup can run
  application code. Constructor failure releases that lease.
- Hosts on the same owner share the endpoint and hold separate leases. Closing or
  destroying a host releases its lease exactly once; last-host closure closes the
  endpoint. `close()` remains idempotent. Another live host keeps delivery active.
- Routing is executor-wide, not document-local. Flushing one host can deliver a
  queued signal change observed by another host on that owner, just as ordinary
  signal settlement can update multiple documents today. Document this explicitly.
- Hosts on different threads have distinct endpoints. Pumping B never executes a
  command for A. Worker submissions use the captured endpoint rather than
  `ReactiveRuntime::current()` or `TaskScheduler::current()` on the producer.
- Preserve the existing owner's lifetime/affinity requirements for host operations;
  adding this setter does not authorize cross-thread host creation/destruction.

## Mailbox synchronization, wakeups, and bounded ownership

### Committed mailbox rules

Use an MPSC FIFO protected by a small mutex; lock-free machinery is not required.
Use one named internal limit, `maximumPendingSignalWrites = 4096`, per endpoint.
Count accepted commands until application or discard finishes, including a detached
active batch. This bounds backlog count, not total bytes or arbitrary payload size.
Do not claim bounded process memory for unconstrained `T`.

Under the admission lock, validate state, then capacity, then insert. Allocation
failure must leave no accepted command. A rejected admission must never apply later.
Queue saturation throws `std::overflow_error`; do not block waiting for UI progress,
drop another command, or replace its value. A short mutex wait is permitted; this
API is not a lock-free, wait-free, or real-time guarantee.

Release the lock before invoking commands, comparators, reactive settlement, error
reporters, payload cleanup, or other application-controlled code. Do not destroy
rejected/retired commands under the mailbox lock: destructors can reenter Stana.
Ensure exception paths and moving a turn snapshot preserve the admission count.

Only mailbox/lifecycle state is shared mutable framework data. Workers may read
immutable routing metadata and copy handles using normal shared ownership; they
must not read `T` in signal storage, equality functors, observers, scope internals,
commit maps, or thread-local runtime fields belonging to another thread.

Copies of a signal handle can submit concurrently. This does not make concurrent
assignment/destruction of the *same C++ handle object* safe; give workers their own
value captures. `T` must support the transfer required by the existing signal and
be safe to construct, move, and destroy on relevant threads. A command's payload
can be destroyed without application on rejection/shutdown. No new blanket promise
of move-only signal payload support is part of this task.

### Native wakeup transport

Install a private owning wakeup provider at session activation. For macOS, use an
activated main-queue data-add dispatch source: producer notification only merges a
wakeup into that source; its owner callback drains the portable mailbox. The
native source does not own the signal graph and its callback holds a weak endpoint.
Keep native types confined to the adapter and expose owning module-free callbacks
to the application bridge.

Creation can fail during owner-side session setup, before admission opens. Once
active, producer notification must not allocate a new callback or throw after a
command has been admitted. Retain the native notification capability across
notify/close races; cancelling the source cannot free an object a producer uses.
Perform native cancellation/teardown on the owner. Late handlers resolve endpoint
state and do nothing after closure; they cannot restart draining or another epoch.

Coalescing wakeups is allowed; coalescing commands is not. Prove the no-lost-wakeup
invariant for submission racing snapshot extraction, wakeup acknowledgement, and
completion of a turn. If a reentrant handler finds the runtime unsafe to drain,
retain the pending obligation and arrange a later safe turn without spinning a
nested event loop. Do not leave a nonempty active native queue permanently asleep.
Use an owner-local deferred-wakeup flag for this case: when the outermost
transaction/flush exits to a safe state, re-arm native notification, but do not
execute mailbox commands from that exit hook. Headless mode leaves the work for
the next explicit host pump. Keep the distinction between requesting a future
turn and implicitly pumping inside synchronous settlement.

### Deterministic owner turns

Extend explicit `TestHost::flush()` to process one ready FIFO snapshot on the owner,
then complete ordinary reactive settlement/document commit. If no mailbox work is
ready, its previous behavior remains unchanged. Work enqueued during that turn is
left for the next explicit flush, matching a later native turn.

`advanceTime()` continues to advance only the existing deterministic timer clock,
then uses `flush()` as above. Mailbox work requires no positive elapsed time and
must not be encoded as a fake timer. Ordinary `Signal::set()` and
`ReactiveRuntime::flush()` must not implicitly pump the mailbox. Test waits occur
outside owner mutation/settlement; production delivery never blocks on a worker.

## Shutdown, expiry, freshness, and exceptions

### Close/admission race

Closing linearizes under the same lifecycle/admission lock as insertion. Mark the
endpoint closed, detach queued work, and prevent future admission before releasing
session/provider ownership. Clear detached commands outside the lock. Cancellation
also applies to the unexecuted portion of a current turn snapshot.

If user code closes the session while a command is already applying, that command
finishes normal synchronous setter semantics; check closure before starting every
subsequent command. No new command starts after the close linearization point.
An admission that won the race may return normally yet be cancelled before
application; one that lost must throw and can never be applied.

Cleanup must not wait for application workers. Workers own their own lifetime and
must catch enqueue rejection as appropriate. Do not join under an endpoint lock,
from a native callback, or from the button callback to make teardown appear safe.

### Signal and component lifetime

Queued commands hold a weak signal reference, not a `Signal` value or strong
document reference. Destroying the final external signal handle makes pending
commands discardable. Command storage must not create an endpoint/command/signal
ownership cycle. Explicitly cancelled backlog releases payloads promptly.

Unmounting a component disposes its subscriptions through existing mechanisms; it
does not invalidate a deliberately shared model signal. If that signal is still
owned elsewhere, its queued value may still apply without reviving the removed UI.
Do not make a global signal's queued lifetime depend on whichever component last
observed it.

Freshness is not FIFO completion order: two background requests can finish in the
opposite order from launch. `setThreadSafe(value)` intentionally does not check a
`Scope::RequestToken`. When a result needs scope/generation validation, retain the
existing pattern: dispatch an owner callback, check `scope.isCurrent(token)` there,
then use synchronous `set()`. Do not read scope state from a worker or advertise
the new setter as a complete request-cancellation abstraction.

### Failure contract

| Failure | Where observed | Required behavior |
| --- | --- | --- |
| Dormant/closed endpoint or owner exited | Calling thread | `std::logic_error`, no admission. |
| Owner computed evaluation attempts queued mutation | Calling owner thread | `std::logic_error`, no admission. |
| Capacity reached | Calling thread | `std::overflow_error`, no admission or displacement. |
| Payload/command construction or queue allocation | Calling thread | Original exception; no partially admitted command. |
| Weak signal expired | Owner drain | Discard without exception or observer delivery. |
| Endpoint closes after admission | Owner cleanup | Cancel unapplied command; do not report synchronous success as application success. |
| Equality/assignment/observer/settlement throws while applying | Owner drain | Preserve existing setter partial-mutation semantics, fail that executor session, cancel its remaining commands, and surface the original failure. |

For headless delivery, close the failed endpoint and rethrow the original exception
from the explicit pumping call. Live hosts release their now-closed leases normally;
do not reactivate it. For native delivery, catch **all** exceptions inside the native
wakeup callback, record an owning failure in application-session state, close the
endpoint, and request native loop shutdown. `Application::run()` must return
nonzero and emit the existing application diagnostic with useful error detail;
handle non-`std::exception` failures with an explicit fallback description.

No C++ exception may cross a libdispatch/Objective-C callback boundary. Do not
silently swallow application failures, terminate because a callback was unguarded,
or try to throw an owner-side failure back through the already-returned producer.
As with current synchronous transactions, this task does not promise rollback of
source writes or side effects that happened before the exception.

## Verification and acceptance matrix

Use real secondary threads for producer tests and production mailbox/settlement
code for delivery. Coordinate with promises/futures, barriers/latches, and bounded
test deadlines, not timing sleeps. Account for the existing libc++ named-module
`jthread` linkage issue documented in `tests/reactive/contracts.cpp`; an owned
`std::async(std::launch::async, ...)` producer is acceptable for fixtures. Its wait
must not be presented as the production UI pattern.

Add a focused portable suite, such as `tests/reactive/thread_safe_writes.cpp`, and
register it as `stana.reactive.threadSafeWrites` in `tests/CMakeLists.txt`. Add UI,
task, native, and external-consumer cases where those boundaries are exercised.
Internal test seams may pause admission/turn boundaries and inspect mailbox counts;
do not export a mutable production endpoint or fake a second delivery engine.

### API, ordering, and settlement gates

| ID | Scenario and required assertion |
| --- | --- |
| S01 | Existing owner `set()` applies and notifies before return; existing transaction batching is unchanged. |
| S02 | Worker `get()`, `set()`, `update()`, and increment still throw affinity errors without changing stored value. |
| S03 | Worker queued setter returns after admission; value/observer remain unchanged until owner pump; delivery records the actual owner thread. |
| S04 | Owner queued setter also defers; an immediate owner `get()` sees the old value. |
| S05 | One producer queues distinct values; every unequal value is observed in FIFO order, without value coalescing. |
| S06 | Multiple producers submit unique labeled values; every admitted command applies once, with per-producer ordering and no duplicate/lost write. |
| S07 | Interleaved signals on one endpoint preserve admitted order; there is no implicit cross-signal transaction. |
| S08 | Custom equality runs on the owner only; equality suppression matches synchronous `set()`, including values equal only when delivery occurs. |
| S09 | Synchronous owner `set()` overtakes queued work exactly as documented; no stale-command auto-cancellation. |
| S10 | A queued setter inside an owner transaction remains deferred; synchronous writes still batch normally. |
| S11 | Observer/effect queues another value; it runs on a later turn, not recursively in the active snapshot. Nested pumps cannot reorder it. |
| S12 | Owner queued mutation during computed evaluation rejects; worker admission does not inspect another thread's collector. |
| S13 | Deliver while UI observes the signal: retained text updates, no remount, and coherent publication through the existing document commit path. |
| S14 | Nested native loop/explicit pump during a transaction or reactive flush defers delivery until safe; no spin or lost wakeup. |

### Activation, isolation, and ownership gates

| ID | Scenario and required assertion |
| --- | --- |
| L01 | Signal created before native run/first TestHost is adopted on activation; dormant submission rejects and leaves no queued work. |
| L02 | Native installation precedes mount effects; prepare/mount failure closes admission and releases wakeup resources. |
| L03 | Two hosts on one owner share delivery; closing one does not cancel the other's endpoint; repeated close is harmless. |
| L04 | Different owner threads remain isolated even if producers have their own thread-local reactive/task runtime. |
| L05 | Closing one native window preserves delivery to another; final run exit closes the epoch. |
| L06 | Native/headless conflicts and nested native sessions reject before replacing a live provider. |
| L07 | Last external signal handle disappears before delivery; weak command expires and payload releases without observation or graph resurrection. |
| L08 | Component unmount removes its observers; shared model can still update; removed nodes are not recreated. |
| L09 | Session closes with backlog; unapplied payloads release, late wakeups no-op, and subsequent admission rejects. |
| L10 | Race submit against close using controlled interleavings; no rejected command applies and no command starts after closure. |
| L11 | Owner thread exits while workers retain handles; submission rejects without accessing destroyed TLS. A new thread/session cannot revive the old endpoint. |
| L12 | New epoch accepts newly created signals; old signal remains synchronously usable on its owner but its queued route stays closed. |
| L13 | Empty native run, unsupported native backend, missing resources, and explicit stop all release the session on their real exit paths. |
| L14 | Native source notification races cancellation; retained native capability remains valid and late callbacks cannot access application stack storage. |

### Capacity, failure, and scheduling gates

| ID | Scenario and required assertion |
| --- | --- |
| F01 | Fill all 4096 pending slots; the next submission throws overflow, never applies later, and does not displace existing values. |
| F02 | Active snapshot still counts toward capacity; applying/discarding commands returns capacity exactly once. |
| F03 | Payload construction/queue allocation fails through a narrow injected seam; no ghost admission, leaked payload, stuck count, or suppressed original exception. |
| F04 | Equality, assignment, observer, and settlement failures surface on the pumping owner, close the failed session, and release remaining backlog. |
| F05 | Real native callback handles both standard and nonstandard application exceptions; run fails diagnostically rather than unwinding across the native boundary. |
| F06 | Reentrant payload destruction and callbacks never execute under the mailbox lock; no deadlock when cleanup calls an allowed Stana operation. |
| F07 | Adversarial admission/snapshot/wakeup interleavings cannot strand a nonempty active native queue. |
| F08 | Task delays/cancellation/error handling still pass; mailbox pumping neither advances fake time nor resumes cancelled coroutines. |
| F09 | Repeated producer/drain/close stress finishes under bounded deadlines; no race, use-after-free, or ownership cycle in available instrumentation. |
| F10 | Headless tests run without AppKit/Metal initialization on all supported targets; unsupported native services remain explicitly unsupported. |

Do not infer race freedom from AddressSanitizer or UndefinedBehaviorSanitizer.
Use deterministic race fixtures on every target. Run a supported ThreadSanitizer
configuration when available and record the exact toolchain/platform result; the
repository currently has no TSan preset. Do not claim that such a preset already
exists or bypass safety policy to obtain one. Record an unavailable TSan environment
as a limitation rather than as a passing test.

### Native and external consumer qualification

Extend the opt-in native suite with a bounded worker-result scenario exercising the
real wakeup source and public setter. Verify main-thread observation, updated scene
content, and continued native event processing while a worker is held behind a test
barrier. Release the worker from a separate UI callback; do not block the UI to wait
for it. Add shutdown-with-pending-work and owner-side failure cases. Synthetic
headless delivery is not proof that the native wakeup/exception boundary works.

Extend the external source/install/relocation consumer to instantiate the public
member and execute a worker-to-headless-delivery assertion using the exported
modules. Keep that coverage active for normal Debug/Release packages. Consumers
must not include private native headers or depend on an uninstalled implementation
source. Confirm portable threading linkage on supported targets; use standard CMake
thread target propagation if needed, not platform-specific consumer linker flags.

## Delivery sequence and exit gates

### Phase A — Preserve contracts and add the portable mailbox

- [ ] Add affinity/synchronous-behavior regression cases before changing dispatch.
- [ ] Define stable endpoint state, dormant/active/closed epochs, lease rules,
  admission/close synchronization, and the named capacity.
- [ ] Implement non-generic mailbox/lifecycle code in the reactive implementation
  unit and register it in the existing target/policy scans.
- [ ] Add weak typed commands and `void Signal<T>::setThreadSafe(T) const` with
  shared owner-side setter behavior and complete Doxygen contracts.
- [ ] Prove API/ordering/weak-ownership/capacity behavior through focused portable
  endpoint fixtures; keep existing reactive tests passing.

**Exit gate:** no cross-thread graph access and no change to synchronous `set()`.

### Phase B — Integrate deterministic headless sessions

- [ ] Add TestHost acquisition/release, shared-owner leases, constructor-failure
  cleanup, and terminal epoch handling.
- [ ] Add explicit one-snapshot mailbox turns to `flush()`/`advanceTime()` without
  changing ordinary runtime settlement or the task clock.
- [ ] Implement safe-turn/reentrant-pump behavior and owner-side failure surfacing.
- [ ] Pass multi-host/multi-owner, retained-UI, task compatibility, pre-host signal,
  shutdown, and real-producer tests without native initialization.

**Exit gate:** portable tests exercise the real mailbox through the real host path.

### Phase C — Native activation, wakeups, and exception containment

- [ ] Guard both `Application::run()` overloads before any preparation and cover
  every early return/throw path, including empty/unsupported runs.
- [ ] Add private owning native wakeup transport and the macOS main-queue source.
- [ ] Prove thread-safe notification, no lost wakeups, safe cancellation, finite
  turns, and no delivery inside unsafe/reentrant runtime phases.
- [ ] Catch all application exceptions at the native callback boundary and report
  nonzero run completion through the existing diagnostic style.
- [ ] Pass real worker-to-UI, event responsiveness, multi-window, and shutdown
  qualification in an actual logged-in native session.

**Exit gate:** normal user code submits results without manual UI dispatch and no
native callback can outlive an unguarded application/session lifetime.

### Phase D — Stress, distribution, documentation, and final evidence

- [ ] Complete every S/L/F gate, including allocation/error injection and controlled
  admission/close/wakeup interleavings.
- [ ] Extend source/install/relocation consumer coverage and verify ABI/toolchain
  requirements without adding an alternative public header API.
- [ ] Document queued-versus-synchronous behavior, active-session requirement,
  bounded admission, failure handling, old-epoch signals, and freshness limits in
  README/architecture/Doxygen, with a public-API-only worker-result example.
- [ ] Run repository policy, formatting, normal build/test, consumer, applicable
  sanitizer, and real native gates; record unavailable environments explicitly.
- [ ] Audit that no manual-dispatch requirement remains for the simple worker
  value-submission case and that no unsupported thread-safe-read claim was added.
- [ ] Fill the completion record and mark complete only when all required gates pass.

**Exit gate:** implementation, exported API, ownership behavior, and documentation
agree; no remaining required test is replaced by a simulated pass.

### Quality-gate commands for the future implementation

These are future implementation checks, not commands required merely to create
this planning document. Use the pinned toolchain with normal analysis enabled.

```sh
cmake --preset debug
cmake --build --preset debug --parallel 3
ctest --preset debug
cmake --build build/debug --target format-check

cmake --preset release
cmake --build --preset release --parallel 3
ctest --preset release

cmake --preset asan-ubsan
cmake --build --preset asan-ubsan --parallel 3
ctest --preset asan-ubsan
```

Use the existing `asan` preset on Windows instead of unavailable `asan-ubsan`.
For native qualification, configure the chosen Apple build with
`-DSTANA_NATIVE_TESTS=ON`, rebuild, and run native-labelled cases in a logged-in
window-server/Metal session. An unregistered or skipped native test is not a pass.
Do not add analysis suppressions or disable policy/consumer checks to land the API.

### Cross-plan integration rules

- Reuse whichever single transaction/publication path exists when implementing;
  apply queued values through it rather than creating mailbox-specific commits.
- If the omnibus host/session bridge has landed, attach wakeup lifetime and failure
  reporting to that bridge. Otherwise extend the current application-session
  lifetime directly; do not implement unrelated host services as a prerequisite.
- Incremental rendering still owns its presentation scheduling and evidence. A
  queued model write is neither GPU completion nor display acknowledgement.
- Keep task delays on their existing clock; share a lower-level native primitive
  only if it does not change the explicit mailbox admission/lifetime semantics.
- Preserve retirement-before-cleanup and stale-handle behavior during queued
  observer delivery. Do not resurrect removed occurrences to consume a value.

### Completion record

All rows start pending. Record commands/configuration, exact test scenarios, observed
results, and actual limitations; an implementation attempt is not verification.

| Gate | Status | Evidence to record |
| --- | --- | --- |
| Public API and synchronous compatibility | Pending | S01–S04, S08–S12; exported signature and documented exceptions. |
| FIFO/concurrent producers | Pending | S05–S07, S11, S14; producer counts, ordering assertions, controlled interleavings. |
| Host activation/routing | Pending | L01–L06, L12–L13; pre-run signals, shared hosts/windows, isolated owners, rejected provider replacement. |
| Lifetime and shutdown | Pending | L07–L11, L14; payload release, TLS exit, race linearization, late callbacks. |
| Retained UI and task compatibility | Pending | S13, F08, F10; real mount/commit path and unchanged coroutine tests. |
| Capacity, exception, and wakeup integrity | Pending | F01–F07, F09; injection seams, cleanup counts, no lost wakeups/deadlocks. |
| Real native behavior | Pending | Native main-thread/event-progress, multi-window, failure, and close scenarios with hardware/OS details. |
| Debug/Release/policy/format | Pending | Commands and results using pinned tools with analysis enabled. |
| Consumer/install/relocation | Pending | External queued-setter instantiation and worker-to-headless assertion. |
| Sanitizers/race instrumentation | Pending | Applicable ASan/UBSan results; TSan result or precise unavailable-environment limitation. |
| Documentation/ownership audit | Pending | Correct naming, active-session/deferred/failure/freshness contracts and no public aliases or new runtime. |

### Definition of done

1. `Signal<T>::setThreadSafe(T) const` returns void, queues from any caller thread
   while its owner session is active, and throws synchronously on enqueue failure.
2. The value, comparator, dependency graph, observations, and document changes are
   accessed only on the signal's owning executor; existing affinity checks remain.
3. `set()` stays synchronous, `get()` stays owner-only, and UI-thread queued calls
   remain deferred. No hidden thread-safe read or update semantics are introduced.
4. FIFO, equality, independent transaction settlement, reentrancy, and set/queue
   interleaving match the explicit contracts, without coalescing intermediate values.
5. Pre-run signals, multi-host/window ownership, terminal epochs, owner exit, failed
   startup, and submit/close races have deterministic, tested outcomes.
6. Pending storage has the documented count bound, weak signal ownership, no cycles,
   safe payload cleanup, and native wakeups that cannot be lost or use freed state.
7. Application-time failures surface on the owner; native exceptions are contained
   and cause diagnostic nonzero exit rather than unwinding through native code.
8. Portable/headless, real native, consumer, policy, formatting, and applicable
   sanitizer gates have recorded evidence; required unavailable gates remain pending.
9. Public documentation teaches a working owned-worker/result-display flow and
   states shutdown, ordering, capacity, and stale-request limitations accurately.

## Reference contracts

- [Framework backlog](00_omnibus.md): owner confinement, coherent publication,
  host/service installation, teardown, and failure conventions.
- [Incremental rendering plan](01-incremental-shared-rendering.md): compatible
  transaction/publication and native lifetime boundaries; not a prerequisite.
- [Architecture](../docs/architecture/README.md): inward module dependencies,
  compiled implementation units, and production-backed testing.
- [Coding conventions](../docs/coding-conventions.md): lowerCamelCase, Doxygen,
  executor/lifetime/error contracts, and ownership guidance.
- [Current reactive implementation](../modules/reactive/reactive.cppm): source
  affinity, typed mutation, transaction settlement, and request-token behavior.
- [Current application interface](../modules/application/application.cppm) and
  [implementation](../src/application/application.cpp): pre-mount installation,
  native bridging, and all run/exit paths.
- [Current TestHost](../modules/testing/testing.cppm) and
  [task scheduler](../modules/tasks/tasks.cppm): deterministic pumping and
  UI-executor timer/coroutine compatibility.
- [C++ identifier rules](https://eel.is/c++draft/lex.name#4.1): double-underscore
  names are reserved; the committed exported name is `setThreadSafe`.
- [Apple dispatch source merge](https://developer.apple.com/documentation/dispatch/dispatch_source_merge_data):
  custom-source wakeup delivery and data coalescing, not signal-value coalescing.
- [Upstream libdispatch source contract](https://github.com/swiftlang/swift-corelibs-libdispatch/blob/main/dispatch/source.h):
  notification, cancellation, ownership, and nonthrowing C function declarations;
  verify the target SDK declarations when implementing the adapter.
