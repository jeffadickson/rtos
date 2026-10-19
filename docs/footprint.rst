Measured minimal configuration
==============================

Measured from ``build/nano/demo.elf``; ELF SHA256 ``1748f24060236e3df48f224bfedf043fddd41dc0ecc9d25d22e922e898d2f346``.

* Code and read-only data: **3812 bytes**.
* Initialized writable data: **0 bytes**.
* BSS: **1164 bytes**, including both512-byte thread/idle stacks.
* Allocated RAM span: **4976 bytes**, including alignment.
* Reserved boot/trap stack: **4096 bytes**.
* Total committed RISCue RAM: **9072 bytes**.
* Loadable code and initialized data: **3812 bytes**.

RISCue executes code from RAM, so RAM includes code, not just data+BSS.
This is the one-thread nano example; web, Ethernet, storage, metrics, guards
and synchronization are OFF. Host tools, HTML assets and debug sections are
excluded. These figures do not prove worst-case stack safety or board execution.
