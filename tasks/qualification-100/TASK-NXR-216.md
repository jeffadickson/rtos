# TASK-NXR-216: Host restart recovery

Status: Planned

Requirement: NXR-216; qualification review 56/100.

## Required behavior

After manager restart the host shall reconcile archived identity and live target state before issuing any state-changing command.

## Reason and source finding

Process restart is a separate state transition from target reset.

## Acceptance criteria

A live old run is attached as observed or stopped by explicit policy; it is never silently duplicated.

## Verification and deliverables

Kill the host after START and before terminal persistence, then restart with persisted manifest.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Bounded recovery handshake deadline and one active lease.

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
