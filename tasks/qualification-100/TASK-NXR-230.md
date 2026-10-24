# TASK-NXR-230: Sacrificial worker reboot policy

Status: Planned

Requirement: NXR-230; qualification review 70/100.

## Required behavior

A test that permanently exits a boot-created worker shall declare the run non-rearmable without the required reset.

## Reason and source finding

Current exit semantics reserve storage and do not permit recreating finished workers.

## Acceptance criteria

After exit the monitor reports reboot required and rejects another run needing that worker.

## Verification and deliverables

Run exit-focused fixtures and then attempt rearm and start with the same worker inventory.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

No runtime thread resurrection in the default kernel profile.

## Dependencies

NXR-039, NXR-048

## Inspected sources

- src/kernel.cpp
- docs/hil-verification-plan.rst
- include/rtos_queue.hpp

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
