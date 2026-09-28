# C style and safety guide

`AGENTS.md` defines this project's C11, four-space, first-party and backend
boundaries. This guide turns those requirements into review checks. It draws
on the [SEI CERT C Coding Standard](https://wiki.sei.cmu.edu/confluence/display/c),
the [Linux kernel's advice on focused functions](https://docs.kernel.org/process/coding-style.html#functions),
and [LLVM's advice to keep edits locally consistent](https://llvm.org/docs/CodingStandards.html#introduction).
The short C function and control-flow examples in
[CPython's PEP 7](https://peps.python.org/pep-0007/#code-lay-out) are a
concrete reference for four-space indentation, explicit braces, and comments
placed before the behavior they explain. The
[curl C guide](https://curl.se/dev/code-style.html#readability) emphasizes
clear intent and names over clever brevity.
Those projects have different formatting rules; the four-space project rule
applies here.
`.editorconfig` helps editors preserve indentation and whitespace without
adding a runtime or build dependency. `.clang-format` records the local
formatting choices for new files and touched functions; apply it to edited
regions instead of reformatting unrelated files.

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
  Use braces for new control-flow blocks. Keep local brace placement
  consistent within a module until that module is edited for a substantive
  reason. Avoid broad formatting-only diffs that hide behavior changes.
  Comments should explain constraints, ownership, draw order, or provenance
  instead of restating expressions.
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

The application has private resource ownership, input routing, and frame
rendering modules. The Board has a private memo model and strict text editor;
keyboard prediction text has its own module. Sequence parsing is separate
from synthesis. GLES2 host/window management and shader caching are separate
from draw submission, and Metal shader source is separate from its adapter.
Five scenes share a checked layout loader. These boundaries keep scene state
and render ordering in their existing owners, with no new per-frame allocation.

The next steps are the remaining event and update paths in `src/app/main.c`,
Board and Settings presentation, layout import and posing, and the
preparation tool's staging/update transaction. [The source audit](refactor-audit.md)
records these seams. Each should move behind a narrow private interface after
its state and order dependencies are mapped. File length alone is not a bug;
make each change reviewable against its tests and rendered behavior.
