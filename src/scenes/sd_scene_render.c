#include "sd_scene_internal.h"

#include "wii_menu/animation/channel_animation.h"
#include "wii_menu/input/source_hit.h"
#include "wii_menu/layout/layout_present.h"
#include "wii_menu/render/material_prepare.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *fallback_message(unsigned identifier) {
    switch (identifier) {
        case 157:
            return "Welcome to the SD Card Menu.";
        case 158:
            return "Channels stored on an SD Card appear here.";
        case 159:
            return "Select a channel to view it.";
        case 160:
            return "SD Card Menu";
        case 163:
            return "Next";
        case 164:
            return "Close";
        case 165:
            return "Back";
        case 166:
            return "SD Card Menu Help";
        case 168:
            return "Wii Menu";
        case 169:
            return "Nothing is inserted in the SD Card Slot.";
        case 170:
            return "Checking the SD Card...";
        case 171:
            return "The inserted device cannot be used.";
        case 195:
            return "An SD Card process failed.";
        case 201:
            return "About the SD Card Menu";
        case 202:
            return "You can open this guide again with Help.";
        default:
            return "";
    }
}

static const char *message(WmSdScene *scene, unsigned identifier) {
    const char *provided =
        scene->message_provider
            ? scene->message_provider(scene->message_context, identifier)
            : NULL;
    return provided ? provided : fallback_message(identifier);
}

typedef struct SdTraversal {
    unsigned page;
    SdTile tiles[SD_VISIBLE_TILES];
    float page_anchor[3][12];
    bool has_page_anchor[3];
} SdTraversal;

static bool collect_page_anchor(void *context, const WmLayoutPaneView *pane) {
    SdTraversal *traversal = context;
    const char *name = pane->name;
    if (strncmp(name, "N_Clock", 7) == 0 && name[7] >= '0' && name[7] <= '2' &&
        name[8] == '\0') {
        unsigned index = (unsigned)(name[7] - '0');
        memcpy(traversal->page_anchor[index], pane->matrix,
               sizeof(traversal->page_anchor[index]));
        traversal->has_page_anchor[index] = true;
    }
    return true;
}

static bool collect_grid_pane(void *context, const WmLayoutPaneView *pane) {
    SdTraversal *traversal = context;
    const char *name = pane->name;
    if (strncmp(name, "N_Ch_", 5) != 0 || name[5] < 'a' || name[5] > 'e' ||
        name[6] < '0' || name[6] > '9' || name[7] < '0' || name[7] > '9' ||
        name[8] != '\0')
        return true;
    unsigned group = (unsigned)(name[5] - 'a');
    unsigned relative_slot =
        (unsigned)(name[6] - '0') * 10u + (unsigned)(name[7] - '0');
    if (relative_slot == 0 || relative_slot > WM_SD_SLOTS_PER_PAGE)
        return true;
    int page = (int)traversal->page + (int)group - 2;
    if (page < 0 || page >= WM_SD_PAGE_COUNT)
        return true;
    unsigned position = group * WM_SD_SLOTS_PER_PAGE + relative_slot - 1;
    SdTile *tile = &traversal->tiles[position];
    const float scale_x = (float)WM_FRAME_WIDTH / 832.0f;
    const float width = 170.0f * scale_x;
    const float center_x = WM_FRAME_WIDTH * 0.5f + pane->matrix[3] * scale_x;
    const float center_y = WM_FRAME_HEIGHT * 0.5f - pane->matrix[7];
    tile->valid = center_x + width * 0.5f > 0.0f &&
                  center_x - width * 0.5f < WM_FRAME_WIDTH && center_y + 48.0f > 0.0f &&
                  center_y - 48.0f < WM_FRAME_HEIGHT;
    tile->absolute_slot = (unsigned)page * WM_SD_SLOTS_PER_PAGE + relative_slot - 1;
    memcpy(tile->matrix, pane->matrix, sizeof(tile->matrix));
    tile->clip = (WmClipRect){center_x - width * 0.5f, center_y - 48.0f, width, 96.0f};
    return true;
}

static bool grid_base_filter(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    if (strcmp(pane->name, "RootPane") == 0 || strcmp(pane->name, "N_Ch") == 0 ||
        strcmp(pane->name, "N_ChAll") == 0)
        return true;
    return strncmp(pane->name, "BaseMask", 8) == 0;
}

static bool grid_trim_filter(void *context, const WmLayoutPaneView *pane) {
    const WmSdScene *scene = context;
    if (strncmp(pane->name, "BaseMask", 8) == 0 || strcmp(pane->name, "ChMask") == 0)
        return false;
    if (strncmp(pane->name, "Edge", 4) == 0 && pane->name[4] >= '0' &&
        pane->name[4] <= '4' && pane->name[5] == '\0') {
        int page = (int)scene->page + pane->name[4] - '2';
        return page >= 0 && page < WM_SD_PAGE_COUNT;
    }
    return true;
}

static bool footer_background_filter(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    return strcmp(pane->name, "RootPane") == 0 || strcmp(pane->name, "background") == 0;
}

static bool footer_front_filter(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    return strcmp(pane->name, "background") != 0;
}

static void present(WmSdScene *scene, const WmLayout *layout, WmLayoutMode mode,
                    const float matrix[12]) {
    wm_layout_present_with_fonts(scene->platform, scene->textures, scene->fonts, layout,
                                 true, mode, matrix);
}

static void translation_matrix(float x, float y, float matrix[12]) {
    static const float identity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    memcpy(matrix, identity, sizeof(identity));
    matrix[3] = x;
    matrix[7] = y;
}

static void add_clip(WmLayoutClip clips[SD_CLIP_CAPACITY], size_t *count,
                     const char *animation, const char *group, float frame, bool loop) {
    if (*count >= SD_CLIP_CAPACITY)
        return;
    clips[*count] = (WmLayoutClip){.animation = animation,
                                   .frame = frame,
                                   .group = group,
                                   .loop_override = loop ? 1 : 0};
    (*count)++;
}

void wm_sd_pose_footer(WmSdScene *scene) {
    WmLayoutClip clips[SD_CLIP_CAPACITY];
    size_t count = 0;
    add_clip(clips, &count, "mn_SdcardMenu_b_Arw_wating_roop", "G_ArwRoop",
             fmodf(scene->age, 55.0f), true);
    const struct {
        WmSdControl control;
        const char *name;
        const char *group;
    } buttons[] = {{WM_SD_CONTROL_BACK, "Wiimenu", "G_BL"},
                   {WM_SD_CONTROL_HELP, "Help", "G_BR"}};
    char names[8][64];
    size_t name_count = 0;
    for (size_t index = 0; index < 2; index++) {
        SdFocus *focus = &scene->button_focus[buttons[index].control];
        snprintf(names[name_count], sizeof(names[name_count]),
                 "mn_SdcardMenu_b_Btn_%s_%s", buttons[index].name,
                 focus->active && !focus->entering ? "rollout" : "rollover");
        add_clip(clips, &count, names[name_count++], buttons[index].group,
                 focus->active ? focus->frame : 0.0f, false);
    }
    for (size_t index = 0; index < 2; index++) {
        const char side = index == 0 ? 'L' : 'R';
        snprintf(names[name_count], sizeof(names[name_count]),
                 "mn_SdcardMenu_b_Arw%c_%s", side,
                 wm_arrow_interaction_side(&scene->arrows, (int)index)->visible
                     ? "in"
                     : "out");
        /* Group names are needed until pose returns, so retain them below. */
        static const char *const end_groups[] = {"G_ArwL_End", "G_ArwR_End"};
        add_clip(clips, &count, names[name_count++], end_groups[index],
                 wm_arrow_interaction_side(&scene->arrows, (int)index)->visibility_age,
                 false);
        const WmArrowSideState *focus =
            wm_arrow_interaction_side(&scene->arrows, (int)index);
        static const char *const focus_groups[] = {"G_ArwL_Focus", "G_ArwR_Focus"};
        snprintf(names[name_count], sizeof(names[name_count]),
                 "mn_SdcardMenu_b_Arw%c_%s", side,
                 focus->focus_active && !focus->focus_entering ? "rollout"
                                                               : "rollover");
        add_clip(clips, &count, names[name_count++], focus_groups[index],
                 focus->focus_active ? focus->focus_age : 0.0f, false);
        if (focus->pressed) {
            static const char *const press_names[] = {"mn_SdcardMenu_b_ArwL_on",
                                                      "mn_SdcardMenu_b_ArwR_on"};
            static const char *const press_groups[] = {"G_ArwL_Ac", "G_ArwR_Ac"};
            add_clip(clips, &count, press_names[index], press_groups[index],
                     focus->press_age, false);
        }
    }
    if (scene->help_press >= 0.0f) {
        add_clip(clips, &count, "mn_SdcardMenu_b_Btn_Help_on", "G_BR",
                 scene->help_press, false);
    }
    wm_layout_pose(scene->footer, clips, count);
    wm_layout_set_pose_text(scene->footer, "T_page", message(scene, 160));
}

static void draw_tiles(WmSdScene *scene, const SdTraversal *traversal) {
    const WmChannelAnimationOptions animation_options = {
        .language = "ENG",
        .measure_text = wm_font_cache_measure_text,
        .measure_context = scene->fonts};
    WmLayoutClip empty_animation = {.animation = "mn_SdcardMenu_d",
                                    .frame = fmodf(scene->age, 1999.0f),
                                    .loop_override = 1};
    wm_layout_pose(scene->empty_tile, &empty_animation, 1);
    for (size_t index = 0; index < SD_VISIBLE_TILES; index++) {
        const SdTile *tile = &traversal->tiles[index];
        if (!tile->valid)
            continue;
        SdChannel *channel = &scene->channels[tile->absolute_slot];
        WmLayout *layout = channel->icon ? channel->icon : scene->empty_tile;
        if (channel->icon) {
            wm_channel_animation_pose(channel->icon, channel->id, WM_CHANNEL_ICON,
                                      scene->age, &animation_options);
        }
        wm_platform_set_clip(scene->platform, &tile->clip);
        present(scene, layout, WM_LAYOUT_EMBEDDED, tile->matrix);
        wm_platform_set_clip(scene->platform, NULL);
    }
    for (size_t tile_index = 0; tile_index < SD_VISIBLE_TILES; tile_index++) {
        const SdTile *tile = &traversal->tiles[tile_index];
        if (!tile->valid)
            continue;
        const SdFocus *focus = &scene->tile_focus[tile->absolute_slot];
        if (!focus->active)
            continue;
        WmLayoutClip clip = {.animation = focus->entering ? "my_IplTop_d_FocusOn"
                                                          : "my_IplTop_d_FocusOff",
                             .frame = focus->frame,
                             .loop_override = 0};
        wm_layout_pose(scene->focus_layout, &clip, 1);
        wm_platform_set_clip(scene->platform, &tile->clip);
        present(scene, scene->focus_layout, WM_LAYOUT_EMBEDDED, tile->matrix);
        wm_platform_set_clip(scene->platform, NULL);
    }
}

static void draw_page_labels(WmSdScene *scene, const SdTraversal *traversal) {
    for (size_t index = 0; index < 3; index++) {
        if (!traversal->has_page_anchor[index])
            continue;
        WmLayout *label = scene->page_labels[index];
        wm_layout_pose(label, NULL, 0);
        char page[8];
        snprintf(page, sizeof(page), "%u", scene->page + (unsigned)index);
        wm_layout_set_pose_text(label, "TextBox_00", page);
        wm_layout_set_pose_text(label, "T_Page00", "/20");
        float position[12];
        translation_matrix(traversal->page_anchor[index][3],
                           traversal->page_anchor[index][7], position);
        present(scene, label, WM_LAYOUT_IPL, position);
    }
}

typedef struct SdPaneSearch {
    const char *name;
    bool found;
    float matrix[12];
} SdPaneSearch;

static bool locate_pane(void *context, const WmLayoutPaneView *pane) {
    SdPaneSearch *search = context;
    if (strcmp(pane->name, search->name) == 0) {
        memcpy(search->matrix, pane->matrix, sizeof(search->matrix));
        search->found = true;
    }
    return true;
}

static bool pane_matrix(const WmLayout *layout, const char *name, float matrix[12]) {
    SdPaneSearch search = {.name = name};
    WmLayoutDrawOptions options = {.wide = true,
                                   .mode = WM_LAYOUT_IPL,
                                   .alpha = 1.0f,
                                   .on_pane = locate_pane,
                                   .context = &search};
    wm_layout_draw(layout, &options);
    if (search.found)
        memcpy(matrix, search.matrix, sizeof(search.matrix));
    return search.found;
}

static void draw_balloon(WmSdScene *scene, WmSdControl control, float frame) {
    const char *pane_name = control == WM_SD_CONTROL_BACK ? "B_Wiimenu" : "B_Help";
    float anchor[12];
    if (!pane_matrix(scene->footer, pane_name, anchor))
        return;
    unsigned identifier = control == WM_SD_CONTROL_BACK ? 168 : 166;
    const char *title = message(scene, identifier);
    WmLayoutPaneState text_pane;
    float title_width = 0.0f;
    if (wm_layout_pane_state(scene->balloon, "T_Balloon", &text_pane)) {
        title_width = wm_font_cache_measure_text(scene->fonts, scene->balloon,
                                                 &text_pane, title, strlen(title));
    }
    float width = fmaxf(160.0f * (832.0f / 608.0f), title_width + 40.0f);
    float x = fmaxf(-416.0f + 120.0f + width * 0.5f,
                    fminf(416.0f - 120.0f - width * 0.5f, anchor[3]));
    float matrix[12];
    translation_matrix(x * (832.0f / 608.0f), anchor[7] + 50.0f, matrix);
    WmLayoutClip clip = {.animation = "my_IplTopBalloon_a_BalloonInOut",
                         .frame = wm_sd_frame_clamp(frame, 6.0f),
                         .loop_override = 0};
    wm_layout_pose(scene->balloon, &clip, 1);
    wm_layout_set_pane_size(scene->balloon, "W_Base", width, 48.0f);
    wm_layout_set_pane_size(scene->balloon, "W_Shade", width, 48.0f);
    wm_layout_set_pose_text(scene->balloon, "T_Balloon", title);
    present(scene, scene->balloon, WM_LAYOUT_IPL, matrix);
}

static unsigned help_message_id(const WmSdScene *scene) {
    static const unsigned visited_pages[] = {201, 158, 159};
    static const unsigned welcome_pages[] = {157, 158, 159, 202};
    if (scene->welcome_active)
        return welcome_pages[scene->dialog_page];
    return visited_pages[scene->dialog_page];
}

static void draw_help(WmSdScene *scene) {
    if (scene->dialog_phase == SD_DIALOG_CLOSED)
        return;
    WmLayoutClip clips[SD_CLIP_CAPACITY];
    size_t count = 0;
    add_clip(clips, &count, "my_DialogWindow_a2_DialogIn", "G_InOut",
             scene->dialog_phase == SD_DIALOG_ENTER ? scene->dialog_frame : 25.0f,
             false);
    const WmSdControl controls[2] = {WM_SD_CONTROL_HELP_BACK, WM_SD_CONTROL_HELP_NEXT};
    for (size_t index = 0; index < 2; index++) {
        SdFocus *focus = &scene->button_focus[controls[index]];
        if (!focus->active)
            continue;
        add_clip(clips, &count,
                 focus->entering ? "my_DialogWindow_a2_FocusBtn_on"
                                 : "my_DialogWindow_a2_FocusBtn_off",
                 index == 0 ? "G_FocusBtnA" : "G_FocusBtnB", focus->frame, false);
    }
    if (scene->dialog_selected != WM_SD_CONTROL_NONE) {
        add_clip(clips, &count, "my_DialogWindow_a2_SelectBtn_Ac",
                 scene->dialog_selected == WM_SD_CONTROL_HELP_BACK ? "G_SelectBtnA"
                                                                   : "G_SelectBtnB",
                 scene->dialog_phase == SD_DIALOG_SELECT ? scene->dialog_frame : 21.0f,
                 false);
    }
    if (scene->dialog_phase == SD_DIALOG_EXIT) {
        add_clip(clips, &count, "my_DialogWindow_a2_DialogOut", "G_InOut",
                 scene->dialog_frame, false);
    }
    wm_layout_pose(scene->dialog, clips, count);
    float text_alpha = scene->dialog_phase == SD_DIALOG_TEXT_OUT
                           ? fmaxf(0.0f, 255.0f - floorf(scene->dialog_frame) * 26.0f)
                       : scene->dialog_phase == SD_DIALOG_TEXT_IN
                           ? fminf(255.0f, floorf(scene->dialog_frame) * 26.0f)
                           : 255.0f;
    wm_layout_set_pose_text(scene->dialog, "T_Dialog",
                            message(scene, help_message_id(scene)));
    wm_layout_set_pane_alpha(scene->dialog, "T_Dialog", text_alpha);
    wm_layout_set_pose_text(scene->dialog, "T_BtnA", message(scene, 165));
    unsigned last = scene->welcome_active ? 3 : 2;
    wm_layout_set_pose_text(scene->dialog, "T_BtnB",
                            message(scene, scene->dialog_page == last ? 164 : 163));
    bool changing_text = scene->dialog_phase == SD_DIALOG_TEXT_OUT ||
                         scene->dialog_phase == SD_DIALOG_TEXT_IN;
    if (changing_text && (scene->dialog_previous_page == last) !=
                             (scene->dialog_destination == (int)last)) {
        wm_layout_set_pane_alpha(scene->dialog, "T_BtnB", text_alpha);
    }
    if (scene->welcome_active) {
        /* Back fades in on the first boundary. Keep it visible on later
         * pages, including text-in frame zero, so it cannot blink. */
        bool show_back = scene->dialog_page > 0 &&
                         !(scene->dialog_phase == SD_DIALOG_TEXT_IN &&
                           text_alpha == 0.0f && scene->dialog_previous_page == 0);
        wm_layout_set_pane_visible(scene->dialog, "N_BtnA", show_back);
        if (changing_text &&
            (scene->dialog_previous_page == 0) != (scene->dialog_destination == 0)) {
            wm_layout_set_descendant_alpha(scene->dialog, "N_BtnA_Pic", text_alpha);
        }
    }
    present(scene, scene->dialog, WM_LAYOUT_IPL, NULL);
    if (scene->dialog_page != 2 && !(scene->welcome_active && scene->dialog_page == 3))
        return;
    float dialog_matrix[12];
    if (!pane_matrix(scene->dialog, "N_Dialog", dialog_matrix))
        return;
    bool final_welcome = scene->welcome_active && scene->dialog_page == 3;
    float matrix[12];
    translation_matrix(dialog_matrix[3],
                       dialog_matrix[7] + (final_welcome ? 108.0f : 74.0f), matrix);
    WmLayout *icon = final_welcome ? scene->help_button : scene->wait_icon;
    if (final_welcome)
        wm_layout_pose(icon, NULL, 0);
    else {
        WmLayoutClip clip = {.animation = "wait_icon_wait_loop",
                             .frame = fmodf(scene->icon_age, 40.0f),
                             .loop_override = 1};
        wm_layout_pose(icon, &clip, 1);
    }
    if (changing_text) {
        wm_layout_set_descendant_alpha(icon, "RootPane", text_alpha);
    }
    present(scene, icon, WM_LAYOUT_IPL, matrix);
}

static void draw_loading(WmSdScene *scene) {
    if (scene->loader_phase == SD_LOADER_CLOSED)
        return;
    WmLayoutClip clips[3];
    size_t count = 0;
    add_clip(clips, &count, "mn_Nocard_IN_02", "Group_01",
             scene->loader_phase == SD_LOADER_ENTER ? scene->loader_frame : 33.0f,
             false);
    if (scene->loader_phase == SD_LOADER_WAIT) {
        add_clip(clips, &count, "mn_Nocard_Wait", "G_Wait",
                 fmaxf(0.0f, scene->loader_frame - 1.0f), true);
    }
    if (scene->loader_phase == SD_LOADER_EXIT) {
        add_clip(clips, &count, "mn_Nocard_OUT_02", "Group_01", scene->loader_frame,
                 false);
    }
    wm_layout_pose(scene->loading_panel, clips, count);
    wm_layout_set_pose_text(scene->loading_panel, "T_TimerMes_01", message(scene, 170));
    present(scene, scene->loading_panel, WM_LAYOUT_IPL, NULL);
}

static void draw_media_error(WmSdScene *scene) {
    if (scene->media_status == WM_SD_MEDIA_READY ||
        scene->loader_phase != SD_LOADER_CLOSED ||
        scene->dialog_phase != SD_DIALOG_CLOSED)
        return;
    unsigned identifier = scene->media_status == WM_SD_MEDIA_ABSENT       ? 169
                          : scene->media_status == WM_SD_MEDIA_READ_ERROR ? 195
                                                                          : 171;
    WmLayoutClip clip = {.animation = "mn_Nocard_IN",
                         .frame = 33.0f,
                         .group = "Group_00",
                         .loop_override = 0};
    wm_layout_pose(scene->error_panel, &clip, 1);
    wm_layout_set_pose_text(scene->error_panel, "T_TimerMes",
                            message(scene, identifier));
    wm_layout_set_pane_visible(scene->error_panel, "T_TimerMes_01", false);
    wm_layout_set_pane_visible(scene->error_panel, "Wait", false);
    present(scene, scene->error_panel, WM_LAYOUT_IPL, NULL);
}

void wm_sd_scene_draw(WmSdScene *scene) {
    if (!scene || scene->phase == WM_SD_CLOSED)
        return;
    wm_sd_pose_footer(scene);
    wm_layout_present_filtered_with_fonts(
        scene->platform, scene->textures, scene->fonts, scene->footer, true,
        WM_LAYOUT_IPL, NULL, footer_background_filter, NULL);
    WmLayoutClip grid_clip = {
        .animation = "mn_SdcardMenu_a",
        .frame = scene->phase == WM_SD_SCROLL
                     ? wm_sd_scroll_animation_frame(scene->scroll_direction,
                                                    scene->scroll_frame)
                     : 0.0f,
        .loop_override = 0};
    wm_layout_pose(scene->grid, &grid_clip, 1);
    SdTraversal traversal = {.page = scene->page};
    /* The incoming page label follows N_Clock2, whose visible bit is clear
     * in the authored grid. Visit its transform to position the label. */
    wm_layout_visit_all_transforms(scene->grid, true, WM_LAYOUT_IPL, NULL,
                                   collect_page_anchor, &traversal);
    WmLayoutDrawOptions options = {.wide = true,
                                   .mode = WM_LAYOUT_IPL,
                                   .alpha = 1.0f,
                                   .on_pane = collect_grid_pane,
                                   .context = &traversal};
    wm_layout_draw(scene->grid, &options);
    wm_layout_present_filtered_with_fonts(scene->platform, scene->textures,
                                          scene->fonts, scene->grid, true,
                                          WM_LAYOUT_IPL, NULL, grid_base_filter, NULL);
    draw_tiles(scene, &traversal);
    wm_layout_present_filtered_with_fonts(scene->platform, scene->textures,
                                          scene->fonts, scene->grid, true,
                                          WM_LAYOUT_IPL, NULL, grid_trim_filter, scene);
    draw_page_labels(scene, &traversal);
    wm_layout_present_filtered_with_fonts(
        scene->platform, scene->textures, scene->fonts, scene->footer, true,
        WM_LAYOUT_IPL, NULL, footer_front_filter, NULL);
    for (size_t index = 0; index < 2; index++) {
        SdBalloon *balloon = &scene->balloons[index];
        if (balloon->phase == SD_BALLOON_ENTER || balloon->phase == SD_BALLOON_LEAVE) {
            draw_balloon(scene, index == 0 ? WM_SD_CONTROL_BACK : WM_SD_CONTROL_HELP,
                         balloon->frame);
        }
    }
    draw_help(scene);
    draw_loading(scene);
    draw_media_error(scene);
}
