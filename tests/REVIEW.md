# RLC test suite review

Review of the test suite against 3GPP TS 38.322 v18.1.0 (Release 18).

Revised against the suite at 82 test cases / 1447 assertions. One assertion
fails by design, documenting the defect in §4.6; everything else passes, with
no AddressSanitizer or UndefinedBehaviorSanitizer findings and no leaks. Line
coverage of `src/` and `include/` is 88.6%, branch coverage 74.1%.

Items closed since the first revision are marked **[closed]** with the case
that closed them; the analysis is kept because it says what the case is for.

Contents:

1. [Scope and method](#1-scope-and-method)
2. [Inventory](#2-inventory)
3. [Logical consistency with the specification](#3-logical-consistency-with-the-specification)
4. [Specification cases not covered](#4-specification-cases-not-covered)
5. [Fidelity of the lossy-link simulation](#5-fidelity-of-the-lossy-link-simulation)
6. [Redundancy and implementation-coupled tests](#6-redundancy-and-implementation-coupled-tests)
7. [Prioritised recommendations](#7-prioritised-recommendations)

---

## 1. Scope and method

Every test file was read and each case matched against the clause it claims to
exercise. Clauses 4.2.1, 5.1-5.6, 6.2.2, 6.2.3 and 7.1-7.4 were walked in order
to find behaviour with no corresponding test.

Two categories are deliberately kept apart below:

- **Wrong** - the test asserts something the specification does not say, or
  asserts it for a reason that does not hold.
- **Missing** - the behaviour is specified and implemented (or specified and
  absent), but nothing verifies it.

Claims about the implementation were checked against the source rather than
inferred from test names.

---

## 2. Inventory

| File | Lines | Cases | Level |
| --- | --- | --- | --- |
| `test_am.cc` | 1097 | 20 | Integration (public API) |
| `test_um.cc` | 566 | 10 | Integration (public API) |
| `test_tm.cc` | 320 | 6 | Integration (public API) |
| `test_arq.cc` | 1368 | 19 | Unit (`arq.c` statics) |
| `test_rx.cc` | 442 | 6 | Unit (`rx.c` statics) |
| `test_tx.cc` | 220 | 2 | Unit (`tx.c` statics) |
| `test_encode.cc` | 280 | 6 | Unit (wire format) |
| `test_seg_list.cc`, `test_seg_buf.cc`, `test_list.cc` | 782 | 13 | Unit (containers) |
| `util/` | 927 | - | Shared helpers |

The three mode files share a common shape: a `fixture::rlc_ctx` wrapper, an
`event::event_handler`, and for the loopback cases a `peer_link` / `pump()`
pair. That consistency is a strength - a reader who knows one file can read
the others.

The helpers were per-file copies at the first revision and are now shared:
`util/event.hh` (event recording, `pop(type)` consuming in order),
`util/fake_sdu.hh` (an SDU owning its own reference so it survives the code
under test releasing one), `util/bytevec.hh`, and `buf::pbuf_ptr::from_weak`
for reading a borrowed buffer. `catch_discover_tests` registers one ctest
entry per case, and `-DRLC_COVERAGE=ON` instruments the build for gcovr;
`README.md` documents both.

---

## 3. Logical consistency with the specification

Most cases map cleanly onto a clause and assert the right thing. The
exceptions follow.

### 3.1 The window-boundary tests do not test the window size they claim

**[partly closed]** - the UM case was restated and now says what actually
discards the PDU; the AM one still reads as below.

`test_am.cc` and `test_um.cc`:

```c
/* RX_Next starts at 0, AM_Window_Size = 131072 for 18-bit SN; a SN
 * far beyond that is outside the window. */
proto::am::header hdr{false, proto::seginfo::ALL, 200000, std::nullopt};
```

Both comments quote the constant from §7.2, but neither entity is configured
that way. `default_config` and `um_conf` both set `window_size = 10`, and
`rlc_rx_init` uses `ctx->conf->window_size` verbatim. SN 200000 and SN 3000 are
outside a window of 10, so the assertions hold - but they would hold for
essentially any window, and they say nothing about 131072 or 2048.

The underlying issue is a specification deviation. §7.2 defines
`AM_Window_Size` and `UM_Window_Size` as **constants determined by the SN
width** (2048 / 131072 for AM at 12 / 18 bits; 32 / 2048 for UM at 6 / 12
bits). The implementation treats window size as a free configuration
parameter with no relation to `sn_width`. The tests encode that deviation
rather than flagging it.

The boundary that matters is untested either way: no case submits a SN
*just* inside and *just* outside the edge.

The UM case was also wrong about its own reasoning, in a way worth recording.
§7.1 makes the receiving UM entity compare SNs against a modulus base of
`RX_Next_Highest - UM_Window_Size`, so at rest its reassembly window is
`[2048, 4096)` and SN 3000 is *inside* it - discarded by §5.2.2.2.2's second
bullet (an SN the window has already passed), not for being outside anything.
Read modularly, `rlc_window_has`'s accept-set and the specification's
not-discarded set coincide whenever `window_size` holds a legal
`UM_Window_Size`, so there is no deviation on that path - only the
configurable-window one above. The case is now titled and commented
accordingly.

### 3.2 `adjust_poll_sn` - "never decreases an already higher poll_sn"

§5.3.3.2 says to **set** POLL_SN to the highest SN submitted; the
implementation takes a maximum (`sdu->sn > ctx->arq.poll_sn`). The two agree
while SNs advance monotonically, and the scenario the test constructs
(POLL_SN 9 while the only submitted SN is 1) is not reachable in normal
operation. The section already carries a comment saying it covers defensive
behaviour rather than a requirement; it is listed here for completeness rather
than as a defect.

### 3.3 `tx_pollable` cannot distinguish the condition it stands for

§5.3.3.2 triggers a poll when *"both the transmission buffer and the
retransmission buffer becomes empty"*. `tx_pollable` inspects only the unsent
list of the single SDU it is handed. All seven sections use one SDU, so none
can tell the two readings apart. A queue with SDU 0 drained but SDU 1 still
pending would poll under this implementation and should not under the
specification.

### 3.4 `tx_nack_clear` has no specification counterpart

Both sections assert that a below-ACK_SN SDU's unsent list is "trimmed to its
last segment". That is an internal invariant of this codebase (the last
segment is retained to distinguish retransmitted from first-time PDUs), not
anything §5.2.3.1 or §5.3.2 requires. The test is reasonable as a regression
guard but should not be read as specification coverage.

### 3.5 UM receive-window modelling

`test_um.cc` asserts `rlc_window_base(...) == 0` after a duplicate. UM has no
`RX_Next`; §5.2.2.2.1 defines its reassembly window relative to
`RX_Next_Highest`, and §7.1 gives UM the distinct variables
`RX_Next_Reassembly` and `RX_Timer_Trigger`. The implementation reuses one
`rlc_window` plus `next_highest` / `next_status_trigger` for both AM and UM,
and the tests follow it. This is consistent within the codebase but means no
test would notice if the UM-specific window rules diverged from AM's. The
`alarm_reassembly` case now runs under both types for that reason, even though
nothing on that path reads `conf->type` today.

### 3.6 Everything else checked out

Spot-verified as faithful: the SI/SO/SN/P/D-C encodings against §6.2.2.3-5 and
§6.2.3.3-5; the `RLC_STATUS_SO_MAX` trailing-gap encoding against §6.2.3.15's
special SOend value; NACK range as "number of consecutively lost SDUs
including NACK_SN" against §6.2.3.17; reserved-CPT rejection against §5.6.1;
the STATUS-collapse behaviour against §5.3.4; TM's no-header / no-segmentation
rules against §4.2.1.1.2-3; and the retransmission limit against §5.3.2 (the
implementation counts RETX_COUNT from one and raises the comparison to
compensate, which the test now states in terms of retransmissions served
rather than the counter's base).

---

## 4. Specification cases not covered

Ordered roughly by risk.

### 4.1 SN wraparound - the largest gap

§7.1 requires *all* state-variable and SN comparisons to use a modulus base
(TX_Next_Ack transmit-side, RX_Next receive-side), with arithmetic modulo 4096
/ 262144 for AM and 64 / 4096 for UM. There is no modulus logic anywhere in
`src/` or `include/`; `rlc_window_has` is plain arithmetic:

```c
return num >= win->base && num < win->base + win->width;
```

Nothing tests wraparound, and nothing currently could - no test runs long
enough to reach the wrap point. Every window check, `adjust_poll_sn`'s
comparison and `tx_ack`'s `sdu->sn >= sn` are affected.

### 4.2 Window stalling

**[closed]** - "AM TX refuses an SDU that would fall outside the window" and
"AM TX accepts again once an acknowledgement moves the window".

§5.2.3.1 forbids submitting an AMD PDU whose SN falls outside the transmitting
window, and §5.3.3.2 and §5.3.3.4 both make *"no new RLC SDU can be
transmitted (e.g. due to window stalling)"* a trigger.

The bound is covered: filling the window makes `rlc_tx` return `-ENOSPC`, and
an acknowledgement moving TX_Next_Ack admits the next SDU. The poll trigger is
a different matter. This implementation applies the bound at **ingress** -
`rlc_tx` refuses the SDU - where §5.2.3.1 phrases it as a bound on submitting
a PDU to the lower layer. The consequence is that the transmission buffer can
never hold an SDU that is not submittable, so "the buffer is non-empty but
nothing new can be sent" does not arise and `tx_pollable` has no stalling
condition. That trigger is therefore unreachable rather than untested, and no
test asserts it. Accepted as the intended API behaviour for now; a future
`rlc_tx` taking a timeout would keep the same shape.

### 4.3 ACK_SN construction

**[closed]** - the `tx_status` case, four sections.

§5.3.4's closing bullet - *"set the ACK_SN to the SN of the next not received
RLC SDU which is not indicated as missing in the resulting STATUS PDU"* - runs
through `tx_status`. Covered for an empty receive queue, a fully received
prefix, an SDU missing entirely (ACK_SN skips past the NACKed SN rather than
stopping at it) and one received in part.

Writing it found a real defect, since fixed in `src/`: `encode_last` set E1 on
every NACK set and cleared it only when the buffer could not hold another, so
a list that ended because nothing more was missing still claimed a successor,
contrary to Table 6.2.3.11-1. Nothing had noticed because the receive side
ignored `has_more` and decoded until the buffer ran out. Both sides now honour
E1, and the case asserts the header's E1 and the last set's independently.

### 4.4 Transmission-priority rules

§5.2.3.1 requires control PDUs to be prioritised over AMD PDUs, and
retransmissions over new transmissions. The first is implemented
(`rlc_tx_avail` calls `rlc_arq_tx_yield` before `rlc_tx_yield`); the second is
satisfied incidentally, because `rlc_tx_yield` walks the SDU queue in SN order
and retransmitted SDUs always hold lower SNs. Neither is asserted, so a
refactor could silently break either.

### 4.5 Delayed STATUS triggering

§5.3.4 says that when a polled PDU arrives with `RX_Highest_Status <= x <
RX_Next + AM_Window_Size`, the STATUS report is **delayed** until the
condition is met, rather than triggered immediately. Only the immediate branch
is tested.

### 4.6 UM specifics

- **RX of a complete SDU with no SN.** **[closed, and failing - an open
  defect in `src/`]** §5.2.2.2.2's first branch - *"if the UMD PDU header does
  not contain an SN: remove the RLC header and deliver the RLC SDU to upper
  layer"* - is the SI=ALL fast path, which the TX side happily produces.
  `rlc_rx_submit` has no such branch: it takes the SN path and reads
  `pdu.sn`, which `rlc_pdu_decode` leaves untouched when the header carries no
  SN field (only `pdu->flags` is zeroed, and `struct rlc_pdu pdu;` in
  `rlc_rx_submit` is uninitialised). The indeterminate SN then usually falls
  outside the receive window and the PDU is dropped - confirmed by
  `next_highest` never advancing. So a single-PDU UM SDU is silently
  discarded, and the read is undefined behaviour besides. "UM RX delivers a
  complete SDU that carries no SN" documents it with a `CHECK`, after reading
  its results out so the failure cannot skip teardown.
- **TX_Next increment across SDUs.** **[closed]** - "UM TX advances TX_Next
  from one SDU to the next": every segment of the first SDU carries SN 0,
  every segment of the second SN 1, so §5.2.2.1.1's "increment once a segment
  maps to the last byte" holds across SDUs.
- **Reassembly-window edges** per §5.2.2.2.3 (`RX_Next_Reassembly` updates,
  discard of PDUs outside the window) are untested at integration level.

### 4.7 Re-segmentation on retransmission

§5.3.2: *"if needed, segment the RLC SDU or the RLC SDU segment"* when
retransmitting into a smaller opportunity. Retransmission of a whole SDU and
of a NACKed byte range are both covered; retransmission that must be split
again because the new grant is smaller is not.

### 4.8 Entity re-establishment and release

§5.1.2 requires discarding all SDUs, segments and PDUs, stopping and resetting
all timers, and resetting all state variables. `rlc_reset` has no test.
§5.1.3 release is exercised only incidentally, as `rlc_deinit` cleanup;
nothing asserts that pending SDUs are discarded. (Previously deferred by
explicit instruction - noted here for completeness.)

### 4.9 `process_nack_range` POLL_SN handling

**[closed]** - two sections, since the check is a window test rather than an
equality: a range spanning POLL_SN stops t-PollRetransmit, and one clear of it
leaves the timer running. Without the negative section the assertion would
pass against an implementation that stopped the timer unconditionally.

### 4.10 Not implemented, so untested

§5.4 (SDU discard) and §5.5 (data volume calculation) have no public API.
Worth recording so their absence is a known state rather than an oversight.

---

## 5. Fidelity of the lossy-link simulation

The loopback model is the same in all three mode files: a backend submit
callback consults a `drop` predicate and, if the PDU survives, calls
`rlc_rx_submit` on the peer.

```c
[&link](::rlc_context *, ::gabs_pbuf buf) -> int {
        if (link.drop(buf)) { ::gabs_pbuf_decref(buf); return 0; }
        ::rlc_rx_submit(link.other, buf);
        return 0;
}
```

**What it models well.** Loss is applied per PDU with full visibility of the
PDU, so predicates can discriminate on content: `pdu_is_data()` reads the D/C
bit (§6.2.3.6) to separate AMD from STATUS, and `pdu_sn()` decodes the SN, so
`drop_sn(1)` can target one specific SDU among several in flight. Both
directions are exercised - the loopback cases use `GENERATE(true, false)` to
run each scenario with the loss on the A->B and then the B->A link. Loss
patterns cover the useful shapes: one PDU (`drop_first`), the first *n*
(`drop_up_to`), one specific SN (`drop_sn`), and a direction that never gets
through (`drop_always`, used for the give-up case). Determinism is total,
which is the right trade for a regression suite.

**Where it departs from a real link.** In rough order of how much they matter:

1. **No reordering.** **[closed]** `peer_link::hold` holds one PDU back and
   releases it after the next, swapping the two on the wire, and the link
   records each data PDU's SN and offset as the peer saw them so a case can
   assert the order really changed. Two AM cases use it: segments of one SDU
   arriving out of order, and an SDU overtaking its predecessor. Both fail if
   the hold is disabled, so they test the reordering rather than passing on
   the in-order path. UM and TM keep their own copies of the link and do not
   have it - UM is where it would exercise the t-Reassembly *drop* path.
2. **Zero latency, and delivery is synchronous and re-entrant.**
   `rlc_rx_submit` on the peer runs nested inside the sender's
   `rlc_tx_avail`, so a STATUS report can be produced before the sender's
   transmit call has returned. Real RLC sees a round-trip delay. This makes
   timer-driven behaviour reachable only by firing timers manually, which the
   tests do - but it also means no test ever observes the interleaving a real
   round trip produces.
3. **No duplication.** The link never duplicates a PDU. Duplicate handling is
   tested by submitting the same buffer twice by hand (AM and UM), never
   through the link.
4. **No corruption.** Nothing produces a malformed or truncated PDU on the
   wire. §5.6.1's reserved/invalid handling is covered only by a hand-built
   reserved-CPT PDU.
5. **Fixed grants, though no longer a fixed order.** `pump()` gives both peers
   exactly `mtu` bytes every iteration. Real MAC grants vary in size, so
   segmentation boundaries are always the same across a run. Which peer is
   served first is now a parameter - it decides whether a pending STATUS goes
   out ahead of the next data PDU or behind it - and one case generates both.
6. **No stochastic or bursty loss.** Every predicate is positional and
   deterministic. There is no seeded-random or burst model, so nothing
   resembles a soak or fuzz test.

None of these are defects in the existing cases - they bound what the suite
can find. With reordering closed, varying the grant size (5) is the remaining
one that would add coverage for little machinery and stay deterministic.

**A lifetime constraint the harness has to respect.** A submitted PDU is a
*view* over the SDU's transmit buffer, not a copy. `rlc_sched_yield` at the
end of `rlc_tx_avail` releases the SDU once its last segment has gone out, so
a test that queues TX PDUs and reads them after the call can be reading freed
memory - ASan caught exactly that while writing §4.6's TX case, which now
decodes each header inside the submit callback. The existing
`queue_submitter`-based cases do the same thing and happen to survive it;
that is dormant, not safe.

---

## 6. Redundancy and implementation-coupled tests

### 6.1 Near-duplicate: `should_start_reassembly` vs `should_restart_reassembly`

`test_rx.cc` tests these two predicates with five sections each, and the
section lists are near-identical ("more than one SDU pending", "one pending
SDU with a gap", "one pending SDU fully contiguous", "no SDU object at base",
"nothing pending"). The duplication mirrors the specification - §5.2.3.2.3's
start condition and §5.2.3.2.4's restart condition genuinely are the same
predicate at different moments - so this is defensible. But it is ten sections
of largely copied fixture code that a single parameterised case could express.

### 6.2 Redundant: `tx_win_shift`

`test_arq.cc:347` tests the helper directly, asserting the window base lands
on TX_Next or the head SDU's SN. The observable behaviour is already asserted
by the `tx_ack` sections, which check `rlc_window_base` after acking. The
helper test adds no independent signal.

### 6.3 Implementation-detail tests with no specification anchor

These assert internal flags or helper contracts rather than observable
behaviour:

| Test | What it asserts | Covered observably by |
| --- | --- | --- |
| `alarm_poll_retransmit` | `ctx.arq.force_poll == true`, backend request count | "AM TX retransmits the polled PDU when t-PollRetransmit expires" |
| `alarm_status_prohibit` | `ctx.arq.status_prohibit == false`, request count | "AM RX collapses multiple STATUS triggers under t-StatusProhibit" |
| `restart_status_prohibit` | `ctx.arq.status_prohibit == true`, timer armed | same as above |
| `tx_nack_clear` | unsent list trimmed to last segment | nothing - internal invariant only |
| `last_segment` | returns final list element | `test_list.cc` already covers list traversal |
| `highest_sn_submitted` | max SN among submitted SDUs | indirectly, via `adjust_poll_sn` |

They are cheap and they localise failures, which has real value - when
`retx_count` semantics changed, the unit tests pinpointed it immediately. The
cost is coupling: every one of these binds the suite to a `static` function
name in `arq.c`, so a behaviour-preserving refactor breaks tests. Worth being
deliberate about rather than accidental.

### 6.4 Marginal overlap, probably worth keeping

- "AM peers recover a lost data segment" vs "AM peers recover multiple lost
  segments of the same SDU" - the second generalises the first, but one-versus-
  many is a meaningful boundary.
- `deliver_ready` (unit) vs "AM peers advance the window and deliver in order
  around a dropped middle SDU" (integration) - same behaviour at two levels,
  which is the usual and reasonable arrangement.
- The three "peers exchange an SDU end-to-end" cases across AM/UM/TM look
  duplicative but exercise genuinely different code paths.

### 6.5 Not redundant despite appearances

`process_nack`, `process_nack_offset` and now `process_nack_range` each have a
"NACK matching POLL_SN stops t-PollRetransmit" section. These look like copies
but cover distinct dispatch paths, and §5.3.3.3 has to hold on each. Keep all
three.

### 6.6 Removed: running every loopback case in both directions

Each loopback case ran twice under `GENERATE(true, false)`, once with peer A
sending and once with peer B. The peers are configured identically and
`peer_link` is direction-agnostic, so the parameter only decided which fixture
was *called* the sender: pinning it left per-file coverage byte for byte
identical, lines and branches alike. Removed, along with the aliases it forced
(`sender`, `receiver_events`, `data_link` and the rest, all constant
references once the direction was fixed).

The rule that came out of it: a generator earns its runtime when the axis
could diverge later, even with no coverage gain today - `alarm_reassembly`'s
AM/UM split and the pump order both qualify - and not when the two runs are
symmetric by construction and can never diverge.

---

## 7. Prioritised recommendations

Recommendations 3-7 of the first revision are closed; §4 records what closed
each. What remains, re-prioritised:

**High**

1. **Fix the UM no-SN receive path** (§4.6). A single-PDU UM SDU is dropped,
   and the SN it is dropped on is read uninitialised. This is the only open
   defect the suite is currently failing on, and it is in `src/`, not the
   tests.
2. **Decide whether window size is configurable or derived from SN width** per
   §7.2, then make the AM window-boundary test exercise the real edge - a SN
   at the boundary and one past it - instead of a value far outside a window
   of 10.
3. **Add SN wraparound coverage, and modulus-base comparisons to match §7.1.**
   This needs an implementation change first; it remains the largest
   correctness gap in the library. The UM analysis in §3.1 is the worked
   example of why plain-integer comparisons mislead.

**Medium**

4. Cover the UM reassembly-window edges at integration level (§4.6, third
   bullet): `RX_Next_Reassembly` updates and discard of PDUs the window has
   passed.
5. Cover re-segmentation on retransmission (§4.7) - a NACKed SDU retransmitted
   into a grant smaller than the original.
6. Assert the §5.2.3.1 priority rules (§4.4) so they cannot regress silently.
7. Cover delayed STATUS triggering (§4.5), the branch of §5.3.4 that holds the
   report back rather than sending it at once.

**Low**

8. Vary grant size across `pump()` iterations so segmentation boundaries are
   not fixed (§5, item 5).
9. Give UM and TM the reordering the AM link now has, or lift `peer_link` into
   `util/` so all three share one (§5, item 1).
10. Convert the remaining `queue_submitter` cases to read PDUs inside the
    submit callback, before the view-lifetime hazard in §5 stops being dormant.
11. Collapse `should_start_reassembly` / `should_restart_reassembly` into one
    parameterised case (§6.1).
12. Drop `tx_win_shift`; fold `last_segment` into the `test_list.cc` coverage
    (§6.2).
