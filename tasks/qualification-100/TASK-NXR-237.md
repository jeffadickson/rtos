# TASK-NXR-237: Reset observation provenance

Status: Planned

Requirement: NXR-237; qualification review 77/100.

## Required behavior

The bench shall distinguish host-requested reset, watchdog reset, power interruption and unexplained reboot using available independent evidence.

## Reason and source finding

A new boot counter alone cannot explain why the previous execution ended.

## Acceptance criteria

Controlled reset cases produce distinct evidence; unavailable reset-cause hardware yields unknown rather than a fabricated cause.

## Verification and deliverables

Correlate reset line, target boot identity and retained hardware reason where supported.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Bounded reset event history and declared reset-domain wiring.

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
