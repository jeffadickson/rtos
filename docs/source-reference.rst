Annotated C++ source reference
================================================================================

The contracts in :doc:`api`, :doc:`synchronization`, :doc:`metrics` and
:doc:`web` are backed by source comments on lifetime, context, ownership, timeout
units and errors. Sphinx ``literalinclude`` renders the actual checked-in headers
below, without custom Python or another documentation generator. Internal comments
explain state-transition invariants rather than restating syntax.

Threads and storage
--------------------------------------------------------------------------------

.. literalinclude:: ../include/rtos.hpp
   :language: cpp
   :linenos:
   :caption: Static registration, ownership and coherent metrics

Synchronization and device ownership
--------------------------------------------------------------------------------

.. literalinclude:: ../include/rtos_sync.hpp
   :language: cpp
   :linenos:
   :caption: Counting tokens and sticky events

.. literalinclude:: ../include/rtos_queue.hpp
   :language: cpp
   :linenos:
   :caption: Copied queues and handoff lifetime

.. literalinclude:: ../include/rtos_mutex.hpp
   :language: cpp
   :linenos:
   :caption: Priority inheritance and whole-device transactions

.. literalinclude:: ../include/rtos_atomic.hpp
   :language: cpp
   :linenos:
   :caption: Single-hart RAM operations, not SMP or MMIO primitives

Queued output
--------------------------------------------------------------------------------

.. literalinclude:: ../include/rtos_log.hpp
   :language: cpp
   :linenos:
   :caption: Private formatting, copied records and bounded drain ownership

Implementation reading order
--------------------------------------------------------------------------------

Read ``src/scheduler.hpp`` for bounded selection/wakeup, then ``src/kernel.cpp``
for registration, dispatch and accounting. ``src/sync.cpp`` owns wait enrollment
and completion; ``src/mutex.cpp`` handles donation and owner death. ``src/log.cpp``
performs bounded conversion without a shared formatting buffer. The detail backend
is not an application API. Reset/trap and inline assembly are in
:doc:`assembly-port`; the complete first application is in
:doc:`minimal-application`.

Transport and storage boundaries
--------------------------------------------------------------------------------

.. literalinclude:: ../include/rtos_ethernet.hpp
   :language: cpp
   :linenos:
   :caption: Synchronous MAC ownership and bounded polling

.. literalinclude:: ../include/rtos_web.hpp
   :language: cpp
   :linenos:
   :caption: Single-owner HTTP sessions and caller-owned JSON output

.. literalinclude:: ../include/rtos_static.hpp
   :language: cpp
   :linenos:
   :caption: Non-synchronizing static containers and generation-checked loans
