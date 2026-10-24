# TASK-NXR-206: Command retry idempotency

Status: Planned

Requirement: NXR-206; qualification review 46/100.

## Required behavior

The target shall return a cached response for an exact retry of the last command without repeating side effects.

## Reason and source finding

Transport retries need execution identity rather than blind re-dispatch of commands.

## Acceptance criteria

Duplicate START and STOP execute once; reuse of the same command ID with different bytes is rejected.

## Verification and deliverables

Drop the first response, resend identical and modified requests and count actual state transitions.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

One command in flight; one bounded request-response cache.

## Dependencies

NXR-137

## Inspected sources

- docs/hardware-in-loop.rst
- docs/hil-verification-plan.rst

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
