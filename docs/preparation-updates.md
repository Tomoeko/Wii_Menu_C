# Prepared channel updates

`wm-prepare` can add channels from another local BootMii NAND dump to an
existing prepared asset directory. The source directory remains untouched.
Each update publishes a **new** output directory only after extraction,
conversion, selection, and catalog validation succeed.

```sh
./build/wm-prepare --plan \
    --update-from .local/native-assets \
    --nand .local/input/newer-nand.bin \
    --replace-channel 0001000148414241 \
    > .local/channel-update-plan.json

./build/wm-prepare \
    --update-from .local/native-assets \
    --nand .local/input/newer-nand.bin \
    --replace-channel 0001000148414241 \
    --expect-plan .local/channel-update-plan.json \
    --output .local/native-assets-next
```

Use `--nand-keys FILE` if the dump has no appended key footer. The NAND
extractor validates the supported BootMii format before the output can be
published. The new output contains the prior menu assets, saved channel
placement, previously installed channels, and selected new channel exports.
Only the channel layout and audio files for selected replacements are changed.

Existing title IDs are kept by default; new IDs are added. Repeat
`--replace-channel ID` to replace particular IDs, or use `--nand-policy replace`
to replace every matching installed ID. Repeat `--keep-channel ID` to retain an
installed copy or skip a new ID, including under the bulk replacement policy.
Selected IDs must be present in the incoming NAND and cannot be both kept and
replaced. IDs are 16 hexadecimal characters, accepted in either case. The
catalog language is inherited from the existing assets.

The read-only `--plan` emits one JSON object with title IDs, TMD versions when
recorded by the channel exporter, default or selected actions, SHA-1 hashes
of each exported channel record, layout tree, and channel audio, and
`inputNandSha1` for the **entire raw NAND file**. Each incoming title also
records `incomingContentSha1` for its TMD-validated active `.app` bytes and
`incomingTmdSha1` for its original TMD bytes. The content hash covers the
content length declared by the TMD; the exporter validates it against the
TMD's own SHA-1 before recording it. Existing source hashes are shown when
the installed catalog recorded them; older catalogs may show `null` because
they do not retain original channel content. The `ExportSha1` fields describe
prepared exports separately. The whole-file NAND hash still requires a new
review if unrelated NAND bytes change. These SHA-1 fingerprints detect
accidental input changes; they do not defend against deliberately constructed
collisions. Redirect the complete JSON plan to a local file and pass it with
`--expect-plan` when publishing. The tool recomputes the plan from the copied
installed assets and newly converted NAND assets before changing the staged
catalog. It refuses to
publish when any planned channel export or selected action differs. Use the
same selection flags for the plan and update. The guard compares the exact
plan emitted by this build, including its formatting, so regenerate the plan
after changing tool versions. The plan file must be a regular file and is not
included in the published output.

The C preparer writes a private recovery journal before creating its random
staging directory. A matching ownership marker is written inside that stage
before extraction starts. Preparations sharing an output parent use an OS-held
lock, so the next run for the same output can safely remove a stage left by
an interrupted process. Recovery never removes an existing output; publication
remains a single no-replace directory rename. Use
`wm-prepare --recover --output DIRECTORY` to clean an interrupted output
preparation without repeating extraction. For an interrupted read-only plan,
use `wm-prepare --recover --plan --update-from DIRECTORY` with the same source
asset directory. A journal for a different output, a mismatched or missing
ownership marker, and any other ambiguous stage are preserved for manual
inspection. An interruption before the stage marker is complete can leave an
empty private stage that needs manual inspection. The source directory and
previously published outputs remain untouched.

The update path accepts a raw BootMii dump; extracted NAND directories,
multiple NAND inputs, channel WAD updates, removals, and in-place replacement
are not implemented. Existing shared fonts are retained. A fresh installation
from the System Menu WAD can still export fonts from its optional NAND input.
The update rejects symlinks and special files in the prepared source tree,
rejects output paths nested inside it, and refuses any pre-existing output
directory. Keep all prepared resources and NAND inputs in ignored local
storage; they are never part of the source repository.
