# macOS fullscreen rendering

Metal window rendering fits the display backing dimensions within 2560 × 1440,
preserving their aspect ratio. Core Animation scales the drawable to the window.
The logical scene projection stays separate from the drawable and display sizes;
pointer coordinates use the drawable's letterboxed viewport. Smaller windows
retain their backing resolution. Offscreen previews and recording keep their
existing sizes, including full backing resolution for recording.

The renderer keeps sample shading and antialiasing enabled. It reuses depth and
multisample textures at stable sizes. During a macOS fullscreen transition it
skips window presentation, allowing AppKit to finish without waiting for an
unavailable drawable. Offscreen rendering and recording can continue.
Layer size changes are flushed to Core Animation before requesting the next
drawable, avoiding a compositor wait that blocks the event-pumping thread.

On October 29, 2026, a Release Metal build on an M2 Max running macOS 14.6 was
profiled on the menu grid at 5120 × 2880 display resolution, with antialiasing
enabled. `WM_PROFILE_FRAMES=1` reported about 29–30 ms of frame work before this
change and about 1–2 ms afterward. The original transition also included a
roughly 1000 ms draw stall. Process samples measured approximately
371 MiB of physical footprint before and 195 MiB afterward. These are host
measurements, exclude frame pacing sleep, and do not establish native Wii
fidelity or performance on other devices. Fullscreen recordings still incur
full backing-resolution rendering and synchronous capture costs.

On November 3, 2026, the remaining grid and channel-preview GPU cost was profiled
without creating or activating an application window. A standalone `CAMetalLayer`
and the real Release renderer drew the local USA 4.3 menu scenes using a simulated
5120 × 2880 backing size, the existing 2560 × 1440 drawable, and four samples.
Each case ran for 240 animated frames, paced at 60 Hz. GPU command-buffer timing
excluded frame sleep and diagnostic readback. The comparison used the general
shader and prepared shader variants in the same build on the same M2 Max/macOS
14.6 host:

| Scene | General shader, mean GPU time | Prepared variants, mean GPU time |
| --- | --- | --- |
| Warm grid | 8.53 ms | 3.98 ms |
| Channel preview and zoom | 8.07 ms | 5.92 ms |
| Message Board | 0.43 ms | 0.43 ms |
| Options | 0.49 ms | 0.50 ms |

The shared Metal backend specializes the fixed TEV stage instructions, swap
tables, and alpha-test operations during material preparation. Colors, texture
handles, and alpha references remain dynamic. Draw order, sampling, blending,
resolution, and antialiasing are unchanged. A bounded cache retains up to 128
program/blend combinations; unprepared or unavailable variants use the same
general shader. This reduces shader work without lowering rendering quality.

Paired renders replayed identical queued commands through both paths. At
2560 × 1440, grid, channel preview, Message Board and Options captures had zero
changed RGBA bytes. Separate full 5120 × 2880 capture comparisons also matched.
The off-screen synthetic regression covers one through six TEV stages, dynamic
registers and alpha references, comparison operations, blending, sampler modes,
antialiasing toggles, cache exhaustion, and compilation failure.

An unpaced run of the same scene sequence measured roughly 162 MiB versus
154 MiB peak process footprint with `/usr/bin/time -l`; shader-cache memory is
included. These off-screen measurements establish renderer behavior and GPU
headroom. They do not measure native fullscreen compositor timing, establish
native Wii fidelity, or guarantee the same timings on other hosts.
