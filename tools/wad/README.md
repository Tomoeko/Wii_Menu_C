# Local WAD content extractor

`wm-wad-extract` is a standalone C11 tool. It parses a Wii WAD container, decrypts
its title key and contents with the common key selected by the ticket, and
checks every decrypted content against the TMD's SHA-1 digest before writing
anything. It does not verify Nintendo signatures.

Build from the repository root with CMake:

```sh
cmake -S . -B build
cmake --build build --target wm-wad-extract
```

Retail ticket indices `0` and `1` have built-in defaults, matching the HTML
importer. No separate common-key file is needed for these inputs. An optional
`--common-key-file FILE` override accepts exactly 16 raw bytes or 32
hexadecimal digits; keep that file in ignored local storage. Use
`--common-key-index N` to require an explicit ticket index. Unsupported
indices require a supplied key file and are otherwise rejected.

```sh
./build/wm-wad-extract --wad .local/input/menu.wad \
  --verify-only
./build/wm-wad-extract --wad .local/input/menu.wad
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
