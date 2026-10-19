How it works: HTML developer guide
================================================================================

``site/how-it-works.html`` is a standalone host-side HTML guide. It covers the
kernel overview, complete minimal application/build, copied messages, shared AXI
driver transactions, assembly frame/startup, and port boundaries. The native C#
Workbench inserts the actual example/assembly source with HTML escaping and the
latest measured footprint; source listings are not separately copied by hand.

.. code-block:: sh

   cmake --build --preset host --target native_tools
   build/host/native-tools/NexRT.Workbench/NexRT.Workbench footprint . build/nano/demo.elf
   build/host/native-tools/NexRT.Workbench/NexRT.Workbench site .
   build/host/native-tools/NexRT.Workbench/NexRT.Workbench serve . 8080

Open ``http://127.0.0.1:8080/how-it-works.html``. The native preview binds loopback
only; it is not a board Ethernet/TCP implementation. The standalone guide can
also be opened directly as a local HTML file. The guide and its complete assembly
listings are not embedded in the nano or HTTP ROM image.

For the detailed Sphinx reference, continue with :doc:`minimal-application`,
:doc:`source-reference`, :doc:`assembly-port`, :doc:`synchronization`,
:doc:`drivers` and :doc:`port-bringup`.
