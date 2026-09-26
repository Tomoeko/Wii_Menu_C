# Parity tracking

This tracker records reported differences from `Wii_Menu_HTML` and separately
verified native Wii behavior. No comparison entries have been recorded here.

The source organization and shared-helper refactor is verified by builds and
automated tests. It does not close any rendered comparison gap.

For each reported gap, record:

- The scene, interaction, and expected behavior, with the HTML source revision.
- Input region/version and content hashes, using logical input names.
- Logical projection, framebuffer dimensions, and displayed aspect ratio.
- Reference source frames and the comparison method.
- Current C build revision, backend, reproduced frames, and verification result.

Mark a gap complete only after recording source-frame and current-build
verification. Keep HTML comparisons and native capture comparisons distinct.
Private inputs, extracted resources, and captures remain in ignored storage.
