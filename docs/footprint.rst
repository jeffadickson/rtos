Measured minimal configuration
==============================

Measured from ``build/nano/demo.elf``; ELF SHA256 ``fb9614f0967cb4eaf04a46bdc36f8f67901bd0e7ab7dca7272b92504765417e7``.

* Code and read-only data: **3648 bytes**.
* Initialized writable data: **0 bytes**.
* BSS: **1152 bytes**, including both512-byte thread/idle stacks.
* Allocated RAM span: **4800 bytes**, including alignment.
* Reserved boot/trap stack: **4096 bytes**.
* Total committed RISCue RAM: **8896 bytes**.
* Loadable code and initialized data: **3648 bytes**.

RISCue executes code from RAM, so RAM includes code, not just data+BSS.
This is the one-thread nano example; web, Ethernet, storage, metrics, guards
and synchronization are OFF. Host tools, HTML assets and debug sections are
excluded. These figures do not prove worst-case stack safety or board execution.
