# Message Board transitions

A calendar day click immediately starts the Message Board's twenty-frame date
slide beneath the calendar. The selected date is applied when that slide ends;
the calendar's selection and closing effects continue on their own clock.
Choosing the current date leaves the Board stationary. Disabled cells remain
inactive. Date selection plays its own cue, without adding the arrow's page cue.

Entering Calendar first slides the existing Board arrows out over ten frames,
then slides the month arrows in at frames 40–50. The common arrow loop phase is
shared by the Board, Calendar, and Create Message and is set before either the
body or footer draws. Opening a child therefore does not replace a visible arrow
with its hidden pose or reset its looping animation. Board date-limit and Memo
page availability determine which arrows depart during Calendar entry.

The background and footer return while Calendar or Create Message closes.
The background holds during the child's body exit, then plays the existing
`my_BbsMask_a_MaskOut` frames 0–20. The arrows and corner buttons use the original
common-footer curves. They finish during closing, with no extra arrow animation
or second background fade at the handoff to the Board.

The C clocks below start when the Back activation is accepted, in 60 Hz updates:

| Transition | Calendar | Create Message selector |
| --- | --- | --- |
| Body leaves | 0–20 | 0–20 |
| Background fades | 20–40 | 20–40 |
| Arrows return | 20–30 | 26–36 |
| Corner buttons return | 26–39 | 33–46 |
| Child closes | 40 | 46 |

Calendar date selection first keeps its thirty-frame selection effect, while
its Board date slide already runs. Its forty-frame exit then uses the same
background and footer schedule as Calendar Back.

Address Book entry and exit use the original sixteen-frame card morph in
`th_Adress_a_note_alp_in/out`. The separate `note_trns_in/out` sideways clips
remain restricted to navigation inside the book, so they cannot overwrite the
card morph's translation.

Memo entry makes the body available after its seventeen source frames, while
the footer finishes its independent twenty-six-frame button change. Early Edit
or Back input preserves the remaining footer motion. Memo Back starts the body
and selector `MailOut` clips together immediately; both reach their endpoints
at frame 16. The footer completes its press and button change at frame 46,
instead of delaying the selector until frame 46 and closing at frame 63.
Keyboard editing and posting keep their original source curves.

## Reference and validation

The comparison used the original USA 4.3 System Menu, title version 513, running
in [Dolphin 2512](https://github.com/dolphin-emu/dolphin/tree/2512) with Metal.
It ran in batch mode with rendering inside its hidden
main window, isolated user data under `Files/board-transition-timing/`, muted
audio, and scripted emulated Wii Remote input. The original Dolphin user data
was not edited. Captured XFB frames were 836 × 456, with the Wii in widescreen
mode; this is distinct from the C renderer's logical projection and window size.

The input WAD SHA-256 is
`bf814c6eb13cf71ab4afe1a1464f3ae27c5a16767030c693abf05e14724f2bbb`.
Dolphin's boot content matched the extracted input executable, SHA-256
`47b9c1bb0ba1890256fb368b1b3272e33ea2467feadf39d20ce469d6de6e6c43`.
Retained frame ranges include 10787–10859 for Calendar entry,
12767–12857 for a previous-date selection,
16103–16184 for Calendar Back, and 16326–16409 for Create Message Back.
The A input was held for roughly five captured frames; event records retain
both press and release bounds. Comparisons use visible motion after activation,
not time spent holding the button. Frame strips show the date moving behind the
calendar, the subsequent background fade, and both footer return stages.
A second capture pass retained frames 777–870 for Memo entry, 912–1017 for
Memo Back, 1057–1183 for Address Book entry, and 1224–1348 for Address Book Back.
These strips distinguish the simultaneous card/body morphs from the longer
footer button changes. Inputs, screenshots, event records, and emulator state
stay ignored under `Files/`.

Asset-independent C tests cover all thirty days of a month, both date directions,
same-day selection, adjacent-month cells, disabled cells, fractional updates,
large batched updates, a shared arrow loop phase, pointer Back and keyboard Back,
reopening either child, and Memo readiness/return boundaries. Resource-backed off-screen tests additionally sample the original arrow
and corner-button tracks, check smooth Calendar arrow departure, and check that
their motion and loop phase do not restart at the handoff. They also compare
Address Book and Memo poses against the individual source morph tracks and
exercise early Memo Edit/Back while the footer is still entering.
These establish C behavior and comparison against an emulated reference. No
independent console-hardware capture was available; hardware fidelity remains
unverified. Binary Ninja returned an empty view for this DOL, so no new native
controller disassembly claim is made here.
