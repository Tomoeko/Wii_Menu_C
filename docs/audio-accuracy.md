# Audio accuracy investigation

## Drag playback

Both `WIPL_SE_CH_DRAG` and `WIPL_SE_BOARD_DRAG` select program 10, key 60,
velocity 127, and bank 1 wave 13. The sample is looping mono DSP ADPCM at
32 kHz, with 33,066 decoded frames and loop start zero. The instrument has
unity pitch, centered pan, volume 127, and ADSR bytes `104/127/127/125`.
Archive volume is `96/127`.

The exporter writes the raw decoded loop, archive gain in
`audio-sequence.json`, and extracted envelope/pan values in `audio-held.json`.
The runtime applies the envelope to the held voice without restarting its
attack at each sample loop.

The executable's attack multiplier for value 104 is `0.8998029232025146`.
Starting at -904 tenths of a decibel, three float32 multiplications advance
each 96-sample, 3 ms block. Gain is approximately 0.027 after 10 ms, 0.284
after 20 ms, and 0.948 after 50 ms; the attack stage ends at roughly 100 ms.
Release value 125 falls by 12 tenths of a decibel per millisecond, taking
about 75 ms from sustain to the silence threshold. These timings describe the
envelope calculation, rather than measured end-to-end output latency.

The runtime also applies the original decibel and pan tables and integer
volume stages. Native 32 kHz blocks and the host 48 kHz adapter retain their
phase across callbacks. Voice start and motion controls are applied together.
A regrab starts a new attack while the prior release drains.

Channel and Memo controllers retain the last pitch during slow motion and
reset it on a new grab. Memo hold and release cues use their queued pan positions.

`System::holdSEwithPosDis` at `0x8136B8A0` was independently checked against
the supplied executable's PPC instructions. Its branch at `0x8136B9AC` skips
the pitch update below 30; above that threshold the pitch is distance divided
by 30. The volume and pan calculations at `0x8136B948` onward use distance and
the logical projection half-width. The 304-unit half-width and the envelope
and volume-ramp addresses below have not all been independently re-traced in
this investigation; treat them as provisional until verified against the
specified executable.

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

## Settings directional cues

The original USA 4.3 Settings resources request sound ID 1 for the horizontal
index-page arrows and the vertical Country-list scroll arrows. Calendar
date/time value arrows request exceptional sound ID 5 on every adjustment,
including held repeats. The source Screen-position value arrows also use the
choice-change cue. These are two different controls despite similar arrow
artwork. The executable's sound dispatcher at `0x813F9834` maps ID 1 to
`WIPL_SE_BT_PUSH` (`0x813F98D4`) and ID 5 to `WIPL_SE_CHOICE_CHG`
(`0x813F9924`). ID 2 maps to `WIPL_SE_BT_TARGETTING` for targeting.

The audio exporter already produces these sequences as
`audio/WIPL_SE_BT_PUSH.wav`, `audio/WIPL_SE_CHOICE_CHG.wav`, and
`audio/WIPL_SE_BT_TARGETTING.wav` from the user's local sound archive; no
sound file is bundled. Settings uses these symbols for arrow presses and held
Calendar repeats.

The same resource handlers request ID 3 for Settings OK and Confirm actions
and ID 4 for No and Cancel actions. Paired prompts with a left Yes (or the
final Format action) use ID 3 on the left and ID 4 on the right. The
executable maps those IDs to `WIPL_SE_DECIDE` and `WIPL_SE_CANCEL`. Country
list rows instead request exceptional ID 5 (`WIPL_SE_CHOICE_CHG`) when a
country is selected, while their scroll arrows request ID 1.

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
| HOME and startup | HOME pause timing has not been checked against native entrance completion. Externally supplied captured background audio with an `includesStartupWave` marker is not distinguished from a separate startup cue. These are separate event/asset issues. |
| Other versions | Held extraction accepts the audited drag structure and USA 4.3 driver tables. Unsupported sources fail visibly; another region/version requires new evidence. |

Generic parser omissions are not evidence that every omitted effect is active.
No LFO or low-pass-filter instructions were found in the parsed paths of all
79 RSEQ cues in this supplied archive;
the cancel-family auxiliary B sends appear inactive under the audited menu
initialization. Full final-output comparisons are still needed for all cues.
