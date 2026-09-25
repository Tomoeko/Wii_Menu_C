# Development guide

## Purpose

Port the Wii Menu presentation and local interactions from the maintained
`Wii_Menu_HTML` project into readable, portable C. Keep the menu's behavior,
resource interpretation, animation timing, draw order, and ownership explicit.
Use the HTML project as an implementation reference; distinguish its behavior
from independently verified native Wii behavior. Console hardware and retired
network services need clearly named local substitutes.

The minimum graphics target is OpenGL ES 2.0. A separate Metal backend is also
required. Both backends should consume the same platform-independent scene and
rendering decisions so that visual and input behavior do not diverge.

## Source quality and organization

- Never author minified code, including shaders, generated-looking tables,
  examples, scripts, and configuration. Use descriptive names and ordinary
  multiline formatting.
- Prefer C11, focused modules, explicit ownership, and narrow interfaces.
  Keep scene state, resource decoding, animation, input, audio, persistence,
  platform integration, and graphics backends separate.
- Keep the runtime self-contained in first-party C. Do not add third-party
  libraries or package-manager dependencies. Isolate any required Apple
  framework and Objective-C runtime interop at the Metal platform boundary.
- Use four-space indentation in C and CMake files. Keep functions small enough
  to understand and comments focused on behavior, constraints, or provenance.
- Check allocation sizes, file bounds, and malformed resource data before use.
  Pair every allocation and GPU object with a clear release path. Avoid hidden
  global mutable state and undocumented fallbacks.
- Keep renderer-facing data independent of graphics API types. Put OpenGL ES
  calls in the GLES2 backend and Metal calls in the Metal backend. Share scene
  traversal and draw ordering; avoid copying whole scene implementations.
- Keep authored source and bundled shaders readable. Do not commit extracted
  Nintendo resources, private WADs, NAND data, keys, captures, or user state.
  Do not copy generated assets from the HTML repository into this one.

## Graphics compatibility and performance

- Treat core OpenGL ES 2.0 and GLSL ES 1.00 as the compatibility floor. Do not
  require ES 3 features or optional extensions for correctness. Guard any
  optional fast path and preserve a core ES 2.0 path.
- Design and measure for the future PowerVR GX6250 target. Reuse textures,
  shaders, buffers, and render targets; avoid per-frame allocation, shader
  compilation, texture decoding, synchronous readback, and redundant state
  changes in normal rendering.
- Preserve pane order, clipping, blending, texture sampling, and animation
  semantics. Batch only where this cannot reorder visible output. Keep
  resolution and effect costs explicit; measure real frame time and memory on
  representative scenes before describing an optimization as effective.
- Build the Metal backend independently behind the same rendering contract.
  Keep Apple-specific source and framework dependencies conditional so the
  GLES2 build remains portable.

## Fidelity and evidence

The HTML repository contains useful layouts, controllers, conversion tools,
tests, and research notes. Original WAD resources, verified binary analysis,
and retained native captures are the fidelity oracles. Record the input
region/version, content hashes, aspect ratio, frame range, and comparison
method for fidelity claims. Browser parity and native parity are separate
claims. Do not infer exact native output from successful parsing or a passing
unit test.

Preserve resource identifiers exactly where they are lookup keys, even if they
contain original spelling errors. Keep the 4:3 and 16:9 logical projections,
framebuffer size, and displayed aspect ratio distinct. Make unsupported
resource variants and simulated console services visible in documentation.

## Validation and privacy

- Add focused tests for parsers, animation curves, scene transitions,
  backend-neutral render commands, resource lifetime, and persistence when
  those components are implemented. Use rendered comparisons for visual
  claims and check both backends when backend behavior changes.
- Test the core GLES2 path without relying on extensions. Verify that Metal
  platform code builds on a supported Apple host when it changes.
- Profile representative startup, idle, page change, channel preview, and
  HOME flows before making performance claims about the PowerVR target.
- Before sharing or publishing, inspect source, logs, manifests, and generated
  metadata for home directory paths, account names, console identifiers,
  private content, and extracted assets. Use relative project paths and
  logical input names in public examples.

Keep user-facing build and status instructions in `README.md`. Describe only
features that are implemented and verified in this repository.
Use `PARITY.md` to track reported HTML comparison gaps and record source-frame
and current-build verification before marking one complete.
