# Audio accuracy investigation

## Evidence and scope

The reported difference is the scratching sound while dragging a channel or
memo, compared with the original menu running in Dolphin. No retained hardware
capture was supplied. This investigation therefore establishes resource and
executable behavior, with emulator output still requiring comparison. It does
not establish exact console output.

Inputs audited on 2026-09-26:

| Logical input | Region/version | SHA-256 |
| --- | --- | --- |
| Menu resource content `00000097.app` | USA 4.3 | `64bc053c764d5be5107b03675ee487a8f83258e9f5518b5df10a135d23ded237` |
| Menu executable `00000098.app` | USA 4.3 | `47b9c1bb0ba1890256fb368b1b3272e33ea2467feadf39d20ce469d6de6e6c43` |
| Extracted `sound/IplSound.brsar` | USA 4.3 | `78c62ce1df5198bd4bb87284c6a8943ff5bb6fca7de7805080271d534d52bd78` |

The HTML implementation reference was revision
`89227eff4557fc9f0c216d566da544429b52fbf5`, including its `docs/audio-cues.md`.
Its retained audio references identify Dolphin recordings, rather than console
recordings, and do not establish the DSP backend or ROM identities. The C
baseline was `0129b91`; the changes described below were tested in the working
tree based on that commit. Source and current-build capture ranges are absent,
so the reported parity gap remains unverified.

Private inputs and generated lookup tables stay in ignored storage. Temporary
Binary Ninja analysis used a private slice of the executable and temporary
function definitions. The original executable was not patched or saved as a
modified analysis database.

## What changed for drag playback

Both `WIPL_SE_CH_DRAG` and `WIPL_SE_BOARD_DRAG` select program 10, key 60,
velocity 127, and bank 1 wave 13. The sample is looping mono DSP ADPCM at
32 kHz, with 33,066 decoded frames and loop start zero. The instrument has
unity pitch, centered pan, volume 127, and ADSR bytes `104/127/127/125`.
Archive volume is `96/127`.

The previous exporter wrote an unchanging loop with archive gain baked into
the PCM. This skipped the instrument envelope. The new exporter writes the
raw decoded loop, keeps archive gain in `audio-sequence.json`, and writes the
locally extracted envelope and pan values to `audio-held.json`. The runtime
applies the envelope to the held voice, rather than restarting a baked attack
every time the sample loops.

The executable's attack multiplier for value 104 is `0.8998029232025146`.
Starting at -904 tenths of a decibel, three float32 multiplications advance
each 96-sample, 3 ms block. Gain is approximately 0.027 after 10 ms, 0.284
after 20 ms, and 0.948 after 50 ms; the attack stage ends at roughly 100 ms.
Release value 125 falls by 12 tenths of a decibel per millisecond, taking
about 75 ms from sustain to the silence threshold. These timings describe the
envelope calculation, rather than measured end-to-end output latency.

The runtime also applies the original decibel and pan tables and integer
volume stages. Native 32 kHz blocks and the host 48 kHz adapter retain their
phase across callbacks. Starting a held voice and applying its motion controls
now occur together, preventing a callback from playing the initial loop at
full motion gain. A regrab starts a new attack while the prior release drains.

The channel controller previously reset pitch to one whenever motion fell
below the pitch threshold. The memo controller always supplied pitch one.
Both now retain the last applied pitch during slow motion and reset it on a
new grab. Memo hold and release cues also use their queued pan positions.

`System::holdSEwithPosDis` at `0x8136B8A0` was independently checked against
the supplied executable's PPC instructions. Its branch at `0x8136B9AC` skips
the pitch update below 30; above that threshold the pitch is distance divided
by 30. The volume and pan calculations at `0x8136B948` onward use distance and
the logical projection half-width. The 304-unit half-width and the envelope
and volume-ramp addresses below follow the HTML project's retained executable
analysis. They have not all been independently re-traced in this investigation.

| Driver behavior | USA 4.3 addresses |
| --- | --- |
| Attack and sustain tables | `0x8161E3F0`, `0x8161E2F0` |
| Decibel and pan tables | `0x8161EAD8`, `0x8161F9EC` |
| Envelope update and parameter conversion | `0x814FF138`, `0x814FF25C`, `0x814FF274`, `0x814FF31C` |
| Integer voice volume and block delta | `0x814FAA24`–`0x814FAA40`, `0x814FAA94`–`0x814FAAB0` |

The public [NW4R envelope reconstruction](https://github.com/doldecomp/ogws/blob/master/src/nw4r/snd/snd_EnvGenerator.cpp)
and [channel reconstruction](https://github.com/doldecomp/ogws/blob/master/src/nw4r/snd/snd_Channel.cpp)
provide supporting structural references. They use a different SDK and do not
override the supplied executable's tables or ramp arithmetic. This repository
uses first-party C and does not bundle those implementations.

## Checks completed

- Independent vgmstream decoding matched all 886,661 compared PCM values
  across 49 RSAR wave channels, including the drag wave. The tested CLI was
  r2083, with SHA-256
  `67bfabe694758aba7b132ed164c3992e45e6a579137d0d70f7af5907006418fa`.
  This checks ADPCM decoding, not DSP filtering or mixing.
- Fresh export produced 79 sequences, 15 aliases, and 19 direct/speaker sounds
  with zero unsupported exports for this archive. All 110 other WAVs were
  byte-identical to a rebuilt baseline exporter. Exactly three WAVs changed:
  the two drag resource identifiers and their `drag` alias.
- The new drag WAVs match the raw decoded sample. All 965 decibel and 257 pan
  values round-trip through JSON exactly as float32.
- All 60 CTest tests passed on the Metal build. New kernel and mocked-device
  tests cover envelope progression, pitch, gain, pan, release, overlapping
  regrabs, arbitrary callback sizes, manifest-supplied loop points, malformed
  metadata, and 64 voice-retirement cycles. Strict warnings and targeted
  address/undefined-behavior sanitizer checks passed.
- A separate sanitizer probe loaded the refreshed original drag assets through
  the runtime, produced finite samples for both cues, and drained their release
  tails to silence. The three drag WAVs and their manifest entries were installed
  in the ignored local asset set with a backup; other local audio entries were
  preserved.

These are implementation checks. No audible or sample-aligned comparison of
the changed C build with a retained Dolphin or hardware capture has occurred.

## Exact DSP filtering and Dolphin

Exact digital filtering is possible if the original coefficient values,
matching DSP microcode, arithmetic, history, and update timing are available.
The menu's CPU-side envelope and pan tables are already recoverable from its
executable. The DSP ROM coefficient table is a separate input, and no verified
original DSP ROM dump was available for this investigation.

Dolphin revision `465c652da1dc0b3048089701a1885084821c9574` was inspected.
Its bundled replacement DSP ROM documentation describes the filter
coefficients as approximately matching the originals. Its AX HLE loader checks
the user coefficient file before the bundled one; without coefficients it can
fall back to linear interpolation. The voice code implements four-tap,
128-phase source filtering. Thus backend selection alone does not identify
the filtering actually used. See the
[replacement ROM notes](https://github.com/dolphin-emu/dolphin/blob/465c652da1dc0b3048089701a1885084821c9574/docs/DSP/free_dsp_rom/dsp_rom_readme.txt),
[coefficient loading](https://github.com/dolphin-emu/dolphin/blob/465c652da1dc0b3048089701a1885084821c9574/Source/Core/Core/HW/DSPHLE/UCodes/AX.cpp),
and [AX voice processing](https://github.com/dolphin-emu/dolphin/blob/465c652da1dc0b3048089701a1885084821c9574/Source/Core/Core/HW/DSPHLE/UCodes/AXVoice.h).

LLE runs the original DSP instructions, but an LLE run with replacement ROMs
still uses replacement data. Verified original ROMs and the matching menu
microcode provide a stronger digital reference. HLE and LLE also share parts
of Dolphin's implementation, including the sample accelerator, so agreement
between them is useful evidence without being independent hardware proof.
See [DSP LLE](https://github.com/dolphin-emu/dolphin/blob/465c652da1dc0b3048089701a1885084821c9574/Source/Core/Core/HW/DSPLLE/DSPLLE.cpp)
and [DSP core ROM validation](https://github.com/dolphin-emu/dolphin/blob/465c652da1dc0b3048089701a1885084821c9574/Source/Core/Core/DSP/DSPCore.cpp).

A verified existing dump can supply exact coefficients without new hardware
access. A fitted filter from a compressed video or ordinary recording cannot
establish the original integer coefficients and all DSP state exactly. Even
an exact digital implementation does not establish the analog console, TV,
or Wii Remote speaker response.

To obtain the original coefficients without a console on hand, use a retained
hardware dump or arrange for a collaborator with a Wii/GameCube to make one.
Dolphin's [DSPSpy ROM-dumping microcode](https://github.com/dolphin-emu/dolphin/blob/465c652da1dc0b3048089701a1885084821c9574/Source/DSPSpy/util/dump_roms.ds)
reads the DSP instruction ROM and coefficient data directly. The relevant
emulator files are `dsp_coef.bin` and, for an original-ROM LLE reference,
`dsp_rom.bin`. Retain the dump's provenance and actual file SHA-256 values,
then validate its contents against the original-ROM identities recognized by
Dolphin. Its internal Adler32 checks use loaded memory, not necessarily the
raw file's byte order.

The System Menu WAD and NAND filesystem do not supply this chip ROM. Running
the dumper inside Dolphin only reads whatever ROM data Dolphin already loaded;
it cannot recover missing original data. No verified public original-coefficient
download was established in this investigation. Without an existing dump or
someone else's hardware access, the current available path remains an
approximation.

## Remaining audio limits

| Area | Current evidence and limitation |
| --- | --- |
| Drag source filtering | Linear interpolation remains in the held renderer. AX four-tap filtering, its original coefficients, and complete microcode arithmetic remain unimplemented. The runtime still bounds pitch to 0.25–4; the native downstream limit has not been established. |
| Host output | The held path uses a linear 32-to-48 kHz adapter. Host output conversion and callback/control timing are outside any current exact-output claim. |
| Other RSEQ cues | Active random pitch instructions in message display and character input/decide/delete are ignored; pre-rendering also prevents fresh random choices on each playback. They do not affect drag. |
| Output-mode cue | The archive uses surround pan, which is not represented by the current stereo sequence renderer. |
| Reverb and overlapping cues | Existing sequence exports bake individual effects. They do not reproduce a shared live AX auxiliary bus for simultaneous voices. |
| Direct WSD sounds | The 11 direct sound identifiers examined use instant ADSR, unity pitch, centered pan, and no auxiliary send. Ignored WSD parameters did not explain this report for the supplied archive. |
| Channel banners | BNS is supported. WAV and AIFF banner variants remain unsupported. Decoder agreement alone does not verify final banner volume, timing, or filtering. |
| Remote-speaker cues | Exported remote-speaker PCM plays through host output as a local substitute. The physical remote transport, codec, and speaker response are not reproduced. |
| HOME and startup | HOME pause timing still differs from the HTML entrance-completion behavior. Externally supplied captured background audio with an `includesStartupWave` marker is not distinguished from a separate startup cue. These are separate event/asset issues. |
| Other versions | Held extraction accepts the audited drag structure and USA 4.3 driver tables. Unsupported sources fail visibly; another region/version requires new evidence. |

Generic parser omissions are not evidence that every omitted effect is active.
No LFO or low-pass-filter instructions were found in the parsed paths of all
79 RSEQ cues in this supplied archive;
the cancel-family auxiliary B sends appear inactive under the audited menu
initialization. Full final-output comparisons are still needed for all cues.

## Reproducible comparison without hardware

1. Record the menu version and input hashes, Dolphin build/commit, HLE or LLE
   interpreter selection, DSP microcode identity, loaded ROM/coefficient hashes,
   output rate, DMA volume, stereo mode, and silence-skipping settings. Identify
   bundled replacements explicitly. Start from a cold boot rather than sharing
   save states between DSP backends.
2. Replay the same timed pointer trace in the original menu and C build. Include
   stationary hold, slow motion, the 30-unit threshold, faster motion, release,
   rapid regrab, and HOME interruption for both channels and memos. Record both
   logical projections, framebuffer sizes, displayed aspect ratios, and source
   and current-build frame ranges.
3. Retain Dolphin's DSP output dump before host resampling or time stretching.
   Its dump path applies DMA channel volume and channel ordering; account for
   those operations explicitly. See
   [wave output](https://github.com/dolphin-emu/dolphin/blob/465c652da1dc0b3048089701a1885084821c9574/Source/Core/AudioCommon/WaveFile.cpp)
   and [mixer dumping](https://github.com/dolphin-emu/dolphin/blob/465c652da1dc0b3048089701a1885084821c9574/Source/Core/AudioCommon/Mixer.cpp).
4. Compare at an agreed digital boundary and rate. Use one recorded alignment
   offset, report sample error and envelope/pitch/loop-boundary differences,
   and avoid fitting separate gains or time offsets to hide local errors.
5. Repeat under HLE and LLE. Classify agreement using replacement ROMs as
   emulator parity. Original-ROM agreement increases confidence in digital
   behavior; reserve console-output claims for a retained native capture or
   independently hardware-validated behavior.

No captured Nintendo audio, original ROM data, or private analysis slices are
included in the repository.
