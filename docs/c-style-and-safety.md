# C style and safety guide

`AGENTS.md` defines this project's C11, four-space, first-party and backend
boundaries. This guide turns those requirements into review checks. It draws
on the [SEI CERT C Coding Standard](https://wiki.sei.cmu.edu/confluence/display/c),
the [Linux kernel's advice on focused functions](https://docs.kernel.org/process/coding-style.html#functions),
and [LLVM's advice to keep edits locally consistent](https://llvm.org/docs/CodingStandards.html#introduction).
Those projects have different formatting rules; the four-space project rule
applies here.
`.editorconfig` helps editors preserve indentation and whitespace without
adding a runtime or build dependency.

## Source shape

- Give each `.c` file one responsibility and a narrow public header under
  `include/wii_menu/`. Keep implementation-only types and helpers private.
  Name exported functions `wm_<module>_<action>` and keep internal helpers
  `static`. Put ownership, valid ranges, and error behavior in interface
  comments when a caller cannot infer them from the type.
- Prefer short functions that express one state change, parse step, or draw
  step. Extract a helper when a function mixes input handling, state updates,
  resource acquisition, rendering, and cleanup. A long data declaration or
  switch is less urgent than a short function with several intertwined states.
- Use four spaces, descriptive names, and ordinary multiline statements.
  Keep local brace placement consistent within a module until that module is
  edited for a substantive reason. Avoid broad formatting-only diffs that hide
  behavior changes. Comments should explain constraints, ownership, draw
  order, or provenance instead of restating expressions.
- Preserve lookup identifiers, pane ordering, and resource semantics exactly.
  Keep graphics API types inside backends, and let GLES2 and Metal consume the
  same backend-neutral draw decisions.

## Safety at boundaries

- Treat WAD, NAND, pack, JSON, TPL, BMG, and banner input as
  untrusted. Check a byte span with `offset <= size && length <= size - offset`
  before reading. Check `count <= SIZE_MAX / sizeof(*items)` before computing
  an allocation. Bound **aggregate** decoded memory as well as each entry:
  repeated offsets can amplify one small encoded record into many allocations.
  See [CERT INT30-C](https://cmu-sei.github.io/secure-coding-standards/sei-cert-c-coding-standard/rules/integers-int/int30-c/)
  and [MEM35-C](https://cmu-sei.github.io/secure-coding-standards/sei-cert-c-coding-standard/rules/memory-management-mem/mem35-c/).
- On parse failure, free all partial allocations and leave the output in a
  documented empty state. Pair each file descriptor, stream, GPU object, and
  thread with one clear release path. Check allocation and I/O results; report
  failures through the module's error interface. See
  [CERT POS54-C](https://cmu-sei.github.io/secure-coding-standards/sei-cert-c-coding-standard/rules/posix-pos/pos54-c/)
  for POSIX library errors.
- Use checked path construction and directory-relative operations when
  extracting or publishing files. A path validated by one lookup can change
  before the next lookup; see
  [CERT FIO45-C](https://cmu-sei.github.io/secure-coding-standards/sei-cert-c-coding-standard/rules/input-output-fio/fio45-c/).
  Keep temporary files private and replace final files only after complete
  writes and checks.
- Use atomics or a lock for values shared by C threads. `volatile` and
  `sig_atomic_t` do not replace thread synchronization. Preserve GPU resource
  identity while a backend has queued draws that refer to it, even if the
  caller releases its handle before submission.
- Assert programmer invariants in tests; return an error for malformed
  external input. Keep fallback behavior explicit and report unsupported
  variants instead of guessing.

## Validation during a refactor

Build the portable core and relevant platform backend, then run focused tests
for changed behavior. Add malformed-input, partial-cleanup, and ownership
cases for parser or lifetime changes. Run targeted address and undefined
behavior sanitizer checks where the host supports them. A source-resource
test that skips because private assets are absent is not a rendered comparison.
Visual and performance claims still need the evidence required by `AGENTS.md`.

The next large structural work should proceed in reviewable steps:

1. Split `src/app/main.c`'s long menu loop into application lifetime,
   input routing, update, and drawing helpers. Consolidate its repeated HOME
   entry path while retaining event order.
2. Isolate the pure UTF-8 and prediction logic from
   `src/board/board_keyboard.c`, with an explicit malformed-UTF-8 policy.
   Keep keyboard layout and input behavior in the scene facade.
3. Separate board model and transitions from presentation inside
   `src/board/board_scene.c`, preserving one public facade and draw order.
4. Split host setup and shader-program management out of
   `src/platform/gles2/platform_gles2.c`. Keep ES 2.0 as the working floor
   and check the Metal contract for each backend-facing change.
5. Consolidate repeated checked asset-path and JSON-load boilerplate across
   scenes after its differing missing-asset behavior is documented.

These are boundaries for future patches, not claims that file length alone is
a bug. Reformatting the whole tree at once would make the behavior-sensitive
changes harder to review.
