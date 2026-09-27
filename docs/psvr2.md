# PSVR2 port

The `codex/psvr2` branch adds a firmware 06.00 device adapter using the
native PSVR2 build system and its first-party `open_vrhmd` hardware
layer. The same menu scene and ES 2.0 renderer produce a 640×456 menu texture,
composed into both 2000×2040 eyes with black surrounds. The displayed aspect
defaults to 16:9; the logical raster and displayed aspect remain separate.

Final presentation uses a separate ES 2.0 shader. An accepted EGL dma-buf
fourcc does not prove the GPU's actual storage order or the native scanout's
color interpretation. At startup, two dim 2×2 patches at the outer framebuffer
edge are rendered, completed, and inspected through
the uncached ION mapping. This determines GL row direction and RGB/BGR byte
order. The adapter also reads the format and red/blue swap bit from all four
MDP_RDMA strip engines. In the verified 24-bit mode, SWAP clear selects packed
little-endian RGB888: its memory bytes are B,G,R. SWAP set instead expects
R,G,B bytes. The final swizzle combines that native contract with measured
GPU stores; CPU framebuffer filler names are not treated as format evidence.
Every sampled pixel and RDMA channel must agree; unfamiliar storage or native
formats stop startup. The probe clears its patches back to black before
returning. The selected projection and optional red/blue swizzle affect only
final scanout, preserving
scene FBO coordinates and asset colors. Normal frames perform no pixel probe
or framebuffer readback. The startup log records the observed bytes and the
chosen correction; physical orientation and color still require headset
validation for each supported device configuration.

## Build the device executable

Run this in your native PSVR2 build checkout, supplying the menu checkout:

```sh
./build.sh wii-menu --project /path/to/menu-checkout --firmware 06.00
```

The native builder uses its saved AArch64 userspace compiler, Zig's glibc 2.28
profile, `/lib/ld-2.28.so`, firmware macros, stripping and build receipts.
CMake supplies the maintained menu source list. Host extraction tools and
tests are excluded from this cross build. No X11 server runs on the headset.

The linker needs local AArch64 `libEGL.so.1` and `libGLESv2.so.2` from a
clean firmware 06.00 runtime export. Supply
`--runtime-root /path/to/device-runtime` when the native build checkout has
not configured its default local runtime export. The libraries remain local and are not
redistributed. The device already supplies their runtime dependencies.

Output is `output/psvr2-build/06.00/tools/wii-menu-folder/wii-menu`, with
`output/psvr2-build/06.00/tools/wii-menu.json` recording source and artifact
hashes. Other source families are rejected by this adapter.

## Prepare the adjacent asset pack

Run host preparation in the Wii Menu repository:

```sh
cmake -S . -B .local/host-build -DWM_BUILD_APP=OFF
cmake --build .local/host-build --parallel
.local/host-build/wm-prepare --wad .local/input/menu.wad \
  --output .local/wii-menu.wm
```

Supply `--nand .local/input/nand.bin` on that command when available, with
`--nand-keys` if the matching BootMii footer is absent. If NAND was omitted,
add it later to the same package:

```sh
.local/host-build/wm-prepare --nand .local/input/nand.bin \
  --output .local/wii-menu.wm
```

This loads the existing menu pack, uses the established channel update
pipeline, adds imported channels/fonts/layout data, and atomically replaces
the complete pack. Existing channels and configuration files are retained
by default. A failed import leaves the previous package intact. Repeating
`--wad` during this enrichment preserves the existing base menu; use a new
output path to prepare a different base. Directory preparation and explicit
channel replacement policies remain available.

To package an already converted asset tree:

```sh
.local/host-build/wm-pack pack .local/native-assets .local/wii-menu.wm
```

The `.wm` format carries relative filenames, byte sizes and CRC32 checksums,
including every converted configuration file in that tree. It rejects links,
special files, unsafe paths, duplicate entries, oversized files and damaged
or truncated data. It is an uncompressed container, not an archive of raw
WADs, NAND dumps or keys. Generated packs are ignored by Git.

On startup the device finds `wii-menu.wm` next to its own executable through
`/proc/self/exe`; its working directory does not matter. An executable named
`wii-menu.elf` also finds `wii-menu.wm`. `--assets /tmp/other.wm` overrides
discovery; directory assets remain supported. Extraction goes into a private
`/tmp/wii-menu-assets-*` tree before hardware initialization. Normal exit and
startup errors remove that owned tree. Runtime user-state changes in this RAM
tree disappear when the app exits; they do not rewrite the uploaded pack.

The pack is limited to 256 MiB of asset payload and 128 MiB per file;
filenames and record metadata add to the archive size. Budget for the
uploaded pack **and** its extracted tree, GPU textures, audio and process
memory. File-size limits do not establish that a near-limit pack fits the
headset's available RAM. Forced termination can leave a temporary extraction
tree until reboot.

## Upload executable first, pack second

Copy the prepared `.wm` into the output folder, then run this from the PSVR2
repository with a working firmware-matched Stage1:

```sh
cp /path/to/menu-checkout/.local/wii-menu.wm \
  output/psvr2-build/06.00/tools/wii-menu-folder/wii-menu.wm
psvr2_krw_c/build-release/psvr2_krw_c --no-serial \
  --fast output/psvr2-build/06.00/tools/wii-menu-folder/wii-menu \
  --fast output/psvr2-build/06.00/tools/wii-menu-folder/wii-menu.wm
```

The toolkit uploads in argument order and uses each file's basename. Wait
for both transfers to finish. The target paths are `/tmp/wii-menu` and
`/tmp/wii-menu.wm`. Launch through the existing target shell or toolkit
supervised job mode:

```text
krw s1exec chmod 755 /tmp/wii-menu
krw s1exec /tmp/wii-menu >/tmp/wii-menu.log 2>&1 & echo $! >/tmp/wii-menu.pid
```

The menu uses signals and the dedicated input endpoint; it does not require
a stdin FIFO. Stop the recorded process with SIGTERM and wait for its log
to confirm display/audio cleanup before launching another display owner or
replacing the executable. It takes over the stock display using the shared
`open_vrhmd` ownership checks and cooling controller. Stage3 is required for
the existing audio patch and dedicated pointer path. Uploading with
`--no-serial` preserves a loaded Stage3; it does not deploy a missing one.

## Unlocked host mouse

On macOS, the host tool is built with the preparation tools:

```sh
.local/host-build/wm-psvr2-pointer --input-port /dev/cu.usbmodemINPUT
```

Select the **second** ACM port belonging to Stage3's `ttyGS1` input bridge.
Stage3 must have been deployed with its explicit `double_evict=1` option to
provide that port. With the rebuilt Stage3 installed, initialize its input
endpoint at the toolkit prompt:

```text
krw s1exec echo 'input bridge' > /proc/stage3
krw s1exec ls -l /dev/fast_input
```

`input bridge` creates the software input ring for the second serial port;
it does not require a Sony input endpoint. Initialize it before launching
the menu. Use the toolkit's `--double-evict` startup option or
`krw serial reset double-evict` for the two-port layout after preparing its
complete matching module chain. The ordinary `ttyGS0` control shell
port is a separate service and cannot carry these packets. See the PSVR2
toolkit's Stage3 deployment instructions; the bridge does not replace or
reconfigure a running module automatically.

The window is black and resizable. Hover inside its menu area to position
the Wii pointer; left and right clicks retain their menu meanings. Resizing
rescales coordinates into the fixed logical menu. The host cursor stays
unlocked, and only window-local AppKit events are observed. There are no
global input hooks or accessibility permissions. Leaving the area, losing
focus, minimizing, closing, or losing the connection cancels held actions
and hides the headset pointer. Returning while holding a button requires
release before another press can act.

Movement is coalesced on an 8 ms tick; button/focus transitions send promptly.
Compact 16-byte binary state packets use session/sequence numbers, checksums
and button-edge counters. A target watchdog cancels stale input. Ambiguous
lost button transitions cancel the action rather than creating a click.
This avoids a shell command round trip for every mouse event. Stage3's
existing finite input ring can still drop packets during long stalls.
Software timing and packet rate do not establish measured end-to-end latency.

## Headset audio

The existing mixer supplies stereo S32_LE samples to the native DL12 SRAM
ring at 48 kHz. Playback uses the headset's headphone jack. The codec's
ADC-to-DAC sidetone sources are disabled, so ambient microphone sound is not
mixed into the menu output. Playback routing and attenuated headphone volume
are reapplied after the stock driver finishes its power sequence. Failure,
a stalled DMA cursor, and normal teardown mute the DAC before cleanup.
Headphone codes follow the firmware's 0 dB origin of 121. The 50% startup
setting requests -18 dB, and the accepted range never permits positive gain.
The earlier origin of 100 introduced an additional, unintended 21 dB of
attenuation; that calculation has been corrected.

The Stage3 audio patch response is checked as well as its write result; a
rejected patch stops audio initialization. `WM_PSVR2_AUDIO_STATS=1` enables
bounded, approximately once-per-second mixer and DMA diagnostics. Redirect
the application log to a regular RAM file when using these counters. They
report active music voices, pause/mute state, render lock misses, converted
samples, and cursor progress; converted-sample counters alone do not verify
physical buffer contents or audible output. Diagnostics are disabled by
default.

## Convergence and device calibration

The adapter reads `/data/optical_calib` from the **running headset**, using
the bounded X/Y fields at offset 32 from the existing 06.00 reference. It
never copies device calibration or identifiers from the retained VFS dump.
Missing/invalid calibration uses zero corrections and logs that limitation.
The asymmetric nominal optical centers follow `open_vrhmd`'s flat-plane
projection; factory pose fields and lens distortion are not inferred.

Optional presentation settings are shown in [psvr2-vr.conf](psvr2-vr.conf).
Upload it as `/tmp/wii-menu.vr.conf`, select another path with
`WM_PSVR2_CONFIG`, or include `psvr2/vr.conf` inside the asset pack. Packed
configuration is selected automatically unless the environment overrides it.
NAND enrichment preserves that file. Invalid or out-of-bounds settings fail
startup before display takeover.

Defaults are a 66-degree menu width, nominal 2 m convergence, 64 mm IPD,
16:9 aspect, zero additional offsets, and software luminance 0.65. Controls
are bounded to 40–70 degrees, 1–5 m, 50–75 mm IPD, ±32 pixels additional
convergence, ±150 pixels vertical offset, and luminance 0.1–0.8; the complete
picture must remain inside each eye. These are presentation parameters,
not a verified user-specific optical fit or a hardware panel brightness
setting. The menu and pointer occupy the same stereo plane.

## Validation and remaining device work

Host checks cover package round trips and corruption/path handling, atomic
NAND enrichment, pointer state recovery, and bounded stereo/configuration
geometry, scanout row direction, FBO corner UVs, and GPU/RDMA byte-order
classification. The canonical native build produces an AArch64 executable,
and the existing macOS renderer remains buildable.

The current port passed all 70 host tests. A real USA v4.3 WAD and
matching NAND import produced a 1,438-file, approximately 189.8 MiB package
with 13 channels and the shared font aliases. Extraction and repacking were
byte-identical, and a second extraction matched every input file. On a
firmware 06.00 headset, the executable and package were uploaded in that
order and the executable's `--help` completed successfully through the
device's real loader and graphics libraries. A subsequent 20-second run
completed native EGL/stereo framebuffer and codec initialization; SIGTERM
returned status 0, stopped the main and fan processes, and removed the
extracted asset tree. The software input bridge delivered a known 16-byte
packet unchanged from the second host ACM port to `/dev/fast_input`.
The refactored Stage3 was installed in persistent device storage and its
hash verified without capturing a partition backup. These checks establish
startup, cleanup and transport, not visual comfort or audible output quality.

Subsequent firmware 06.00 checks measured GPU stores as RGB with GL-bottom
at memory row zero, while all four RDMA engines required BGR bytes. The
separate final presentation corrects both contracts. A 15-second input run
delivered every left/right button transition without reconnects, rejected
packets, lost edges, or ring drops; see the
[pointer test details](../tools/pointer/README.md). Headset user testing
confirmed correct colors and normal pointer movement/click interaction.
Live home-menu audio
diagnostics showed an active, unpaused background voice and nonzero samples
in all 1,152 DL12 SRAM words, with a moving DMA cursor, DAC mute clear, and
both sidetone sources disabled. SIGTERM set the DAC mute bit. These register
and buffer checks do not establish acoustic output by themselves. After the
headphone gain-origin correction, headset user testing confirmed that menu
music is audible through the headphone jack with microphone monitoring
disabled. Audio continuity and quality still need longer device testing.

This is a device bring-up port with a head-relative flat menu. It does not
implement tracked world placement, lens-distortion compensation, or verified
vblank-synchronized presentation. The referenced driver assigns one shared
buffer to all scanout slots; GPU completion precedes presentation, but tearing
remains possible. The 120 Hz application deadline preserves time-based menu
animation and provides a polling target, not a measured frame-rate guarantee.
Stereo comfort, audio continuity, motion/click latency,
thermal behavior, and representative idle/page/preview/HOME frame times must
be measured before claiming production-quality VR behavior.
