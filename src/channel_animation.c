#include "wii_menu/channel_animation.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct ClipTiming {
    bool has_minimum;
    bool has_maximum;
    bool has_initial;
    bool has_speed;
    bool has_loop;
    float minimum;
    float maximum;
    float initial;
    float speed;
    bool loop;
} ClipTiming;

static int hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool short_id_from_title(const char *title_id, char short_id[5]) {
    if (!title_id || strlen(title_id) != 16) return false;
    for (size_t index = 0; index < 16; index++) {
        if (hex_digit(title_id[index]) < 0) return false;
    }
    for (size_t index = 0; index < 4; index++) {
        int high = hex_digit(title_id[8 + index * 2]);
        int low = hex_digit(title_id[9 + index * 2]);
        short_id[index] = (char)((high << 4) | low);
    }
    short_id[4] = '\0';
    return true;
}

static const char *kind_name(WmChannelAnimationKind kind) {
    return kind == WM_CHANNEL_ICON ? "icon" : "banner";
}

static const char *language_code(const WmChannelAnimationOptions *options) {
    const char *language = options && options->language ? options->language : "ENG";
    if (strcmp(language, "JPN") == 0) return "J";
    if (strcmp(language, "GER") == 0) return "G";
    if (strcmp(language, "FRA") == 0) return "F";
    if (strcmp(language, "SPA") == 0) return "Sp";
    if (strcmp(language, "ITA") == 0) return "I";
    if (strcmp(language, "NED") == 0) return "N";
    return "E";
}

float wm_channel_animation_frame(float elapsed, float minimum, float maximum,
                                  float initial, float speed, bool loop) {
    if (!isfinite(elapsed) || !isfinite(minimum) || !isfinite(maximum) ||
        !isfinite(initial) || !isfinite(speed)) return NAN;
    double frame = (double)initial + (double)fmaxf(0, elapsed) * speed;
    if (loop && maximum > minimum && frame >= maximum) {
        frame = minimum + fmod(frame - maximum, (double)maximum - minimum);
    }
    if (!loop) frame = fmin(frame, maximum);
    return isfinite(frame) ? (float)frame : NAN;
}

static bool append_clip(const WmLayout *layout, WmChannelAnimationPlan *plan,
                        const char *name, float elapsed, const ClipTiming *timing,
                        int rso_index) {
    WmLayoutAnimationInfo animation;
    if (!wm_layout_animation_info(layout, name, &animation)) return true;
    if (plan->count == WM_CHANNEL_ANIMATION_MAX_CLIPS) return false;

    float minimum = timing && timing->has_minimum ? timing->minimum : 0;
    float maximum = timing && timing->has_maximum ? timing->maximum
                     : fmaxf(0, animation.frames - (animation.loop ? 0 : 1));
    float initial = timing && timing->has_initial ? timing->initial : minimum;
    float speed = timing && timing->has_speed ? timing->speed : 1;
    bool loop = timing && timing->has_loop ? timing->loop : animation.loop;
    float frame = wm_channel_animation_frame(elapsed, minimum, maximum,
                                              initial, speed, loop);
    if (!isfinite(frame)) return false;

    WmChannelAnimationClip *clip = &plan->clips[plan->count++];
    snprintf(clip->animation, sizeof(clip->animation), "%s", animation.name);
    clip->frame = frame;
    if (rso_index >= 0) {
        char group[16];
        snprintf(group, sizeof(group), "Rso%d", rso_index);
        if (wm_layout_has_group(layout, group)) {
            snprintf(clip->group, sizeof(clip->group), "%s", group);
        }
    }
    return true;
}

static bool append_rso(const WmLayout *layout, WmChannelAnimationPlan *plan,
                       WmChannelAnimationKind kind, unsigned index,
                       float elapsed, const ClipTiming *timing) {
    char name[32];
    snprintf(name, sizeof(name), "%s_Rso%u", kind_name(kind), index);
    return append_clip(layout, plan, name, elapsed, timing, (int)index);
}

static bool append_base(const WmLayout *layout, WmChannelAnimationPlan *plan,
                        WmChannelAnimationKind kind, float elapsed,
                        bool custom_banner) {
    if (kind == WM_CHANNEL_ICON) {
        static const char *const names[] = {"icon_Start", "icon", "icon_Whole"};
        WmLayoutAnimationInfo animation;
        for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
            if (wm_layout_animation_info(layout, names[index], &animation)) {
                return append_clip(layout, plan, names[index], elapsed, NULL, -1);
            }
        }
        return true;
    }

    WmLayoutAnimationInfo start, loop;
    bool has_start = wm_layout_animation_info(layout, "banner_Start", &start);
    bool has_loop = wm_layout_animation_info(layout, "banner_Loop", &loop);
    if (has_start) {
        float end = fmaxf(0, start.frames - (start.loop ? 0 : 1));
        ClipTiming one_shot = {.has_loop = true, .loop = false};
        if (!append_clip(layout, plan, "banner_Start", fminf(elapsed, end),
                         &one_shot, -1)) return false;
        if (has_loop && (custom_banner || elapsed >= end)) {
            ClipTiming repeating = {.has_loop = true, .loop = true};
            float loop_elapsed = custom_banner ? elapsed : elapsed - end;
            if (!append_clip(layout, plan, "banner_Loop", loop_elapsed,
                             &repeating, -1)) return false;
        }
        return true;
    }
    ClipTiming repeating = {.has_loop = true, .loop = true};
    if (has_loop) return append_clip(layout, plan, "banner_Loop", elapsed,
                                     &repeating, -1);
    return append_clip(layout, plan, "banner", elapsed, &repeating, -1);
}

static bool seat_holder(const char *short_id) {
    static const char *const ids[] = {"HADE", "HAJE", "HAPE", "HATE", "HCLE"};
    for (size_t index = 0; index < sizeof(ids) / sizeof(ids[0]); index++) {
        if (strcmp(short_id, ids[index]) == 0) return true;
    }
    return false;
}

static bool measured_telop_duration(const WmLayout *layout,
                                    const WmChannelAnimationOptions *options,
                                    float *duration) {
    if (!options || !options->measure_text) return false;
    char name[32];
    snprintf(name, sizeof(name), "T_telop%s_00", language_code(options));
    WmLayoutPaneState pane;
    if (!wm_layout_pane_state(layout, name, &pane) || !pane.text) return false;
    float width = options->measure_text(options->measure_context, layout, &pane,
                                        pane.text, strlen(pane.text));
    if (!isfinite(width)) return false;
    float value = ceilf((width + 608) / (6262.0f / 5660.0f));
    *duration = fmaxf(0, value);
    return true;
}

bool wm_channel_animation_plan(const WmLayout *layout, const char *title_id,
                                WmChannelAnimationKind kind, float elapsed_frames,
                                const WmChannelAnimationOptions *options,
                                WmChannelAnimationPlan *plan) {
    char id[5];
    if (!layout || !plan || !short_id_from_title(title_id, id) ||
        (kind != WM_CHANNEL_ICON && kind != WM_CHANNEL_BANNER) ||
        !isfinite(elapsed_frames) ||
        (options && options->has_base_frame && !isfinite(options->base_frame))) {
        return false;
    }
    memset(plan, 0, sizeof(*plan));
    float frame = fmaxf(0, elapsed_frames);
    float base_frame = options && options->has_base_frame
                           ? fmaxf(0, options->base_frame) : frame;
    if (!append_base(layout, plan, kind, base_frame,
                     options && options->custom_banner)) return false;

    if (strcmp(id, "HAYA") == 0) {
        ClipTiming photo = {.has_minimum = true, .minimum = 40,
                            .has_initial = true, .initial = 0};
        return append_rso(layout, plan, kind, 0, frame,
                          kind == WM_CHANNEL_BANNER ? &photo : NULL);
    }
    if (strcmp(id, "HAFE") == 0) {
        if (!append_rso(layout, plan, kind, 0, frame, NULL)) return false;
        return kind == WM_CHANNEL_BANNER ||
               append_rso(layout, plan, kind, 1, 0, NULL);
    }
    if (strcmp(id, "HAGE") == 0) {
        if (kind == WM_CHANNEL_ICON) {
            return append_rso(layout, plan, kind, 1, 0, NULL) &&
                   append_rso(layout, plan, kind, 2, 0, NULL);
        }
        WmLayoutAnimationInfo animation;
        if (!wm_layout_animation_info(layout, "banner_Rso0", &animation)) return true;
        ClipTiming news = {.has_minimum = true, .has_initial = true,
                           .has_loop = true, .initial = 0, .loop = true,
                           .minimum = fmaxf(0, animation.frames -
                                             (animation.loop ? 0 : 1)) - 1};
        return append_rso(layout, plan, kind, 0, frame, &news);
    }
    if (strcmp(id, "HABA") == 0 && kind == WM_CHANNEL_ICON) {
        ClipTiming shop = {.has_maximum = true, .maximum = 630};
        for (unsigned index = 0; index < 16; index++) {
            if (!append_rso(layout, plan, kind, index,
                            index == 0 ? frame : 0,
                            index == 0 ? &shop : NULL)) return false;
        }
        return true;
    }
    if (seat_holder(id)) {
        if (kind == WM_CHANNEL_ICON) {
            const float length = 470;
            const float speed = 1024.0f / 290.0f;
            ClipTiming first = {.has_minimum = true, .minimum = 1,
                                .has_maximum = true, .maximum = length + 1,
                                .has_initial = true, .initial = 180,
                                .has_loop = true, .loop = true};
            ClipTiming second = {.has_maximum = true, .maximum = length,
                                 .has_initial = true, .initial = length + 60,
                                 .has_loop = true, .loop = true};
            ClipTiming third = {.has_maximum = true, .maximum = length,
                                .has_initial = true, .initial = length - 230,
                                .has_loop = true, .loop = true};
            ClipTiming fourth = {.has_maximum = true, .maximum = length * speed,
                                 .has_initial = true, .initial = length * speed,
                                 .has_speed = true, .speed = speed,
                                 .has_loop = true, .loop = true};
            return append_rso(layout, plan, kind, 0, frame, &first) &&
                   append_rso(layout, plan, kind, 1, frame, &second) &&
                   append_rso(layout, plan, kind, 2, frame, &third) &&
                   append_rso(layout, plan, kind, 3, frame, &fourth);
        }
        ClipTiming first = {.has_maximum = true, .maximum = 40,
                            .has_loop = true, .loop = false};
        ClipTiming third = {.has_minimum = true, .minimum = 40,
                            .has_maximum = true, .maximum = 190,
                            .has_initial = true, .initial = 0,
                            .has_loop = true, .loop = true};
        if (!append_rso(layout, plan, kind, 0, frame, &first) ||
            !append_rso(layout, plan, kind, 2, frame, &third)) return false;
        float duration;
        if (measured_telop_duration(layout, options, &duration)) {
            ClipTiming telop = {.has_maximum = true, .maximum = duration + 40,
                                .has_initial = true, .initial = duration,
                                .has_loop = true, .loop = true};
            return append_rso(layout, plan, kind, 1, frame, &telop);
        }
        return true;
    }
    if (strcmp(id, "HCGE") == 0) {
        if (kind == WM_CHANNEL_ICON) {
            float minimum = options && options->network_configured ? 1 : 1000;
            ClipTiming video = {.has_minimum = true, .minimum = minimum,
                                .has_initial = true, .initial = minimum,
                                .has_loop = true, .loop = true};
            return append_rso(layout, plan, kind, 0, frame, &video);
        }
        ClipTiming video = {.has_loop = true, .loop = false};
        return append_rso(layout, plan, kind, 0, frame, &video);
    }
    return true;
}

static void visible(WmLayout *layout, const char *name, bool enabled) {
    (void)wm_layout_set_pane_visible(layout, name, enabled);
}

static void visible_localized(WmLayout *layout, const char *prefix,
                              const char *selected) {
    static const char *const languages[] = {"J", "E", "G", "F", "Sp", "I", "N"};
    for (size_t index = 0; index < sizeof(languages) / sizeof(languages[0]); index++) {
        for (unsigned suffix = 0; suffix < 2; suffix++) {
            char name[64];
            snprintf(name, sizeof(name), "%s%s_%02u", prefix, languages[index], suffix);
            visible(layout, name, strcmp(languages[index], selected) == 0);
        }
    }
}

static bool replace_pose_text_if_present(WmLayout *layout, const char *name,
                                         const char *value) {
    WmLayoutPaneState pane;
    if (!wm_layout_pane_state(layout, name, &pane) ||
        strcmp(pane.type, "txt1") != 0) return true;
    return wm_layout_set_pose_text(layout, name, value);
}

static void resize_seat_message(WmLayout *layout, const char *code,
                                const WmChannelAnimationOptions *options) {
    char name[64];
    snprintf(name, sizeof(name), "T_message%s_00", code);
    WmLayoutPaneState pane;
    WmLayoutPaneState window;
    if (!wm_layout_pane_state(layout, name, &pane) || !pane.text ||
        !wm_layout_pane_state(layout, "W_messWindow_00", &window)) return;

    if (options && options->measure_text) {
        float width = 0;
        const char *line = pane.text;
        const char *end = pane.text + strlen(pane.text);
        for (const char *cursor = line; cursor <= end; cursor++) {
            if (cursor != end && *cursor != '\n') continue;
            float measured = options->measure_text(options->measure_context,
                                                    layout, &pane, line,
                                                    (size_t)(cursor - line));
            if (isfinite(measured) && measured > width) width = measured;
            line = cursor + 1;
        }
        if (width > pane.size[0] - 3 && width > 0) {
            float ratio = pane.size[0] / width * 0.99f;
            (void)wm_layout_set_text_style(layout, name,
                                            pane.font_size[0] * ratio,
                                            pane.font_size[1],
                                            pane.char_space * ratio);
        }
    }
    size_t lines = 1;
    for (const char *cursor = pane.text; *cursor; cursor++) {
        if (*cursor == '\n') lines++;
    }
    float height = fmaxf(100, 140 - (6 - (float)lines) * 20);
    (void)wm_layout_set_pane_size(layout, "W_messWindow_00",
                                   window.size[0], height);
}

static bool apply_choices(WmLayout *layout, const char *id,
                          WmChannelAnimationKind kind,
                          const WmChannelAnimationOptions *options) {
    const char *code = language_code(options);
    if (strcmp(id, "HABA") == 0 && kind == WM_CHANNEL_ICON) {
        visible(layout, "N_SuperParent", true);
        static const char letters[] = "JEGFSIN";
        const char *selected = strcmp(code, "Sp") == 0 ? "S" : code;
        for (size_t index = 0; index < sizeof(letters) - 1; index++) {
            for (unsigned suffix = 0; suffix < 2; suffix++) {
                char name[32];
                snprintf(name, sizeof(name), "P_title_%c_%02u", letters[index], suffix);
                visible(layout, name, letters[index] == selected[0]);
            }
        }
    }
    if (strcmp(id, "HABA") == 0 && kind == WM_CHANNEL_BANNER) {
        static const char letters[] = "jegfsin";
        const char *selected = strcmp(code, "Sp") == 0 ? "S" : code;
        char chosen = selected[0] >= 'A' && selected[0] <= 'Z'
                          ? (char)(selected[0] + ('a' - 'A')) : selected[0];
        for (size_t index = 0; index < sizeof(letters) - 1; index++) {
            char name[16];
            snprintf(name, sizeof(name), "font_%c", letters[index]);
            visible(layout, name, letters[index] == chosen);
        }
    }
    if (strcmp(id, "HAFE") == 0) {
        if (kind == WM_CHANNEL_ICON) {
            visible(layout, "code", false);
        } else {
            visible(layout, "all", true);
            visible(layout, "weather", false);
            visible(layout, "textB0", true);
            for (unsigned index = 0; index < 3; index++) {
                char name[16];
                snprintf(name, sizeof(name), "textT%u", index);
                visible(layout, name, index == 0);
            }
        }
    }
    if (strcmp(id, "HAGE") == 0) {
        if (kind == WM_CHANNEL_ICON) {
            if (!replace_pose_text_if_present(layout, "send_id", "")) return false;
        } else {
            for (unsigned index = 0; index < 4; index++) {
                char name[16];
                snprintf(name, sizeof(name), "textT%u", index);
                visible(layout, name, index == 0);
            }
        }
    }
    if (seat_holder(id)) {
        visible(layout, "N_base_00", false);
        visible_localized(layout, "P_logo", code);
        if (kind == WM_CHANNEL_ICON) {
            visible(layout, "P_BG_00", true);
            visible(layout, "P_BG_01", true);
        } else {
            visible_localized(layout, "T_title", code);
            visible_localized(layout, "T_telop", code);
            static const char *const languages[] = {"J", "E", "G", "F", "Sp", "I", "N"};
            for (size_t index = 0; index < sizeof(languages) / sizeof(languages[0]); index++) {
                char name[64];
                snprintf(name, sizeof(name), "N_message%s_00", languages[index]);
                visible(layout, name, strcmp(languages[index], code) == 0);
                for (unsigned suffix = 0; suffix < 2; suffix++) {
                    snprintf(name, sizeof(name), "T_message%s_%02u",
                             languages[index], suffix);
                    visible(layout, name,
                            strcmp(languages[index], code) == 0 && suffix == 0);
                }
            }
            static const char *const always[] = {
                "N_title_00", "N_logoU_00", "N_logoU_01", "N_logoD_00", "N_logoD_01"
            };
            for (size_t index = 0; index < sizeof(always) / sizeof(always[0]); index++) {
                visible(layout, always[index], true);
            }
            resize_seat_message(layout, code, options);
        }
    }
    if (strcmp(id, "HCGE") == 0 && kind == WM_CHANNEL_ICON) {
        bool configured = options && options->network_configured;
        visible(layout, "fade", configured);
        visible(layout, "txt_3", configured);
        static const char *const shown[] = {
            "bg170_96", "color", "wii", "txt_green", "txt_purple",
            "txt_yellow", "txt_orange"
        };
        for (size_t index = 0; index < sizeof(shown) / sizeof(shown[0]); index++) {
            visible(layout, shown[index], true);
        }
        if (strcmp(code, "E") == 0) {
            static const struct {
                const char *name;
                const char *text;
                float width;
            } labels[] = {
                {"txt_green", "Get more\nchannels", 20},
                {"txt_purple", "Get new\nsoftware", 20},
                {"txt_yellow", "Get 100s\nof classic\ngames", 20},
                {"txt_orange", "Play with\nfriends near\nand far", 18},
                {"txt_3", "You can delete\nthis channel after\nwatching the video.", 12}
            };
            for (size_t index = 0; index < sizeof(labels) / sizeof(labels[0]); index++) {
                WmLayoutPaneState pane;
                if (!wm_layout_pane_state(layout, labels[index].name, &pane) ||
                    strcmp(pane.type, "txt1") != 0) continue;
                if (!wm_layout_set_pose_text(layout, labels[index].name,
                                             labels[index].text)) return false;
                if (!wm_layout_set_text_style(layout, labels[index].name,
                                               labels[index].width, 20,
                                               pane.char_space)) return false;
            }
        }
    }
    return true;
}

bool wm_channel_animation_pose(WmLayout *layout, const char *title_id,
                                WmChannelAnimationKind kind, float elapsed_frames,
                                const WmChannelAnimationOptions *options) {
    WmChannelAnimationPlan plan;
    if (!wm_channel_animation_plan(layout, title_id, kind, elapsed_frames,
                                    options, &plan)) return false;
    WmLayoutClip clips[WM_CHANNEL_ANIMATION_MAX_CLIPS];
    for (size_t index = 0; index < plan.count; index++) {
        clips[index] = (WmLayoutClip) {
            .animation = plan.clips[index].animation,
            .frame = plan.clips[index].frame,
            .group = plan.clips[index].group[0] ? plan.clips[index].group : NULL,
            .recursive_group = false,
            .loop_override = 0
        };
    }
    if (!wm_layout_pose(layout, clips, plan.count)) return false;
    char id[5];
    (void)short_id_from_title(title_id, id); /* Validated by the planner. */
    if (!apply_choices(layout, id, kind, options)) return false;
    return wm_layout_mask_language_groups(layout,
                                          options ? options->language : NULL);
}
