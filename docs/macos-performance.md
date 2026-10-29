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
