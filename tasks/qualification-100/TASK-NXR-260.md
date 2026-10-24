# TASK-NXR-260: Ten-review milestone integrity

Status: Planned

Requirement: NXR-260; qualification review 100/100.

## Required behavior

The campaign shall build and verify each ten-review boundary before making its local milestone commit.

## Reason and source finding

Counting authored rows is not equivalent to executing 100 runtime engineering changes and must remain labeled as requirements review.

## Acceptance criteria

Boundaries 10 through 100 each retain command logs, catalogue hash and successful exit codes; a failed command prevents commit.

## Verification and deliverables

Run the documented local checkpoint workflow and verify milestone ancestry without network operations.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Ten bounded milestones; no push; source and build evidence retained separately.

## Dependencies

NXR-134, NXR-135, NXR-159

## Inspected sources

- include/rtos_dma.hpp
- src/dma.cpp
- tests/dma.cpp
- docs/dma.rst

## Completion checklist

- [ ] Implement required behavior without weakening default contracts.
- [ ] Pass the specific positive and negative acceptance checks above.
- [ ] Record resource measurements and limits for the tested profile.
- [ ] Retain evidence and update requirement status through the qualified evidence workflow.
