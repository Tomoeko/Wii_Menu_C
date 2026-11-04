# Channel language recovery

English is the default. The USA Settings document offers English, French, and
Spanish. A row selection is staged; Back/Esc cancels it, and Confirm commits it.
The app propagates the committed choice to channel icon/banner animation,
language groups, SD and channel-storage thumbnails, and available IMET titles.
Missing title translations fall back to English. User-authored channel titles
without translations keep their original value. HOME restarts retain the choice.
This does not recover every System Menu screen's text or retired channel service.

## Local preference

The ignored config lives beside `channels.json`, normally at
`Files/.local/native-assets/.language.json`. It is outside the asset integrity
manifest. No config is created on an untouched English startup. Confirming a
different language writes once using atomic replacement; repeated frames,
cancellation, and confirming the current choice do not write. A later explicit
change back to English writes English. Normal shutdown needs no further write.

Close the app before editing the config. Its entire format is:

```json
{
    "schemaVersion": 1,
    "language": "FRA"
}
```

`language` accepts `ENG`, `FRA`, or `SPA`. Invalid JSON, duplicate/unknown fields,
unsupported versions and language codes select English and print a warning.
An invalid file is preserved until an explicit language change replaces it.
Unreadable, nonregular or oversized files (over 2 KiB) disable saving for that
session. Edits made externally during a session are preserved with a warning.
Layout inspection with `--layout` does not load or save this preference.

## Original channel setup

Get Connected Video (`0001000148434745`, USA) stores its icon setup in
`arc/icon.cs.lz7`, separate from the icon BRLYT. The exporter discovers that entry
in active TMD content, checks the content size and SHA-1, validates its envelope,
and retains the unwrapped RCHE script as `icon/icon.json.cs` under the channel's
prepared layout directory. Re-prepare from the original NAND to upgrade older
assets; the runtime warns when the companion is missing and preserves authored
text rather than inventing translations. Exported resources remain ignored under
`Files/`.

The bounded first-party evaluator reads constant initialization, follows
`setRegionPanes`'s language branches, and records the five `setTitle` calls per
language. Text, pane bindings, and both font dimensions come from the script.
The script's default branch uses English. The runtime checks the recovered
TextWriter call recipe, retains the pane's font and character/line spacing,
measures explicit lines with the source font, sets the pane size, and replaces
its string. The existing local network/EULA substitute controls the conditional
notice's visibility. No console save, network, or general script services execute.
Unrecognized script variants are reported as unsupported. Other channel families
continue to use the recovered language-pane/group rules.

Evidence: the original unwrapped icon script SHA-256 is
`a648fd0638b5ed65b057bb702a87f49fd3709c1fef643f4e9ffe41c6b1ea9b9d`;
the icon BRLYT SHA-256 is
`852748bf954f8e4864b8329a8070835eca62883c03d0d3040a81bd9686d1008e`.
All fifteen evaluated text/font bindings were compared byte-for-byte with an
independent read of the original constant assignments and retained variable
names. Portable synthetic tests cover language branches, fallback, pose reset,
source metrics, malformed tables/UTF-16 and bounded loops. Address/undefined
behavior sanitizers also passed. These checks establish resource interpretation
and C behavior. Pixel fidelity on native Wii remains unverified: no native frame
capture or independently validated runtime trace was available for either aspect
ratio. The off-screen C checks do not substitute for that evidence.
