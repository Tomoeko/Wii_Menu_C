#include "frame_damage.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { DAMAGE_COLUMNS = 32, DAMAGE_ROWS = 24, DAMAGE_LOOKAHEAD = 8 };

struct WmFrameDamage {
    WmFrameCommand storage[2][WM_FRAME_COMMAND_CAPACITY];
    WmFrameCommand *current;
    WmFrameCommand *previous;
    size_t current_count;
    size_t previous_count;
    int width;
    int height;
    WmColor clear;
    WmColor previous_clear;
    bool valid;
    bool tiles[DAMAGE_ROWS][DAMAGE_COLUMNS];
    WmViewport regions[DAMAGE_ROWS * DAMAGE_COLUMNS];
    size_t region_count;
};

WmFrameDamage *wm_frame_damage_create(void)
{
    WmFrameDamage *damage = calloc(1, sizeof(*damage));
    if (damage) {
        damage->current = damage->storage[0];
        damage->previous = damage->storage[1];
    }
    return damage;
}

void wm_frame_damage_destroy(WmFrameDamage *damage)
{
    free(damage);
}

void wm_frame_damage_begin(WmFrameDamage *damage, int width, int height,
                            WmColor clear)
{
    if (damage->width != width || damage->height != height)
        damage->valid = false;
    damage->width = width;
    damage->height = height;
    damage->clear = clear;
    damage->current_count = 0;
}

static WmFrameCommand *append_command(WmFrameDamage *damage,
                                      const WmClipRect *clip)
{
    if (damage->current_count == WM_FRAME_COMMAND_CAPACITY) return NULL;
    WmFrameCommand *command = &damage->current[damage->current_count++];
    /* Canonical padding makes byte comparisons deterministic. */
    memset(command, 0, sizeof(*command));
    command->clip = clip ? *clip
        : (WmClipRect){0, 0, WM_FRAME_WIDTH, WM_FRAME_HEIGHT};
    return command;
}

static void finish_bounds(WmFrameCommand *command, const float positions[4][2])
{
    float left = positions[0][0], right = left;
    float top = positions[0][1], bottom = top;
    for (size_t index = 0; index < 4; index++) {
        if (!isfinite(positions[index][0]) || !isfinite(positions[index][1])) {
            command->bounds = (WmClipRect){0, 0, WM_FRAME_WIDTH, WM_FRAME_HEIGHT};
            return;
        }
        left = fminf(left, positions[index][0]);
        right = fmaxf(right, positions[index][0]);
        top = fminf(top, positions[index][1]);
        bottom = fmaxf(bottom, positions[index][1]);
    }
    const WmClipRect *clip = &command->clip;
    left = fmaxf(left, fmaxf(0, clip->x));
    right = fminf(right, fminf(WM_FRAME_WIDTH, clip->x + clip->width));
    top = fmaxf(top, fmaxf(0, clip->y));
    bottom = fminf(bottom, fminf(WM_FRAME_HEIGHT, clip->y + clip->height));
    command->bounds = (WmClipRect){left, top, fmaxf(0, right - left),
                                  fmaxf(0, bottom - top)};
}

bool wm_frame_damage_quad(WmFrameDamage *damage,
                          const WmDrawVertex vertices[4], uint32_t texture,
                          const WmClipRect *clip)
{
    WmFrameCommand *command = append_command(damage, clip);
    if (!command) return false;
    command->kind = WM_FRAME_COMMAND_QUAD;
    command->texture = texture;
    memcpy(command->draw.vertices, vertices, sizeof(command->draw.vertices));
    float positions[4][2];
    for (size_t index = 0; index < 4; index++) {
        positions[index][0] = vertices[index].x;
        positions[index][1] = vertices[index].y;
    }
    finish_bounds(command, positions);
    return true;
}

bool wm_frame_damage_material(WmFrameDamage *damage,
                              const WmMaterialQuad *quad,
                              const WmClipRect *clip)
{
    WmFrameCommand *command = append_command(damage, clip);
    if (!command) return false;
    command->kind = WM_FRAME_COMMAND_MATERIAL;
    WmMaterialQuad *copy = &command->draw.material;
    memcpy(copy->vertices, quad->vertices, sizeof(copy->vertices));
    memcpy(copy->textures, quad->textures, sizeof(copy->textures));
    memcpy(copy->wrap_s, quad->wrap_s, sizeof(copy->wrap_s));
    memcpy(copy->wrap_t, quad->wrap_t, sizeof(copy->wrap_t));
    copy->texture_count = quad->texture_count;
    memcpy(copy->registers, quad->registers, sizeof(copy->registers));
    memcpy(copy->konst_colors, quad->konst_colors, sizeof(copy->konst_colors));
    memcpy(copy->tev_stages, quad->tev_stages, sizeof(copy->tev_stages));
    copy->tev_stage_count = quad->tev_stage_count;
    memcpy(copy->tev_swap_table, quad->tev_swap_table, sizeof(copy->tev_swap_table));
    memcpy(copy->alpha_compare, quad->alpha_compare, sizeof(copy->alpha_compare));
    memcpy(copy->blend_mode, quad->blend_mode, sizeof(copy->blend_mode));
    copy->has_alpha_compare = quad->has_alpha_compare;
    copy->has_blend_mode = quad->has_blend_mode;
    float positions[4][2];
    for (size_t index = 0; index < 4; index++) {
        positions[index][0] = quad->vertices[index].x;
        positions[index][1] = quad->vertices[index].y;
    }
    finish_bounds(command, positions);
    return true;
}

const WmFrameCommand *wm_frame_damage_commands(const WmFrameDamage *damage,
                                              size_t *count)
{
    *count = damage->current_count;
    return damage->current;
}

static WmViewport pixel_bounds(const WmFrameCommand *command, int width, int height)
{
    const WmClipRect *bounds = &command->bounds;
    if (bounds->width <= 0 || bounds->height <= 0) return (WmViewport){0};
    /* One extra pixel covers scissor rounding and rasterization at edges. */
    double scale_x = (double)width / WM_FRAME_WIDTH;
    double scale_y = (double)height / WM_FRAME_HEIGHT;
    int left = (int)fmax(0, floor(bounds->x * scale_x) - 1);
    int top = (int)fmax(0, floor(bounds->y * scale_y) - 1);
    int right = (int)fmin(width, ceil((bounds->x + bounds->width) * scale_x) + 1);
    int bottom = (int)fmin(height, ceil((bounds->y + bounds->height) * scale_y) + 1);
    return (WmViewport){left, top, right - left, bottom - top};
}

bool wm_frame_command_intersects(const WmFrameCommand *command,
                                 WmViewport region, int width, int height)
{
    WmViewport bounds = pixel_bounds(command, width, height);
    return bounds.width > 0 && bounds.height > 0 &&
        bounds.x < region.x + region.width && bounds.x + bounds.width > region.x &&
        bounds.y < region.y + region.height && bounds.y + bounds.height > region.y;
}

static void mark_command(WmFrameDamage *damage, const WmFrameCommand *command)
{
    WmViewport bounds = pixel_bounds(command, damage->width, damage->height);
    if (bounds.width <= 0 || bounds.height <= 0) return;
    int left = (int)(((int64_t)(bounds.x + 1) * DAMAGE_COLUMNS - 1) / damage->width);
    int right = (int)(((int64_t)(bounds.x + bounds.width) * DAMAGE_COLUMNS - 1) /
                      damage->width);
    int top = (int)(((int64_t)(bounds.y + 1) * DAMAGE_ROWS - 1) / damage->height);
    int bottom = (int)(((int64_t)(bounds.y + bounds.height) * DAMAGE_ROWS - 1) /
                       damage->height);
    for (int row = top; row <= bottom; row++) {
        for (int column = left; column <= right; column++)
            damage->tiles[row][column] = true;
    }
}

static bool commands_equal(const WmFrameCommand *first, const WmFrameCommand *second)
{
    return memcmp(first, second, sizeof(*first)) == 0;
}

static void compare_commands(WmFrameDamage *damage)
{
    size_t old = 0, current = 0;
    while (old < damage->previous_count && current < damage->current_count) {
        if (commands_equal(&damage->previous[old], &damage->current[current])) {
            old++;
            current++;
            continue;
        }
        /* Animated pane visibility often inserts or removes a few commands.
         * Bounded realignment avoids invalidating the rest of the scene. */
        bool aligned = false;
        for (size_t offset = 1; offset <= DAMAGE_LOOKAHEAD; offset++) {
            if (old + offset < damage->previous_count &&
                commands_equal(&damage->previous[old + offset],
                                &damage->current[current])) {
                for (size_t skipped = 0; skipped < offset; skipped++)
                    mark_command(damage, &damage->previous[old++]);
                aligned = true;
                break;
            }
            if (current + offset < damage->current_count &&
                commands_equal(&damage->previous[old],
                                &damage->current[current + offset])) {
                for (size_t skipped = 0; skipped < offset; skipped++)
                    mark_command(damage, &damage->current[current++]);
                aligned = true;
                break;
            }
        }
        if (!aligned) {
            mark_command(damage, &damage->previous[old++]);
            mark_command(damage, &damage->current[current++]);
        }
    }
    while (old < damage->previous_count) mark_command(damage, &damage->previous[old++]);
    while (current < damage->current_count) mark_command(damage, &damage->current[current++]);
}

static void collect_regions(WmFrameDamage *damage)
{
    int previous_row[DAMAGE_COLUMNS];
    for (int column = 0; column < DAMAGE_COLUMNS; column++) previous_row[column] = -1;
    for (int row = 0; row < DAMAGE_ROWS; row++) {
        int next_row[DAMAGE_COLUMNS];
        for (int column = 0; column < DAMAGE_COLUMNS; column++) next_row[column] = -1;
        for (int column = 0; column < DAMAGE_COLUMNS;) {
            if (!damage->tiles[row][column]) {
                column++;
                continue;
            }
            int first = column;
            while (column < DAMAGE_COLUMNS && damage->tiles[row][column]) column++;
            int left = (int)((int64_t)first * damage->width / DAMAGE_COLUMNS);
            int right = (int)((int64_t)column * damage->width / DAMAGE_COLUMNS);
            int top = (int)((int64_t)row * damage->height / DAMAGE_ROWS);
            int bottom = (int)((int64_t)(row + 1) * damage->height / DAMAGE_ROWS);
            int prior = previous_row[first];
            if (prior >= 0 && damage->regions[prior].width == right - left) {
                damage->regions[prior].height = bottom - damage->regions[prior].y;
                next_row[first] = prior;
            } else {
                next_row[first] = (int)damage->region_count;
                damage->regions[damage->region_count++] =
                    (WmViewport){left, top, right - left, bottom - top};
            }
        }
        memcpy(previous_row, next_row, sizeof(previous_row));
    }
}

const WmViewport *wm_frame_damage_regions(WmFrameDamage *damage, size_t *count)
{
    damage->region_count = 0;
    if (damage->width <= 0 || damage->height <= 0) {
        *count = 0;
        return damage->regions;
    }
    bool full = !damage->valid ||
        memcmp(&damage->clear, &damage->previous_clear, sizeof(damage->clear)) != 0;
    if (!full) {
        memset(damage->tiles, 0, sizeof(damage->tiles));
        compare_commands(damage);
        unsigned dirty = 0;
        for (int row = 0; row < DAMAGE_ROWS; row++) {
            for (int column = 0; column < DAMAGE_COLUMNS; column++)
                dirty += damage->tiles[row][column];
        }
        full = dirty > DAMAGE_ROWS * DAMAGE_COLUMNS / 2;
    }
    if (full) {
        damage->regions[damage->region_count++] =
            (WmViewport){0, 0, damage->width, damage->height};
    } else {
        collect_regions(damage);
    }
    *count = damage->region_count;
    return damage->regions;
}

void wm_frame_damage_commit(WmFrameDamage *damage)
{
    WmFrameCommand *swap = damage->previous;
    damage->previous = damage->current;
    damage->current = swap;
    damage->previous_count = damage->current_count;
    damage->previous_clear = damage->clear;
    damage->valid = true;
}

void wm_frame_damage_invalidate(WmFrameDamage *damage)
{
    if (damage) damage->valid = false;
}
