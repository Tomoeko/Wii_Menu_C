# C source refactor audit

This is a whole-tree triage of the C sources and headers under `src/`,
`tools/`, `tests/`, and `include/`, plus the Objective-C Metal adapter. It
uses the source inventory, function boundaries, ownership paths, repeated
helpers, and existing tests to identify *module seams*. It is not a claim of
line-by-line verification or native Wii parity. File length alone does not
justify a split.

| Area | Assessment and next boundary |
| --- | --- |
| Application | `app/main.c` still combines a long, priority-ordered event loop with per-scene updates. Extract one event or update path at a time behind a private context; preserve health, entrance, fade, restart, HOME, then screen priority. Move balloon cue state changes out of drawing only with timing tests. |
| Board | `board_scene.c` and `board_compose.c` still mix transitions, input, and presentation. `board_keyboard.c` combines candidates with visual state, while `board_address.c` combines field editing with contact presentation. Give each edit model and presentation code a narrow private interface before moving large draw functions. Keep memo ordering and compose cue timing covered by tests. |
| Scenes | `settings_scene.c` has a particularly clear state/input versus drawing boundary. `resource_scene.c`, `storage_scene.c`, `sd_scene.c`, and `home_overlay.c` are also long, but their draw order and scene-specific state are tightly coupled. Split a proven state or presentation seam rather than sharing a generic scene framework. |
| Layout and resources | `layout_runtime.c` owns JSON import, animation posing, and pane traversal. `resource_layout.c` exports both BRLAN and BRLYT with a shared JSON writer. These are good private-module candidates after their shared data contracts are made explicit. Smaller format decoders and render helpers are already reasonably focused. |
| Audio | Sequence command parsing and PCM synthesis have separate lifetimes. The remaining renderer contains voice scheduling, envelopes, and reverb; further changes need PCM comparisons because the drag cue's fidelity is still under study. `audio.c` also combines manifest loading, voice control, and mixing. |
| Graphics adapters | Keep window/event code separate from GPU draw submission, while preserving the common `WmPlatform` contract. Both adapters should compile independently. Neither a source move nor a passing parser test establishes visual parity or PowerVR performance. |
| Preparation tools | `tools/preparation/prepare.c` combines staging/recovery, plan construction, and update publishing. `tools/channels/export.c` and `tools/nand/reader.c` combine several format and filesystem workflows. Their transactional and path-security behavior merits focused tests before a split. Other tools are smaller or already have format-specific files. |
| Tests and public headers | Some Board and Settings tests are large scenario suites. Split them by feature when editing those behaviors, not merely to reduce line counts. The public headers mostly follow module folders; avoid widening them to support private refactors. |

Prioritize ownership and input safety first, then interaction/presentation
boundaries with deterministic tests. Preserve lookup identifiers, pane order,
GPU object lifetime, and per-frame allocation behavior. Reformat only touched
code so reviews can distinguish behavior changes from whitespace.
