#include "board_scene_internal.h"

#include "wii_menu/layout/layout_present.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Board drawing and its hit-test poses share the authored layout state. The
 * scene owns all layouts; this module borrows them and allocates nothing per
 * frame. */
enum { BOARD_CLIP_CAPACITY = 20 };

static unsigned weekday(WmBoardDate date) {
    int64_t value = board_model_day_number(date) + 4;
    /* 1970-01-01 was Thursday. */
    int64_t remainder = value % 7;
    return (unsigned)(remainder < 0 ? remainder + 7 : remainder);
}

static void date_text(WmBoardDate date, int day_offset, char buffer[32]) {
    static const char *const weekdays[7] = {"Sun", "Mon", "Tue", "Wed",
                                            "Thu", "Fri", "Sat"};
    WmBoardDate displayed =
        board_model_date_from_day(board_model_day_number(date) + day_offset);
    snprintf(buffer, 32, "%s %d/%d", weekdays[weekday(displayed)], displayed.month,
             displayed.day);
}

static void matrix_translation(float x, float y, float matrix[12]) {
    static const float identity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    memcpy(matrix, identity, sizeof(identity));
    matrix[3] = x;
    matrix[7] = y;
}

static void present(WmBoardScene *board, const WmLayout *layout,
                    const float matrix[12]) {
    wm_layout_present_with_fonts(board->platform, board->textures, board->fonts, layout,
                                 true, WM_LAYOUT_IPL, matrix);
}

static void append_clip(WmLayoutClip clips[BOARD_CLIP_CAPACITY], size_t *count,
                        const char *animation, const char *group, float frame) {
    if (*count >= BOARD_CLIP_CAPACITY)
        return;
    clips[*count] = (WmLayoutClip){
        .animation = animation, .group = group, .frame = frame, .loop_override = 0};
    (*count)++;
}

static bool capture_scroll_offset(void *context, const WmLayoutPaneView *pane) {
    if (strcmp(pane->name, "N_TopBack") == 0) {
        *(float *)context = pane->matrix[3];
    }
    return true;
}

static void pose_background(WmBoardScene *board) {
    /* The authored base N_TopBack X is 121.6. Frame zero places it at the
     * screen center, which both the grid and Board use for the bottom date. */
    bool returning = board->phase == WM_BOARD_EXIT && board->return_direction != 0;
    bool scrolling = board->phase == WM_BOARD_DATE_SCROLL || returning;
    int direction = returning ? board->return_direction : board->direction;
    WmLayoutClip clip = {
        .animation = "my_IplTop_c",
        .frame = scrolling ? (direction > 0 ? 30.0f : 0.0f) +
                                 board_scene_clamp_frame(board->phase_frame, 20.0f)
                           : 0.0f,
        .loop_override = 0};
    wm_layout_pose(board->background, &clip, 1);
    /* The previous-day copy has an authored one-unit X offset. Reuse the
     * central label's local anchor so a forward-page return lands at the
     * same position as the menu date, without a final ownership jump. */
    WmLayoutPaneState center_date;
    WmLayoutPaneState previous_date;
    if (wm_layout_pane_state(board->background, "T_Day_b", &center_date) &&
        wm_layout_pane_state(board->background, "T_Day_a", &previous_date)) {
        wm_layout_set_pane_translation(
            board->background, "T_Day_a", center_date.translation[0],
            previous_date.translation[1], previous_date.translation[2]);
    }
    char before[32], current[32], after[32];
    date_text(board->date, -1, before);
    date_text(board->date, 0, current);
    date_text(board->date, 1, after);
    if (scrolling) {
        /* Arrows, Calendar jumps and return-to-menu share one authored slide,
         * whose entering label names the actual target rather than ±1 day. */
        if (direction < 0)
            date_text(board->next_date, 0, before);
        else
            date_text(board->next_date, 0, after);
    }
    wm_layout_set_pose_text(board->background, "T_Day_a", before);
    /* Calendar and composer keep the date with the cards underneath their
     * shade and controls, including Memo, Letter and Address Book children.
     * A held card also owns the foreground over that date. Normal Board
     * entry/idle/exit retains the date above its own footer. */
    bool composing = wm_board_compose_phase(board->compose) != WM_COMPOSE_CLOSED;
    bool calendar_open = wm_board_calendar_phase(board->calendar) != WM_CALENDAR_CLOSED;
    bool retain_date =
        !composing && !calendar_open && !board->dragging &&
        board->mask_direction >= 0 &&
        (board->phase == WM_BOARD_ENTER || board->phase == WM_BOARD_READY ||
         (board->phase == WM_BOARD_EXIT && !returning));
    wm_layout_set_pose_text(board->background, "T_Day_b", retain_date ? "" : current);
    wm_layout_set_pose_text(board->background, "T_Day_c", after);
    board->scroll_offset_x = 0.0f;
    if (scrolling) {
        WmLayoutDrawOptions options = {.wide = true,
                                       .mode = WM_LAYOUT_IPL,
                                       .alpha = 1.0f,
                                       .on_pane = capture_scroll_offset,
                                       .context = &board->scroll_offset_x};
        wm_layout_draw(board->background, &options);
    }
}

static size_t utf8_unit_length(const char *text, size_t offset, unsigned *utf16_units) {
    unsigned char lead = (unsigned char)text[offset];
    if (lead < 0x80) {
        *utf16_units = 1;
        return 1;
    }
    size_t bytes = lead >= 0xF0 && lead <= 0xF4   ? 4
                   : lead >= 0xE0 && lead <= 0xEF ? 3
                   : lead >= 0xC2 && lead <= 0xDF ? 2
                                                  : 1;
    for (size_t index = 1; index < bytes; index++) {
        if (!text[offset + index] ||
            ((unsigned char)text[offset + index] & 0xC0) != 0x80) {
            bytes = 1;
            break;
        }
    }
    *utf16_units = bytes == 4 ? 2 : 1;
    return bytes;
}

static void thumbnail_text(const char *text, char buffer[64]) {
    size_t offset = 0;
    unsigned units = 0;
    while (text[offset] && text[offset] != '\n' && units < 6) {
        unsigned next_units = 0;
        size_t bytes = utf8_unit_length(text, offset, &next_units);
        if (units + next_units > 6)
            break;
        units += next_units;
        offset += bytes;
    }
    memcpy(buffer, text, offset);
    buffer[offset] = '\0';
    if (text[offset] && text[offset] != '\n')
        strcat(buffer, "...");
}

static bool memo_exit_phase(WmBoardPhase phase) {
    return phase == WM_BOARD_MEMO_CLOSE || phase == WM_BOARD_MEMO_ERASE_CLOSE;
}

static bool memo_reader_phase(WmBoardPhase phase) {
    return phase == WM_BOARD_MEMO_OPEN || phase == WM_BOARD_MEMO_READ ||
           phase == WM_BOARD_MEMO_BACK_SELECT || phase == WM_BOARD_MEMO_CLOSE ||
           phase == WM_BOARD_MEMO_TRASH_SELECT || phase == WM_BOARD_MEMO_DIALOG ||
           phase == WM_BOARD_MEMO_TRASH_CANCEL || phase == WM_BOARD_MEMO_ERASE_CLOSE;
}

void board_scene_pose_card(WmBoardScene *board, const WmBoardMemoPresentation *card,
                           bool neutral) {
    WmLayoutClip clips[BOARD_CLIP_CAPACITY];
    size_t count = 0;
    append_clip(clips, &count, "LetterS_a_PasteLetter", NULL, card->paste_frame);
    size_t position = card->entering
                          ? SIZE_MAX
                          : board_scene_visible_position(board, card->memo_index);
    BoardFocus *focus = !neutral && position < BOARD_MEMOS_PER_PAGE
                            ? &board->card_focus[position]
                            : NULL;
    if (focus && focus->active) {
        append_clip(clips, &count,
                    focus->entering ? "LetterS_a_FocusIn" : "LetterS_a_FocusOut", NULL,
                    board_scene_clamp_frame(focus->frame, 6.0f));
    }
    if (!neutral && card->next_page_frame >= 0.0f) {
        append_clip(clips, &count, "LetterS_a_NextPage", NULL, card->next_page_frame);
    }
    if (!neutral && !card->entering && board->selected == card->memo_index &&
        memo_reader_phase(board->phase)) {
        if (board->phase == WM_BOARD_MEMO_CLOSE && board->phase_frame >= 16.0f) {
            /* ExitLetter ends at the focused card size. Let its authored
             * FocusOut return the card to neutral before releasing it. */
            append_clip(clips, &count, "LetterS_a_FocusOut", NULL,
                        board_scene_clamp_frame(board->phase_frame - 16.0f, 6.0f));
        } else {
            bool closing = memo_exit_phase(board->phase);
            float frame = board->phase == WM_BOARD_MEMO_OPEN || closing
                              ? board_scene_clamp_frame(board->phase_frame, 16.0f)
                              : 16.0f;
            append_clip(clips, &count,
                        closing ? "LetterS_a_ExitLetter" : "LetterS_a_SelectLetter",
                        NULL, frame);
        }
    }
    const char *pin_animation = card->pin_kind == WM_BOARD_PIN_NEW ? "LetterS_a_NewAnim"
                                : card->pin_kind == WM_BOARD_PIN_DEFAULT
                                    ? "LetterS_a_DefAnim"
                                    : NULL;
    if (pin_animation) {
        /* Home's parked presentation uses the source's settled-card age. */
        float frame = card->pin_frame;
        append_clip(clips, &count, pin_animation, "G_New", frame);
        clips[count - 1].loop_override = 1;
    }
    wm_layout_pose(board->card, clips, count);
    char thumbnail[64];
    thumbnail_text(board->model.memos[card->memo_index].text, thumbnail);
    wm_layout_set_pose_text(board->card, "T_Letter", thumbnail);
    wm_layout_set_pane_visible(board->card, "Nigaoe", false);
}

void board_scene_card_matrix(const WmBoardScene *board, size_t position,
                             float matrix[12]) {
    const BoardMemo *memo = &board->model.memos[board->visible[position]];
    float x = memo->x;
    float y = memo->y;
    if (board->phase == WM_BOARD_MEMO_PAGE) {
        float progress = board_scene_clamp_frame(board->phase_frame, 15.0f) / 15.0f;
        float target = board->direction < 0 ? 304.0f : -304.0f;
        x += (target - x) * progress;
        y += (53.0f - y) * progress;
    }
    if (board->dragging && board->visible[position] == board->dragged_index) {
        x += board->drag_delta_x;
        y += board->drag_delta_y;
    }
    matrix_translation(x * (832.0f / 608.0f) + board->scroll_offset_x, y, matrix);
}

static void
append_visible_card(WmBoardScene *board, size_t memo_index,
                    WmBoardMemoPresentation cards[WM_BOARD_MAX_PRESENTED_MEMOS],
                    size_t *count) {
    size_t position = board_scene_visible_position(board, memo_index);
    float arrival_age = board_scene_card_arrival_age(board, memo_index);
    if (position == SIZE_MAX || *count >= WM_BOARD_MAX_PRESENTED_MEMOS ||
        arrival_age < 0.0f ||
        (board->phase == WM_BOARD_MEMO_ERASE_CLOSE && memo_index == board->selected))
        return;
    float matrix[12];
    board_scene_card_matrix(board, position, matrix);
    WmBoardPinKind pin = board_scene_sample_pin_kind(board, memo_index);
    cards[(*count)++] = (WmBoardMemoPresentation){
        .memo_index = memo_index,
        .x = matrix[3],
        .y = matrix[7],
        .paste_frame = board_scene_clamp_frame(arrival_age, 10.0f),
        .next_page_frame = board->phase == WM_BOARD_MEMO_PAGE
                               ? board_scene_clamp_frame(board->phase_frame, 14.0f)
                               : -1.0f,
        .pin_kind = pin,
        .pin_frame = board->pin_age - board->model.memos[memo_index].pin_start_clock};
}

static void
append_date_cards(WmBoardScene *board, WmBoardDate date, int direction,
                  WmBoardMemoPresentation cards[WM_BOARD_MAX_PRESENTED_MEMOS],
                  size_t *count) {
    size_t entered = 0;
    float page_offset = direction < 0 ? -832.0f : 832.0f;
    for (size_t position = 0;
         position < board->model.memo_count && entered < BOARD_MEMOS_PER_PAGE;
         position++) {
        size_t index = board->model.memo_order[position].index;
        const BoardMemo *memo = &board->model.memos[index];
        if (!board_model_same_date(memo->date, date))
            continue;
        float arrival_age = board_scene_card_arrival_age(board, index);
        if (arrival_age < 0.0f) {
            entered++;
            continue;
        }
        if (*count >= WM_BOARD_MAX_PRESENTED_MEMOS)
            break;
        WmBoardPinKind pin = board_scene_sample_pin_kind(board, index);
        cards[(*count)++] = (WmBoardMemoPresentation){
            .memo_index = index,
            .x = memo->x * (832.0f / 608.0f) + board->scroll_offset_x + page_offset,
            .y = memo->y,
            .paste_frame = board_scene_clamp_frame(arrival_age, 10.0f),
            .next_page_frame = -1.0f,
            .pin_kind = pin,
            .pin_frame = board->pin_age - board->model.memos[index].pin_start_clock,
            .entering = true};
        entered++;
    }
}

size_t wm_board_scene_memo_presentation(
    WmBoardScene *board, WmBoardMemoPresentation cards[WM_BOARD_MAX_PRESENTED_MEMOS]) {
    if (!board || !cards || board->phase == WM_BOARD_CLOSED)
        return 0;
    pose_background(board);
    size_t count = 0;
    for (size_t index = 0; index < board->visible_count; index++) {
        size_t memo_index = board->card_order[index];
        if (board->dragging && memo_index == board->dragged_index)
            continue;
        append_visible_card(board, memo_index, cards, &count);
    }
    if (board->dragging) {
        append_visible_card(board, board->dragged_index, cards, &count);
    }
    if (board->phase == WM_BOARD_DATE_SCROLL) {
        append_date_cards(board, board->next_date, board->direction, cards, &count);
    } else if (board->phase == WM_BOARD_EXIT && board->return_direction != 0) {
        append_date_cards(board, board->next_date, board->return_direction, cards,
                          &count);
    }
    return count;
}

size_t wm_board_scene_parked_memo_presentation(
    WmBoardScene *board, WmBoardDate today,
    WmBoardMemoPresentation cards[WM_BOARD_MAX_PRESENTED_MEMOS]) {
    if (!board || !cards || board->phase != WM_BOARD_CLOSED ||
        !wm_board_date_valid(today))
        return 0;
    size_t count = 0;
    size_t found = 0;
    for (size_t position = 0;
         position < board->model.memo_count && found < BOARD_MEMOS_PER_PAGE;
         position++) {
        size_t index = board->model.memo_order[position].index;
        const BoardMemo *memo = &board->model.memos[index];
        if (!board_model_same_date(memo->date, today))
            continue;
        found++;
        float arrival_age = board_scene_card_arrival_age(board, index);
        if (arrival_age < 0.0f)
            continue;
        WmBoardPinKind pin = board_scene_sample_pin_kind(board, index);
        bool scheduled = false;
        for (size_t arrival = 0; arrival < board->card_arrival_count; arrival++) {
            if (board->card_arrivals[arrival].memo_index == index) {
                scheduled = true;
                break;
            }
        }
        cards[count++] = (WmBoardMemoPresentation){
            .memo_index = index,
            .x = memo->x * (832.0f / 608.0f),
            .y = memo->y,
            .paste_frame = board_scene_clamp_frame(arrival_age, 10.0f),
            .next_page_frame = -1.0f,
            .pin_kind = pin,
            .pin_frame =
                scheduled ? board->pin_age - board->model.memos[index].pin_start_clock
                          : 100000.0f};
    }
    return count;
}

static void card_camera_matrix(const WmBoardMemoPresentation *card,
                               const float camera[12], float matrix[12]) {
    matrix_translation(card->x, card->y, matrix);
    if (!camera)
        return;
    memcpy(matrix, camera, 12 * sizeof(*matrix));
    matrix[3] += camera[0] * card->x + camera[1] * card->y;
    matrix[7] += camera[4] * card->x + camera[5] * card->y;
    matrix[11] += camera[8] * card->x + camera[9] * card->y;
}

void wm_board_scene_draw_parked_memos(WmBoardScene *board, WmBoardDate today,
                                      const float camera[12]) {
    WmBoardMemoPresentation cards[WM_BOARD_MAX_PRESENTED_MEMOS];
    size_t count = wm_board_scene_parked_memo_presentation(board, today, cards);
    for (size_t index = 0; index < count; index++) {
        board_scene_pose_card(board, &cards[index], true);
        float matrix[12];
        card_camera_matrix(&cards[index], camera, matrix);
        present(board, board->card, matrix);
    }
}

static bool reading_memo(const WmBoardScene *board) {
    return memo_reader_phase(board->phase);
}

void board_scene_pose_reader(WmBoardScene *board) {
    WmLayoutClip clips[8] = {
        {.animation = memo_exit_phase(board->phase) ? "my_Memo_a_ExitLetter"
                                                    : "my_Memo_a_SelectLetter",
         .frame = board->phase == WM_BOARD_MEMO_OPEN || memo_exit_phase(board->phase)
                      ? board_scene_clamp_frame(board->phase_frame, 16.0f)
                      : 16.0f,
         .loop_override = 0},
        {.animation = "my_Memo_a_Loop",
         .group = "G_ArwRoop",
         .frame = fmodf(board->age, 55.0f),
         .loop_override = 1}};
    size_t extra = wm_board_reader_scroll_clips(&board->reader_scroll, clips + 2, 6);
    wm_layout_pose(board->reader, clips, 2 + extra);
    /* The source text panes contain visible runs of placeholder 'i' glyphs.
     * The HTML reader clears every authored text pane before filling the two
     * reader labels; leaving these defaults overlays the posted Memo. */
    wm_layout_set_pose_text(board->reader, "T_2l_TextBox", "");
    wm_layout_set_pose_text(board->reader, "T_TouchLetter", "");
    wm_layout_set_pose_text(board->reader, "T_Nigaoe", "");
    wm_layout_set_pose_text(board->reader, "T_Header", "Memo");
    wm_layout_set_pose_text(board->reader, "T_Letter",
                            board->model.memos[board->selected].text);
    wm_layout_set_pane_visible(board->reader, "Nigaoe", false);
    wm_layout_set_pane_visible(board->reader, "B_Nigaoe", false);
    wm_layout_set_pane_translation(board->reader, "N_Memo", 0.0f,
                                   wm_board_reader_scroll_offset(&board->reader_scroll),
                                   0.0f);
    wm_layout_set_pane_translation(
        board->reader, "N_Footer", 0.0f,
        wm_board_reader_scroll_footer_shift(&board->reader_scroll), 0.0f);
}

static bool reader_body_pane(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    static const char *const names[] = {
        "RootPane",   "N_Memo",     "N_MemoRoot", "N_Body",   "Body_s", "Body3",
        "Picture_11", "Picture_12", "Picture_13", "Body3_04", "B_Body"};
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        if (strcmp(pane->name, names[index]) == 0)
            return true;
    }
    return false;
}

typedef enum ReaderDrawPart {
    READER_DRAW_HEADER_AND_BODY,
    READER_DRAW_REST
} ReaderDrawPart;

typedef enum ReaderBranch {
    READER_BRANCH_COMMON,
    READER_BRANCH_HEADER,
    READER_BRANCH_BODY,
    READER_BRANCH_REST
} ReaderBranch;

typedef struct ReaderDrawFilter {
    ReaderDrawPart part;
    ReaderBranch branch;
} ReaderDrawFilter;

static bool reader_layer_pane(void *context, const WmLayoutPaneView *pane) {
    ReaderDrawFilter *filter = context;
    if (strcmp(pane->name, "RootPane") == 0 || strcmp(pane->name, "N_Memo") == 0 ||
        strcmp(pane->name, "N_MemoRoot") == 0) {
        filter->branch = READER_BRANCH_COMMON;
        return true;
    }
    if (strcmp(pane->name, "N_Header") == 0) {
        filter->branch = READER_BRANCH_HEADER;
    } else if (strcmp(pane->name, "N_Body") == 0) {
        filter->branch = READER_BRANCH_BODY;
    } else if (strcmp(pane->name, "N_Footer") == 0 ||
               strcmp(pane->name, "Nigaoe") == 0 ||
               strcmp(pane->name, "B_Nigaoe") == 0 ||
               strcmp(pane->name, "T_2l_TextBox") == 0 ||
               strcmp(pane->name, "B_2l_TextBox") == 0 ||
               strcmp(pane->name, "T_TouchLetter") == 0 ||
               strcmp(pane->name, "T_Letter") == 0 ||
               strcmp(pane->name, "T_Nigaoe") == 0 ||
               strcmp(pane->name, "N_TopBtn") == 0) {
        filter->branch = READER_BRANCH_REST;
    }
    if (filter->branch == READER_BRANCH_COMMON)
        return true;
    if (filter->part == READER_DRAW_HEADER_AND_BODY) {
        return filter->branch == READER_BRANCH_HEADER ||
               filter->branch == READER_BRANCH_BODY;
    }
    return filter->branch == READER_BRANCH_REST;
}

static void draw_reader_body_rows(WmBoardScene *board, const float matrix[12]) {
    size_t rows = wm_board_reader_scroll_repeated_rows(&board->reader_scroll);
    float height = board->reader_scroll.line_height;
    if (!rows || height <= 0.0f)
        return;
    /* Body strips are identical authored panes. Draw only strips intersecting
     * the 456-unit reader viewport, keeping long memos at bounded draw cost. */
    float origin = matrix[7] + wm_board_reader_scroll_offset(&board->reader_scroll);
    float first_float = ceilf((origin - 280.0f) / height);
    float last_float = floorf((origin + 280.0f) / height);
    if (last_float < 1.0f || first_float > (float)rows)
        return;
    size_t first = first_float > 1.0f ? (size_t)first_float : 1;
    size_t last = last_float < (float)rows ? (size_t)last_float : rows;
    for (size_t row = first; row <= last; row++) {
        wm_layout_set_pane_translation(board->reader, "N_Body", 0.0f,
                                       -height * (float)row, 0.0f);
        wm_layout_present_filtered_with_fonts(
            board->platform, board->textures, board->fonts, board->reader, true,
            WM_LAYOUT_IPL, matrix, reader_body_pane, NULL);
    }
    wm_layout_set_pane_translation(board->reader, "N_Body", 0.0f, 0.0f, 0.0f);
}

static void draw_reader(WmBoardScene *board) {
    if (!reading_memo(board) || board->selected >= board->model.memo_count)
        return;
    WmLayoutClip mask = {
        .animation = memo_exit_phase(board->phase) ? "my_BbsMask_a_MaskOut"
                                                   : "my_BbsMask_a_MaskIn",
        .frame = board->phase == WM_BOARD_MEMO_OPEN || memo_exit_phase(board->phase)
                     ? board_scene_clamp_frame(board->phase_frame, 20.0f)
                     : 20.0f,
        .loop_override = 0};
    wm_layout_pose(board->mask, &mask, 1);
    present(board, board->mask, NULL);

    if (memo_exit_phase(board->phase) && board->phase_frame >= 17.0f)
        return;
    board_scene_pose_reader(board);
    const BoardMemo *memo = &board->model.memos[board->selected];
    float fraction =
        board->phase == WM_BOARD_MEMO_OPEN
            ? 1.0f - board_scene_clamp_frame(board->phase_frame, 17.0f) / 17.0f
        : memo_exit_phase(board->phase)
            ? board_scene_clamp_frame(board->phase_frame, 17.0f) / 17.0f
            : 0.0f;
    float matrix[12];
    matrix_translation(memo->x * fraction * (832.0f / 608.0f), memo->y * fraction,
                       matrix);
    ReaderDrawFilter first = {READER_DRAW_HEADER_AND_BODY, READER_BRANCH_COMMON};
    wm_layout_present_filtered_with_fonts(
        board->platform, board->textures, board->fonts, board->reader, true,
        WM_LAYOUT_IPL, matrix, reader_layer_pane, &first);
    draw_reader_body_rows(board, matrix);
    ReaderDrawFilter rest = {READER_DRAW_REST, READER_BRANCH_COMMON};
    wm_layout_present_filtered_with_fonts(
        board->platform, board->textures, board->fonts, board->reader, true,
        WM_LAYOUT_IPL, matrix, reader_layer_pane, &rest);
}

static void draw_child_mask(WmBoardScene *board) {
    if (board->mask_direction == 0)
        return;
    WmLayoutClip clip = {.animation = board->mask_direction > 0
                                          ? "my_BbsMask_a_MaskIn"
                                          : "my_BbsMask_a_MaskOut",
                         .frame = board_scene_clamp_frame(board->mask_age, 20.0f),
                         .loop_override = 0};
    wm_layout_pose(board->mask, &clip, 1);
    present(board, board->mask, NULL);
}

void wm_board_scene_draw_body(WmBoardScene *board) {
    if (!board || board->phase == WM_BOARD_CLOSED)
        return;
    WmBoardMemoPresentation cards[WM_BOARD_MAX_PRESENTED_MEMOS];
    size_t count = wm_board_scene_memo_presentation(board, cards);
    present(board, board->background, NULL);
    for (size_t index = 0; index < count; index++) {
        board_scene_pose_card(board, &cards[index], false);
        float matrix[12];
        matrix_translation(cards[index].x, cards[index].y, matrix);
        present(board, board->card, matrix);
    }
    draw_reader(board);
    draw_child_mask(board);
    if (wm_board_calendar_phase(board->calendar) != WM_CALENDAR_CLOSED) {
        wm_board_calendar_draw(board->calendar);
    }
    if (wm_board_compose_phase(board->compose) != WM_COMPOSE_CLOSED) {
        wm_board_compose_draw(board->compose);
    }
}

static float footer_scene_frame(const WmBoardScene *board) {
    if (board->phase == WM_BOARD_ENTER) {
        return 1000.0f + board_scene_clamp_frame(board->phase_frame, 40.0f);
    }
    if (board->phase == WM_BOARD_EXIT) {
        return 6000.0f + board_scene_clamp_frame(board->phase_frame, 40.0f);
    }
    if (board->phase == WM_BOARD_MEMO_OPEN) {
        return board->phase_frame < 13.0f
                   ? 3100.0f + board->phase_frame
                   : 3600.0f +
                         board_scene_clamp_frame(board->phase_frame - 13.0f, 13.0f);
    }
    if (board->phase == WM_BOARD_MEMO_READ)
        return 3613.0f;
    if (board->phase == WM_BOARD_MEMO_BACK_SELECT) {
        return 3613.0f;
    }
    if (board->phase == WM_BOARD_MEMO_CLOSE) {
        return board->phase_frame < 13.0f
                   ? 3620.0f + board->phase_frame
                   : 3426.0f +
                         board_scene_clamp_frame(board->phase_frame - 13.0f, 13.0f);
    }
    if (board->phase == WM_BOARD_MEMO_TRASH_SELECT) {
        return board->phase_frame < 20.0f
                   ? 3613.0f
                   : 3620.0f +
                         board_scene_clamp_frame(board->phase_frame - 20.0f, 13.0f);
    }
    if (board->phase == WM_BOARD_MEMO_DIALOG)
        return 3633.0f;
    if (board->phase == WM_BOARD_MEMO_TRASH_CANCEL) {
        return 3600.0f + board_scene_clamp_frame(board->phase_frame, 13.0f);
    }
    if (board->phase == WM_BOARD_MEMO_ERASE_CLOSE) {
        return 3426.0f + board_scene_clamp_frame(board->phase_frame, 13.0f);
    }
    return 1040.0f;
}

void board_scene_pose_footer(WmBoardScene *board) {
    WmLayoutClip clips[BOARD_CLIP_CAPACITY];
    size_t count = 0;
    append_clip(clips, &count, "my_IplTop_e", "G_SeenChange",
                footer_scene_frame(board));
    append_clip(clips, &count, "my_IplTop_e", "G_ArwRoop",
                10000.0f + fmodf(board->age, 55.0f));
    static const char *const arrow_end_groups[2] = {"G_ArwL_End", "G_ArwR_End"};
    static const char *const arrow_tab_groups[2] = {"G_TabaL", "G_TabaR"};
    static const char *const arrow_focus_groups[2] = {"G_ArwL_Focus", "G_ArwR_Focus"};
    static const char *const arrow_press_groups[2] = {"G_ArwL_Ac", "G_ArwR_Ac"};
    const WmBoardControl arrow_controls[2] = {WM_BOARD_CONTROL_PREVIOUS,
                                              WM_BOARD_CONTROL_NEXT};
    for (size_t arrow = 0; arrow < 2; arrow++) {
        WmBoardDate arrow_date =
            board->phase == WM_BOARD_EXIT && board->return_direction != 0 ? board->today
                                                                          : board->date;
        bool hidden =
            arrow == 0
                ? board_model_same_date(arrow_date, (WmBoardDate){2000, 1, 1}) &&
                      board->page + 1 >= wm_board_scene_memo_page_count(board)
                : board_model_same_date(arrow_date, (WmBoardDate){2035, 12, 31}) &&
                      board->page == 0;
        bool reader_opening = board->phase == WM_BOARD_MEMO_OPEN;
        bool reader_closing = board->phase == WM_BOARD_MEMO_CLOSE;
        bool reader_hidden = memo_reader_phase(board->phase) && !reader_closing;
        float arrow_frame = 10.0f;
        if (reader_opening || (reader_closing && !hidden)) {
            arrow_frame = board_scene_clamp_frame(board->phase_frame, 10.0f);
        } else if (board->phase == WM_BOARD_EXIT) {
            arrow_frame = board_scene_clamp_frame(board->phase_frame, 10.0f);
        } else if (board->phase == WM_BOARD_ENTER && !hidden) {
            arrow_frame = board_scene_clamp_frame(board->phase_frame - 30.0f, 10.0f);
        } else if (board->mask_direction < 0 && !hidden) {
            arrow_frame = board_scene_clamp_frame(board->mask_age, 10.0f);
        }
        append_clip(clips, &count, "my_IplTop_e", arrow_end_groups[arrow],
                    (board->phase == WM_BOARD_EXIT || hidden || reader_hidden
                         ? 10100.0f
                         : 10150.0f) +
                        arrow_frame);
        bool memo_page = arrow == 0
                             ? board->page + 1 < wm_board_scene_memo_page_count(board)
                             : board->page > 0;
        append_clip(clips, &count, "my_IplTop_e", arrow_tab_groups[arrow],
                    memo_page && board->phase == WM_BOARD_READY ? 10.0f : 40.0f);
        BoardFocus *focus = &board->button_focus[arrow_controls[arrow]];
        append_clip(clips, &count, "my_IplTop_e", arrow_focus_groups[arrow],
                    (focus->active && !focus->entering ? 10800.0f : 10600.0f) +
                        board_scene_clamp_frame(focus->frame, 15.0f));
        if (board->arrow_press[arrow] >= 0.0f) {
            append_clip(clips, &count, "my_IplTop_e", arrow_press_groups[arrow],
                        10700.0f +
                            board_scene_clamp_frame(board->arrow_press[arrow], 30.0f));
        }
    }
    const struct {
        WmBoardControl control;
        const char *group;
        float enter;
        float leave;
        float enter_frames;
        float leave_frames;
    } buttons[] = {{WM_BOARD_CONTROL_BACK, "G_Ch", 5900, 5930, 6, 8},
                   {WM_BOARD_CONTROL_CALENDAR, "G_Cal", 1900, 1930, 6, 8},
                   {WM_BOARD_CONTROL_CREATE, "G_Add", 3900, 3930, 6, 8}};
    for (size_t index = 0; index < sizeof(buttons) / sizeof(buttons[0]); index++) {
        BoardFocus *focus = &board->button_focus[buttons[index].control];
        if (!focus->active)
            continue;
        append_clip(clips, &count, "my_IplTop_e", buttons[index].group,
                    (focus->entering ? buttons[index].enter : buttons[index].leave) +
                        board_scene_clamp_frame(focus->frame,
                                                focus->entering
                                                    ? buttons[index].enter_frames
                                                    : buttons[index].leave_frames));
    }
    if (memo_reader_phase(board->phase)) {
        const struct {
            WmBoardControl control;
            const char *group;
            float enter_frames;
        } reader_buttons[] = {{WM_BOARD_CONTROL_MEMO_BACK, "G_CalExit", 6.0f},
                              {WM_BOARD_CONTROL_MEMO_TRASH, "G_Dust", 9.0f}};
        for (size_t index = 0;
             index < sizeof(reader_buttons) / sizeof(reader_buttons[0]); index++) {
            BoardFocus *focus = &board->button_focus[reader_buttons[index].control];
            if (!focus->active)
                continue;
            append_clip(clips, &count, "my_IplTop_e", reader_buttons[index].group,
                        (focus->entering ? 2900.0f : 2930.0f) +
                            board_scene_clamp_frame(
                                focus->frame, focus->entering
                                                  ? reader_buttons[index].enter_frames
                                                  : 8.0f));
        }
    }
    if (board->phase == WM_BOARD_MEMO_TRASH_SELECT) {
        append_clip(clips, &count, "my_IplTop_e", "G_Dust",
                    2800.0f + board_scene_clamp_frame(board->phase_frame, 20.0f));
    }
    if (board->phase == WM_BOARD_MEMO_BACK_SELECT) {
        append_clip(clips, &count, "my_IplTop_e", "G_CalExit",
                    3000.0f + board_scene_clamp_frame(board->phase_frame, 20.0f));
    }
    if ((board->phase == WM_BOARD_ENTER && board->phase_frame < 10.0f) ||
        (board->phase == WM_BOARD_EXIT && board->phase_frame >= 20.0f)) {
        bool entering_grid = board->phase == WM_BOARD_EXIT;
        float grid_frame =
            entering_grid ? board_scene_clamp_frame(board->phase_frame - 20.0f, 10.0f)
                          : board_scene_clamp_frame(board->phase_frame, 10.0f);
        append_clip(clips, &count, "my_IplTop_e", "G_ArwL_End",
                    (board->grid_page > 0
                         ? (entering_grid ? 10150.0f : 10100.0f) + grid_frame
                         : 10110.0f));
        append_clip(clips, &count, "my_IplTop_e", "G_ArwR_End",
                    (board->grid_page < 3
                         ? (entering_grid ? 10150.0f : 10100.0f) + grid_frame
                         : 10110.0f));
    }
    unsigned badge = wm_board_scene_today_count(board);
    append_clip(clips, &count, "my_IplTop_e", "G_BbsSignal",
                badge ? 1.0f + fmodf(board->age, 399.0f) : 0.0f);
    append_clip(clips, &count, "my_IplTop_e", "G_BbsSignal_new", 0.0f);
    wm_layout_pose(board->footer, clips, count);
    bool reader = memo_reader_phase(board->phase);
    wm_layout_set_pose_text(board->footer, "T_CalAdd_R", "");
    wm_layout_set_pose_text(board->footer, "T_CalExit", reader ? "Back" : "");
    wm_layout_set_pose_text(board->footer, "T_Add", "");
    wm_layout_set_pose_text(board->footer, "T_Dust", reader ? "Trash" : "");
    char counter[4] = {0};
    if (badge)
        snprintf(counter, sizeof(counter), "%u", badge);
    wm_layout_set_pose_text(board->footer, "T_BbsMark1", counter);
}

void wm_board_scene_draw_footer(WmBoardScene *board) {
    if (!board || board->phase == WM_BOARD_CLOSED)
        return;
    WmBoardChild child = wm_board_scene_child(board);
    if (child == WM_BOARD_CHILD_CALENDAR || child == WM_BOARD_CHILD_COMPOSE)
        return;
    board_scene_pose_footer(board);
    present(board, board->footer, NULL);
    if (child == WM_BOARD_CHILD_ERASE)
        wm_board_erase_draw(board->erase);
    if (child == WM_BOARD_CHILD_NONE && !board->dragging &&
        board->mask_direction >= 0 &&
        (board->phase == WM_BOARD_ENTER || board->phase == WM_BOARD_READY ||
         (board->phase == WM_BOARD_EXIT && board->return_direction == 0))) {
        /* These picture panes are not alpha-influencing, so setting their
         * opacity to zero keeps the authored T_Day_b text transform intact. */
        wm_layout_set_pane_alpha(board->background, "TopBack_a", 0.0f);
        wm_layout_set_pane_alpha(board->background, "TopBack_b", 0.0f);
        wm_layout_set_pane_alpha(board->background, "TopBack_c", 0.0f);
        wm_layout_set_pose_text(board->background, "T_Day_a", "");
        wm_layout_set_pose_text(board->background, "T_Day_c", "");
        char date[32];
        date_text(board->date, 0, date);
        wm_layout_set_pose_text(board->background, "T_Day_b", date);
        present(board, board->background, NULL);
    }
}

typedef struct BoardFooterAnchorSearch {
    const char *name;
    float x;
    float y;
    bool found;
} BoardFooterAnchorSearch;

static bool capture_footer_anchor(void *context, const WmLayoutPaneView *pane) {
    BoardFooterAnchorSearch *search = context;
    if (strcmp(pane->name, search->name) == 0) {
        search->x = pane->matrix[3];
        search->y = pane->matrix[7];
        search->found = true;
    }
    return true;
}

bool wm_board_scene_footer_button_anchor(const WmBoardScene *board,
                                         WmBoardControl control, float *x, float *y) {
    bool page_arrow =
        control == WM_BOARD_CONTROL_PREVIOUS || control == WM_BOARD_CONTROL_NEXT;
    if (!board || !x || !y ||
        (board->phase != WM_BOARD_READY &&
         !(page_arrow && memo_reader_phase(board->phase))) ||
        wm_board_scene_child(board) != WM_BOARD_CHILD_NONE)
        return false;
    const char *name = control == WM_BOARD_CONTROL_BACK       ? "B_Ch"
                       : control == WM_BOARD_CONTROL_CALENDAR ? "B_Cal"
                       : control == WM_BOARD_CONTROL_CREATE   ? "B_Add"
                       : control == WM_BOARD_CONTROL_PREVIOUS ? "N_ArwL_End"
                       : control == WM_BOARD_CONTROL_NEXT     ? "N_ArwR__End"
                                                              : NULL;
    if (!name)
        return false;
    BoardFooterAnchorSearch search = {.name = name};
    WmLayoutDrawOptions draw = {.wide = true,
                                .mode = WM_LAYOUT_IPL,
                                .alpha = 1.0f,
                                .on_pane = capture_footer_anchor,
                                .context = &search};
    wm_layout_draw(board->footer, &draw);
    if (!search.found)
        return false;
    *x = search.x;
    *y = search.y;
    return true;
}

typedef struct BoardVisualScaleSearch {
    const char *name;
    float scale;
    bool found;
} BoardVisualScaleSearch;

static bool capture_footer_visual_scale(void *context, const WmLayoutPaneView *pane) {
    BoardVisualScaleSearch *search = context;
    if (strcmp(pane->name, search->name) == 0) {
        search->scale = hypotf(pane->matrix[0], pane->matrix[4]);
        search->found = true;
    }
    return true;
}

bool wm_board_scene_footer_button_visual_scale(const WmBoardScene *board,
                                               WmBoardControl control, float *scale) {
    if (!board || !scale ||
        (board->phase != WM_BOARD_READY && !memo_reader_phase(board->phase)) ||
        wm_board_scene_child(board) != WM_BOARD_CHILD_NONE)
        return false;
    const char *name =
        control == WM_BOARD_CONTROL_MEMO_BACK && board->phase == WM_BOARD_MEMO_READ
            ? "N_BtnL_a3_Cal"
        : control == WM_BOARD_CONTROL_MEMO_TRASH && board->phase == WM_BOARD_MEMO_READ
            ? "N_Dust_00"
        : control == WM_BOARD_CONTROL_CALENDAR && board->phase == WM_BOARD_READY
            ? "N_BtnL_a0_Cal"
        : control == WM_BOARD_CONTROL_CREATE && board->phase == WM_BOARD_READY
            ? "N_BtnL_a0_Add"
        : control == WM_BOARD_CONTROL_BACK ? "N_BtnR_a0_Ch"
                                           : NULL;
    if (!name)
        return false;
    BoardVisualScaleSearch search = {.name = name};
    wm_layout_visit_all_transforms(board->footer, true, WM_LAYOUT_IPL, NULL,
                                   capture_footer_visual_scale, &search);
    if (!search.found)
        return false;
    *scale = search.scale;
    return true;
}
