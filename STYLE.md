# C source style

This is the working style guide for the C runtime and tools. It applies to new
code and to code being refactored. Preserve existing behavior, resource names,
animation timing, draw order, and platform portability while improving source
quality. A clean build and formatter result do not, by themselves, establish
that a function is readable or that native Wii behavior is accurate.

## Reference and precedence

Use C11 and this repository's `.clang-format` settings: four-space indentation,
an 88-column target, attached braces, and no compressed one-line functions or
conditionals. The development constraints in `AGENTS.md` take precedence. Use
the [Linux kernel C style guide](https://docs.kernel.org/process/coding-style.html)
for its principles on focused functions, shallow nesting, descriptive helpers,
and centralized cleanup. Its indentation convention does not replace this
repository's four-space rule. The [OpenBSD style guide](https://man.openbsd.org/style)
is a second reference for clear declarations, comments, and consistent source
layout. Use the [C11 working draft](https://open-std.org/jtc1/sc22/wg14/www/standards)
and [SEI CERT C rules](https://wiki.sei.cmu.edu/confluence/display/c) when
language behavior or safety is in question. Formatting is checked with the
repository's `wm-check-format` target and [ClangFormat](https://clang.llvm.org/docs/ClangFormat.html).

## Functions and control flow

- Give each function one recognizable job. An orchestrator should show the
  phases in order; move a phase with its own state or policy into a named
  helper. Do not create helpers solely to shorten a file or hide a single
  obvious expression.
- Prefer guard clauses for invalid inputs and inactive states. If branching
  requires several nested levels, extract a cohesive operation or reconsider
  the state representation. A long, linear table or switch may remain long
  when splitting it would obscure the mapping.
- Name helpers for the work they perform, such as `pane_world_matrix` or
  `activate_phone_prediction`. Avoid names such as `handle_stuff` or
  `do_more`. Keep parameter lists narrow; group related state only when the
  group has a real lifetime or invariant.
- Use one cleanup path when several exits must release the same resources.
  Name the label for its action, such as `release_paths`. Direct returns are
  fine before resources are acquired. Make ownership transfer explicit.
- Use `const` for borrowed input that is not modified. Use `size_t` for sizes
  and indices, fixed-width integers for file formats, and `bool` for true/false
  results. Use named constants for format bounds and flag bits. Keep source
  resource identifiers unchanged when they are lookup keys.

For example, keep frame phases visible without embedding every controller in
one large loop:

```c
static void advance_frame(AppRuntime *app, float elapsed) {
    advance_board(app, elapsed);
    advance_settings(app, elapsed);
    advance_keyboard(app, elapsed);
    sync_audio_and_pointer(app);
}
```

This example describes a boundary, not a mandate to call inactive controllers.
Each helper still owns its relevant state checks, and real call order must stay
unchanged during a refactor.

## Data, ownership, and safety

- State who owns allocated memory, file handles, and GPU objects at each API
  boundary. Borrowed pointers must not outlive their owner. Free, close, or
  release each owned resource on every success and failure path.
- Check input lengths before indexing, pointer arithmetic, multiplication, or
  allocation. Prefer subtraction-based bounds checks so the check itself
  cannot overflow. Validate a resource's count against the destination's
  capacity before copying it.
- Check allocation, conversion, I/O, and decoding results. Do not use
  `assert()` as validation for untrusted files or command-line input. Treat
  malformed WAD, NAND, JSON, and image data as ordinary errors.
- Copy complete arrays with `memcpy` only when source and destination have
  matching element types and dimensions. Otherwise convert elements in a
  clearly named loop. A loop is appropriate when each element needs distinct
  work; nesting is not itself a defect.
- Keep Apple framework interop inside the Metal platform boundary and GLES2
  calls inside the GLES2 backend. Do not add a runtime package dependency.

A bounded copy makes its precondition and the zero-length case explicit:

```c
static bool copy_span(uint8_t *destination, size_t capacity, size_t offset,
                      const uint8_t *source, size_t length) {
    if (!destination || (length && !source) || offset > capacity ||
        length > capacity - offset)
        return false;
    if (length)
        memcpy(destination + offset, source, length);
    return true;
}
```

For multiple owned resources, keep failure cleanup together:

```c
static bool read_payload(FILE *file, size_t length, size_t max_length,
                         uint8_t **output) {
    uint8_t *payload = NULL;
    bool okay = false;

    if (!file || !output)
        return false;
    *output = NULL;
    if (length == 0 || length > max_length)
        return false;
    payload = malloc(length);
    if (!payload)
        goto release_payload;
    if (fread(payload, 1, length, file) != length)
        goto release_payload;

    *output = payload; /* Ownership passes to the caller. */
    payload = NULL;
    okay = true;

release_payload:
    free(payload);
    return okay;
}
```

Here `file` remains borrowed; its caller closes it. Real code should supply a
project size limit and report why the operation failed. The applicable safety
references are [array bounds](https://wiki.sei.cmu.edu/confluence/spaces/c/pages/87152322/ARR30-C.%2BDo%2Bnot%2Bform%2Bor%2Buse%2Bout-of-bounds%2Bpointers%2Bor%2Barray%2Bsubscripts),
[allocation size](https://wiki.sei.cmu.edu/confluence/spaces/flyingpdf/pdfpageexport.action?pageId=87152128),
[integer overflow](https://wiki.sei.cmu.edu/confluence/pages/viewpage.action?pageId=88014921),
[allocation ownership](https://wiki.sei.cmu.edu/confluence/spaces/flyingpdf/pdfpageexport.action?pageId=88040013),
and [checked number conversion](https://wiki.sei.cmu.edu/confluence/pages/viewpage.action?pageId=87162519).

## Comments and names

Write comments for a constraint, non-obvious reason, lifetime, source
provenance, or fidelity limitation. Do not narrate what the next statement
already says. Explain why a fixed ratio, bit, threshold, delay, or ordering
matters. When behavior comes from a WAD or binary analysis, identify the
specific input and evidence where that information is recorded; do not imply
hardware fidelity from a C test alone. Keep comments short enough to scan.

```c
/* Keep the pressed key above its neighbors until the release scale ends. */
draw_pressed_key_last(keyboard);
```

Prefer names that state units and roles: `elapsed_frames`, `byte_count`,
`source_offset`, `owned_texture`, `borrowed_layout`. Avoid changing an existing
public identifier merely for taste. Use a private `static` helper when logic
is local to one translation unit; place a declaration in a narrow internal
header when two modules genuinely share it.

## Rendering and performance

- Keep scene traversal and draw order shared between Metal and core GLES2.
  Preserve clipping, blending, texture sampling, and animation timing.
- Avoid allocation, decoding, shader compilation, synchronous readback, and
  redundant state changes in ordinary frame rendering. Reuse buffers and
  textures. Do not replace a clear loop with clever arithmetic unless a
  representative profile shows a meaningful benefit.
- Refactor hot paths with the same output before optimizing them. Measure
  startup, idle, page changes, preview, and HOME on representative scenes.
  Report the platform, backend, scene, frame time, and memory before making
  a performance claim. Keep the GLES2 core path correct without extensions.

## Review and validation

Before a refactor is called complete, check the affected call sites, pointer
lifetimes, failure exits, integer bounds, and platform guards. Compare the
before/after behavior of state machines, scene transitions, render commands,
and audio cues. Run the build, `wm-check-format`, the relevant focused tests,
and the full test suite for a broad change. Test both graphics backends when
backend behavior changes. A passing suite verifies this implementation; any
claim of native visual or timing parity also needs independently retained
source evidence and a rendered comparison.
