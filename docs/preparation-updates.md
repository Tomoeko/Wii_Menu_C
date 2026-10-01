# Prepared channel updates

`wm-prepare` adds or replaces channels from a BootMii NAND dump while keeping
the prepared source directory intact. It publishes a new output only after
extraction, conversion, and catalog validation succeed. For authored folders,
channel WADs, removal, and deletion, use [wm-channels](../tools/channels/README.md).

Commands below run from the repository root. On Linux, substitute
`./Files/build-gles2/wm-prepare` for `./Files/build/wm-prepare`.

## Plan and publish

```sh
./Files/build/wm-prepare --plan \
    --update-from Files/.local/native-assets \
    --nand Files/.local/input/newer-nand.bin \
    > Files/.local/channel-update-plan.json

./Files/build/wm-prepare \
    --update-from Files/.local/native-assets \
    --nand Files/.local/input/newer-nand.bin \
    --expect-plan Files/.local/channel-update-plan.json \
    --output Files/.local/native-assets-next
```

Review the generated plan before publishing. `--expect-plan` refuses the
update if inputs or choices have changed. Use the same tool build and selection
flags for both commands; regenerate the plan after changing either.

Use `--nand-keys FILE` if the dump has no matching appended key footer. The
output must not exist or be nested inside the source tree. Source trees with
symlinks or special files are rejected. Keep inputs, plans, and outputs under
ignored local storage. Select the result with the app's
`--assets Files/.local/native-assets-next` option.

## Choose channels

| Option | Effect |
| --- | --- |
| Default | Keep existing IDs and add new IDs. |
| `--replace-channel ID` | Replace one matching installed ID; repeat for more. |
| `--nand-policy replace` | Replace all matching installed IDs. |
| `--keep-channel ID` | Retain an installed copy or skip a new ID, including under bulk replacement. |

Explicit IDs must occur in the incoming NAND, contain 16 hexadecimal
characters, and cannot be both kept and replaced. Either case is accepted.
Use the same selection flags for planning and publishing.

Updates preserve saved placement, retained channels, menu assets, and shared
fonts. Selected replacements change their channel layout and audio exports.
Catalog language is inherited from the source assets. The update path accepts
one raw BootMii dump; extracted directories, channel WAD updates, removal,
and in-place replacement are not supported here.

## Recover interrupted preparation

Preparation uses a private staging directory, recovery journal, ownership
marker, and OS-held lock. A subsequent run for the same output can clean a
matching interrupted stage. To recover without repeating extraction:

```sh
./Files/build/wm-prepare --recover --output Files/.local/native-assets-next

# For an interrupted plan, use the same source asset directory.
./Files/build/wm-prepare --recover --plan --update-from Files/.local/native-assets
```

Recovery preserves published outputs and the source. A different journal
owner, a missing or mismatched marker, or another ambiguous stage requires
manual inspection. Publication uses one directory rename that refuses to
replace an existing output.
