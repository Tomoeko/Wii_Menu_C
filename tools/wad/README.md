# Local WAD content extractor

`wm-wad-extract` is a standalone C11 tool. It parses a Wii WAD container, decrypts
its title key and contents with a locally supplied 16-byte common key, and
checks every decrypted content against the TMD's SHA-1 digest before writing
anything. It does not verify Nintendo signatures.

Build from the repository root with CMake:

```sh
cmake -S . -B build
cmake --build build --target wm-wad-extract
```

Supply a key file containing exactly 16 raw bytes or 32 hexadecimal digits.
Keep this file in ignored local storage. The expected ticket key index defaults
to `0`; use `--common-key-index N` for another supplied key.

```sh
./build/wm-wad-extract --wad .local/input/menu.wad \
  --common-key-file .local/common-key.bin --verify-only
./build/wm-wad-extract --wad .local/input/menu.wad \
  --common-key-file .local/common-key.bin
```

Successful extraction creates `.local/wad/<title-id>/content/*.app`,
`content/title.tmd`, `ticket.bin`, and `import.json`. The JSON records only
logical title/content metadata and hashes. The tool stages a validated title
under `.local/wad/` and publishes it after every file is written. If that title
already exists, the tool preserves it and exits with an error.

The extractor covers the WAD header, section bounds, ticket and TMD metadata,
AES-128-CBC content decryption, and content SHA-1 checks. The separate
`wm-layout-export` tool interprets the menu's U8/ASH/TPL/BRLYT/BRLAN resources
and writes local layouts, textures, and fonts. Neither tool verifies Nintendo
signatures or supplies installed channels from other titles. A System Menu WAD
alone supplies the menu title, not every installed channel.

Cryptographic known vectors run with the CTest suite:

```sh
ctest --test-dir build --output-on-failure
```
