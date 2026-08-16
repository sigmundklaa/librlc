---
name: rlc-tests
description: Write, extend, or debug tests for librlc (Catch2 suite under tests/). Use whenever adding or changing a test_*.cc file, a helper in tests/util/, the timer override, or when running the suite under ctest, ASan/UBSan or gcovr. Covers the hard rules (tests only, no src/ fixes), the shared fixtures, timer and loopback doubles, TS 38.322 citation and modular-SN reasoning, and the build commands.
---

# Writing librlc tests

Catch2 v3, one executable (`tests`), covering the three RLC modes and the internals
beneath them. `tests/README.md` documents the suite in full; this skill is the working
procedure. Read `tests/README.md` and `tests/REVIEW.md` when you need the layout or the
known gaps.

## Hard rules

**Tests are the deliverable; `src/` is not yours to fix.** A test that asserts the
specification and fails is documenting a defect. Leave it failing and say so in your
report. Never edit `src/` to make a test pass.

Keep the test itself neutral about its own outcome: no "(known bug)" in the `TEST_CASE`
or `SECTION` name, no comment saying it currently fails or how the implementation gets
it wrong. Just assert the spec-correct behaviour.

The one exception, and only after asking the user: a memory-corruption crash that kills
the whole test process hides every other case's result. There the options are a source
fix, avoiding the trigger, or keeping the repro tagged Catch2-hidden (`[.]` in the tag
list, e.g. `"[um][tx][.]"`) so a bare `./tests` run stays clean while `./tests "[um]"`
still reproduces it.

**A test helper releases only what it took itself.** Never tidy up state the code under
test left behind — a still-queued SDU, an armed timer, a held buffer. That is the class
of bug ASan is there to catch, and a helpful destructor turns a real defect into a
silent pass. If removing the cleanup makes the run leak, work out who owns the leak:
code under test → genuine finding, report it; the test's own fixture → tear down at that
level (`test_rx.cc` builds an `::rlc_context` by hand, so each case ends with
`rlc_sdu_queue_clear(&ctx.rx.sdus)`, which is what `rlc_deinit` would have done).

**Never rationalize a sanitizer finding.** Before calling anything "expected", check
whether a sibling test already handles it correctly and whether a cheap correct fix
exists. Tests that go through `rlc_init` must call `rlc_deinit`.

**An intentionally-failing assert must release everything first.** A failing `REQUIRE`
throws out of the `SECTION` and skips the cleanup below it. Read the observed value into
a local, free the SDU/timer, then assert. Where a case has shared teardown and several
assertions, use `CHECK` so the teardown still runs.

## Reasoning about the spec

The spec is on disk — no network fetch:
`/home/agent/.claude/uploads/92533820-96be-4319-943e-ec0e7a71f306/da6f0530-ts_138322v180100p.pdf`
(ETSI TS 138 322 V18.1.0 = 3GPP TS 38.322 v18.1.0). Search it as text:
`pdftotext -layout <pdf> out.txt` then grep — `-layout` matters, the PDU-format figures
collapse without it. Every clause number appears twice (contents and body); narrow with
`awk '/^6\.2\.2\.3 *UMD PDU/,/^6\.2\.2\.4/'`.

**Cite the clause** on any assertion that encodes a requirement — number and rule, not a
quotation with commentary. PDU header sizes and field layouts are normative (§6.2.2,
§6.2.3), so a test may pin them.

**SN comparisons are modular (§7.1).** Subtract the per-entity base, then compare:
transmitting AM → `TX_Next_Ack`; receiving AM → `RX_Next`; receiving UM →
`RX_Next_Highest − UM_Window_Size`. At rest a 12-bit UM entity's reassembly window is
`[2048, 4096)`, so a "large" SN like 3000 is *inside* it and discarded — the opposite of
plain-integer intuition. The spec says inside/outside a window, never above/below.
Reasoning non-modularly here has produced a bogus deviation report before.

**Pin observable behaviour, not internal state.** Phrase a test as calls and outcomes
("survives maxRetxThreshold retransmissions, then fails"), with presets relative to
`conf.max_retx_threshhold`, so a correct refactor of an internal counter doesn't break
it.

**Drive functions through shapes the dispatcher actually produces.** Calling a static
directly with a field combination `rlc_arq_rx_status` never generates hides real bugs.

## The test doubles

**Fixture.** `tests/util/fixture.hh` — `fixture::rlc_ctx` wraps an `::rlc_context` in
gabs's `handle_wrapper` so the instance can be recovered from the raw pointer handed to
the C listener. Two usage patterns:

- Contexts built by the real `rlc_init` (`test_am.cc`, `test_um.cc`, `test_tm.cc`):
  `fx.attach_listener(events.listener())`, which goes through `::rlc_attach_listener`.
- Contexts hand-built to reach a static (`test_rx.cc`, `test_arq.cc`): never call
  `rlc_init`, so `ctx->lock` is uninitialized. Use `fx.on_event(...)` plus a direct
  `ctx.listener = fixture::rlc_ctx::listener_trampoline;`.

**Events.** `util/event.hh` — `event::event_handler` records emitted events;
`pop(type)` requires the next one to be of that type and consumes it, `empty()`/`size()`
report what is left. `rlc_event`'s payload is a **union**: branch on `ev.type` before
reading it. `RLC_EVENT_RX_DONE_DIRECT` carries a `gabs_pbuf *` (pointing at the caller's
stack), not an SDU — a helper testing `ev.sdu != nullptr` misreads it. TM's only
delivery path is `RX_DONE_DIRECT`.

`rlc_event_tx_fail` and `rlc_event_tx_done` both report `RLC_EVENT_TX_RELEASE`, so
delivered-successfully and gave-up must be told apart some other way (e.g. the receiver
never got anything).

**SDUs and buffers.** `util/fake_sdu.hh` — holds its own reference so it stays readable
after the code under test drops the one it was given; `strong()` hands a reference to
something that takes ownership. `util/buf.hh` — `buf::pbuf_ptr`, with `create()`,
`strong()`, `from_weak()` (adopts a borrowed buffer by increfing first) and `vec()`.

**Timers.** `tests/gabs-overrides/timer/` offers `default_resolver` (real thread, real
delay) and `manual_resolver` (never fires on its own). Drive the latter with
`timer_ctx.fire(timer.gtimer)`, which blocks until the callback has run, and query
`timer_ctx.armed(timer.gtimer)` for the synchronous "meant to be running" state —
`gabs_timer_active()` cannot answer that, because stopping is asynchronous by design.

**Extend a test double's API; never change what an existing call means.** Add a named
extension rather than reaching into internals. If `struct timer` grows a member, keep
synchronization primitives declared *before* the `jthread` that uses them — reverse
destruction order otherwise tears down the mutex under a running thread.

Any scenario needing *two* STATUS reports must use `manual_resolver`: `pump()` runs
everything with no real delay, so a real `t-StatusProhibit` armed by the first report
will not have expired by the second. Symptom of getting this wrong is too few
`TX_RELEASE`/delivery events with everything else correct.

A unit-style case that reaches a timer needs both a `gabs_override::timer_ctx` *and* a
real `rlc_timer_install` — the override's `stop()` dereferences the handle, and a null
`gtimer` segfaults. `std::logic_error("No instance registered")` skipping cleanup shows
up as a leak spike, not an obvious error.

**Loopback.** `peer_link` connects two entities: `drop` discards a PDU, `hold` keeps one
back and releases it after the next. `pump()` grants both sides transmit opportunities
until neither has anything left, first argument before second — the argument order is
the interleaving. The link records each data PDU's SN and offset as the peer saw them.

An AM PDU that empties the transmission buffer is polled unconditionally (§5.3.3.2), so
any single-SDU AM transmission drags a poll, a STATUS report and possibly a
retransmission behind it. Design scenarios around that rather than fighting it.

## Reaching statics

A `test_*.cc` may `#include` a `.c` file inside `extern "C" { }`, with the file excluded
from the `rlc` target by `remove_sources(...)` in `tests/CMakeLists.txt` (currently
`tx.c`, `rx.c`, `arq.c`). The **whole** file must then parse as C++ — C99 compound
literals with nested designators (`(struct x){.ext.has_range = ...}`) are the construct
that has broken this before, and C++20 forbids them. That is a `src/` change, so it
needs user sign-off; the pure-syntax rewrite is `memset` to zero then assign fields,
matching the existing idiom.

## `GENERATE`

Keep a generator whose axis **could** diverge later, even with no coverage gain today —
`test_rx.cc`'s `GENERATE(RLC_AM, RLC_UM)` in `alarm_reassembly`, `test_am.cc`'s
`sender_first` (same lines in a different order, invisible to line coverage). Drop one
that is symmetric by construction and can never diverge — `a_sends` in the loopback
cases was removed for that reason. To measure: build with `-DRLC_COVERAGE=ON`, run
`gcovr --txt` with the generator in place and pinned to one value, and diff. Identical
output is necessary evidence for removal, not sufficient.

## Running

```sh
cmake -S tests -B tests/build
cmake --build tests/build -j"$(nproc)"
cd tests/build && ctest --output-on-failure    # one entry per TEST_CASE
ctest -R process_nack                          # a subset
./tests "[am]"                                 # or drive Catch2 by tag
```

Sanitizers — the baseline is **clean**, so treat any finding as a regression from the
change under test:

```sh
cmake -S tests -B <dir> -DCMAKE_BUILD_TYPE=Debug \
  -DFETCHCONTENT_SOURCE_DIR_CATCH2=tests/build/_deps/catch2-src \
  -DFETCHCONTENT_SOURCE_DIR_GABS=tests/build/_deps/gabs-src \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fsanitize-recover=address -fno-omit-frame-pointer -g" \
  -DCMAKE_CXX_FLAGS="<same>" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build <dir> -j"$(nproc)"
ASAN_OPTIONS=halt_on_error=0:detect_leaks=1 <dir>/tests
```

`-fsanitize-recover=address` with `halt_on_error=0` matters: without them the first
memory error aborts the binary and Catch2 reports **zero** results.

Coverage — gcovr is run by hand, no CMake target wraps it:

```sh
cmake -S tests -B <dir> -DRLC_COVERAGE=ON
cmake --build <dir> -j"$(nproc)" && (cd <dir> && ctest)
gcovr <dir> --root <repo> --filter <repo>/src --filter <repo>/include \
        --txt --print-summary --delete
```

The flags go on `rlc` as `PUBLIC` so `tests` inherits them; both targets need them,
since the `#include`d `.c` files' counters live in the test objects. gcovr matches
`--filter` against the path as written — a doubled slash silently matches nothing and
reports 0%.

## Style

Prefer **no comment**. When one is needed, a couple of plain sentences on what the code
does and why it has to work that way; then cut it to the single non-obvious fact.
Comments describe the code as it stands — no history, no "this used to", no rationale
for a fix (that goes in the commit message). A spec citation earns its place: clause
number and rule.

No trailing-underscore private members. Name a method for its effect — an accessor that
consumes is `pop()`, not `get()`.

Match the surrounding file: `rlc::test` namespace, `using namespace util;`, helpers in
an anonymous namespace, tags like `"[am]"`, `"[rx][static]"`.

## Finishing

Commit and push each unit of work without being asked. In a sandbox, `origin` is SSH and
the proxy only injects credentials for HTTPS:

```sh
git remote set-url origin https://github.com/sigmundklaa/librlc.git
```
