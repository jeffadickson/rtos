# TASK-NXR-209: Capability validity encoding

Status: Planned

Requirement: NXR-209; qualification review 49/100.

## Required behavior

Optional telemetry fields shall carry explicit validity and capability metadata rather than treating zero as unsupported.

## Reason and source finding

Zero measurements can otherwise create false claims about unavailable hardware features.

## Acceptance criteria

A supported zero-error ECC counter differs from absent ECC hardware; unsupported latency does not display as zero latency.

## Verification and deliverables

Round-trip absent, valid-zero, saturated and stale metrics in schema fixtures.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Validity bits and typed units fit the fixed schema; no implicit feature inference.

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
