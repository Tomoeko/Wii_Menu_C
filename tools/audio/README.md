# Local audio export

`wm-audio-export` reads the resource content and executable from an extracted
USA 4.3 System Menu WAD. It decodes direct waves and HOME remote-speaker PCM,
then renders RSEQ cues using the executable's audio tables. Prefer
[wm-prepare](../../README.md#prepare-assets) to create complete assets with
integrity metadata.

For a separate export, run from the repository root. On Linux, replace
`./build/` with `./build-gles2/`:

```sh
./build/wm-audio-export \
    .local/wad/0000000100000002/content/00000097.app \
    .local/wad/0000000100000002/content/00000098.app \
    .local/audio-export
```

The output contains WAVs and playback metadata. Drag cues use raw looping PCM,
archive gain in `audio-sequence.json`, and envelope/pan tables in
`audio-held.json`. Keep these files together; older held assets use approximate
playback and request re-export. Unsupported exports are reported.

`wm-channel-export` separately decodes supported BNS channel audio into
`channel-audio/`. The app loads clips on first use and mixes them through shared
C playback logic; macOS uses CoreAudio and Linux uses the system ALSA runtime.
Remote-speaker cues play through host output as a local substitute.

Held playback uses linear source interpolation; AX filtering and shared
effects are incomplete. See [audio details](../../docs/audio-accuracy.md).
Manual changes to a sealed asset directory trigger its startup integrity check;
reprepare a new complete directory or use `--bypass` for intentional edits.
