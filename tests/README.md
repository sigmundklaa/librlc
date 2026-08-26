# librlc test suite

Catch2 v3, one executable (`tests`) covering the three RLC modes and the
internals beneath them. `tests/CMakeLists.txt` is the entry point: it fetches
Catch2 and gabs, then adds the library at `../` as the `rlc` target.

The repo-wide rules — tests only, citing clauses, style, and the protocol
facts that mislead if you assume otherwise — are in
[`../AGENTS.md`](../AGENTS.md). What follows is specific to this suite.

## Running

```sh
cmake -S tests -B tests/build
cmake --build tests/build -j"$(nproc)"

cd tests/build && ctest              # one entry per TEST_CASE
ctest -R process_nack                # a subset
ctest --output-on-failure
./tests "[am]"                       # or drive Catch2 directly, by tag
```

`catch_discover_tests` registers each `TEST_CASE` separately, so a failure
names the case.

## Sanitizers

```sh
cmake -S tests -B <dir> -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fsanitize-recover=address -fno-omit-frame-pointer -g" \
  -DCMAKE_CXX_FLAGS="<same>" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build <dir> -j"$(nproc)"
ASAN_OPTIONS=halt_on_error=0:detect_leaks=1 <dir>/tests
```

`-fsanitize-recover=address` with `halt_on_error=0` matters: without them the
first memory error aborts the binary and Catch2 reports **zero** results.

The baseline is clean — no reports, no leaks — so treat any finding as a
regression from the change under test rather than pre-existing noise.

This build also reports 82 passing cases and 1450 assertions, where the
default build fails one case at 1447. The difference is the uninitialised SN
of `REVIEW.md` §4.6: here the indeterminate value lands inside the receive
window and the SDU is delivered, which carries the case three assertions
further. Neither sanitizer flags it — an uninitialised read is MemorySanitizer
territory — so a green run says nothing about that defect.

## Coverage

`-DRLC_COVERAGE=ON` instruments the build; gcovr is run by hand.

```sh
cmake -S tests -B <dir> -DRLC_COVERAGE=ON
cmake --build <dir> -j"$(nproc)"
cd <dir> && ctest
gcovr . --root <repo> --filter <repo>/src --filter <repo>/include \
        --txt --print-summary --delete
```

The flags go on `rlc` as `PUBLIC`, so `tests` inherits them through the link.
Both need them: `tx.c`, `rx.c` and `arq.c` are `#include`d into the test
objects, but the rest of the library is only reached through `rlc`. `--delete`
clears the counters so each run reports itself. Note gcovr matches `--filter`
against the path as written — a doubled slash silently matches nothing and
reports 0%.

Currently ~88% of lines, ~94% of functions, ~74% of branches. The weakest
files are `log.c`, `rlc.c` and `sched.c` — mostly init, teardown and logging.

## Layout

| Path | Contents |
| --- | --- |
| `test_am.cc`, `test_um.cc`, `test_tm.cc` | Per-mode behaviour, including peer-to-peer loopback cases |
| `test_arq.cc`, `test_rx.cc`, `test_tx.cc` | Statics of `arq.c`, `rx.c`, `tx.c`, reached by including the `.c` file |
| `test_encode.cc`, `test_list.cc`, `test_seg_buf.cc`, `test_seg_list.cc` | Encoding and the container primitives |
| `util/` | Shared helpers, below |
| `gabs-overrides/timer/` | Fake `gabs_timer` backend, selected with `gabs_select(gabs-timer-rlc-tests)` |
| `REVIEW.md` | Audit of the suite against the spec, with a prioritised list of gaps |

### `util/`

- `fixture.hh` — `fixture::rlc_ctx`, an `::rlc_context` wrapped in gabs's
  `handle_wrapper` so the owning instance can be recovered from the raw
  pointer handed to the C listener callback. `attach_listener` for an
  `rlc_init`'d context, `on_event` for one built by hand.
- `event.hh` — `event::event_handler` records emitted events;
  `pop(type)` requires the next one to be of that type and consumes it,
  `empty()`/`size()` report what is left.
- `fake_sdu.hh` — an SDU for code driven directly. It holds a reference of its
  own so it stays readable after the code under test releases the one it was
  given; `strong()` hands a reference to something that takes ownership, such
  as a queue.
- `buf.hh` — `buf::pbuf_ptr` owns a `gabs_pbuf`; `create()` builds one,
  `strong()` hands out an owning reference, `from_weak()` adopts a borrowed
  buffer by increfing first, `vec()` copies the contents out.
- `proto.hh` — independent encoder/decoder for UMD, AMD and STATUS PDUs,
  written from the spec figures so it can be compared against `src/encode.c`.
- `bytevec.hh`, `mem.hh`, `backend.hh` — byte-vector conversion, the test
  allocator, and backend callbacks.

## Conventions

**A failing assertion is a finding.** Leave it failing and say so; `REVIEW.md`
records what it documents. Where a case has shared teardown and several
checks, use `CHECK` rather than `REQUIRE` so the teardown still runs, and read
the observed value into a local before asserting on it.

**Helpers release only what they took.** A destructor must not tidy up state
the code under test left behind — a still-queued SDU, an armed timer — because
that is the class of bug the sanitizer is there to catch. Where the leak
belongs to the test's own fixture, tear down at that level: the cases that
build an `::rlc_context` by hand call `rlc_sdu_queue_clear` themselves, as
`rlc_deinit` would.

**Reaching statics.** A `test_*.cc` may `#include` a `.c` file inside
`extern "C" { }`, with the file excluded from the `rlc` target by
`remove_sources(...)`. The whole file then has to parse as C++ — C99 compound
literals with nested designators are the construct that has broken this
before.

When that forces a syntax change in `src/`, check the file still builds as C
afterwards. `remove_sources` only drops it from this build's local `rlc`
target, so it can keep compiling here while breaking for every real consumer:

```sh
INC=$(grep -o '\-I[^ "]*' tests/build/compile_commands.json | sort -u | tr '\n' ' ')
cc -std=gnu11 -fsyntax-only $INC -Isrc src/arq.c
```

Take the include flags from `compile_commands.json` rather than writing them
out: gabs contributes one directory per selected backend, so a hand-written
`-I` list will be missing several.

**Timers.** `gabs-overrides/timer` offers `default_resolver` (a real thread,
real delay) and `manual_resolver` (never fires on its own). Drive the latter
with `timer_ctx.fire(timer.gtimer)`, which blocks until the callback has run,
and query `timer_ctx.armed(timer.gtimer)` for the synchronous "meant to be
running" state — `gabs_timer_active()` cannot answer that, since stopping is
asynchronous by design. Extend this double's API; do not change what an
existing call means.

Any scenario needing *two* STATUS reports must use `manual_resolver`:
`pump()` runs everything with no real delay, so a real `t-StatusProhibit`
armed by the first report will not have expired by the second.

A unit-style case that reaches a timer needs both a `timer_ctx` *and* a real
`rlc_timer_install`. Registering the context alone is not enough: the
override's `stop()` dereferences the handle, and an uninstalled one is null.
The symptom is `std::logic_error("No instance registered")` unwinding past the
case's cleanup, which shows up as a leak spike rather than as an obvious
error.

If you extend `struct timer`, keep `std::jthread runner` declared **last**, as
the comment there asks. Members destruct in reverse declaration order, so a
`jthread` declared ahead of the mutex and condition variable its thread waits
on has those torn down underneath it while it is still running. The failure is
a hang at destruction, with nothing pointing back at the declaration order.

**The loopback link.** `peer_link` connects two entities: `drop` discards a
PDU, `hold` keeps one back and releases it after the next, reordering the two.
`pump()` grants both sides transmit opportunities until neither has anything
left, and grants its first argument before its second — the argument order is
therefore the interleaving. The link records each data PDU's SN and offset as
the peer saw them, so a case can assert the order.

Delivery is synchronous and re-entrant: `rlc_rx_submit` on the peer runs
nested inside the sender's `rlc_tx_avail`, so a STATUS report can come back
before the transmit call has returned. Scenarios also have to allow for the
unconditional poll on the last AM segment and the shared
`RLC_EVENT_TX_RELEASE` for both success and give-up, described in
`../AGENTS.md`.

**`GENERATE`.** Worth keeping when the axis could diverge later, even if it
adds no coverage today — the AM/UM split in `alarm_reassembly`, the pump order.
Not worth keeping when the two runs are symmetric by construction and can never
diverge, as running each loopback case in both directions was.

## Known gaps

`REVIEW.md` audits the suite against the specification and lists what is
missing, prioritised. The open items worth knowing while writing tests:

- SN wraparound is untested, and the implementation compares SNs without a
  modulus base, so this needs a source change first.
- `tx_pollable` only inspects the passed SDU's own unsent list, where §5.3.3.2
  triggers on both buffers becoming empty. No test distinguishes them.
- `tx_ack` breaks on `RLC_READY` before its SN check, so an SDU below ACK_SN
  holding unsent segments may block itself and every later one.
- Window stalling, delayed STATUS triggering, re-segmentation on
  retransmission and entity re-establishment have no coverage.
