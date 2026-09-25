#include "wii_menu/board_reader_scroll.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void near(float actual, float expected) {
    assert(fabsf(actual - expected) < 0.01f);
}

static void test_measurement(void) {
    WmBoardReaderScroll scroll;
    wm_board_reader_scroll_reset(&scroll);
    assert(wm_board_reader_scroll_configure(&scroll, 0, 42, 140, 68));
    assert(wm_board_reader_scroll_line_count(&scroll) == 1);
    assert(wm_board_reader_scroll_repeated_rows(&scroll) == 0);
    near(wm_board_reader_scroll_limit(&scroll), 0);
    assert(!wm_board_reader_scroll_arrow_visible(
        &scroll, WM_BOARD_READER_ARROW_DOWN));

    assert(wm_board_reader_scroll_configure(&scroll, 5, 42, 140, 68));
    assert(wm_board_reader_scroll_line_count(&scroll) == 5);
    assert(wm_board_reader_scroll_repeated_rows(&scroll) == 4);
    near(wm_board_reader_scroll_footer_shift(&scroll), -168);
    near(wm_board_reader_scroll_limit(&scroll), 122);
    assert(!wm_board_reader_scroll_configure(&scroll, 5, NAN, 140, 68));
    assert(wm_board_reader_scroll_line_count(&scroll) == 5);
}

static void test_scroll_timing_and_bounds(void) {
    WmBoardReaderScroll scroll;
    assert(wm_board_reader_scroll_configure(&scroll, 20, 42, 140, 68));
    near(wm_board_reader_scroll_limit(&scroll), 752);
    wm_board_reader_scroll_set_active(&scroll, true);
    assert(wm_board_reader_scroll_arrow_visible(
        &scroll, WM_BOARD_READER_ARROW_DOWN));
    assert(!wm_board_reader_scroll_arrow_visible(
        &scroll, WM_BOARD_READER_ARROW_UP));
    assert(wm_board_reader_scroll_press(&scroll, WM_BOARD_READER_ARROW_DOWN));
    assert(!wm_board_reader_scroll_press(&scroll, WM_BOARD_READER_ARROW_DOWN));
    assert(wm_board_reader_scroll_advance(&scroll, 1));
    near(wm_board_reader_scroll_offset(&scroll), 0);
    assert(wm_board_reader_scroll_advance(&scroll, 10));
    near(wm_board_reader_scroll_offset(&scroll), 150);
    assert(wm_board_reader_scroll_sound_active(&scroll));
    assert(wm_board_reader_scroll_advance(&scroll, 10));
    near(wm_board_reader_scroll_offset(&scroll), 300);
    assert(!wm_board_reader_scroll_moving(&scroll));
    assert(wm_board_reader_scroll_arrow_visible(
        &scroll, WM_BOARD_READER_ARROW_UP));
    assert(wm_board_reader_scroll_press(&scroll, WM_BOARD_READER_ARROW_DOWN));
    assert(wm_board_reader_scroll_advance(&scroll, 21));
    near(wm_board_reader_scroll_offset(&scroll), 600);
    assert(wm_board_reader_scroll_press(&scroll, WM_BOARD_READER_ARROW_DOWN));
    assert(wm_board_reader_scroll_advance(&scroll, 21));
    near(wm_board_reader_scroll_offset(&scroll), 752);
    assert(!wm_board_reader_scroll_arrow_visible(
        &scroll, WM_BOARD_READER_ARROW_DOWN));
    assert(!wm_board_reader_scroll_press(&scroll, WM_BOARD_READER_ARROW_DOWN));
    assert(wm_board_reader_scroll_press(&scroll, WM_BOARD_READER_ARROW_UP));
    assert(wm_board_reader_scroll_advance(&scroll, 21));
    near(wm_board_reader_scroll_offset(&scroll), 452);
    assert(!wm_board_reader_scroll_advance(&scroll, -1));
    assert(!wm_board_reader_scroll_advance(&scroll, NAN));
    near(wm_board_reader_scroll_offset(&scroll), 452);
}

static void test_arrows(void) {
    WmBoardReaderScroll scroll;
    WmLayoutClip clips[6];
    assert(wm_board_reader_scroll_configure(&scroll, 20, 42, 140, 68));
    assert(wm_board_reader_scroll_clips(&scroll, clips, 6) == 4);
    assert(strcmp(clips[0].animation, "my_Memo_a_Lost") == 0);
    near(clips[0].frame, 10);
    wm_board_reader_scroll_set_active(&scroll, true);
    assert(wm_board_reader_scroll_clips(&scroll, clips, 6) == 4);
    assert(strcmp(clips[1].animation, "my_Memo_a_Appear") == 0);
    assert(strcmp(clips[1].group, "G_ArwL_End") == 0);
    near(clips[1].frame, 0);
    assert(wm_board_reader_scroll_hover(&scroll, WM_BOARD_READER_ARROW_DOWN));
    assert(wm_board_reader_scroll_advance(&scroll, 15));
    assert(wm_board_reader_scroll_clips(&scroll, clips, 6) == 4);
    assert(strcmp(clips[3].animation, "my_Memo_a_FocusOn") == 0);
    near(clips[3].frame, 15);
    assert(wm_board_reader_scroll_press(&scroll, WM_BOARD_READER_ARROW_DOWN));
    assert(wm_board_reader_scroll_clips(&scroll, NULL, 0) == 5);
    assert(wm_board_reader_scroll_clips(&scroll, clips, 4) == 5);
    assert(wm_board_reader_scroll_clips(&scroll, clips, 6) == 5);
    assert(strcmp(clips[4].animation, "my_Memo_a_Select") == 0);
    assert(strcmp(clips[4].group, "G_ArwL_Ac") == 0);

    WmSourceRect up = {10, 10, 20, 20};
    WmSourceRect down = {40, 10, 20, 20};
    assert(wm_board_reader_scroll_hit(&scroll, 50, 20, &up, &down) ==
           WM_BOARD_READER_ARROW_DOWN);
    WmSourceRect shifted_down = {46, 10, 20, 20};
    assert(wm_board_reader_scroll_hit(&scroll, 40, 20, &up,
                                       &shifted_down) ==
           WM_BOARD_READER_ARROW_DOWN);
    assert(wm_board_reader_scroll_hit(&scroll, 37, 20, &up,
                                       &shifted_down) ==
           WM_BOARD_READER_ARROW_NONE);
    assert(wm_board_reader_scroll_hit(&scroll, 20, 20, &up, &down) ==
           WM_BOARD_READER_ARROW_NONE);
    assert(strcmp(wm_board_reader_scroll_pane(WM_BOARD_READER_ARROW_UP),
                  "B_ArwR") == 0);
    assert(strcmp(wm_board_reader_scroll_pane(WM_BOARD_READER_ARROW_DOWN),
                  "B_ArwL") == 0);

    wm_board_reader_scroll_set_active(&scroll, false);
    assert(!wm_board_reader_scroll_arrow_visible(
        &scroll, WM_BOARD_READER_ARROW_DOWN));
    assert(wm_board_reader_scroll_clips(&scroll, clips, 6) == 5);
    assert(strcmp(clips[1].animation, "my_Memo_a_Lost") == 0);
    assert(strcmp(clips[3].animation, "my_Memo_a_FocusOff") == 0);
    assert(wm_board_reader_scroll_hit(&scroll, 50, 20, &up, &down) ==
           WM_BOARD_READER_ARROW_NONE);
    assert(wm_board_reader_scroll_advance(&scroll, 31));
    assert(wm_board_reader_scroll_clips(&scroll, clips, 6) == 4);
}

static void test_back_press_keeps_arrows_until_close(void) {
    WmBoardReaderScroll scroll;
    WmLayoutClip clips[6];
    assert(wm_board_reader_scroll_configure(&scroll, 20, 42, 140, 68));
    wm_board_reader_scroll_set_active(&scroll, true);
    assert(wm_board_reader_scroll_press(&scroll, WM_BOARD_READER_ARROW_DOWN));
    assert(wm_board_reader_scroll_advance(&scroll, 11));
    assert(wm_board_reader_scroll_sound_active(&scroll));

    /* Back presses its footer for 20 frames before the reader starts ExitLetter.
     * The arrows remain at their appeared pose, but scroll audio stops now. */
    wm_board_reader_scroll_set_sound_enabled(&scroll, false);
    assert(!wm_board_reader_scroll_sound_active(&scroll));
    assert(wm_board_reader_scroll_advance(&scroll, 9));
    assert(wm_board_reader_scroll_arrow_visible(
        &scroll, WM_BOARD_READER_ARROW_DOWN));
    assert(wm_board_reader_scroll_clips(&scroll, clips, 6) >= 4);
    assert(strcmp(clips[1].animation, "my_Memo_a_Appear") == 0);
    near(clips[1].frame, 10);

    wm_board_reader_scroll_set_active(&scroll, false);
    assert(wm_board_reader_scroll_clips(&scroll, clips, 6) >= 4);
    assert(strcmp(clips[1].animation, "my_Memo_a_Lost") == 0);
    near(clips[1].frame, 0);
    assert(wm_board_reader_scroll_advance(&scroll, 5));
    assert(wm_board_reader_scroll_clips(&scroll, clips, 6) >= 4);
    near(clips[1].frame, 5);
    assert(wm_board_reader_scroll_advance(&scroll, 5));
    assert(wm_board_reader_scroll_clips(&scroll, clips, 6) >= 4);
    near(clips[1].frame, 10);
}

static void test_hover_priority_when_hit_panes_overlap(void) {
    WmBoardReaderScroll scroll;
    assert(wm_board_reader_scroll_configure(&scroll, 20, 42, 140, 68));
    wm_board_reader_scroll_set_active(&scroll, true);
    assert(wm_board_reader_scroll_press(&scroll,
                                         WM_BOARD_READER_ARROW_DOWN));
    assert(wm_board_reader_scroll_advance(&scroll, 21.0f));
    assert(wm_board_reader_scroll_arrow_visible(
        &scroll, WM_BOARD_READER_ARROW_UP));
    const WmSourceRect up = {20, 20, 30, 30};
    const WmSourceRect down = {25, 20, 30, 30};
    assert(wm_board_reader_scroll_hover(&scroll,
                                         WM_BOARD_READER_ARROW_UP));
    assert(wm_board_reader_scroll_hit(&scroll, 30, 30, &up, &down) ==
           WM_BOARD_READER_ARROW_UP);
    assert(wm_board_reader_scroll_hover(&scroll,
                                         WM_BOARD_READER_ARROW_DOWN));
    assert(wm_board_reader_scroll_hit(&scroll, 30, 30, &up, &down) ==
           WM_BOARD_READER_ARROW_DOWN);
}

int main(void) {
    test_measurement();
    test_scroll_timing_and_bounds();
    test_arrows();
    test_back_press_keeps_arrows_until_close();
    test_hover_priority_when_hit_panes_overlap();
    puts("Message Board reader scrolling tests passed.");
    return 0;
}
