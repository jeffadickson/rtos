Tang Nano and MicroBlaze bring-up
================================================================================

The concrete FPGA CPU and hardware export are currently unknown. Port work is
therefore staged as explicit hardware contracts and compiler/BSP scaffolding.
Neither target is advertised as runnable. VORAGO VA108xx/M0 and VA416xx/M4 also
remain catalogue profiles rather than implemented ARM context/BSP ports.

Capture the hardware first
--------------------------------------------------------------------------------

Copy the relevant example JSON in ``config/boards`` and fill every required null
from the actual bitstream/Vivado export. The templates select proposed CPU
contracts; their non-null identity/cache values are not detected hardware facts.
Verify those too. Then run the native C# tool:

.. code-block:: sh

   cmake --build --preset host --target configure_tool
   build/host/native-tools/NexRT.Configure/NexRT.Configure board-check . path/to/board.json

The check rejects absent inputs, RAM overflow/alignment, invalid timer/interrupt
settings and incompatible CPU identities. The board RAM extent is not a claim
that a particular image fits; the eventual linker must check that. Passing means structurally complete
metadata; it does not check a physical address, install a compiler or enable the
planned firmware profile. All firmware generation guards remain active.

Tang Nano 9K
--------------------------------------------------------------------------------

Sipeed documents the board as an FPGA platform supporting soft cores, including
PicoRV and a separate NEORV32 example. Therefore the board name does not establish
a machine-mode interrupt implementation. See
`Sipeed's board description <https://wiki.sipeed.com/hardware/en/tang/Tang-Nano-9K/Nano-9K>`_
and `NEORV32 example <https://en.wiki.sipeed.com/hardware/en/tang/Tang-Nano-9K/examples/neorv32>`_.

PicoRV32 uses custom interrupt instructions and q-registers; its IRQ path is not
interchangeable with this port's machine CSRs. See the upstream
`PicoRV32 interrupt contract <https://github.com/YosysHQ/picorv32>`_.

.. literalinclude:: ../ports/tang-nano-9k/README.rst
   :language: text

Classic MicroBlaze
--------------------------------------------------------------------------------

AMD specifies distinct register conventions and interrupt return behavior. The
port must preserve its own ABI and delay slots, rather than emit RV32 mret.
See `register usage <https://docs.amd.com/r/2024.1-English/ug984-vivado-microblaze-ref/Register-Usage-Conventions>`_,
`interrupt entry <https://docs.amd.com/r/en-US/ug984-vivado-microblaze-ref/Interrupt>`_
and `RTID semantics <https://docs.amd.com/r/2024.1-English/ug984-vivado-microblaze-ref/rtid>`_.
Compiler features/endian mode must match the exported design; see
`GCC MicroBlaze options <https://gcc.gnu.org/onlinedocs/gcc/MicroBlaze-Options.html>`_.

.. literalinclude:: ../ports/microblaze/README.rst
   :language: text

.. literalinclude:: ../cmake/microblaze-toolchain.cmake
   :language: cmake
   :caption: Compiler scaffold; the missing firmware port is still rejected

Qualification still required
--------------------------------------------------------------------------------

Each target needs a compiler/ABI probe, fully documented assembly, startup/vector
implementation, timer/interrupt adapter, board linker map and register/IRQ tests.
Then apply the shared scheduler/IPC, stack, allocator and static-constructor gates.
Optional UART, Ethernet, QSPI and DMA drivers follow actual hardware ownership
contracts. No CPU port gains SMP semantics from a local interrupt mask.
