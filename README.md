# Wii Menu in C

Wii Menu presentation and local interactions in C11, using Metal on macOS
and OpenGL ES 2.0 on Linux. This project uses Codex/ChatGPT extensively.

## Build

Run commands from the repository root. Use CMake 3.20 or newer and a C11
compiler. macOS needs the Xcode command-line tools.

### macOS

```sh
git submodule update --init --recursive
cmake -S . -B Files/build -DWM_BACKEND=metal -DCMAKE_BUILD_TYPE=Release
cmake --build Files/build --parallel
ctest --test-dir Files/build --output-on-failure
```

### Ubuntu / Linux

```sh
sudo apt install build-essential cmake libx11-dev libegl-dev libgles-dev
git submodule update --init --recursive
cmake -S . -B Files/build-gles2 -DWM_BACKEND=gles2 -DCMAKE_BUILD_TYPE=Release
cmake --build Files/build-gles2 --parallel
ctest --test-dir Files/build-gles2 --output-on-failure
```

Linux needs an X11 display and an ALSA-compatible audio output.
Use `-DWM_BUILD_APP=OFF` to build only the core, tools, and tests.

## Prepare assets

Use your own USA 4.3 System Menu WAD. Keep inputs and assets in ignored
`Files/.local/` storage. Run:

```sh
./Files/build/wm-prepare --wad Files/.local/input/menu.wad \
    --output Files/.local/native-assets
```

The output directory must be new. Add `--nand Files/.local/input/nand.bin` to include
installed channels and shared fonts. The NAND dump needs its matching BootMii
key footer or `--nand-keys Files/.local/input/keys.bin`.

Retail WAD common keys are selected automatically. Unsupported key indices
need `--common-key-file FILE`; `--common-key-index N` requires a particular
index. See [NAND updates and recovery](docs/preparation-updates.md) for later
imports.

On Linux, use `./Files/build-gles2/` instead of `./Files/build/` for all tools below.

## Run

```sh
# macOS
./Files/build/wii-menu.app/Contents/MacOS/wii-menu

# Linux
./Files/build-gles2/wii-menu
```

The app finds `Files/.local/native-assets` in the current directory or its parents,
then beside the executable or its parents. Use `--assets DIRECTORY` for a
different location.

Use the pointer, arrow keys, Enter, Escape, or H for HOME. Run with `--help`
to see the command-line options.

| Option | Action |
| --- | --- |
| `--aa` | Smooth edges. Uses extra GPU resources. |
| `--record` | Save an MP4 in Movies until exit. |
| `--record half` | Record at half width and height. |
| `--audio web` | Use AAC audio for web previews on macOS. |

`--audio web` requires `--record`. Recorded audio stays unchanged by default.

```sh
./Files/build/wii-menu.app/Contents/MacOS/wii-menu --aa --record half --audio web
```

## Channels

Close the menu before changing channels. Restart it afterward.

```sh
./Files/build/wm-channels add examples/custom-channels/custom-example
./Files/build/wm-channels add Files/.local/input/channel.wad
./Files/build/wm-channels list
./Files/build/wm-channels preview "Example Channel"
./Files/build/wm-channels remove custom-example
./Files/build/wm-channels restore custom-example
```

`remove` hides the channel and keeps its files. To delete the installed copy:

```sh
./Files/build/wm-channels remove custom-example
./Files/build/wm-channels purge custom-example --yes
```

Purge keeps the original folder or WAD. See the [channel guide](tools/channels/README.md)
for custom channel creation, removal by name or folder, and deletion limits.

## Missing or changed assets

The app checks files listed in `asset-manifest.sha1`. It prints missing or
changed paths in the terminal and shows the system-files-corrupted message.
Reprepare assets if the manifest is missing. No external OTF is needed for
the message.

Use `--bypass` when you intentionally edit prepared files:

```sh
./Files/build/wii-menu.app/Contents/MacOS/wii-menu --bypass
```

On Linux, run `./Files/build-gles2/wii-menu --bypass`. Normal `wm-channels` changes
do not need this option.

## Documentation

- [Channels](tools/channels/README.md) and [NAND updates](docs/preparation-updates.md)
- [WAD extraction](tools/wad/README.md) and [audio export](tools/audio/README.md)
- [Settings](docs/settings-rendering.md), [Memo keyboard](docs/board-keyboard.md),
  and [audio accuracy](docs/audio-accuracy.md)
- [GLES2 performance](docs/ubuntu-gles2-performance.md)
- [Architecture](docs/architecture.md), [C style](STYLE.md), and [development](AGENTS.md)

Private WADs, NAND data, console keys, extracted resources, captures, and user
state are not included.
