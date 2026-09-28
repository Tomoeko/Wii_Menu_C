#include "resource_scene_internal.h"

#include "wii_menu/layout/layout_present.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct BalloonAnchorSearch {
    const char *pane_name;
    float x;
    float y;
    bool found;
} BalloonAnchorSearch;

static bool collect_balloon_anchor(void *context, const WmLayoutPaneView *pane) {
    BalloonAnchorSearch *search = context;
    if (strcmp(pane->name, search->pane_name) == 0) {
        search->x = pane->matrix[3];
        search->y = pane->matrix[7];
        search->found = true;
    }
    return true;
}

static bool footer_balloon_anchor(const WmLayout *layout, const char *pane_name,
                                  const float parent_matrix[12], float *x, float *y) {
    BalloonAnchorSearch search = {.pane_name = pane_name};
    WmLayoutDrawOptions options = {.wide = true,
                                   .mode = WM_LAYOUT_IPL,
                                   .alpha = 1.0f,
                                   .parent_matrix = parent_matrix,
                                   .on_pane = collect_balloon_anchor,
                                   .context = &search};
    wm_layout_draw(layout, &options);
    if (!search.found)
        return false;
    *x = search.x;
    *y = search.y;
    return true;
}

void wm_resource_scene_draw_balloons(WmResourceScene *scene, const WmMenu *menu,
                                     const WmBoardScene *board) {
    static const char *const titles[] = {"Wii Options",  "Wii Message Board",
                                         "SD Card Menu", "Wii Menu",
                                         "Calendar",     "Create Message"};
    static const char *const footer_panes[] = {"B_Set", "B_Bbs", "Ac"};
    for (int index = 0; index < BALLOON_COUNT; index++) {
        if (board ? index < BALLOON_BOARD_BACK : index >= BALLOON_BOARD_BACK)
            continue;
        BalloonAnimation *state = &scene->balloons[index];
        if (state->phase != BALLOON_ENTER && state->phase != BALLOON_HOLD &&
            state->phase != BALLOON_LEAVE)
            continue;
        const char *original = index < WM_SLOT_COUNT ? menu->slots[index].title
                                                     : titles[index - WM_SLOT_COUNT];
        if (index < WM_SLOT_COUNT && (!menu || !menu->slots[index].occupied))
            continue;
        char label[160];
        snprintf(label, sizeof(label), "%s", original);
        WmLayoutClip clip = {.animation = "my_IplTopBalloon_a_BalloonInOut",
                             .frame =
                                 index < WM_SLOT_COUNT && state->phase == BALLOON_LEAVE
                                     ? scene->balloon_end + 1.0f - state->frame
                                     : state->frame,
                             .loop_override = 0};
        if (!wm_layout_pose(scene->balloon, &clip, 1))
            continue;
        WmLayoutPaneState text_pane;
        if (!wm_layout_pane_state(scene->balloon, "T_Balloon", &text_pane))
            continue;
        float text_width = wm_font_cache_measure_text(scene->fonts, scene->balloon,
                                                      &text_pane, label, strlen(label));
        while (
            (text_width > 390.32f || (index >= WM_SLOT_COUNT && strlen(label) > 20)) &&
            strlen(label) > 3) {
            size_t length = strlen(label);
            if (length >= 3 && strcmp(label + length - 3, "...") == 0) {
                label[length - 3] = '\0';
                length -= 3;
            }
            if (length <= 3)
                break;
            label[length - 1] = '\0';
            strncat(label, "...", sizeof(label) - strlen(label) - 1);
            text_width = wm_font_cache_measure_text(scene->fonts, scene->balloon,
                                                    &text_pane, label, strlen(label));
        }
        float width = fmaxf(160.0f * 832.0f / 608.0f, text_width + 40.0f);
        wm_layout_set_pane_size(scene->balloon, "W_Base", width, 48.0f);
        wm_layout_set_pane_size(scene->balloon, "W_Shade", width, 48.0f);
        wm_layout_set_pose_text(scene->balloon, "T_Balloon", label);

        float x, y;
        float margin = index < WM_SLOT_COUNT ? 60.0f : 120.0f;
        if (index < WM_SLOT_COUNT) {
            int local = index % WM_CHANNELS_PER_PAGE;
            x = (-192.0f + (float)(local % 4) * 128.0f) * (832.0f / 608.0f);
            y = 75.0f - (float)(local / 4) * 96.0f;
        } else if (index < BALLOON_BOARD_BACK) {
            int footer_index = index - WM_SLOT_COUNT;
            const WmLayout *layout =
                footer_index == 2 ? scene->sd_button : scene->footer;
            const float *parent =
                footer_index == 2 ? wm_resource_scene_sd_position : NULL;
            if (!footer_balloon_anchor(layout, footer_panes[footer_index], parent, &x,
                                       &y))
                continue;
            y += 50.0f;
            if (footer_index == 2)
                margin = 200.0f;
        } else {
            WmBoardControl control = index == BALLOON_BOARD_BACK ? WM_BOARD_CONTROL_BACK
                                     : index == BALLOON_CALENDAR
                                         ? WM_BOARD_CONTROL_CALENDAR
                                         : WM_BOARD_CONTROL_CREATE;
            if (!wm_board_scene_footer_button_anchor(board, control, &x, &y))
                continue;
            y += 50.0f;
        }
        x = fmaxf(-416.0f + margin + width * 0.5f,
                  fminf(416.0f - margin - width * 0.5f, x));
        /* Footer text moves N_Balloon beneath the IPL-scaled root, while
         * channel text moves the root itself. Only the footer translation
         * inherits the root's widescreen X scale. */
        float draw_x = index < WM_SLOT_COUNT ? x : x * (832.0f / 608.0f);
        float position[12] = {1, 0, 0, draw_x, 0, 1, 0, y, 0, 0, 1, 0};
        wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts,
                                     scene->balloon, true, WM_LAYOUT_IPL, position);
    }
}

void wm_resource_scene_draw_board_balloons(WmResourceScene *scene,
                                           const WmBoardScene *board, WmBoardHit hover,
                                           float elapsed_seconds) {
    if (!scene || !board || !isfinite(elapsed_seconds))
        return;
    wm_resource_scene_advance_board_balloon_state(scene, board, hover, elapsed_seconds);
    if (wm_board_scene_phase(board) == WM_BOARD_READY &&
        wm_board_scene_child(board) == WM_BOARD_CHILD_NONE)
        wm_resource_scene_draw_balloons(scene, NULL, board);
}
