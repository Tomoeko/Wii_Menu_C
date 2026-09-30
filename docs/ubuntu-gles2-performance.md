# Ubuntu GLES2 performance check

On September 28, 2026, the GLES2 Release build was tested in an Ubuntu 26.04.1
ARM64 Fusion VM with two virtual CPUs, Mesa 26.0.8 llvmpipe, and a 960 x 540
application window. The application requests an OpenGL ES 2.0 context. The menu
used a local prepared asset set with a populated first channel page.

## Findings and changes

The initial populated-grid draw time averaged roughly 35–38 ms per 120-frame
sample, with recurring 140–190 ms stalls and some longer outliers. A `perf` sample
attributed about 95% of process CPU samples to llvmpipe's generated code. Texture
residency stayed around 17–19 MiB under the 128 MiB budget, with no evictions.
Scene submission itself was under a millisecond in sampled frames; rasterizing
the screen repeatedly was the main application CPU cost.

The renderer compares complete draw commands against the preceding frame,
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

With the renderer and guest scheduling changes, populated-grid samples
mostly averaged 8–15 ms per draw. Some animation phases remained more expensive;
full-screen animation and transitions can still exceed the 16.7 ms frame budget.
These measurements do not guarantee 60 fps in every scene. Limiting llvmpipe to
one worker was also tested and made drawing slower, so worker scheduling remains
under Mesa's control.

## Renderer options and VM workaround

The GLES2 backend retains unchanged screen pixels on llvmpipe and softpipe when
EGL supports preserved window buffers. Changed regions are redrawn at the full
window resolution, in their original draw order. Unsupported systems use the
normal full redraw path. Set `WM_GLES2_RETAIN_FRAME=0` for a direct-rendering
comparison.

Set `WM_PROFILE_FRAMES=1` to print frame and draw timing every 120 frames,
plus texture memory and evictions. The reported work time excludes frame
pacing sleep.

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
