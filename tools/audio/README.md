# Local menu audio export

`wm-audio-export` reads the System Menu resource content and executable from
your own extracted USA v4.3 WAD. It extracts `sound/IplSound.brsar` in C, decodes
the original direct waves, and synthesizes RSEQ sounds using the executable's
audio lookup tables. It writes WAV files and sound metadata under the ignored
asset directory. The same resource content supplies the five original HOME
remote-speaker PCM cues:

```sh
./build/wm-audio-export \
  .local/wad/0000000100000002/content/00000097.app \
  .local/wad/0000000100000002/content/00000098.app \
  .local/native-assets
```

`wm-channel-export` separately decodes installed channels' `meta/sound.bin`
BNS audio into `channel-audio/`. The app loads these local WAV files once on
first use and mixes them in C. Apple output uses CoreAudio; Linux uses the
system ALSA runtime when available. Both backends use the same event and
playback logic.

Direct wave and channel BNS sample decoding retains the original PCM. The
RSEQ renderer follows the original sequence and lookup tables, while native AX
mix and effects still need complete capture comparison before any 1:1 audio
claim. Unsupported sequence commands are reported during export and never
replaced with another cue.

Channel and memo drag sounds are exported as raw looping PCM with archive gain
in the sequence manifest. `audio-held.json` contains the locally extracted
USA 4.3 envelope and pan tables used by the runtime's held voices. Copy that
metadata together with the newly exported drag WAVs and sequence manifest;
older assets use approximate playback and print a re-export notice.
Regenerate existing assets with the command above while the app is closed.
The held path still uses linear interpolation instead of exact AX source
filtering. See [audio evidence and limits](../../docs/audio-accuracy.md).
