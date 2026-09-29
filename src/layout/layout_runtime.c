#include "layout_runtime_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void wm_layout_destroy(WmLayout *layout) {
    if (!layout)
        return;
    for (size_t index = 0; index < layout->group_count; index++) {
        free(layout->groups[index].members);
    }
    for (size_t index = 0; index < layout->animation_count; index++) {
        free(layout->animations[index].textures);
    }
    free(layout->textures);
    free(layout->fonts);
    free(layout->base_materials);
    free(layout->materials);
    for (size_t index = 0; index < layout->next_pane; index++) {
        free(layout->base_panes[index].text);
    }
    if (layout->pose_text_overrides) {
        for (size_t index = 0; index < layout->pane_count; index++) {
            free(layout->pose_text_overrides[index]);
        }
    }
    free(layout->base_panes);
    free(layout->panes);
    free(layout->pose_text_overrides);
    free(layout->pose_text_capacities);
    free(layout->groups);
    free(layout->animations);
    free(layout->tracks);
    free(layout->keys);
    free(layout->allowed_panes);
    free(layout->allowed_materials);
    free(layout);
}

size_t wm_layout_pane_count(const WmLayout *layout) {
    return layout ? layout->pane_count : 0;
}

size_t wm_layout_material_count(const WmLayout *layout) {
    return layout ? layout->material_count : 0;
}

size_t wm_layout_texture_count(const WmLayout *layout) {
    return layout ? layout->texture_count : 0;
}

const WmLayoutTexture *wm_layout_texture_at(const WmLayout *layout, size_t index) {
    return layout && index < layout->texture_count ? &layout->textures[index] : NULL;
}

bool wm_layout_material_info(const WmLayout *layout, size_t index,
                             WmLayoutMaterialInfo *info, uint8_t wraps[4][2]) {
    if (!layout || !info || !wraps || index >= layout->material_count) {
        return false;
    }
    const LayoutMaterial *material = &layout->materials[index];
    wm_layout_material_info_from_data(material, info);
    memset(wraps, 0, 4 * 2 * sizeof(wraps[0][0]));
    for (size_t unit = 0; unit < material->map_count && unit < 4; unit++) {
        wraps[unit][0] = material->maps[unit].wrap_s;
        wraps[unit][1] = material->maps[unit].wrap_t;
    }
    return true;
}

size_t wm_layout_font_count(const WmLayout *layout) {
    return layout ? layout->font_count : 0;
}

const char *wm_layout_font_name(const WmLayout *layout, size_t index) {
    return layout && index < layout->font_count ? layout->fonts[index] : NULL;
}

bool wm_layout_set_text(WmLayout *layout, const char *pane_name, const char *utf8) {
    if (!layout || !pane_name || !utf8)
        return false;
    int index = wm_layout_find_pane(layout, pane_name);
    if (index < 0 || strcmp(layout->base_panes[index].type, "txt1") != 0) {
        return false;
    }
    size_t length = 0;
    while (length <= WM_LAYOUT_MAX_TEXT_BYTES && utf8[length])
        length++;
    if (length > WM_LAYOUT_MAX_TEXT_BYTES)
        return false;
    LayoutPane *pane = &layout->base_panes[index];
    if (pane->text && strcmp(pane->text, utf8) == 0)
        return true;
    bool pose_override_active =
        layout->pose_text_overrides[index] &&
        layout->panes[index].text == layout->pose_text_overrides[index];
    char *copy = malloc(length + 1);
    if (!copy)
        return false;
    memcpy(copy, utf8, length + 1);
    free(pane->text);
    pane->text = copy;
    if (!pose_override_active)
        layout->panes[index].text = copy;
    return true;
}

bool wm_layout_set_pose_text(WmLayout *layout, const char *pane_name,
                             const char *utf8) {
    if (!layout || !pane_name || !utf8)
        return false;
    int index = wm_layout_find_pane(layout, pane_name);
    if (index < 0 || strcmp(layout->panes[index].type, "txt1") != 0)
        return false;
    size_t length = 0;
    while (length <= WM_LAYOUT_MAX_TEXT_BYTES && utf8[length])
        length++;
    if (length > WM_LAYOUT_MAX_TEXT_BYTES)
        return false;
    if (utf8 == layout->pose_text_overrides[index]) {
        layout->panes[index].text = layout->pose_text_overrides[index];
        return true;
    }
    if (length + 1 > layout->pose_text_capacities[index]) {
        char *copy = realloc(layout->pose_text_overrides[index], length + 1);
        if (!copy)
            return false;
        layout->pose_text_overrides[index] = copy;
        layout->pose_text_capacities[index] = length + 1;
    }
    memmove(layout->pose_text_overrides[index], utf8, length + 1);
    layout->panes[index].text = layout->pose_text_overrides[index];
    return true;
}

bool wm_layout_set_pose_text_colors(WmLayout *layout, const char *pane_name,
                                    const WmLayoutTextColorRange *ranges,
                                    size_t count) {
    if (!layout || !pane_name || count > WM_LAYOUT_TEXT_COLOR_RANGES ||
        (count && !ranges))
        return false;
    int index = wm_layout_find_pane(layout, pane_name);
    if (index < 0 || strcmp(layout->panes[index].type, "txt1") != 0)
        return false;
    LayoutPane *pane = &layout->panes[index];
    size_t length = strlen(pane->text ? pane->text : "");
    for (size_t range = 0; range < count; range++) {
        if (ranges[range].first_byte > ranges[range].end_byte ||
            ranges[range].end_byte > length)
            return false;
    }
    if (count)
        memcpy(pane->text_color_ranges, ranges, count * sizeof(*ranges));
    pane->text_color_range_count = count;
    return true;
}

bool wm_layout_copy_texture_map(WmLayout *layout, const char *donor_pane,
                                const char *target_pane, unsigned unit) {
    if (!layout || !donor_pane || !target_pane || unit >= 4)
        return false;
    int donor_index = wm_layout_find_pane(layout, donor_pane);
    int target_index = wm_layout_find_pane(layout, target_pane);
    if (donor_index < 0 || target_index < 0)
        return false;
    int donor_material = layout->base_panes[donor_index].material;
    int target_material = layout->base_panes[target_index].material;
    if (donor_material < 0 || target_material < 0 ||
        (size_t)donor_material >= layout->material_count ||
        (size_t)target_material >= layout->material_count ||
        layout->base_materials[donor_material].map_count <= unit ||
        layout->base_materials[target_material].map_count <= unit)
        return false;
    LayoutTextureMap source = layout->base_materials[donor_material].maps[unit];
    layout->base_materials[target_material].maps[unit] = source;
    layout->materials[target_material].maps[unit] = source;
    return true;
}

static const LayoutAnimation *find_animation(const WmLayout *layout, const char *name) {
    for (size_t index = 0; index < layout->animation_count; index++) {
        if (strcmp(layout->animations[index].name, name) == 0) {
            return &layout->animations[index];
        }
    }
    return NULL;
}

static const LayoutGroup *find_group(const WmLayout *layout, const char *name) {
    for (size_t index = 0; index < layout->group_count; index++) {
        if (strcmp(layout->groups[index].name, name) == 0)
            return &layout->groups[index];
    }
    return NULL;
}

static unsigned ascii_lower(unsigned character) {
    return character >= 'A' && character <= 'Z' ? character + ('a' - 'A') : character;
}

static bool ascii_equal_ignore_case(const char *left, const char *right) {
    while (*left && *right) {
        if (ascii_lower((unsigned char)*left) != ascii_lower((unsigned char)*right))
            return false;
        left++;
        right++;
    }
    return *left == *right;
}

bool wm_layout_animation_info(const WmLayout *layout, const char *name,
                              WmLayoutAnimationInfo *info) {
    if (!layout || !name || !info)
        return false;
    for (size_t index = 0; index < layout->animation_count; index++) {
        const LayoutAnimation *animation = &layout->animations[index];
        if (!ascii_equal_ignore_case(animation->name, name))
            continue;
        *info = (WmLayoutAnimationInfo){.name = animation->name,
                                        .frames = animation->frames,
                                        .loop = animation->loop};
        return true;
    }
    return false;
}

bool wm_layout_has_group(const WmLayout *layout, const char *name) {
    return layout && name && find_group(layout, name) != NULL;
}

bool wm_layout_pane_state(const WmLayout *layout, const char *name,
                          WmLayoutPaneState *state) {
    if (!layout || !name || !state)
        return false;
    int index = wm_layout_find_pane(layout, name);
    if (index < 0)
        return false;
    const LayoutPane *pane = &layout->panes[index];
    *state = (WmLayoutPaneState){
        .name = pane->name,
        .type = pane->type,
        .text = pane->text,
        .font_name =
            pane->font_index >= 0 && (size_t)pane->font_index < layout->font_count
                ? layout->fonts[pane->font_index]
                : NULL,
        .flags = pane->flags,
        .translation = {pane->translation[0], pane->translation[1],
                        pane->translation[2]},
        .scale = {pane->scale[0], pane->scale[1]},
        .size = {pane->size[0], pane->size[1]},
        .font_size = {pane->font_size[0], pane->font_size[1]},
        .char_space = pane->char_space};
    return true;
}

bool wm_layout_set_pane_visible(WmLayout *layout, const char *name, bool visible) {
    if (!layout || !name)
        return false;
    int index = wm_layout_find_pane(layout, name);
    if (index < 0)
        return false;
    if (visible)
        layout->panes[index].flags |= 1u;
    else
        layout->panes[index].flags &= ~1u;
    return true;
}

bool wm_layout_set_pane_alpha(WmLayout *layout, const char *name, float alpha) {
    if (!layout || !name || !isfinite(alpha) || alpha < 0.0f || alpha > 255.0f)
        return false;
    int index = wm_layout_find_pane(layout, name);
    if (index < 0)
        return false;
    layout->panes[index].alpha = alpha;
    return true;
}

static void set_descendant_alpha(WmLayout *layout, int parent, float alpha) {
    for (int child = layout->panes[parent].first_child; child >= 0;
         child = layout->panes[child].next_sibling) {
        layout->panes[child].alpha = alpha;
        set_descendant_alpha(layout, child, alpha);
    }
}

bool wm_layout_set_descendant_alpha(WmLayout *layout, const char *name, float alpha) {
    if (!layout || !name || !isfinite(alpha) || alpha < 0.0f || alpha > 255.0f)
        return false;
    int index = wm_layout_find_pane(layout, name);
    if (index < 0)
        return false;
    set_descendant_alpha(layout, index, alpha);
    return true;
}

bool wm_layout_set_pane_size(WmLayout *layout, const char *name, float width,
                             float height) {
    if (!layout || !name || !isfinite(width) || !isfinite(height) || width < 0 ||
        height < 0)
        return false;
    int index = wm_layout_find_pane(layout, name);
    if (index < 0)
        return false;
    layout->panes[index].size[0] = width;
    layout->panes[index].size[1] = height;
    return true;
}

bool wm_layout_set_pane_translation(WmLayout *layout, const char *name, float x,
                                    float y, float z) {
    if (!layout || !name || !isfinite(x) || !isfinite(y) || !isfinite(z)) {
        return false;
    }
    int index = wm_layout_find_pane(layout, name);
    if (index < 0)
        return false;
    LayoutPane *pane = &layout->panes[index];
    pane->translation[0] = x;
    pane->translation[1] = y;
    pane->translation[2] = z;
    return true;
}

static bool raise_pane_branch(WmLayout *layout, int parent, int target) {
    if (parent == target)
        return true;
    int previous = -1;
    for (int child = layout->panes[parent].first_child; child >= 0;
         child = layout->panes[child].next_sibling) {
        if (raise_pane_branch(layout, child, target)) {
            int next = layout->panes[child].next_sibling;
            if (next >= 0) {
                if (previous < 0)
                    layout->panes[parent].first_child = next;
                else
                    layout->panes[previous].next_sibling = next;
                int last = next;
                while (layout->panes[last].next_sibling >= 0)
                    last = layout->panes[last].next_sibling;
                layout->panes[last].next_sibling = child;
                layout->panes[child].next_sibling = -1;
            }
            return true;
        }
        previous = child;
    }
    return false;
}

bool wm_layout_raise_pane(WmLayout *layout, const char *name) {
    if (!layout || !name || layout->pane_count == 0)
        return false;
    int target = wm_layout_find_pane(layout, name);
    return target >= 0 && raise_pane_branch(layout, 0, target);
}

bool wm_layout_raise_pane_within(WmLayout *layout, const char *ancestor,
                                 const char *name) {
    if (!layout || !ancestor || !name)
        return false;
    int parent = wm_layout_find_pane(layout, ancestor);
    int target = wm_layout_find_pane(layout, name);
    return parent >= 0 && target >= 0 && raise_pane_branch(layout, parent, target);
}

bool wm_layout_set_text_style(WmLayout *layout, const char *name, float font_width,
                              float font_height, float char_space) {
    if (!layout || !name || !isfinite(font_width) || !isfinite(font_height) ||
        !isfinite(char_space) || font_width < 0 || font_height < 0)
        return false;
    int index = wm_layout_find_pane(layout, name);
    if (index < 0 || strcmp(layout->panes[index].type, "txt1") != 0)
        return false;
    LayoutPane *pane = &layout->panes[index];
    pane->font_size[0] = font_width;
    pane->font_size[1] = font_height;
    pane->char_space = char_space;
    return true;
}

static bool is_language_group(const char *name) {
    static const char *const languages[] = {"JPN", "ENG", "GER", "FRA", "SPA",
                                            "ITA", "NED", "CHN", "CHT", "KOR"};
    for (size_t index = 0; index < sizeof(languages) / sizeof(languages[0]); index++) {
        if (strcmp(name, languages[index]) == 0)
            return true;
    }
    return false;
}

bool wm_layout_mask_language_groups(WmLayout *layout, const char *language) {
    if (!layout)
        return false;
    const char *selected = language && is_language_group(language) ? language : "ENG";
    memset(layout->allowed_panes, 0,
           layout->pane_count * sizeof(*layout->allowed_panes));
    for (size_t group_index = 0; group_index < layout->group_count; group_index++) {
        const LayoutGroup *group = &layout->groups[group_index];
        if (!is_language_group(group->name) || strcmp(group->name, selected) == 0)
            continue;
        for (size_t member_index = 0; member_index < group->member_count;
             member_index++) {
            int pane = group->members[member_index];
            if (pane >= 0)
                layout->allowed_panes[pane] = true;
        }
    }
    const LayoutGroup *selected_group = find_group(layout, selected);
    if (selected_group) {
        for (size_t member_index = 0; member_index < selected_group->member_count;
             member_index++) {
            int pane = selected_group->members[member_index];
            if (pane >= 0)
                layout->allowed_panes[pane] = false;
        }
    }
    for (size_t index = 0; index < layout->pane_count; index++) {
        if (layout->allowed_panes[index])
            layout->panes[index].flags &= ~1u;
    }
    return true;
}

static void allow_pane(WmLayout *layout, int index, bool recursive) {
    if (index < 0 || (size_t)index >= layout->pane_count)
        return;
    LayoutPane *pane = &layout->panes[index];
    layout->allowed_panes[index] = true;
    if (pane->material >= 0 && (size_t)pane->material < layout->material_count) {
        layout->allowed_materials[pane->material] = true;
    }
    for (size_t frame = 0; frame < pane->frame_count; frame++) {
        int material = pane->frame_materials[frame];
        if (material >= 0 && (size_t)material < layout->material_count) {
            layout->allowed_materials[material] = true;
        }
    }
    if (recursive) {
        for (int child = pane->first_child; child >= 0;
             child = layout->panes[child].next_sibling) {
            allow_pane(layout, child, true);
        }
    }
}

static void bind_group(WmLayout *layout, const char *name, bool recursive) {
    memset(layout->allowed_panes, 0,
           layout->pane_count * sizeof(*layout->allowed_panes));
    memset(layout->allowed_materials, 0,
           layout->material_count * sizeof(*layout->allowed_materials));
    const LayoutGroup *group = find_group(layout, name);
    if (!group)
        return;
    for (size_t index = 0; index < group->member_count; index++) {
        allow_pane(layout, group->members[index], recursive);
    }
}

static float sample_track(const WmLayout *layout, const LayoutTrack *track,
                          float frame) {
    if (!track->key_count)
        return NAN;
    const LayoutKey *keys = layout->keys + track->first_key;
    if (frame <= keys[0].frame)
        return keys[0].value;
    if (frame >= keys[track->key_count - 1].frame) {
        return keys[track->key_count - 1].value;
    }
    size_t low = 1, high = track->key_count - 1;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (keys[middle].frame <= frame)
            low = middle + 1;
        else
            high = middle;
    }
    const LayoutKey *a = &keys[low - 1], *b = &keys[low];
    if (track->curve_type == 1)
        return a->value;
    float span = b->frame - a->frame;
    if (span <= 0)
        return b->value;
    float t = (frame - a->frame) / span;
    float t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * a->value + (t3 - 2 * t2 + t) * span * a->slope +
           (-2 * t3 + 3 * t2) * b->value + (t3 - t2) * span * b->slope;
}

static float blend_value(float current, float value, float weight) {
    return weight == 1.0f ? value : current + (value - current) * weight;
}

static void apply_pane_track(LayoutPane *pane, const LayoutTrack *track, float value,
                             float weight) {
    int property = track->property;
    switch (track->kind) {
        case LAYOUT_RLPA:
            if (property >= 0 && property < 3)
                pane->translation[property] =
                    blend_value(pane->translation[property], value, weight);
            else if (property < 6 && property >= 3)
                pane->rotation[property - 3] =
                    blend_value(pane->rotation[property - 3], value, weight);
            else if (property < 8 && property >= 6)
                pane->scale[property - 6] =
                    blend_value(pane->scale[property - 6], value, weight);
            else if (property < 10 && property >= 8)
                pane->size[property - 8] =
                    blend_value(pane->size[property - 8], value, weight);
            break;
        case LAYOUT_RLVI:
            if (weight < 1.0f)
                break;
            if (value != 0)
                pane->flags |= 1;
            else
                pane->flags &= ~1u;
            break;
        case LAYOUT_RLVC:
            if (property == 16)
                pane->alpha = blend_value(pane->alpha, value, weight);
            else if (property >= 0 && property < 16 && pane->has_text_colors) {
                float *color = &pane->text_colors[property / 8][property % 4];
                *color = blend_value(*color, value, weight);
            } else if (property >= 0 && property < 16 && pane->has_vertex_colors) {
                float *color = &pane->vertex_colors[property / 4][property % 4];
                *color = blend_value(*color, value, weight);
            }
            break;
        default:
            break;
    }
}

static void apply_material_track(LayoutMaterial *material, const LayoutTrack *track,
                                 const LayoutAnimation *animation, float value,
                                 float weight) {
    int property = track->property;
    switch (track->kind) {
        case LAYOUT_RLMC:
            if (property >= 0 && property < 4 && material->has_material_color) {
                float *color = &material->material_color[property];
                *color = blend_value(*color, value, weight);
            } else if (property >= 4 && property < 16) {
                float *color = &material->registers[(property - 4) / 4][property % 4];
                *color = blend_value(*color, value, weight);
            } else if (property >= 16 && property < 32) {
                float *color =
                    &material->konst_colors[(property - 16) / 4][property % 4];
                *color = blend_value(*color, value, weight);
            }
            break;
        case LAYOUT_RLTS:
            if (track->id < 0 || (size_t)track->id >= material->srt_count)
                break;
            if (property >= 0 && property < 2) {
                float *translation = &material->srts[track->id].translate[property];
                *translation = blend_value(*translation, value, weight);
            } else if (property == 2) {
                float *rotation = &material->srts[track->id].rotation;
                *rotation = blend_value(*rotation, value, weight);
            } else if (property >= 3 && property < 5) {
                float *scale = &material->srts[track->id].scale[property - 3];
                *scale = blend_value(*scale, value, weight);
            }
            break;
        case LAYOUT_RLTP:
            if (weight < 1.0f)
                break;
            if (property != 0 || track->id < 0 ||
                (size_t)track->id >= material->map_count || value < 0 ||
                floorf(value) != value || (size_t)value >= animation->texture_count)
                break;
            strcpy(material->maps[track->id].texture_name,
                   animation->textures[(size_t)value]);
            break;
        default:
            break;
    }
}

bool wm_layout_pose(WmLayout *layout, const WmLayoutClip *clips, size_t clip_count) {
    if (!layout || (clip_count && !clips))
        return false;
    /* Pane pointers reset below; keep text storage for the next frame. */
    memcpy(layout->panes, layout->base_panes,
           layout->pane_count * sizeof(*layout->panes));
    memcpy(layout->materials, layout->base_materials,
           layout->material_count * sizeof(*layout->materials));
    for (size_t clip_index = 0; clip_index < clip_count; clip_index++) {
        const WmLayoutClip *clip = &clips[clip_index];
        if (!clip->animation || !isfinite(clip->frame) || clip->loop_override < -1 ||
            clip->loop_override > 1)
            return false;
        float weight = clip->blend_from_current ? clip->weight : 1.0f;
        if (!isfinite(weight) || weight < 0.0f || weight > 1.0f)
            return false;
        if (weight == 0.0f)
            continue;
        const LayoutAnimation *animation = find_animation(layout, clip->animation);
        if (!animation)
            continue;
        int source_pane = -1, destination_pane = -1;
        int source_material = -1, destination_material = -1;
        if (clip->rebind_name) {
            if (!clip->target_name)
                return false;
            source_pane = wm_layout_find_pane(layout, clip->target_name);
            destination_pane = wm_layout_find_pane(layout, clip->rebind_name);
            if (source_pane < 0 || destination_pane < 0)
                return false;
            source_material = layout->base_panes[source_pane].material;
            destination_material = layout->base_panes[destination_pane].material;
        }
        if (clip->group)
            bind_group(layout, clip->group, clip->recursive_group);
        bool repeating =
            clip->loop_override < 0 ? animation->loop : clip->loop_override != 0;
        float frame = repeating && animation->frames > 0
                          ? fmodf(clip->frame, animation->frames)
                          : fminf(clip->frame, animation->frames);
        for (size_t offset = 0; offset < animation->track_count; offset++) {
            const LayoutTrack *track = &layout->tracks[animation->first_track + offset];
            if (track->target_index < 0)
                continue;
            int target_index = track->target_index;
            if (clip->rebind_name) {
                if (track->target_type == 1) {
                    if (source_material < 0 || destination_material < 0 ||
                        target_index != source_material)
                        continue;
                    target_index = destination_material;
                } else {
                    if (target_index != source_pane)
                        continue;
                    target_index = destination_pane;
                }
            } else if (clip->target_name) {
                const char *name = track->target_type == 1
                                       ? layout->materials[track->target_index].name
                                       : layout->panes[track->target_index].name;
                if (strcmp(name, clip->target_name) != 0)
                    continue;
            }
            float value = sample_track(layout, track, frame);
            if (isnan(value))
                continue;
            if (track->target_type == 1) {
                if (clip->group && !layout->allowed_materials[target_index])
                    continue;
                apply_material_track(&layout->materials[target_index], track, animation,
                                     value, weight);
            } else {
                if (clip->group && !layout->allowed_panes[target_index])
                    continue;
                apply_pane_track(&layout->panes[target_index], track, value, weight);
            }
        }
    }
    return true;
}
