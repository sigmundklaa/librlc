# librlc test suite

Catch2 v3, one executable (`tests`) covering the three RLC modes and the
internals beneath them. `tests/CMakeLists.txt` is the entry point: it fetches
Catch2 and gabs, then adds the library at `../` as the `rlc` target.

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

**Tests only.** Do not edit `src/` to make a test pass. A test that asserts
the specification and fails is documenting a defect; leave it failing and say
so. If the assertion is one of several in a case with shared teardown, use
`CHECK` rather than `REQUIRE` so the teardown still runs.

**Cite the clause.** Assertions that encode a requirement name the TS 38.322
clause they come from. Keep it to the number and the rule. PDU header sizes
and field layouts are normative (§6.2.2, §6.2.3), so a test may pin them.

**SN comparisons are modular.** §7.1 requires subtracting a per-entity modulus
base before comparing: `TX_Next_Ack` on the transmitting AM side, `RX_Next` on
the receiving AM side, `RX_Next_Highest - UM_Window_Size` on the receiving UM
side. Reasoning about SNs as plain integers gives wrong answers — at rest a UM
entity's reassembly window is `[2048, 4096)`, so a "large" SN like 3000 is
inside it. The spec says *inside*/*outside* a window, never *above*/*below*.

**`rlc_event`'s payload is a union.** Branch on `ev.type` before reading it;
`RLC_EVENT_RX_DONE_DIRECT` carries a `gabs_pbuf *`, not an SDU, and the
pointer is to the caller's stack.

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

**The loopback link.** `peer_link` connects two entities: `drop` discards a
PDU, `hold` keeps one back and releases it after the next, reordering the two.
`pump()` grants both sides transmit opportunities until neither has anything
left, and grants its first argument before its second — the argument order is
therefore the interleaving. The link records each data PDU's SN and offset as
the peer saw them, so a case can assert the order.

Note an AM PDU that empties the transmission buffer is polled unconditionally
(§5.3.3.2), so any single-SDU AM transmission drags a poll, a STATUS report
and possibly a retransmission behind it. Design scenarios around that.
`rlc_event_tx_fail` and `rlc_event_tx_done` both report `RLC_EVENT_TX_RELEASE`,
so success and give-up have to be told apart some other way.

**`GENERATE`.** Worth keeping when the axis could diverge later, even if it
adds no coverage today — the AM/UM split in `alarm_reassembly`, the pump order.
Not worth keeping when the two runs are symmetric by construction and can never
diverge, as running each loopback case in both directions was.

**Style.** Prefer no comment; when one is needed, a couple of plain sentences
on what the code does and why it has to. Comments describe the code as it
stands — rationale for a change belongs in the commit message. No
trailing-underscore private members. Name a method for its effect: an accessor
that consumes is `pop()`, not `get()`.

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
