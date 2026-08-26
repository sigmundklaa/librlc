# AGENTS.md

Repo-wide guidance for agents working in `librlc`. Test-suite specifics live in
[`tests/README.md`](tests/README.md); an audit of the suite against the
specification lives in [`tests/REVIEW.md`](tests/REVIEW.md).

## What this is

An RLC (Radio Link Control) implementation for 5G NR, following 3GPP TS 38.322
v18.1.0. All three modes are implemented: AM (acknowledged, with ARQ), UM
(unacknowledged, with segmentation and reassembly) and TM (transparent, no
header). C11, no dynamic allocation of its own — mutexes, semaphores, timers,
logging and packet buffers all come from
[gabs](https://github.com/sigmundklaa/gabs), and the two allocators are passed
in at `rlc_init`.

The specification is not in the repo. Clause numbers in code, tests and commit
messages refer to TS 38.322 v18.1.0.

## Building

The root `CMakeLists.txt` is not standalone — it aborts with `Gabs not found`
unless a `gabs` target already exists. There are two entry points:

- **Zephyr module.** `zephyr/module.yml` and `zephyr/CMakeLists.txt`; the
  application's build provides gabs.
- **The test suite.** `tests/CMakeLists.txt` fetches Catch2 and gabs, then adds
  `../` as the `rlc` target. This is the way to build and check the library on
  a host.

```sh
cmake -S tests -B tests/build
cmake --build tests/build -j"$(nproc)"
cd tests/build && ctest --output-on-failure
```

Sanitizer and coverage recipes are in `tests/README.md`. `tests/build/` is
gitignored; do not commit it.

**Baseline.** 82 cases, 1447 assertions, one of them failing: "UM RX delivers
a complete SDU that carries no SN" documents an open defect in `src/`
(`REVIEW.md` §4.6). That case is the one exception to a clean run —
everything else passes, with no AddressSanitizer or UndefinedBehaviorSanitizer
findings and no leaks, at 88.6% line coverage. Treat any other failure or
finding as coming from the change in front of you.

The defect it documents is an uninitialised read, so its outcome is
build-dependent: under the sanitizer recipe the indeterminate SN lands inside
the receive window and all 82 cases pass, at 1450 assertions. A green
sanitizer run is not evidence the defect is gone.

## Layout

| Path | Contents |
| --- | --- |
| `include/rlc/` | Public headers. `rlc.h` pulls in the rest and defines `rlc_context` |
| `src/` | Implementation, plus the private `common.h`, `encode.h`, `arq.h`, `log.h` |
| `tests/` | Catch2 suite, its helpers and the fake timer backend |
| `zephyr/` | Zephyr module glue: `module.yml`, `Kconfig`, source list |
| `.claude/skills/writing-tests/` | General test-writing principles, not specific to this repo |

Within `src/`:

| File | Role |
| --- | --- |
| `rlc.c` | `rlc_init` / `rlc_deinit` / `rlc_reset`, listener attachment |
| `tx.c` | Transmit side: accept an SDU, segment it into a grant |
| `rx.c` | Receive side: decode, reassemble, drive t-Reassembly |
| `arq.c` | AM only — STATUS reports, polling, retransmission, the three timers |
| `encode.c` | PDU header encode/decode for UMD, AMD and STATUS (§6.2) |
| `sdu.c`, `seg_buf.c`, `seg_list.c` | SDU objects, reassembly buffer, segment lists |
| `sched.c`, `event.c` | Deferred work queue and the events delivered on it |
| `backend.c` | Calls out to the lower layer: submit a PDU, request a grant |
| `timer.c`, `log.c` | Thin wrappers over the gabs timer and logger |

Sources are listed in **both** `src/CMakeLists.txt` and
`zephyr/CMakeLists.txt`. Adding or removing a file means editing both, or the
Zephyr build silently diverges from the host one.

## Conventions

**Tests only.** Unless fixing the library is the task, do not edit `src/` to
make a test pass. A test that asserts the specification and fails is
documenting a defect — leave it failing and say so.

**Cite the clause.** Where code or a test encodes a requirement, name the TS
38.322 clause it comes from: the number and the rule, not a pasted quotation.
PDU header sizes and field layouts (§6.2.2, §6.2.3) are normative, so a test
may pin them.

**Style.** `.clang-format` is authoritative: LLVM base, 8-space indent, 80
columns, Linux braces, includes never re-sorted. Header guards are
`RLC_<NAME>_H__`, and public headers wrap their declarations in
`RLC_BEGIN_DECL` / `RLC_END_DECL`.

Prefer no comment; when one is needed, a couple of plain sentences on what the
code does and why it has to. Comments describe the code as it stands —
rationale for a change belongs in the commit message. No trailing-underscore
private members. Name a function for its effect: an accessor that consumes is
`pop()`, not `get()`.

**Commits.** `area: Sentence-case summary` (`arq:`, `tests:`, `tests: um:`,
`pdu:`), subject in the imperative, body wrapped at 72 columns saying why
rather than what.

## Things that mislead if you assume otherwise

**SN comparisons are modular.** §7.1 requires subtracting a per-entity modulus
base before comparing: `TX_Next_Ack` on the transmitting AM side, `RX_Next` on
the receiving AM side, `RX_Next_Highest - UM_Window_Size` on the receiving UM
side. Reasoning about SNs as plain integers gives confident wrong answers — at
rest a UM entity's reassembly window is `[2048, 4096)`, so a "large" SN like
3000 is inside it. The specification says *inside* / *outside* a window, never
*above* / *below*. Note the implementation does not yet do this (`REVIEW.md`
§4.1); it compares raw values, so wraparound is an open correctness gap.

**`rlc_event`'s payload is a union.** Branch on `ev.type` before reading it.
`RLC_EVENT_RX_DONE_DIRECT` carries a `gabs_pbuf *`, not an SDU, and the pointer
is to the caller's stack. `rlc_event_tx_done` and `rlc_event_tx_fail` both
report `RLC_EVENT_TX_RELEASE`, so success and give-up look alike from outside.

**A submitted PDU is a view, not a copy.** The buffer handed to
`rlc_backend_tx_submit` points into the SDU's transmit buffer, and
`rlc_sched_yield` releases the SDU once its last segment has gone out. Reading
a PDU after the transmit call has returned can be reading freed memory; read it
inside the submit callback.

**An AM PDU that empties the transmission buffer is polled unconditionally**
(§5.3.3.2), so any single-SDU AM transmission drags a poll, a STATUS report and
possibly a retransmission behind it.

**Stopping a timer is asynchronous.** `gabs_timer_active()` therefore cannot
answer "is this timer meant to be running?" — a stopped timer can still report
active, and `timer_alarm` re-checks it under the context lock for exactly that
reason. The test suite's timer double exposes the synchronous state separately.

## Known gaps

`tests/REVIEW.md` §7 carries the prioritised list. The open items with the
widest blast radius: the UM no-SN receive path (a defect, currently failing),
SN wraparound and modulus-base comparisons, and whether window size is
configurable or derived from SN width per §7.2.
