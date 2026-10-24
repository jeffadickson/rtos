# TASK-NXR-239: RISCue hardware claim boundary

Status: Planned

Requirement: NXR-239; qualification review 79/100.

## Required behavior

RISCue RTL, ECC and TMR hardware claims shall require the corresponding FPGA image and observable counters rather than commercial RISC-V silicon alone.

## Reason and source finding

A compatible ISA does not implement the custom processor's redundancy or protection hardware.

## Acceptance criteria

A commercial MCU run is accepted only for its HAL scope; absent ECC or TMR support cannot receive a hardware pass.

## Verification and deliverables

Validate image hashes, feature manifests and one controlled hardware event per claimed detector.

Deliver the implementation or fixture, discriminating negative tests, exact commands and exit codes, firmware/tool hashes, and retained evidence. Document unsupported configurations. A host test cannot close a hardware acceptance criterion.

## Configuration and resource ceiling

Opt-in qualification profile; boot-static storage; single CPU unless explicitly qualified otherwise.

Declared FPGA image and exposed counter widths; unsupported checks skipped explicitly.

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
