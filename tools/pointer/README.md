# PSVR2 host pointer window

`wm-psvr2-pointer` is a first-party macOS AppKit tool. Its black, resizable
content area represents the entire Wii Menu. Hover moves the headset cursor;
left and right mouse buttons keep their menu meanings. The host pointer stays
visible and free to leave the window. The tool uses ordinary window events:
there is no mouse capture, pointer warping, global event tap, accessibility
permission, or network connection.

The initial content size is 960 x 540, matching the current menu's 16:9
presentation. Resizing stretches the mapping across the entire content area.
Coordinates are normalized independently on each axis and then mapped to the
menu's 640 x 456 anamorphic raster. Resizing also remaps a stationary pointer.
This mapping does not change the headset presentation. When the headset VR
configuration selects a 4:3 plane, resize the host window to a matching 4:3
content area; both aspects preserve the same logical 640 x 456 raster.

Build on macOS using the normal **host** compiler, separately from the headset
cross compilation:

```sh
cmake -S . -B build-host -DWM_BUILD_APP=OFF
cmake --build build-host --target wm-psvr2-pointer
build-host/wm-psvr2-pointer --input-port /dev/cu.usbmodemINPUT
```

The path above is a placeholder. Select the **second Stage3 ACM interface**,
which corresponds to `/dev/ttyGS1` on the headset. Do not select the first
interface: `/dev/ttyGS0` is the control shell and upload connection. The pointer
tool never opens a control-shell session or runs a shell command for motion.
It requires an explicit input-port path and requests exclusive host serial
ownership. An in-use port is retried without disrupting its owner.

The current Stage3 module defaults to one ACM port. A pointer connection needs
its explicit `double_evict=1` configuration; that configuration evicts Sony
data8/data9, so follow the firmware-specific Stage3 setup in the native PSVR2
toolkit. It also needs `/dev/fast_input` initialized with the rebuilt module's
`input bridge` command, which creates a software ring without taking over a
Sony input endpoint. Run only
one consumer of `/dev/fast_input`; `input_verify` or another controller reader
would compete with the menu. The target intentionally does not mutate Sony
USB endpoints by itself. Kernel bridge initialization supports ring setup
after the serial bridge starts.

The window title shows whether the host serial descriptor is connected. This
does not prove that the headset menu is receiving input; the existing kernel
bridge is one-way and has no acknowledgement channel. A missing device is
retried once per second. Leaving the content area, losing focus, minimizing,
or pressing Escape immediately sends an inactive state that cancels held
actions and hides the menu cursor. A held button is suppressed after
cancellation or reconnect until it is physically released. Clicking the black
window activates it with the normal macOS focus behavior. Hover is accepted
while the window is key and the application is active; movement or resize in
an inactive window cannot undo focus-loss cancellation.

## Transport and latency

The host sends fixed 16-byte `CT` packets through the dedicated serial
descriptor. This preserves the existing Stage3 bridge framing and avoids
per-motion filesystem or process overhead. Version 1 contains flags, both
button states, a session ID, wrapping sequence, normalized X/Y, separate
wrapping left/right transition counters, and CRC-16/CCITT-FALSE. Multi-byte
fields are little endian. The target validates all fields, discards stale or
duplicate sequences, resynchronizes partial/corrupt streams, and bounds its
event storage.

Mouse movement coalesces over an 8 ms tick (nominally 125 updates/second).
Button changes and cancellation send immediately with the latest coordinates.
An active heartbeat uses the same 8 ms tick; inactive heartbeats use 50 ms.
Nonblocking writes happen directly on events and resume through a serial
writability dispatch source when necessary. The bounded host queue replaces
only unsent movement snapshots; it preserves queued button transitions and
partial writes. A full queue or more than 100 ms of write backpressure closes
the transport. The target cancels input after 250 ms without a valid advancing
packet, even when the serial endpoint remains open.

These are scheduling intervals and failsafe limits, not measured end-to-end
latency. AppKit scheduling, USB transfers, the existing Stage3 bridge polling,
and the headset render loop all add finite delay. The existing bridge has a
15-packet usable ring and can drop packets if the target stalls. Button
counters detect missing transition history: the target cancels ambiguous held
actions and suppresses currently held buttons instead of inventing clicks.
There is no delivery guarantee for a click during a device stall or disconnect.
The serial baud setting is only a CDC line-coding value; it is not a measured
USB transfer rate. Headset responsiveness and frame-time profiling remain
hardware validation tasks.

Focused tests cover resize mapping, every single-bit packet corruption,
all packet split points, noise resynchronization, sequence wrap, stale packets,
left/right transitions, dropped edge detection, reconnect/timeout cancellation,
bounded event consumption, and host motion coalescing under write backpressure.
They run without extracted assets or a headset:

```sh
cmake --build build-host --target wii-menu-psvr2-pointer-protocol-test \
    wii-menu-psvr2-pointer-transport-test
ctest --test-dir build-host -R 'psvr2-pointer' --output-on-failure
```

The macOS tool's black content area and native zoom resizing were visually
inspected. A separate firmware 06.00 device check sent one inactive packet
through the real transport to the second ACM port and received identical
bytes from `/dev/fast_input`. This verifies USB delivery, while interactive
headset behavior and end-to-end latency still require measurement.
