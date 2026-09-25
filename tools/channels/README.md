# Installed channel resource converter

`wm-channel-export` converts already decrypted installed title content into
native menu assets. It reads the extracted title directory produced by
`wm-nand-extract`; it does not read or decrypt a raw NAND dump.

Build and run from the repository root:

```sh
cmake -S . -B build
cmake --build build --target wm-channel-export
./build/wm-channel-export .local/nand-extracted .local/native-assets ENG
```

The input must contain `title/<8-hex>/<8-hex>/content/title.tmd` and the
corresponding decrypted `<content-id>.app` files. The optional
`title/00000001/00000002/data/iplsave.bin` is copied to the output root only
after its RIPL v3 checksum and structure are validated. The output root should
remain under `.local/` or another ignored private directory. This converter
does not place source content or personal titles in the tracked repository.

The converter selects TMD-active nonshared content with an IMET header,
validates its TMD SHA-1 digest, and exports `meta/icon.bin` and
`meta/banner.bin` resources. IMD5, LZ77, ASH0, U8, TPL, BRLYT, and BRLAN
resources use the first-party C decoders in this repository. It writes
`channels.json`, root-relative layout JSON under `channel-layouts/`, and
parallel `.wmra` RGBA textures. The layout JSON retains `.png` texture URLs
for compatibility with the prepared asset schema; the native texture cache
resolves these to the `.wmra` files. Every available layout is listed in the
manifest, and the layout named `icon` or `banner` is selected as the default.

The manifest uses schema version 1. Each channel has an ID, localized titles,
`iconLayout`, `bannerLayout`, `resources.icon.layouts`, and
`resources.banner.layouts`. `defaultOrder` follows a valid `iplsave.bin` when
present; `savedLayout` remains `null` because the runtime reads the validated
binary save directly. Each `source` record includes the active content's
TMD-validated SHA-1 and a SHA-1 of the original TMD bytes for reviewed
updates. All paths in the generated JSON are relative to the output root.
The C converter does not execute native channel modules or channel scripts.
It verifies content hashes recorded in the TMD, but it does not verify
Nintendo's TMD signature or attest the extracted NAND source.
