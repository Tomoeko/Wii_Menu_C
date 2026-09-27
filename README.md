# Wii Menu in C

Disclaimer: This project heavily utilizes Codex/ChatGPT.

This is a port of [Wii_Menu_HTML](https://github.com/Tomoeko/Wii_Menu_HTML).
The current renderer targets OpenGL ES 2.0; Apple builds use a separate Metal
adapter.

The `codex/psvr2` branch includes a firmware 06.00 PSVR2 adapter, adjacent
`.wm` asset packs with later NAND enrichment, and an unlocked macOS mouse
bridge. It uses the public
[PSVR2_Research toolkit](https://github.com/Tomoeko/PSVR2_Research).
See the [macOS deployment steps](#psvr2-on-macos) and
[PSVR2 packaging, input and VR status](docs/psvr2.md).

## Build

```sh
# macOS
cmake -S . -B build -DWM_BACKEND=metal
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# Linux: configure with -DWM_BACKEND=gles2 and install system X11, EGL,
# and OpenGL ES 2.0 development headers.
```

Use `-DWM_BUILD_APP=OFF` to build the portable core, preparation tools, and
tests without a windowing or graphics SDK. Executables keep their existing
names and locations under the build directory.

## PSVR2 on macOS

Use the `codex/psvr2` branch and firmware **06.00**. Keep `Wii_Menu_C` and
`PSVR2_Research` in sibling directories; the paths below work from any parent
directory. Follow the toolkit's
[macOS build instructions](https://github.com/Tomoeko/PSVR2_Research/blob/main/docs/build.md#macos-from-a-fresh-checkout)
to build its host program, matching modules, and BusyBox. A first headset setup
also needs its
[RAM deployment sequence](https://github.com/Tomoeko/PSVR2_Research/blob/main/docs/build.md#ram-deployment-check),
which loads `rmmod_helper` before Stage1. A verified persistent installation
can supply that initial module chain instead.

From the Wii Menu checkout, load Homebrew's environment, build the host tools,
and prepare your own assets. Use `/usr/local/bin/brew` in the first command
on an Intel Mac:

```sh
eval "$(/opt/homebrew/bin/brew shellenv)"
cmake -S . -B .local/host-build -DWM_BUILD_APP=OFF
cmake --build .local/host-build --parallel
.local/host-build/wm-prepare --wad .local/input/menu.wad \
  --nand .local/input/nand.bin --output .local/wii-menu.wm
```

NAND is optional; it can be added later to the same `.wm` with `wm-prepare
--nand`. See the [packaging details](docs/psvr2.md#prepare-the-adjacent-asset-pack)
for key-footer requirements and configuration preservation.

Open **one macOS Terminal in the PSVR2_Research checkout** for the following
commands. Start with the menu and pointer application closed.

1. Set portable paths, build the headset executable, and place its pack beside it:

   ```sh
   WM_MENU=../Wii_Menu_C
   WM_KRW="$PWD/psvr2_krw_c/.local/build-release/psvr2_krw_c"
   WM_TOOLS="$PWD/output/psvr2-build/06.00/tools"
   WM_FILES="$WM_TOOLS/wii-menu-folder"
   WM_MODULES="$PWD/output/psvr2-build/06.00/modules"
   export PSVR2_BUSYBOX="$WM_TOOLS/busybox"

   ./build.sh wii-menu --project "$WM_MENU" --firmware 06.00 \
     --runtime-root .local/inputs/psvr2-runtime
   cp "$WM_MENU/.local/wii-menu.wm" "$WM_FILES/wii-menu.wm"
   ```

   The runtime root must contain your local AArch64 firmware 06.00
   `lib/libEGL.so.1` and `lib/libGLESv2.so.2`. Those graphics libraries, WADs,
   NAND dumps, console-specific keys, and converted assets are not bundled
   in either repository.

2. Upload the executable **first**, the `.wm` **second**, then BusyBox and the
   matching serial modules. Enable both Stage3 serial ports:

   ```sh
   "$WM_KRW" --tmp --no-serial \
     --fast "$WM_FILES/wii-menu" \
     --fast "$WM_FILES/wii-menu.wm" \
     --fast "$WM_TOOLS/busybox" \
     --fast "$WM_MODULES/u_serial.ko" \
     --fast "$WM_MODULES/usb_f_acm.ko" \
     --fast "$WM_MODULES/stage3_serial.ko" \
     --command 'serial reset double-evict'
   ```

   Use the toolkit's automatically detected firmware; it must report 06.00.
   Wait for USB reconnection and `OK acm x2 active after USB reset`. This
   sequence loads the RAM candidates. It does not install persistent files.

3. Initialize the mouse input bridge before starting the menu:

   ```sh
   "$WM_KRW" --tmp --no-serial \
     --command "s1exec echo 'input bridge' > /proc/stage3 && test -c /dev/fast_input && cat /proc/stage3"
   ```

   Expected: `OK input bridge ttyGS1 software ring`. Select the software
   `input bridge` route. Hardware endpoint takeover is not used for the mouse.

4. Start the menu in the background and record its PID:

   ```sh
   "$WM_KRW" --tmp --no-serial \
     --command 's1exec chmod 755 /tmp/wii-menu && { /tmp/wii-menu </dev/null >/tmp/wii-menu.log 2>&1 & echo $! >/tmp/wii-menu.pid; }'
   ```

   It automatically loads `/tmp/wii-menu.wm`. Allow roughly 10–20 seconds
   for extraction and initialization, then inspect startup:

   ```sh
   "$WM_KRW" --tmp --no-serial \
     --command 's1exec tail -n 60 /tmp/wii-menu.log'
   ```

   Successful startup includes `DISPLAY PIPELINE ACTIVE` and
   `WM1801: init complete`, with no graphics initialization failure.

5. Identify the dedicated input port and start the Mac pointer application:

   ```sh
   ls /dev/cu.usbmodem*
   "$PWD/psvr2_krw_c/.local/build-release/psvr2_serial_tool" \
     /dev/cu.usbmodemCANDIDATE shell
   ```

   Replace `usbmodemCANDIDATE` with a new Stage3 port and press Return.
   The control interface, target `/dev/ttyGS0`, returns a shell prompt.
   Close that client with Ctrl-] before trying another candidate. The
   toolkit's [port guide](https://github.com/Tomoeko/PSVR2_Research/blob/main/docs/build.md#control-and-input-ports)
   covers identification. Use the other Stage3 interface, target
   `/dev/ttyGS1`, for the pointer. macOS suffixes vary; replace
   `usbmodemINPUT` below with that dedicated input port:

   ```sh
   WM_INPUT_PORT=/dev/cu.usbmodemINPUT
   "$WM_MENU/.local/host-build/wm-psvr2-pointer" \
     --input-port "$WM_INPUT_PORT"
   ```

   Activate the black window, then hover and use left/right clicks. Resizing
   rescales pointer coordinates; the Mac mouse remains unlocked. Keep one
   host consumer per serial port and only the menu reading `/dev/fast_input`.

6. To stop, close the pointer window, then send SIGTERM to the recorded menu
   process after verifying its executable:

   ```sh
   "$WM_KRW" --tmp --no-serial \
     --command 's1exec wm_pid=$(cat /tmp/wii-menu.pid) && test "$(/tmp/busybox readlink "/proc/$wm_pid/exe")" = /tmp/wii-menu && kill -TERM "$wm_pid"'

   "$WM_KRW" --tmp --no-serial \
     --command 's1exec tail -n 25 /tmp/wii-menu.log'
   ```

   Wait for `TEARDOWN COMPLETE` before uploading again or resetting Stage3.

## Source layout

Runtime implementations live in `src/`, grouped by responsibility, with
matching public headers under `include/wii_menu/`. The app entry point and
platform adapters have their own folders. Tests follow the same module groups,
and synthetic input fixtures live in `tests/fixtures/`.

See [source organization](docs/architecture.md) for module boundaries and
the build structure. Preparation utilities are grouped by input or export
format under `tools/`.

## Prepare and run

Keep your WAD and optional BootMii NAND dump in ignored `.local/` storage.
Preparation selects the built-in retail common key from the WAD ticket,
matching the HTML importer; a separate common-key file is not required.
For a fresh asset installation:

```sh
./build/wm-prepare --wad .local/input/menu.wad \
  --nand .local/input/nand.bin \
  --output .local/native-assets

# macOS
./build/wii-menu.app/Contents/MacOS/wii-menu --assets .local/native-assets

# Linux
./build/wii-menu --assets .local/native-assets
```

Omit `--nand` to prepare only the System Menu. For an unsupported key index,
use `--common-key-file FILE` and `--common-key-index N` to supply an override.
A NAND dump needs its matching
BootMii key footer or `--nand-keys` file for installed channels and shared
fonts. The WAD also supplies the local Wii Settings outline font. Preparation
extracts the software keyboard's OEM word containers into ignored local assets
when present. The keyboard retains its built-in word list if they are absent;
the containers do not implement Zi8's candidate algorithm. Preparation
publishes to a new directory and never replaces an existing one.

To add channels from another local NAND, preview the changes and publish a
new asset directory:

```sh
./build/wm-prepare --plan --update-from .local/native-assets \
  --nand .local/input/newer-nand.bin \
  > .local/channel-update-plan.json
./build/wm-prepare --update-from .local/native-assets \
  --nand .local/input/newer-nand.bin \
  --expect-plan .local/channel-update-plan.json \
  --output .local/native-assets-next
```

Existing channels are kept by default. See [prepared channel updates](docs/preparation-updates.md)
for explicit replacement options and current limits. An interrupted update
cleans its owned staging on the next run for the same output; use
`./build/wm-prepare --recover --output .local/native-assets-next` to clean it
without repeating extraction.

See [WAD notes](tools/wad/README.md), [channel notes](tools/channels/README.md),
[parity status](PARITY.md), and [development guide](AGENTS.md). Nintendo
resources, console-specific keys, NAND data, and user state are never bundled
here. The retail WAD common-key defaults are built into the preparation tool.
