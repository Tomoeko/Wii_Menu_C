# Local WAD extraction

`wm-wad-extract` decrypts a Wii WAD and checks content against TMD SHA-1
values. It validates container bounds and title metadata, but does not verify
Nintendo signatures. For complete application assets, use
[wm-prepare](../../README.md#prepare-assets).

Run from the repository root after building. On Linux, replace `./Files/build/`
with `./Files/build-gles2/`:

```sh
# Validate and decrypt without publishing output.
./Files/build/wm-wad-extract --wad Files/.local/input/menu.wad --verify-only

# Extract to ignored local storage.
./Files/build/wm-wad-extract --wad Files/.local/input/menu.wad
```

Retail ticket indices 0 and 1 select built-in common keys. An unsupported
index requires `--common-key-file FILE`, containing 16 raw bytes or 32
hexadecimal digits. `--common-key-index N` requires a particular ticket index.
Keep overrides and source WADs in ignored storage.

Extraction publishes `Files/.local/wad/<title-id>/` containing `content/*.app`,
`content/title.tmd`, `ticket.bin`, and `import.json`. It stages the title before
publication and refuses to replace an existing title directory. Metadata uses
logical identifiers and hashes.

A System Menu WAD supplies that title's resources, not every installed channel.
Layout, font, Settings, and audio conversion are separate tools coordinated by
`wm-prepare`. Channel WAD installation is described in the
[channel guide](../channels/README.md).
