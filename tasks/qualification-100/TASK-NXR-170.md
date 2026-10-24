# TASK-NXR-170: Performance regression envelope

Status: Planned

Requirement: NXR-170; qualification review 10/100.

## Required behavior

The performance gate shall compare fixed workloads only under matching hardware, compiler, features and measurement methods.

## Reason and source finding

An observed speed difference is meaningless when build or measurement conditions change.

## Acceptance criteria

Changed profile identities prevent direct speedup claims; a seeded slow-path injection crosses and fails the recorded threshold.

## Verification and deliverables

Retain baseline distributions, repetitions, machine identity and confidence method alongside raw observations.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Per-workload threshold and repeat count fixed before candidate execution.

## Dependencies

NXR-081, NXR-025

## Inspected sources

- include/rtos.hpp
- src/kernel.cpp
- src/scheduler.hpp
- docs/memory.rst

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
