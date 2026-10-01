# Channels

Use `wm-channels` to install and manage channels in the C app. Build and prepare
assets first; see the [README](../../README.md). Close the menu before making
changes and restart it afterward.

Run from the repository root. On Linux, replace `./Files/build/` with `./Files/build-gles2/`.
For a different asset directory, add `--assets DIRECTORY` before the command:

```sh
./Files/build/wm-channels --assets Files/.local/native-assets-next list
```

## Install

```sh
./Files/build/wm-channels validate examples/custom-channels/custom-example
./Files/build/wm-channels add examples/custom-channels/custom-example
./Files/build/wm-channels add Files/.local/input/channel.wad
./Files/build/wm-channels list
```

`validate` checks a custom folder without installing it. It needs prepared
assets. `add` copies the channel files; it keeps the source folder or WAD.
WAD import also needs `wm-wad-extract` and `wm-channel-export` beside the
manager; the normal build includes them. Unsupported WAD key indices need
`--common-key-file FILE`. Imported WAD titles use English metadata.

## Preview

```sh
./Files/build/wm-channels preview "Example Channel"
```

This opens the banner in the app. The channel must be installed and visible,
and the app must be built beside the manager. Restore a removed channel first.

## Remove and restore

`remove` hides a channel without deleting files. These examples remove the
same custom channel by ID, name, or source folder:

```sh
./Files/build/wm-channels remove custom-example
./Files/build/wm-channels remove "Example Channel"
./Files/build/wm-channels remove examples/custom-channels/custom-example
```

Quote names or paths with spaces. If a name matches several channels, use
its ID from `list`. WAD paths work only with `add`; use the installed ID or
name afterward. A folder needs its `channel.json` to identify the channel.

To remove several channels at once:

```sh
./Files/build/wm-channels remove "Example Channel" "Studio Channel"
```

All targets are checked before saving. Repeated IDs are rejected.
To restore a channel using its installed files:

```sh
./Files/build/wm-channels restore custom-example
```

## Permanently delete

Remove the channel first, then run purge with `--yes`:

```sh
./Files/build/wm-channels remove custom-example
./Files/build/wm-channels purge custom-example --yes
```

Purge deletes the installed layouts, textures, audio, and local catalog entry.
It keeps the source folder or WAD. To delete that too, check its contents and
remove it with your file manager. Keep the source if you want to reinstall.

Purge only accepts channels added with `wm-channels`. Channels included by
`wm-prepare` can be hidden and restored. Missing files, unexpected files,
or symlinks can block purge. An I/O failure can leave a partly deleted copy;
purge has no undo.

## Create a custom channel

Copy the vector example into local storage:

```sh
mkdir -p Files/.local/custom-channels
cp -R examples/custom-channels/custom-example Files/.local/custom-channels/my-channel
```

Edit the copied `channel.json`, `icon.json`, and `banner.json`. Use a new ID
and title in `channel.json`; change the displayed text in both layouts too.
For PNG artwork, start with `examples/custom-channels/custom-studio-channel-9899b686`.

```json
{
    "schemaVersion": 1,
    "id": "custom-my-channel",
    "title": "My Channel",
    "iconLayout": "icon.json",
    "bannerLayout": "banner.json"
}
```

Custom IDs start with `custom-` and contain 8–63 characters: lowercase letters,
digits, `_`, or `-`. The first character after `custom-` must be a letter or
digit. Titles must be nonempty and fit in 127 UTF-8 bytes.

The layouts define panes, materials, textures, and animation. Use the examples
as templates. Animation names are `icon`, `banner_Start`, and `banner_Loop`.
SVG guides are in [examples/channel-guides](../../examples/channel-guides/).
Hide their overlays before exporting artwork. The red outlines are approximate
alignment guides. See the
[placement reference](../../examples/channel-guides/placement-reference.json)
for coordinates and sizes.

Keep PNGs beside the layouts. Use names such as `icon.png`, without subfolders;
letters, digits, `_`, and `-` are allowed before `.png`. PNGs must be
non-interlaced, 8-bit RGBA, at most 4096 pixels per dimension and 8 MiB per file.
Their dimensions must match the layout. Each layout supports up to 16 texture
entries; omit `resourceTextures` or leave it empty.

For sound, include a mono or stereo PCM16 `sound.wav` under 8 MiB without loop
metadata. Add this member to `channel.json`:

```json
{
    "audio": {
        "src": "sound.wav",
        "loop": false
    }
}
```

Validate the folder, install it, then preview it:

```sh
./Files/build/wm-channels validate Files/.local/custom-channels/my-channel
./Files/build/wm-channels add Files/.local/custom-channels/my-channel
./Files/build/wm-channels preview custom-my-channel
```

Custom channels provide artwork, animation, and sound; they do not run native
channel programs.

## Update or replace a channel

`add` rejects active duplicate IDs. Adding a removed channel with the same
ID and title restores the installed copy; it does not copy edited source files.
To install a revised copy, remove and purge the old one, then add the folder
or WAD again. Use [wm-prepare updates](../../docs/preparation-updates.md) for
channels from another NAND.

## Storage and limits

`channels.local.json` stores additions and removal flags. The base
`channels.json` and integrity manifest stay unchanged. Installed files use
`custom-channels/<id>/` for authored packages, `channel-layouts/<id>/` for WAD
imports, and `channel-audio/<id>.wav` for audio. PNGs become `.wmra` textures.

The menu has four pages of twelve slots; the Disc Channel owns the first slot.
New channels fill the first free slot. Add and restore fail when the menu is
full. There is no command-line placement option. The local catalog holds up
to 48 entries, including removed channels; purge old copies to free entries.

## Export decrypted NAND titles

`wm-channel-export` reads an extracted NAND title tree, not a raw dump. Use
`wm-prepare` for complete app assets. To make a separate export:

```sh
./Files/build/wm-channel-export Files/.local/nand-extracted Files/.local/channel-export ENG
```

Input titles need `title/<8-hex>/<8-hex>/content/title.tmd` and the matching
decrypted `<content-id>.app` files. The exporter checks content SHA-1 against
the TMD, then writes the channel catalog, layouts, textures, and supported BNS
audio. A valid `iplsave.bin` supplies placement. Nintendo signatures are not
verified.
