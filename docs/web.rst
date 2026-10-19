Web content and serial transport
================================

The optional ``nexrt::web`` component serves one bounded read-only HTTP request
per connection using caller-provided immutable assets. ``RTOS_WEB`` selects it.
It is a protocol component, not a TCP/IP stack, MAC driver or TLS implementation.
GET and HEAD support exact registered paths. Query text is ignored; percent
encoding, traversal, duplicate paths and header injection are rejected.
Requests have a511-byte limit and at most16 fields. HTTP/1.1 requires one Host.
Request bodies, transfer encoding, duplicate/nonzero Content-Length, upgrades,
keep-alive and pipelining are unsupported. The response always closes.

``Session::feed`` reports consumed input bytes; discard the remainder when a
complete request is accepted because this connection cannot carry another.
``pump`` accepts synchronous consume/copy callbacks, partial progress and busy.
It performs at most16 callbacks of at most1024 bytes, constrained further by
the caller's budget. The owner checks ``expire`` using one monotonic clock and
resets only for a new connection. Asset paths, MIME strings and byte arrays
must outlive the session. One owner serializes access; these are not SMP locks.

The fixed-capacity JSON writer escapes names and returns no usable output on
exhaustion. Snapshot kernel metrics first, then format outside interrupt-masked
sections into an owner-held buffer; keep it immutable until the send completes.

Ethernet integration
--------------------

Connect the same session to an existing stack's TCP receive/send callbacks.
Copy or consume stack buffers according to that stack's ownership rules.
The current Ethernet adapter only handles frames; it does not provide IP/TCP.
A real MAC/PHY, bounded pools and protocol timers remain platform work.

Serial integration
------------------

A board UART stream can feed exactly the same HTTP bytes. It must implement
nonblocking bounded receive/transmit callbacks and explicit framing/error policy.
The RISCue programming monitor is not automatically a firmware console.
The browser cannot treat a serial device as an ordinary HTTP URL. The standalone
introduction includes a user-triggered Web Serial viewer for supported desktop
browsers on HTTPS/localhost. It sends GET /, applies a timeout/64KiB limit and
shows the response in a script-disabled, network-disabled sandbox. Device/core,
baud rate and protocol must be confirmed; no unknown device is opened silently.

Host preview and ROM assets
---------------------------

``NexRT.Workbench serve . 8087`` previews ``site/index.html`` on loopback.
``footprint``, ``site`` and ``assets`` generate measured HTML and immutable C++
bytes. The firmware ``web_link_probe`` cross-links those bytes and the HTTP
component and passes allocator audit; it is a link probe, not a board server.
The full introduction is excluded from the measured nano image.

References: `HTTP/1.1 framing <https://www.rfc-editor.org/rfc/rfc9112.html>`_,
`HTTP semantics <https://www.rfc-editor.org/rfc/rfc9110.html>`_,
`Web Serial <https://developer.chrome.com/docs/capabilities/serial>`_,
and `lwIP HTTP server <https://www.nongnu.org/lwip/2_1_x/group__httpd.html>`_.
