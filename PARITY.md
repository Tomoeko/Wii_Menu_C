# Reported fidelity gaps

This log records observed C behavior and the evidence needed before claiming
native Wii fidelity. Code-level cue tests establish event order, not waveform
or hardware output accuracy.

| Scenario | Current C behavior and verification | Native fidelity |
| --- | --- | --- |
| The C build previously played a date-arrow cue while returning from a Message Board date on either side of today. | `wm_board_scene_back()` starts the exit transition without the page cue. `tests/board/board_scene_test.c` checks cue silence when returning from earlier and later dates, including parked and batched updates, while retaining ordinary arrow-cue assertions. | Unverified: no aligned native hardware audio or frame capture has been retained. |
| SD Card Menu icon vanished immediately when opening the Message Board. | The Board clock now plays `mn_Sdcard_Btn_BtnL_Out` through frames 0–15 and retains the reverse reveal on return. Board state and SD draw-command tests sample entry, fade/shrink, return, and delayed drawing in the 16:9 projection. | Native timing unverified; source curves come from locally prepared USA 4.3 resources, with no aligned native capture retained. |
| Software keyboard keys snapped or fell behind neighbors after a click and quick pointer exit. | Pointer exit immediately stops the click pulse and samples the authored focus-exit curve, blending from the retained pose while preserving foreground order until settled, including prediction words. Render-command tests cover QWERTY, toolbar, modifiers, telephone, and symbol handoffs, the authored release overshoot, rapid clicks, and overlapping key draw order. | USA 4.3 binary analysis confirms immediate Pushed → Focus-OUT selection; addresses and content hashes are recorded in `docs/board-keyboard.md`. The C continuity blend and native pixels remain unverified without an aligned capture. |
| Hovering Back/Quit or OK covered the keyboard layout selectors. | Foreground reordering stays within the toolbar's separate control groups. Render-command tests verify both selectors remain above the wide background during hover, press, and exit in QWERTY and telephone layouts for Memo and Console Nickname. | Source hierarchy verified from locally prepared USA 4.3 `fs_VK_toolbar_a`; 16:9 C draw order checked. Native pixel comparisons remain unverified. |
