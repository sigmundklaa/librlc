---
name: writing-tests
description: Principles for writing and changing tests in any language or framework. Use when adding a test, extending a test suite, building or modifying test helpers, fixtures and doubles, or investigating a failing, flaky or leaking test. Covers what a test is allowed to assert, how helpers own resources, keeping tests deterministic, and reading validation output honestly.
---

# Writing tests

Apply these on top of whatever the project's own conventions are. Read a neighbouring
test file first and match its structure, naming and tags; a test that looks foreign to
its file is harder to maintain than one that repeats a local idiom you dislike.

## What a test is for

**A test states what the code should do, not what it currently does.** Derive the
assertion from the specification, the documented contract, or the requirement in the
task — not from running the code and recording the output. Recording current behaviour
produces a test that passes forever and detects nothing.

**When the task is to write tests, a failure is a finding, not a blocker.** Don't edit
the code under test to make an assertion pass unless fixing it is part of the ask.
Report the failure with the assertion that produced it and what the contract says should
have happened, and let the person who owns the code decide.

**Keep the test neutral about its own outcome.** No "(known bug)" in the test name, no
comment explaining that it currently fails or how the implementation gets it wrong.
Those go stale as soon as the code moves, and they encode today's defect into something
meant to outlive it. The test asserts correct behaviour; the commit message or your
report carries the news that it doesn't hold yet.

**Pin observable behaviour, not internal state.** Assert through the public surface —
return values, emitted events, persisted rows, bytes on the wire. A test that asserts an
internal counter's base value or a private field's shape breaks on a correct refactor,
which trains people to edit tests until they pass. Where a threshold matters, phrase the
test relative to the configured value ("survives N retries, then fails") rather than
hard-coding the internal representation of N.

**Cite the source of a requirement.** When an assertion encodes a rule from a spec, RFC,
standard or ticket, name it — the clause number and the rule, not a pasted quotation.
That citation is the one comment in a test that reliably earns its place, because it
answers "why this number?" for the next reader.

## Exercising the code the way it is really called

**Drive functions through the shapes their real callers produce.** Calling an internal
function directly with a field combination the dispatcher above it never generates
proves nothing and hides real bugs behind a passing test. If you must call an internal
directly, construct the input the way the production path would.

**Know the domain's own rules before asserting on ordering or comparison.** Many domains
define comparison that is not ordinary integer or lexical order — sequence numbers on a
modular ring, version precedence, collation, floating-point tolerance, time across
zones. Reasoning with everyday intuition in such a domain produces confident, wrong
assertions and bogus bug reports. Look up the comparison rule, then use the domain's own
vocabulary in the test (inside/outside a window, precedes/follows) rather than
smuggling in above/below.

**Read tagged unions and variant payloads by their tag.** Where a result carries a
payload whose meaning depends on a type field, branch on the field before touching the
payload. A helper that sniffs "is this pointer non-null?" silently misreads one variant
as another, and every test built on that helper is wrong in the same invisible way.

**Reaching internals.** When a language hides what you need (statics, private members,
unexported functions), prefer the project's established escape hatch over inventing a
new one — a test-only accessor, a friend declaration, including the implementation file
into the test translation unit, an internal-visibility test target. Check what the
existing tests do. Note that such tricks impose constraints on the source file (it must
stay parseable, linkable or importable in the test's context), so a source change that
breaks the trick blocks every test that relies on it, not just the one you are writing.

## Helpers, fixtures and doubles

**A helper releases only what it acquired itself.** Never have a fixture or destructor
tidy up state the code under test left behind — a still-queued object, an armed timer, a
held reference, an open handle. That leftover state is exactly the class of bug the leak
detector exists to catch, and a helpful cleanup converts a real defect into a silent
pass, in the one place nobody re-reads.

When removing that cleanup makes the run leak, work out who owns the leak rather than
putting the cleanup back:

- the code under test owns it → genuine finding, leave it and report it;
- the test's own fixture owns it → tear down at the fixture's level, doing what the
  real teardown API would have done.

**Set up completely, tear down completely.** If the code has an init/deinit or
open/close pair, a test that calls one calls the other. Leaks that trace back to a
skipped teardown are noise that buries real findings.

**Extend a double's API; never change what an existing call means.** A test double
should model the real thing's semantics faithfully, including inconvenient ones — if
stopping is asynchronous in production, the double's `stop` stays asynchronous. When a
test needs a different question answered, add a named test-only extension rather than
redefining an existing method or reaching into the double's internals. Redefining
silently changes the meaning of every existing caller.

**Prefer one shared fixture over near-identical copies.** When three test files grow the
same setup struct, collapse them. Name each helper for its effect: an accessor that
consumes what it returns is `pop`, not `get`; one that hands out ownership says so.

## Determinism

**Replace real time, real threads and real I/O with something you drive.** A test that
sleeps, races a background thread, or waits on a network is slow and flaky, and both
failures look like the code's fault. Use a manual clock or timer the test fires
explicitly, and make firing synchronous — the call should return only after the callback
has finished, so the assertion after it sees a settled state.

**Watch for helpers that collapse time.** A pump/drain/run-until-idle helper executes
everything with no real delay between steps. Any scenario whose correctness depends on a
real interval elapsing (a cooldown, a prohibit timer, a debounce) must use the
controllable double and fire it explicitly, or it will fail for reasons that have
nothing to do with the behaviour under test. The symptom is usually one missing event
with everything else correct.

**Order dependence is a bug in the test.** Each case sets up what it needs and leaves no
global state behind. Where a scenario genuinely depends on interleaving, make the
interleaving an explicit parameter of the test rather than an accident of execution
order.

## Parameterized tests

A parameter that adds no coverage today is still worth keeping when the code under test
**could** diverge along that axis later — a path shared between two modes that may grow
a branch, or an ordering that line coverage cannot distinguish. Drop a parameter only
when the runs are symmetric by construction and can never diverge.

To decide, measure: run the suite with the parameter in place and again pinned to one
value, and diff the coverage output. Identical output is necessary evidence for removal,
not sufficient — the question is whether divergence is possible, not whether it happens
now.

## Reading validation output honestly

**Know the baseline.** Before blaming a finding on pre-existing noise, establish what a
clean run looks like — sanitizer reports, leak counts, warning counts, failing-test
list. With a clean baseline, any finding is a regression from the change in front of
you.

**Never label a finding "expected" as a first move.** Check two things first: does
sibling code in this repo already handle this correctly, in a pattern you should be
matching; and is there a cheap correct fix. Only after both come up empty is
"acceptable as-is" an honest conclusion. The habit of explaining findings away is how a
real defect gets committed with a rationalization attached.

**Configure the tooling so one failure doesn't hide the rest.** A sanitizer that aborts
on first error, a runner that stops at the first failing case, or a crash that kills the
process can report zero results for a suite full of information. Prefer the
recover-and-continue settings, and if a crash in one case would take down the whole run,
isolate that case (excluded from the default run, still runnable on demand) rather than
losing every other result.

**An intentionally-failing assertion must release everything first.** In frameworks
where a failed assertion unwinds or aborts the case, any cleanup written after it never
runs, and the leak it causes pollutes the baseline for everyone. Read the observed value
into a local, release resources, then assert. Where a case has shared teardown and
several checks, use the non-fatal assertion form so teardown still runs.

**Run the suite the way CI does before reporting done**, and report the result
faithfully: if tests fail, say so with the output; if you skipped a scenario, say which
and why.

## Style

Prefer **no comment**. When one is genuinely needed, write a couple of plain sentences
on what the code does and why it has to work that way, then cut it to the single
non-obvious fact; if nothing non-obvious remains, delete it. Comments describe the code
as it stands — no history, no "this used to", no rationale for a fix. That belongs in
the commit message, attached to the diff it explains.

Let the test name carry the intent, so the failure output reads as a sentence about what
broke. Keep each case to one behaviour; a case asserting five unrelated things reports
only the first failure.
