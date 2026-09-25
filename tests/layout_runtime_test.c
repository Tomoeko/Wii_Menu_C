#include "wii_menu/layout_runtime.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct Capture {
    int pane_count;
    int quad_count;
    float left;
    float top;
    float alpha;
    float register_red;
    uint32_t image;
    char texture_url[128];
} Capture;

typedef struct WindowCapture {
    int count;
    char texture_names[19][16];
    float x[19];
    float y[19];
    float width[19];
    float height[19];
    float uv[19][4][2];
} WindowCapture;

typedef struct TextCapture {
    int text_count;
    float width;
    float alpha;
    float top_red;
    uint8_t top_red_byte;
} TextCapture;

static bool near(float actual, float expected) {
    return fabsf(actual - expected) < 0.0001f;
}

typedef struct PaneOrder {
    const char *names[2];
    unsigned count;
} PaneOrder;

static bool collect_pane_order(void *context, const WmLayoutPaneView *pane) {
    PaneOrder *order = context;
    if ((strcmp(pane->name, "Source") == 0 ||
         strcmp(pane->name, "Destination") == 0) && order->count < 2)
        order->names[order->count++] = pane->name;
    return true;
}

static void test_raise_pane_restores_on_pose(void) {
    char error[128] = {0};
    WmLayout *layout = wm_layout_load_json("tests/layout_rebind_fixture.json",
                                            error, sizeof(error));
    assert(layout);
    assert(!wm_layout_raise_pane(layout, "Missing"));
    assert(wm_layout_raise_pane(layout, "Source"));
    PaneOrder raised = {0};
    wm_layout_visit_all_transforms(layout, false, WM_LAYOUT_IPL, NULL,
                                    collect_pane_order, &raised);
    assert(raised.count == 2);
    assert(strcmp(raised.names[0], "Destination") == 0);
    assert(strcmp(raised.names[1], "Source") == 0);
    assert(wm_layout_pose(layout, NULL, 0));
    PaneOrder reset = {0};
    wm_layout_visit_all_transforms(layout, false, WM_LAYOUT_IPL, NULL,
                                    collect_pane_order, &reset);
    assert(reset.count == 2);
    assert(strcmp(reset.names[0], "Source") == 0);
    assert(strcmp(reset.names[1], "Destination") == 0);
    wm_layout_destroy(layout);
}

typedef struct HiddenAnchors {
    float x[3], y[3];
    bool found[3];
} HiddenAnchors;

static bool collect_clock_anchor(void *context, const WmLayoutPaneView *pane) {
    HiddenAnchors *anchors = context;
    if (strncmp(pane->name, "N_Clock", 7) == 0 &&
        pane->name[7] >= '0' && pane->name[7] <= '2' &&
        pane->name[8] == '\0') {
        int index = pane->name[7] - '0';
        anchors->found[index] = true;
        anchors->x[index] = pane->matrix[3];
        anchors->y[index] = pane->matrix[7];
    }
    return true;
}

static void test_hidden_clock_anchors(void) {
    char error[128] = {0};
    WmLayout *layout = wm_layout_load_json("tests/hidden_anchor_fixture.json",
                                            error, sizeof(error));
    if (!layout) fprintf(stderr, "hidden anchor fixture: %s\n", error);
    assert(layout);
    HiddenAnchors anchors = {0};
    wm_layout_visit_all_transforms(layout, false, WM_LAYOUT_IPL, NULL,
                                    collect_clock_anchor, &anchors);
    assert(anchors.found[0] && anchors.found[2]);
    assert(!anchors.found[1]);
    assert(near(anchors.x[0], 0) && near(anchors.y[0], 32));
    assert(near(anchors.x[2], 224) && near(anchors.y[2], 8));
    wm_layout_destroy(layout);
}

static void test_pane_and_material_rebind(void) {
    char error[128] = {0};
    WmLayout *layout = wm_layout_load_json("tests/layout_rebind_fixture.json",
                                            error, sizeof(error));
    if (!layout) fprintf(stderr, "rebind fixture: %s\n", error);
    assert(layout);
    WmLayoutClip clip = {
        .animation = "MoveAndTint",
        .frame = 10.0f,
        .target_name = "Source",
        .rebind_name = "Destination"
    };
    assert(wm_layout_pose(layout, &clip, 1));
    WmLayoutPaneState source;
    WmLayoutPaneState destination;
    assert(wm_layout_pane_state(layout, "Source", &source));
    assert(wm_layout_pane_state(layout, "Destination", &destination));
    assert(near(source.translation[0], 10.0f));
    assert(near(destination.translation[0], 25.0f));
    WmLayoutMaterialInfo original_material;
    WmLayoutMaterialInfo rebound_material;
    uint8_t wraps[4][2];
    assert(wm_layout_material_info(layout, 0, &original_material, wraps));
    assert(wm_layout_material_info(layout, 1, &rebound_material, wraps));
    assert(near(original_material.registers[0][0], 1.0f));
    assert(near(rebound_material.registers[0][0], 64.0f / 255.0f));
    clip.target_name = "Missing";
    assert(!wm_layout_pose(layout, &clip, 1));
    wm_layout_destroy(layout);
}

static bool capture_pane(void *context, const WmLayoutPaneView *pane) {
    Capture *capture = context;
    capture->pane_count++;
    assert(pane->name && pane->type);
    return true;
}

static bool resolve_image(void *context, const WmLayoutTexture *resource,
                          uint32_t *handle) {
    (void)context;
    *handle = strcmp(resource->name, "other.tpl") == 0 ? 77 : 55;
    return true;
}

static void capture_quad(void *context, const WmLayoutQuad *quad) {
    Capture *capture = context;
    capture->quad_count++;
    capture->left = quad->vertices[0].position[0];
    capture->top = quad->vertices[0].position[1];
    capture->alpha = quad->vertices[0].color[3];
    capture->register_red = quad->material->registers[0][0];
    capture->image = quad->textures[0].handle;
    if (quad->textures[0].resource) {
        snprintf(capture->texture_url, sizeof(capture->texture_url), "%s",
                 quad->textures[0].resource->url);
    }
    assert(strcmp(quad->pane_name, "Picture") == 0);
    assert(near(quad->vertices[1].uv[0][0], 1));
}

static Capture draw(WmLayout *layout, bool wide) {
    Capture capture = {0};
    WmLayoutDrawOptions options = {
        .wide = wide,
        .mode = WM_LAYOUT_IPL,
        .alpha = 1,
        .on_pane = capture_pane,
        .on_quad = capture_quad,
        .image_provider = resolve_image,
        .context = &capture
    };
    wm_layout_draw(layout, &options);
    return capture;
}

static void capture_window_quad(void *context, const WmLayoutQuad *quad) {
    WindowCapture *capture = context;
    assert(capture->count < 19);
    int index = capture->count++;
    const WmLayoutTexture *texture = quad->textures[0].resource;
    assert(texture);
    snprintf(capture->texture_names[index], sizeof(capture->texture_names[index]),
             "%s", texture->name);
    capture->x[index] = quad->vertices[0].position[0];
    capture->y[index] = quad->vertices[0].position[1];
    capture->width[index] = quad->vertices[1].position[0] - capture->x[index];
    capture->height[index] = capture->y[index] - quad->vertices[2].position[1];
    for (int vertex = 0; vertex < 4; vertex++) {
        for (int axis = 0; axis < 2; axis++) {
            capture->uv[index][vertex][axis] = quad->vertices[vertex].uv[0][axis];
        }
    }
}

static void test_window_frames(void) {
    char error[128];
    WmLayout *layout = wm_layout_load_json("tests/layout_window_fixture.json",
                                            error, sizeof(error));
    assert(layout);
    WindowCapture capture = {0};
    WmLayoutDrawOptions options = {
        .alpha = 1,
        .on_quad = capture_window_quad,
        .context = &capture
    };
    wm_layout_draw(layout, &options);
    assert(capture.count == 19);
    const char *order[19] = {
        "content", "f0", "f0", "f0", "f0",
        "content", "f0", "f1", "f3", "f2",
        "content", "f0", "f6", "f1", "f5", "f3", "f7", "f2", "f4"
    };
    for (int index = 0; index < 19; index++) {
        assert(strcmp(capture.texture_names[index], order[index]) == 0);
    }
    assert(near(capture.x[0], 3) && near(capture.y[0], -2));
    assert(near(capture.x[5], 3) && near(capture.y[5], 98));
    assert(near(capture.x[10], 3) && near(capture.y[10], 198));
    assert(near(capture.width[0], 45) && near(capture.height[0], 27));
    assert(near(capture.width[5], 43) && near(capture.height[5], 25));
    assert(near(capture.width[10], 43) && near(capture.height[10], 25));
    assert(near(capture.x[2], 46) && near(capture.y[2], 0));
    assert(near(capture.width[2], 4) && near(capture.height[2], 25));
    /* Fixed oracle values from the HTML renderer's windowQuads fixture. */
    assert(near(capture.uv[1][1][0], 11.5f));        /* flip 0 */
    assert(near(capture.uv[2][0][0], 1));            /* flip 1 */
    assert(near(capture.uv[2][2][1], 5));
    assert(near(capture.uv[4][0][1], 5));            /* flip 2 */
    assert(near(capture.uv[8][0][0], -1.0f / 6));   /* flip 3 */
    assert(near(capture.uv[8][0][1], 46.0f / 7));
    assert(near(capture.uv[3][0][0], 11.5f));        /* flip 4 */
    assert(near(capture.uv[3][0][1], 1));
    assert(near(capture.uv[14][0][0], 1));           /* flip 5 */
    assert(near(capture.uv[14][0][1], -0.2f));
    assert(near(capture.uv[14][2][0], -3.5f));
    wm_layout_destroy(layout);
}

static bool capture_text_pane(void *context, const WmLayoutPaneView *pane) {
    TextCapture *capture = context;
    if (!pane->text) return true;
    const WmLayoutTextInfo *text = pane->text;
    capture->text_count++;
    assert(strcmp(pane->name, "Label") == 0);
    assert(strcmp(text->value, "A\xc3\xa9\xf0\x9f\x98\x80\nB") == 0);
    assert(strcmp(text->font_name, "Second.brfnt") == 0);
    assert(text->font_index == 1 && text->material_index == 0);
    assert(text->material && strcmp(text->material->name, "TextMat") == 0);
    assert(text->pane.origin == 4 && text->pane.text_position == 8);
    assert(text->horizontal_align == 2 && text->vertical_align == 2);
    assert(near(text->pane.size[1], 40));
    assert(near(text->pane.font_size[0], 24));
    assert(near(text->pane.font_size[1], 28));
    assert(near(text->pane.char_space, 1.5f));
    assert(near(text->pane.line_space, 2.5f));
    assert(text->pane.no_wrap);
    assert((pane->flags & 1) != 0);
    capture->width = text->pane.size[0];
    capture->alpha = pane->alpha;
    capture->top_red = text->colors[0][0];
    capture->top_red_byte = text->pane.top_color[0];
    assert(text->pane.top_color[1] == 34);
    assert(text->pane.bottom_color[3] == 120);
    return true;
}

static TextCapture draw_text(WmLayout *layout) {
    TextCapture capture = {0};
    WmLayoutDrawOptions options = {
        .alpha = 1,
        .on_pane = capture_text_pane,
        .context = &capture
    };
    wm_layout_draw(layout, &options);
    return capture;
}

static void test_text_metadata(void) {
    char error[128];
    WmLayout *layout = wm_layout_load_json("tests/layout_text_fixture.json",
                                            error, sizeof(error));
    assert(layout);
    assert(wm_layout_font_count(layout) == 2);
    assert(strcmp(wm_layout_font_name(layout, 0), "First.brfnt") == 0);
    assert(wm_layout_font_name(layout, 2) == NULL);
    TextCapture initial = draw_text(layout);
    assert(initial.text_count == 1);
    assert(near(initial.width, 120));
    assert(near(initial.alpha, 128.0f * 200 / (255 * 255)));
    assert(near(initial.top_red, 12));
    WmFontPane font_pane;
    const char *font_name = NULL;
    assert(wm_layout_pane_font(layout, "Label", &font_pane, &font_name));
    assert(strcmp(font_name, "Second.brfnt") == 0);
    assert(near(font_pane.size[0], 120));
    assert(near(font_pane.font_size[0], 24));
    assert(near(font_pane.font_size[1], 28));
    assert(near(font_pane.char_space, 1.5f));
    assert(near(font_pane.line_space, 2.5f));
    assert(font_pane.no_wrap);
    assert(font_pane.top_color[0] == 12);
    assert(!wm_layout_pane_font(layout, "Missing", &font_pane, &font_name));
    WmLayoutClip clip = {
        .animation = "TintAndHide",
        .frame = 5,
        .loop_override = -1
    };
    assert(wm_layout_pose(layout, &clip, 1));
    TextCapture posed = draw_text(layout);
    assert(posed.text_count == 1);
    assert(near(posed.width, 130));
    assert(near(posed.alpha, 128.0f * 150 / (255 * 255)));
    assert(near(posed.top_red, 62));
    assert(posed.top_red_byte == 62);
    assert(wm_layout_pane_font(layout, "Label", &font_pane, &font_name));
    assert(near(font_pane.size[0], 130));
    assert(font_pane.top_color[0] == 62);
    clip.frame = 10;
    assert(wm_layout_pose(layout, &clip, 1));
    assert(draw_text(layout).text_count == 0);
    assert(wm_layout_pose(layout, NULL, 0));
    assert(near(draw_text(layout).top_red, 12));
    wm_layout_destroy(layout);
}

static void test_pose_text_reuse(void) {
    char error[128] = {0};
    WmLayout *layout = wm_layout_load_json("tests/layout_text_fixture.json",
                                            error, sizeof(error));
    assert(layout);
    assert(wm_layout_set_pose_text(layout, "Label", "12:34 PM"));
    WmLayoutPaneState state;
    assert(wm_layout_pane_state(layout, "Label", &state));
    const char *storage = state.text;
    assert(strcmp(storage, "12:34 PM") == 0);

    /* The override is discarded by a pose, while its capacity stays ready
     * for changing clock labels on every subsequent frame. */
    assert(wm_layout_pose(layout, NULL, 0));
    assert(wm_layout_pane_state(layout, "Label", &state));
    assert(strcmp(state.text, "A\xc3\xa9\xf0\x9f\x98\x80\nB") == 0);
    assert(wm_layout_set_pose_text(layout, "Label", "1:02 PM"));
    assert(wm_layout_pane_state(layout, "Label", &state));
    assert(state.text == storage);
    assert(strcmp(state.text, "1:02 PM") == 0);

    /* A persistent text change must not replace the active pose override.
     * It becomes visible as soon as the next pose resets the pane. */
    assert(wm_layout_set_text(layout, "Label", "Source"));
    assert(wm_layout_pane_state(layout, "Label", &state));
    assert(state.text == storage);
    assert(strcmp(state.text, "1:02 PM") == 0);
    assert(wm_layout_pose(layout, NULL, 0));
    assert(wm_layout_pane_state(layout, "Label", &state));
    assert(strcmp(state.text, "Source") == 0);
    assert(wm_layout_set_text(layout, "Label", "Changed"));
    assert(wm_layout_pane_state(layout, "Label", &state));
    assert(strcmp(state.text, "Changed") == 0);
    assert(wm_layout_set_pose_text(layout, "Label", "2:03 PM"));
    assert(wm_layout_pane_state(layout, "Label", &state));
    assert(state.text == storage);
    assert(wm_layout_pose(layout, NULL, 0));
    assert(wm_layout_pane_state(layout, "Label", &state));
    assert(strcmp(state.text, "Changed") == 0);
    wm_layout_destroy(layout);
}

static void test_pane_translation(void) {
    char error[128];
    WmLayout *layout = wm_layout_load_json("tests/layout_fixture.json",
                                            error, sizeof(error));
    assert(layout);
    assert(!wm_layout_set_pane_translation(layout, "Missing", 1, 2, 3));
    assert(!wm_layout_set_pane_translation(layout, "Picture", NAN, 2, 3));
    assert(near(draw(layout, false).left, -40));
    assert(wm_layout_set_pane_translation(layout, "Picture", 30, 42, 0));
    Capture shifted = draw(layout, false);
    assert(near(shifted.left, -20));
    assert(near(shifted.top, 67));
    Capture wide = draw(layout, true);
    assert(near(wide.left, -20.0f * 832.0f / 608.0f));
    assert(near(wide.top, 67));
    assert(wm_layout_pose(layout, NULL, 0));
    Capture reset = draw(layout, false);
    assert(near(reset.left, -40));
    assert(near(reset.top, 45));
    wm_layout_destroy(layout);
}

int main(int argc, char **argv) {
    test_hidden_clock_anchors();
    test_pane_and_material_rebind();
    test_pane_translation();
    const char *fixture = argc > 1 ? argv[1] : "tests/layout_fixture.json";
    char error[128];
    WmLayout *layout = wm_layout_load_json(fixture, error, sizeof(error));
    if (!layout) {
        fprintf(stderr, "fixture load failed: %s\n", error);
        return 1;
    }
    assert(wm_layout_pane_count(layout) == 2);
    assert(wm_layout_material_count(layout) == 1);
    assert(wm_layout_texture_count(layout) == 2);
    assert(strcmp(wm_layout_texture_at(layout, 0)->url,
                  "textures/base.png") == 0);
    assert(strcmp(wm_layout_texture_at(layout, 1)->url,
                  "textures/other.png") == 0);
    assert(wm_layout_texture_at(layout, 2) == NULL);

    Capture initial = draw(layout, false);
    assert(initial.pane_count == 2 && initial.quad_count == 1);
    assert(near(initial.left, -40) && near(initial.top, 45));
    assert(near(initial.alpha, 128.0f / 255.0f));
    assert(initial.image == 55);

    WmLayoutClip clip = {
        .animation = "MoveAndSwap",
        .frame = 5,
        .group = "Root",
        .recursive_group = false,
        .loop_override = -1
    };
    assert(wm_layout_pose(layout, &clip, 1));
    Capture restricted = draw(layout, false);
    assert(near(restricted.left, -40) && restricted.image == 55);

    clip.recursive_group = true;
    assert(wm_layout_pose(layout, &clip, 1));
    Capture moved = draw(layout, false);
    assert(near(moved.left, -30) && moved.image == 77);
    assert(strcmp(moved.texture_url, "textures/other.png") == 0);
    assert(near(moved.register_red, 128.0f / 255.0f));

    Capture wide = draw(layout, true);
    assert(near(wide.left, -30 * 832.0f / 608.0f));

    clip.group = NULL;
    clip.target_name = "Picture";
    assert(wm_layout_pose(layout, &clip, 1));
    Capture pane_only = draw(layout, false);
    assert(near(pane_only.left, -30));
    assert(pane_only.image == 55);

    clip.target_name = "PictureMat";
    assert(wm_layout_pose(layout, &clip, 1));
    Capture material_only = draw(layout, false);
    assert(near(material_only.left, -40));
    assert(material_only.image == 77);
    assert(near(material_only.register_red, 128.0f / 255.0f));

    WmLayoutClip overlay[] = {
        {
            .animation = "MoveAndSwap",
            .frame = 5,
            .loop_override = 0,
            .target_name = "Picture"
        },
        {
            .animation = "MoveAndSwap",
            .frame = 5,
            .loop_override = 0,
            .target_name = "PictureMat"
        }
    };
    assert(wm_layout_pose(layout, overlay, 2));
    Capture combined = draw(layout, false);
    assert(near(combined.left, -30));
    assert(combined.image == 77);

    WmLayoutClip hide = {
        .animation = "Hide",
        .frame = 5,
        .loop_override = -1
    };
    assert(wm_layout_pose(layout, &hide, 1));
    Capture hidden = draw(layout, false);
    assert(hidden.pane_count == 1 && hidden.quad_count == 0);

    assert(wm_layout_pose(layout, NULL, 0));
    Capture reset = draw(layout, false);
    assert(near(reset.left, -40) && reset.image == 55);
    wm_layout_destroy(layout);
    test_window_frames();
    test_text_metadata();
    test_pose_text_reuse();
    test_raise_pane_restores_on_pose();
    return 0;
}
