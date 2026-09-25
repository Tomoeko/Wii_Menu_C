#include "wii_menu/source_hit.h"

#include <math.h>
#include <string.h>

typedef struct PaneRectSearch {
    const char *name;
    float scale_x;
    WmSourceRect rect;
    bool found;
} PaneRectSearch;

typedef struct ChannelSearch {
    WmSourceRect channels[WM_CHANNELS_PER_PAGE];
    bool found[WM_CHANNELS_PER_PAGE];
    float scale_x;
} ChannelSearch;

static bool source_rect(const WmLayoutPaneView *pane, float scale_x,
                        WmSourceRect *rect)
{
    float left = INFINITY;
    float right = -INFINITY;
    float top = INFINITY;
    float bottom = -INFINITY;
    for (size_t corner = 0; corner < 4; corner++) {
        float x = WM_FRAME_WIDTH * 0.5f + pane->corners[corner][0] * scale_x;
        float y = WM_FRAME_HEIGHT * 0.5f - pane->corners[corner][1];
        if (!isfinite(x) || !isfinite(y)) {
            return false;
        }
        if (x < left) left = x;
        if (x > right) right = x;
        if (y < top) top = y;
        if (y > bottom) bottom = y;
    }
    if (right <= left || bottom <= top) {
        return false;
    }
    *rect = (WmSourceRect){left, top, right - left, bottom - top};
    return true;
}

static bool collect_named_pane(void *context, const WmLayoutPaneView *pane)
{
    PaneRectSearch *search = context;
    if (strcmp(pane->name, search->name) == 0) {
        search->found = source_rect(pane, search->scale_x, &search->rect);
    }
    return true;
}

bool wm_source_pane_rect(const WmLayout *layout, const char *pane_name,
                         bool wide, WmLayoutMode mode,
                         const float parent_matrix[12],
                         WmSourceRect *rect)
{
    if (layout == NULL || pane_name == NULL || pane_name[0] == '\0' ||
        rect == NULL) {
        return false;
    }
    PaneRectSearch search = {
        .name = pane_name,
        .scale_x = (float)WM_FRAME_WIDTH / (wide ? 832.0f : 608.0f)
    };
    WmLayoutDrawOptions options = {
        .wide = wide,
        .mode = mode,
        .alpha = 1.0f,
        .parent_matrix = parent_matrix,
        .on_pane = collect_named_pane,
        .context = &search
    };
    wm_layout_draw(layout, &options);
    if (!search.found) return false;
    *rect = search.rect;
    return true;
}

static bool channel_index(const char *name, size_t *index)
{
    static const char prefix[] = "N_Ch_c";
    if (strlen(name) != sizeof(prefix) + 1 ||
        strncmp(name, prefix, sizeof(prefix) - 1) != 0) {
        return false;
    }
    char tens = name[sizeof(prefix) - 1];
    char ones = name[sizeof(prefix)];
    if (tens < '0' || tens > '9' || ones < '0' || ones > '9') {
        return false;
    }
    int number = (tens - '0') * 10 + (ones - '0');
    if (number < 1 || number > WM_CHANNELS_PER_PAGE) return false;
    *index = (size_t)(number - 1);
    return true;
}

static bool collect_channel_pane(void *context, const WmLayoutPaneView *pane)
{
    ChannelSearch *search = context;
    size_t index = 0;
    if (channel_index(pane->name, &index)) {
        search->found[index] = source_rect(
            pane, search->scale_x, &search->channels[index]);
    }
    return true;
}

static bool contains(const WmSourceRect *rect, int x, int y)
{
    return (float)x >= rect->x && (float)x < rect->x + rect->width &&
           (float)y >= rect->y && (float)y < rect->y + rect->height;
}

int wm_source_menu_slot_at(const WmLayout *grid, const WmMenu *menu,
                           int x, int y)
{
    if (!grid || !menu || menu->screen != WM_SCREEN_GRID ||
        menu->home_open || menu->page < 0 || menu->page >= WM_PAGE_COUNT)
        return -1;
    ChannelSearch search = {
        .scale_x = (float)WM_FRAME_WIDTH / 832.0f
    };
    WmLayoutDrawOptions options = {
        .wide = true,
        .mode = WM_LAYOUT_IPL,
        .alpha = 1.0f,
        .on_pane = collect_channel_pane,
        .context = &search
    };
    wm_layout_draw(grid, &options);
    for (size_t relative = 0; relative < WM_CHANNELS_PER_PAGE; relative++) {
        if (search.found[relative] &&
            contains(&search.channels[relative], x, y)) {
            return menu->page * WM_CHANNELS_PER_PAGE + (int)relative;
        }
    }
    return -1;
}

WmHit wm_source_menu_hit(const WmLayout *grid, const WmMenu *menu,
                         int x, int y)
{
    const WmHit none = {WM_HIT_NONE, -1};
    if (menu == NULL) return none;
    if (grid == NULL || menu->screen != WM_SCREEN_GRID || menu->home_open) {
        return wm_ui_hit(menu, x, y);
    }
    int slot = wm_source_menu_slot_at(grid, menu, x, y);
    if (slot >= 0 && menu->slots[slot].occupied) {
        return (WmHit){WM_HIT_CHANNEL, slot};
    }
    WmHit fallback = wm_ui_hit(menu, x, y);
    return fallback.type == WM_HIT_CHANNEL ? none : fallback;
}
