# Reported parity gaps

This log separates behavior checked against the maintained `Wii_Menu_HTML`
implementation from independently verified native Wii behavior. Code-level
cue tests establish event order, not waveform or hardware output accuracy.

| Scenario | HTML reference and source frames | Current C verification | Status |
| --- | --- | --- | --- |
| Returning from a Message Board date on either side of today played a date-arrow cue during the return slide. | At HTML revision `89227ef`, `web/src/menu-scenes.js` emits `WSD_SELECT` only for an accepted date-arrow action (lines 542–579). `back()` emits `WIPL_SE_DECIDE` and starts the return slide without the arrow action (lines 684–725). The date clip covers source frames 0–20 or 30–50, with the exit clip at 6000–6040. `web/tests/menu-scenes.test.js` checks the cue sequence (lines 224–248) and return slide in both TV aspects (lines 697–753); all 29 tests in that file passed. | `wm_board_scene_back()` starts the exit transition without its page cue. `tests/board/board_scene_test.c` checks cue silence during return from earlier and later dates, including parked and batched updates, while retaining ordinary arrow-cue assertions. | HTML cue behavior aligned; native hardware audio remains unverified. |
