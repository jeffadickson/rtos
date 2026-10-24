# TASK-NXR-191: Random stream versioning

Status: Planned

Requirement: NXR-191; qualification review 31/100.

## Required behavior

Randomized tests shall identify the generator algorithm and version as well as the seed and stream identifier.

## Reason and source finding

A seed alone cannot reproduce a test after the generator or stream partition changes.

## Acceptance criteria

A golden vector reproduces across host and target builds; changing the algorithm without changing version fails.

## Verification and deliverables

Publish fixed seed vectors including zero and maximal seeds and compare independent implementations.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Fixed generator state per worker; no entropy dependency for replay.

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
