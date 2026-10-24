# TASK-NXR-236: Checker fault injection controls

Status: Planned

Requirement: NXR-236; qualification review 76/100.

## Required behavior

Each observer detection channel shall have a controlled injection proving it can fail the run.

## Reason and source finding

An FPGA signature accumulator can appear healthy while its comparison or reporting path is disabled.

## Acceptance criteria

Disable a detector or alter a known-bad waveform; qualification detects the missing expected failure.

## Verification and deliverables

Run byte corruption, missing sequence, timing overrun and reset injections before acceptance tests.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Finite preflight corpus and explicit injection-enabled profile.

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
