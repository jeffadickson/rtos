# TASK-NXR-235: Checker arming handshake

Status: Planned

Requirement: NXR-235; qualification review 75/100.

## Required behavior

A target run requiring an observer shall start only after the matching checker run identity is armed and acknowledged.

## Reason and source finding

An independently running checker can still observe the wrong target run without identity binding.

## Acceptance criteria

Unarmed, stale or wrong-profile checker rejects the run; target and checker terminal identities must match.

## Verification and deliverables

Race checker reset and host START while validating a physical start marker.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

One active checker run and finite arming deadline.

## Dependencies

NXR-099

## Inspected sources

- docs/hardware-in-loop.rst
- docs/hil-verification-plan.rst

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
