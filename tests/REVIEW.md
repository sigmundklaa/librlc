# RLC test suite review

Review of the test suite against 3GPP TS 38.322 v18.1.0 (Release 18).

At the time of writing the suite is 75 test cases / 1298 assertions, all
passing, with no AddressSanitizer or UndefinedBehaviorSanitizer findings and no
leaks.

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
| `test_am.cc` | 952 | 16 | Integration (public API) |
| `test_um.cc` | 504 | 8 | Integration (public API) |
| `test_tm.cc` | 370 | 6 | Integration (public API) |
| `test_arq.cc` | 1224 | 18 | Unit (`arq.c` statics) |
| `test_rx.cc` | 499 | 6 | Unit (`rx.c` statics) |
| `test_tx.cc` | 220 | 2 | Unit (`tx.c` statics) |
| `test_encode.cc` | 282 | 6 | Unit (wire format) |
| `test_seg_list.cc`, `test_seg_buf.cc`, `test_list.cc` | 785 | 13 | Unit (containers) |

The three mode files share a common shape: a `fixture::rlc_ctx` wrapper, a
caller-owned `std::vector<captured_event>`, and for the loopback cases a
`peer_link` / `pump()` pair. That consistency is a strength - a reader who
knows one file can read the others.

---

## 3. Logical consistency with the specification

Most cases map cleanly onto a clause and assert the right thing. The
exceptions follow.

### 3.1 The two window-boundary tests do not test the window size they claim

`test_am.cc:268` and `test_um.cc:218`:

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
test would notice if the UM-specific window rules diverged from AM's.

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

§5.2.3.1 forbids submitting an AMD PDU whose SN falls outside the transmitting
window, and §5.3.3.2 and §5.3.3.4 both make *"no new RLC SDU can be
transmitted (e.g. due to window stalling)"* a trigger. With `window_size = 10`
this is reachable by queuing 11 SDUs. No test does.

### 4.3 ACK_SN construction

§5.3.4's closing bullet - *"set the ACK_SN to the SN of the next not received
RLC SDU which is not indicated as missing in the resulting STATUS PDU"* - runs
through `tx_status`, which has no test at any level. The receive-side
counterpart (`tx_ack`) is covered. This is the largest hole in ARQ coverage.

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

- **RX of a complete SDU with no SN.** §5.2.2.2.2's first branch - *"if the
  UMD PDU header does not contain an SN: remove the RLC header and deliver the
  RLC SDU to upper layer"* - is the SI=ALL fast path. The TX side is covered
  ("UM TX omits the SN when a segment fills the entire SDU"); the RX side is
  not.
- **TX_Next increment across SDUs.** §5.2.2.1.1 increments TX_Next only when a
  segment maps to the last byte of an SDU. Only a single segmented SDU is ever
  sent, so no test observes the SN advancing from one SDU to the next.
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

§5.3.3.3 is now implemented on all three NACK dispatch paths, and
`process_nack` and `process_nack_offset` each have a "NACK matching POLL_SN
stops t-PollRetransmit" section. `process_nack_range` has the implementation
(`rlc_window_has(&nack_win, ctx->arq.poll_sn)`) but no matching test - an
asymmetry that is easy to close.

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

1. **No reordering.** Delivery is strictly in submission order. A real link -
   especially one under HARQ - reorders. AM's out-of-order reassembly is
   tested, but only by hand-feeding `rlc_rx_submit` directly
   (`test_am.cc:203`); the loopback never produces it. Since reordering is
   precisely what drives RX_Next_Highest / t-Reassembly / STATUS logic, this
   is the most significant omission.
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
5. **Fixed, symmetric grants.** `pump()` gives both peers exactly `mtu` bytes
   every iteration, strictly alternating. Real MAC grants vary in size and
   arrive asymmetrically. Segmentation boundaries are therefore always the
   same across a run.
6. **No stochastic or bursty loss.** Every predicate is positional and
   deterministic. There is no seeded-random or burst model, so nothing
   resembles a soak or fuzz test.

None of these are defects in the existing cases - they bound what the suite
can find. Reordering (1) and varying grant size (5) are the two that would add
the most coverage for the least machinery, since both can stay fully
deterministic.

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

`process_nack` and `process_nack_offset` both have a "NACK matching POLL_SN
stops t-PollRetransmit" section. These look like copies but cover distinct
dispatch paths, and §5.3.3.3 has to hold on each. Keep both - and see §4.9
for the missing third.

---

## 7. Prioritised recommendations

**High**

1. Decide whether window size is configurable or derived from SN width per
   §7.2, then make the two window-boundary tests exercise the real edge
   (SN at the boundary and one past it) instead of a value far outside a
   window of 10.
2. Add SN wraparound coverage, and modulus-base comparisons to match §7.1.
   This needs an implementation change first; it is the largest correctness
   gap in the library.
3. Test ACK_SN construction (`tx_status`, §5.3.4).

**Medium**

4. Add reordering to the loopback link - a predicate that holds one PDU back
   and releases it after the next. Deterministic, and it would exercise the
   RX_Next_Highest / t-Reassembly / STATUS paths the way a real link does.
5. Cover window stalling (§5.2.3.1, §5.3.3.2, §5.3.3.4) by queuing more SDUs
   than the window admits.
6. Add the `process_nack_range` POLL_SN section (§4.9) for symmetry.
7. Cover UM's no-SN receive fast path (§5.2.2.2.2) and TX_Next incrementing
   across successive SDUs.

**Low**

8. Vary grant size across `pump()` iterations so segmentation boundaries are
   not fixed.
9. Collapse `should_start_reassembly` / `should_restart_reassembly` into one
   parameterised case.
10. Drop `tx_win_shift`; fold `last_segment` into the `test_list.cc` coverage.
11. Assert the §5.2.3.1 priority rules so they cannot regress silently.
