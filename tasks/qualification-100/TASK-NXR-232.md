# TASK-NXR-232: Observer calibration identity

Status: Planned

Requirement: NXR-232; qualification review 72/100.

## Required behavior

Every timing result shall identify checker clock, sampling resolution, synchronizer uncertainty and calibration validity.

## Reason and source finding

An external clock is independent but still requires measurement uncertainty accounting.

## Acceptance criteria

Expired or mismatched calibration prevents a timing pass; known pulse widths fall within declared uncertainty.

## Verification and deliverables

Apply a reference pulse train at boundary widths and compare captured intervals.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Calibration version and finite validity interval recorded per checker.

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
