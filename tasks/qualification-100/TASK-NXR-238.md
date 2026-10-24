# TASK-NXR-238: Supply and disconnect domain

Status: Planned

Requirement: NXR-238; qualification review 78/100.

## Required behavior

Power interruption tests shall document which target, bridge and checker domains lose power and preserve evidence outside the tested domain.

## Reason and source finding

USB power wiring can reset the evidence collector along with the target.

## Acceptance criteria

Target-only power cycling does not erase checker evidence; shared-power loss is explicitly classified incomplete.

## Verification and deliverables

Execute the declared wiring matrix and verify reconnect identity and retained files.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Equipment current and voltage limits declared per board; no undeclared reset assumptions.

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
