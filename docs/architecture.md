# Source organization

The portable runtime is built as `wii_menu_core`. Public headers under
`include/wii_menu/` follow the same responsibility groups as the C sources.
Include a header by its full module path, for example
`wii_menu/resources/resource_u8.h` or `wii_menu/board/board_scene.h`.

| Folder in `src/` | Responsibility |
| --- | --- |
| `animation/` | Channel animation, menu transitions, and scene fade timing |
| `audio/` | Playback, sequence interpretation, wave data, and audio cues |
| `board/` | Message board interaction, calendar, composition, address book, and keyboard |
| `fonts/` | Font caching, outline font interpretation, and shared font export |
| `input/` | Pointer state, hit testing, arrows, and channel dragging |
| `layout/` | Layout runtime, pane traversal, and presentation |
| `menu/` | Menu state, channel catalog, and restart behavior |
| `persistence/` | Saved channel layout, board records, and checked file replacement |
| `render/` | Shared geometry, images, texture caching, material preparation, and fallback UI |
| `resources/` | Resource format decoding, independent of graphics APIs |
| `scenes/` | Grid, channel preview, health, HOME, options, settings, storage, and SD scenes |
| `support/` | JSON tokenization and value conversion |

`src/app/main.c` coordinates the frame loop, input, and updates. Its private
`app_resources` module owns startup and teardown; `input_routing` handles key
and hit activation; `frame_render` owns frame selection, draw order, and the
retained HOME target. The Board's private `board_model` owns memo copies and
ordering, `board_text` owns strict text validation and bounded Memo edits, and
`keyboard_text` handles prediction matching. The shared
`scenes/scene_assets` helper checks layout paths and preserves each scene's
missing-asset diagnostics.

`src/platform/gles2/` and `src/platform/metal/` implement
the common contract in `include/wii_menu/platform/platform.h`. The adapters
consume shared logical draw data and geometry; API objects and GPU calls stay
inside their respective backends. The GLES2 private `host` module owns X11/EGL
window and event handling, while its `shaders` module owns shader sources,
program compilation, and the TEV cache. The Metal adapter keeps its shader
source in a private module. Apple and Linux audio devices live in
`src/platform/apple/` and `src/platform/linux/`, behind the private
`src/audio/audio_platform.h` interface. Sequence parsing and PCM rendering
also have separate source files under `src/audio/`.

Private helpers stay with their owning implementations. The geometry helper
belongs to rendering, and file replacement belongs to persistence. They are
not installed public interfaces.

## Build and tests

The root `CMakeLists.txt` selects options and delegates target definitions to
`src/`, `tools/`, and `tests/`. Shared build helpers live in `cmake/`.
Source lists are explicit so adding a file requires a visible build change.
Apple frameworks and Objective-C compilation are conditional on building
the Metal application. `WM_BUILD_APP=OFF` builds the core, tools, and tests
without a platform adapter.

Tests are grouped by module under `tests/`. Synthetic JSON fixtures live in
`tests/fixtures/`; extracted resources and optional local comparison inputs
remain in ignored local storage. Test registration preserves assertions in
Release builds. Commands to build, prepare assets, and run tests are in
[README.md](../README.md).

Preparation commands keep their existing executable names. Source folders
under `tools/` distinguish preparation orchestration, WAD and NAND readers,
and the layout, channel, audio, font, settings, keyboard, and restart exporters.

[The whole-tree refactor audit](refactor-audit.md) records remaining module
boundaries without treating file size or source moves as fidelity evidence.
