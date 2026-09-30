# Source organization

`wii_menu_core` contains the portable runtime. Public headers under
`include/wii_menu/` mirror the source groups; include them by full module path,
such as `wii_menu/resources/resource_u8.h`. Private helpers stay beside their
owning implementation.

| Folder in `src/` | Responsibility |
| --- | --- |
| `animation/` | Channel animation and menu transition curves |
| `audio/` | Asset loading, sequences, PCM rendering, voice control, and mixing |
| `board/` | Message board, calendar, Memo editing, address book, and keyboard |
| `fonts/` | Font parsing, metrics, rasterization, and caches |
| `input/` | Pointer, hit testing, arrows, and channel dragging |
| `layout/` | Layout import, runtime posing, and ordered pane traversal |
| `menu/` | Menu state, base/local channel catalogs, and restart behavior |
| `persistence/` | Saved channel placement, Board records, and contacts |
| `render/` | Shared draw data, geometry, textures, materials, and frame damage |
| `resources/` | Resource decoding independent of graphics APIs |
| `scenes/` | Grid, Preview, Settings, Storage, SD, HOME, Options, and Health |
| `support/` | JSON, checked conversions, regular-file reads, and atomic replacement |
| `app/` | Startup, events, scene updates, transitions, and frame orchestration |
| `platform/` | Windowing, graphics submission, and audio devices |

## Runtime boundaries

`app/main.c` coordinates the frame loop. Private modules own input dispatch,
scene updates, transitions, resource lifetime, pointer routing, and frame draw
order. Board editors and resource-backed scenes separate state/input from
presentation. Shared scene-asset checks preserve each scene's diagnostics.

Layout traversal produces backend-neutral rendering decisions. GLES2 and
Metal implement `wii_menu/platform/platform.h`; graphics API types and calls
stay in their own backends. Both validate blend factors through the shared
material helper. GLES2 separates X11/EGL hosting, shaders, retained frames,
and submission. Metal separates window/events, shaders, and GPU submission.

`render/frame_damage` compares fixed-capacity command lists and identifies
changed regions. On supported Mesa software renderers, GLES2 replays affected
commands in order into an EGL-preserved buffer. Resize, texture changes,
scene captures, and failed presentation invalidate it. Overflow falls back to
direct rendering. See [performance checks](ubuntu-gles2-performance.md).

The audio callback owns playback cursors and envelopes. `audio_control`
adopts UI snapshots through a nonblocking lock and identifies voice reuse by
generation. Decoding happens on the UI thread; decoded clips remain immutable
until the device closes. Apple and Linux device code sits behind the private
`audio/audio_platform.h` interface.

The texture cache holds an open asset-root directory and validates each image
from one regular-file descriptor before bounded decoding. Persistence and
exporters share checked reads and atomic replacement in `support/`.

## Tools and build

`tools/` groups preparation orchestration, WAD/NAND readers, channel management,
and layout, audio, font, Settings, keyboard, and restart exports. Preparation
filesystem staging, recovery, and update selection have private modules.
The channel manager separates package validation/install, WAD import, and
managed deletion. Exporters share directory checks; path-based writes still
require care around concurrent directory replacement.

The root `CMakeLists.txt` delegates targets to `src/`, `tools/`, and `tests/`;
shared helpers live in `cmake/`. Source lists are explicit. Apple frameworks
and Objective-C are conditional on the Metal app. `WM_BUILD_APP=OFF` omits the
application and graphics adapter.

Tests follow module groups under `tests/`; synthetic fixtures live in
`tests/fixtures/`. Assertions remain enabled in Release tests. Private source
resources and comparison inputs stay in ignored local storage.

See [README.md](../README.md) for commands and [STYLE.md](../STYLE.md) for
source conventions.
