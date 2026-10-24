# TASK-NXR-219: Safe test selection

Status: Planned

Requirement: NXR-219; qualification review 59/100.

## Required behavior

The manager shall start only declared allowlisted test IDs and reject arbitrary address, register or memory-write requests.

## Reason and source finding

The HIL monitor is a test executive rather than an unrestricted remote debugger.

## Acceptance criteria

Unknown IDs, out-of-range parameters and unsupported fault injection fail before START.

## Verification and deliverables

Fuzz command selection and validate that no raw debug command reaches the monitor.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Finite test registry and bounded parameter schema.

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
