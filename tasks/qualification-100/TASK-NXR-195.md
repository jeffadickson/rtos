# TASK-NXR-195: First-fault retention scope

Status: Planned

Requirement: NXR-195; qualification review 35/100.

## Required behavior

Fault reports shall state whether their storage survives only the current boot or a qualified reset domain.

## Reason and source finding

The current FirstFault helper explicitly does not provide reset or double-fault retention.

## Acceptance criteria

Ordinary RAM is never labeled reset-retained; a later fault cannot overwrite the first fault in the same capture epoch.

## Verification and deliverables

Trigger two distinct faults and then each supported reset kind; compare retained identity and validity markers.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

One fixed first-fault record per capture epoch; no allocation in capture.

## Dependencies

NXR-072, NXR-073, NXR-094

## Inspected sources

- include/rtos_trace.hpp
- include/rtos_fault_record.hpp
- docs/hardware-in-loop.rst

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
