# C source refactor audit

This is a whole-tree review of authored C and headers under `src/`, `tools/`,
`tests/`, and `include/`, plus the Objective-C Metal adapter. It maps source
ownership and module boundaries. It is not a line-by-line proof of correctness,
a native Wii fidelity claim, or a PowerVR performance measurement. File length
alone is not a reason to split a cohesive module.

| Area | Current boundary |
| --- | --- |
| Application | `app/main.c` coordinates the priority-ordered loop. Private input, event dispatch, update, transition, pointer, resource, and frame modules own their corresponding paths. The event order remains health, entrance, fade, restart, HOME, then active screen. |
| Board | Scene and calendar presentation, compose draft/scroll/presentation, address presentation, keyboard presentation, and candidate prediction have private modules. The remaining scene and editor files own transitions, state, and input; memo ordering and cue timing remain in their established paths. Compose activation dispatches to focused handlers in its owning module. |
| Scenes | Options, Preview, Settings, Storage, SD, HOME, and resource scene presentation have private modules. Resource interactions and balloon rendering have separate owners. Category-specific draw routines stay with their scenes to preserve pane and cue order. |
| Layout and resources | Layout JSON import, runtime posing, and draw traversal are separate. Font parsing and text placement are separate. BRLAN and BRLYT export share a bounded JSON writer through a private interface. Format decoders retain their own resource limits; RSAR wave-table offsets have malformed-input tests. |
| Audio | Asset/manifest loading, voice control, mixing, sequence parsing, and sequence PCM rendering have distinct source files. The held drag cue's DSP parity remains an independent research gap in `docs/audio-accuracy.md`. |
| Graphics adapters | GLES2 and Metal host/events, shaders, and draw submission have separate owners. The platform-independent render contract is shared. Backend source moves do not establish visual parity or device performance. |
| Preparation tools | Preparation filesystem staging, recovery, and update orchestration have focused modules. Channel resource export, channel manifest publishing, NAND reader discovery, and NAND extraction are separate. |
| Persistence and image I/O | Contact validation and JSON formatting are separate from transactional contact updates. Board and contact stores share bounded regular-file reads. Shared atomic replacement owns temporary-file publication for persistence, render images, audio exports, and channel output. Texture source validation and bounded image decoding are separate from GPU cache policy. |
| Tests and public headers | Tests remain grouped by feature; large scenario suites are kept intact. New helpers use private headers alongside their owners rather than widening installed interfaces. |

The Photo and Shop preview reentry test checks that BACK's advancing capture
cannot be reused as the next SELECT capture. It checks the requested frame-zero
behavior with local prepared resources when available; it does not claim native
visual parity. The remaining changes should follow observed behavior, safety
findings, or measured costs. In particular, move draw-time cues into update
logic only with tests for exact frame timing. Split remaining large Board and
Settings functions only when a feature change can establish a narrower state
contract and verify draw or interaction order. Native sound and visual parity
still require original resources or retained captures with provenance recorded
by `AGENTS.md`.
