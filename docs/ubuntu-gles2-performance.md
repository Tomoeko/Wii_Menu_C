# Ubuntu GLES2 performance check

On September 28, 2026, the GLES2 Release build was tested in an Ubuntu 26.04.1
ARM64 Fusion VM with two virtual CPUs, Mesa 26.0.8 llvmpipe, and a 960 x 540
application window. The application requests an OpenGL ES 2.0 context. The menu
used a local, ignored prepared asset set with a populated first channel page.
This is a software-renderer measurement, not a PowerVR GX6250 result or a native
Wii fidelity comparison.

## Findings and changes

The initial populated-grid draw time averaged roughly 35–38 ms per 120-frame
sample, with recurring 140–190 ms stalls and some longer outliers. A `perf` sample
attributed about 95% of process CPU samples to llvmpipe's generated code. Texture
residency stayed around 17–19 MiB under the 128 MiB budget, with no evictions.
Scene submission itself was under a millisecond in sampled frames; rasterizing
the screen repeatedly was the main application CPU cost.

The new renderer compares complete draw commands against the preceding frame,
including geometry, texture handles, materials, colors, and clips. It marks both
old and new locations when a command changes, then clears and replays intersecting
draws in order within disjoint damaged regions. Unchanged pixels remain in EGL's
preserved window buffer. Larger changes trigger a full repaint. This preserves
resolution, sampling, animation updates, and blending; it does not throttle
channel animation or lower the rendering resolution. Unsupported EGL configurations
retain the original direct path.

The recurring long pauses had a second cause. An independent process waking every
10 ms also recorded 150–160 ms delays, while the menu process consumed only about
3 ms of CPU during a typical 150 ms stall. Removing the output stream for diagnosis
removed the delays after the virtual device suspended. Enabling IRQ scheduling
for the virtual HD Audio output then removed those delays with playback enabled:
the repeated 15-second independent timer check recorded no wake-up gap over 50 ms.
The guest override and its reversal are documented below. This observation
isolates a scheduling problem in the VM's active output path; it does not establish
which component of the virtualization or sound stack contains the underlying bug.
The user also confirmed substantially smoother operation after the change.

With the renderer and guest scheduling changes, recent populated-grid samples
mostly averaged 8–15 ms per draw. Some animation phases remained more expensive;
full-screen animation and transitions can still exceed the 16.7 ms frame budget.
These measurements do not guarantee 60 fps in every scene. Limiting llvmpipe to
one worker was also tested and made drawing slower, so worker scheduling remains
under Mesa's control.

## Validation

The synthetic GLES2 comparison renders frames 0–19 at both 321 x 241 and
960 x 540 using direct and retained rendering. Every RGBA byte must match. Cases
cover unchanged frames, inserted/removed/moving elements, fractional clips,
translucent overlapping draws, fallback and specialized TEV materials, alpha tests,
scene capture replacement, texture destruction during a frame, command-capacity
overflow, fades, and changing clear alpha. A linker wrapper reads pixels before
swap only in the test executable; the runtime has no readback instrumentation.
The test verifies preserved swap behavior so a silent fallback cannot count as a
retained-rendering pass. It passed normally and with
`MESA_GLES_VERSION_OVERRIDE=2.0`.

Portable tests also cover dirty-region generation, storage bounds, and audio
control contention. The Metal application and its 70-test suite build and pass on
macOS. The frame-damage test also passes AddressSanitizer and
UndefinedBehaviorSanitizer. On Ubuntu, 70 tests pass headlessly and the graphical comparison passes
separately with an active display. No extracted assets, captures, or VM account
configuration are tracked in the repository.

`WM_PROFILE_FRAMES=1` reports frame work, draw averages, and maximum draw time in
120-frame batches along with texture residency and eviction counts. It excludes
the application's explicit frame pacing sleep. Measurements with tracing enabled
are diagnostic only; the normal runtime contains no per-draw timing or forced GPU
completion calls. Build and run commands remain in the README.

## Reproducing the checks and VM workaround

The GLES2 backend retains unchanged screen pixels on llvmpipe and softpipe when
EGL supports preserved window buffers. Changed regions are redrawn at the full
window resolution, in their original draw order. Unsupported systems use the
normal full redraw path. Set `WM_GLES2_RETAIN_FRAME=0` for a direct-rendering
comparison. In a graphical Linux session, run the pixel comparison with:

```sh
ctest --test-dir build-gles2 --output-on-failure -R wii-menu-gles2-retained
```

Without a display or preserved buffers, this test reports a skip.

For an affected Fusion guest using the same virtual output, save the following as
`~/.config/wireplumber/wireplumber.conf.d/90-wii-menu-vm-scheduling.conf`, then run
`systemctl --user restart wireplumber.service` inside the guest:

```ini
monitor.alsa.rules = [
    {
        matches = [
            { node.name = "alsa_output.pci-0000_01_01.0.analog-stereo" }
        ]
        actions = {
            update-props = {
                api.alsa.disable-tsched = true
            }
        }
    }
]
```

Check the device name with `wpctl inspect @DEFAULT_AUDIO_SINK@`; the rule must
match the VM's output node. Removing this file and restarting WirePlumber restores
the previous scheduling. This setting uses interrupts instead of timer-based
scheduling, as documented in the
[PipeWire ALSA properties](https://pipewire.pages.freedesktop.org/pipewire/page_man_pipewire-props_7.html).
