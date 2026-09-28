# Reported fidelity gaps

This log records observed C behavior and the evidence needed before claiming
native Wii fidelity. Code-level cue tests establish event order, not waveform
or hardware output accuracy.

| Scenario | Current C behavior and verification | Native fidelity |
| --- | --- | --- |
| The C build previously played a date-arrow cue while returning from a Message Board date on either side of today. | `wm_board_scene_back()` starts the exit transition without the page cue. `tests/board/board_scene_test.c` checks cue silence when returning from earlier and later dates, including parked and batched updates, while retaining ordinary arrow-cue assertions. | Unverified: no aligned native hardware audio or frame capture has been retained. |
