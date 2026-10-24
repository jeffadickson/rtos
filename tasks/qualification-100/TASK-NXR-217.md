# TASK-NXR-217: Cancellation cleanup evidence

Status: Planned

Requirement: NXR-217; qualification review 57/100.

## Required behavior

Host cancellation shall record stop request, acknowledgement, cleanup deadline and residual target state.

## Reason and source finding

Returning from a cancelled task does not prove that the device stopped running.

## Acceptance criteria

Cancel during handshake, running and result fetch; each phase preserves a truthful terminal outcome.

## Verification and deliverables

Use cancellation injection at every awaited operation and a target that refuses stop.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Finite cleanup allowance; no indefinite wait after host cutoff.

## Dependencies

NXR-076, NXR-094

## Inspected sources

- docs/hil-verification-plan.rst
- requirements/hil-design.csv

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
