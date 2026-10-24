# TASK-NXR-218: Raw and decoded evidence linkage

Status: Planned

Requirement: NXR-218; qualification review 58/100.

## Required behavior

Every decoded report shall retain a link to raw bytes and decoder version for later reanalysis.

## Reason and source finding

JSON summaries alone cannot diagnose decoder defects or framing loss.

## Acceptance criteria

A malformed frame remains available as raw evidence; changing decoder version produces a separate interpretation.

## Verification and deliverables

Replay an archived byte stream through two schema versions and compare stable raw hashes.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Raw capture quota declared; any discarded bytes have explicit counters.

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
