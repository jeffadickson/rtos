# TASK-NXR-165: Feature removal differential

Status: Planned

Requirement: NXR-165; qualification review 5/100.

## Required behavior

The release gate shall compare disabled-feature firmware against enabled firmware using identical toolchains and application behavior.

## Reason and source finding

Optional source compilation does not guarantee that associated static storage disappears.

## Acceptance criteria

Disabled DMA, logging and tracing remove their symbols and reserved pools; a hidden residual pool fails.

## Verification and deliverables

Retain map diffs and ELF symbol checks for one toggle at a time.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Disabled feature contributes zero feature-owned RAM and code beyond documented shared dependencies.

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
