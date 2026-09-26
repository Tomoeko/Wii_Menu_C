# Wii Menu in C

Disclaimer: This project heavily utilizes Codex/ChatGPT.

This is a port of [Wii_Menu_HTML](https://github.com/Tomoeko/Wii_Menu_HTML).
The current renderer targets OpenGL ES 2.0; Apple builds use a separate Metal
adapter.

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
