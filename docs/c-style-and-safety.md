# C review checklist

Follow [STYLE.md](../STYLE.md) for C formatting, functions, shared helpers,
comments, ownership, and performance. [AGENTS.md](../AGENTS.md) defines the
platform and fidelity constraints; [architecture.md](architecture.md) identifies
module owners.

## Before submitting

- Keep each change within a clear module boundary. Share genuine duplication
  through a narrow helper; preserve state-machine order and resource names.
- Validate external counts, offsets, paths, and allocation sizes. Bound total
  decoded memory as well as individual entries. Return errors for malformed
  input instead of asserting.
- Check each partial-failure path. Release owned memory, files, GPU objects,
  and threads; leave failed outputs in their documented state.
- Keep path checks and file operations together where possible. Account for
  directory replacement races; a preflight check alone is insufficient.
- Synchronize state shared by threads with a lock or atomics. Keep resources
  alive while queued rendering or audio work still refers to them.
- Preserve pane order, clipping, blending, timing, and both graphics backends.
  Measure representative scenes before claiming a performance improvement.
- Keep private resources, user state, captures, and identifying paths out of
  tracked files.

Additional references retained from the source review:
[CPython C layout](https://peps.python.org/pep-0007/#code-lay-out),
[curl readability](https://curl.se/dev/code-style.html#readability),
[LLVM consistency](https://llvm.org/docs/CodingStandards.html#introduction),
[CERT allocation sizes](https://cmu-sei.github.io/secure-coding-standards/sei-cert-c-coding-standard/rules/memory-management-mem/mem35-c/),
and [CERT file races](https://cmu-sei.github.io/secure-coding-standards/sei-cert-c-coding-standard/rules/input-output-fio/fio45-c/).
Use the repository's formatting rules where these references differ.
