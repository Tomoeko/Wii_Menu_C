#include "wii_menu/resource_layout.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_be16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void write_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void write_float(uint8_t *bytes, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    write_be32(bytes, bits);
}

static void make_layout(uint8_t bytes[112])
{
    memset(bytes, 0, 112);
    memcpy(bytes, "RLYT", 4);
    bytes[4] = 0xfe;
    bytes[5] = 0xff;
    write_be32(bytes + 8, 112);
    write_be16(bytes + 12, 16);
    write_be16(bytes + 14, 2);

    memcpy(bytes + 16, "lyt1", 4);
    write_be32(bytes + 20, 20);
    bytes[24] = 1;
    write_float(bytes + 28, 608.0f);
    write_float(bytes + 32, 456.0f);

    memcpy(bytes + 36, "pan1", 4);
    write_be32(bytes + 40, 76);
    bytes[44] = 1;
    bytes[45] = 4;
    bytes[46] = 255;
    memcpy(bytes + 48, "RootPane", 8);
    write_float(bytes + 36 + 60, 1.0f);
    write_float(bytes + 36 + 64, 1.0f);
    write_float(bytes + 36 + 68, 608.0f);
    write_float(bytes + 36 + 72, 456.0f);
}

static void make_animation(uint8_t bytes[100])
{
    memset(bytes, 0, 100);
    memcpy(bytes, "RLAN", 4);
    bytes[4] = 0xfe;
    bytes[5] = 0xff;
    write_be32(bytes + 8, 100);
    write_be16(bytes + 12, 16);
    write_be16(bytes + 14, 1);

    uint8_t *pai = bytes + 16;
    memcpy(pai, "pai1", 4);
    write_be32(pai + 4, 84);
    write_be16(pai + 8, 60);
    pai[10] = 1;
    write_be16(pai + 14, 1);
    write_be32(pai + 16, 20);
    write_be32(pai + 20, 24);

    uint8_t *target = pai + 24;
    memcpy(target, "RootPane", 8);
    target[20] = 1;
    write_be32(target + 24, 28);

    uint8_t *tag = target + 28;
    memcpy(tag, "RLVI", 4);
    tag[4] = 1;
    write_be32(tag + 8, 12);

    uint8_t *track = tag + 12;
    track[0] = 3;
    track[1] = 7;
    track[2] = 1;
    write_be16(track + 4, 1);
    write_be32(track + 8, 12);
    write_float(track + 12, 2.0f);
    write_be16(track + 16, 257);
}

int main(void)
{
    uint8_t layout[112];
    uint8_t animation[100];
    make_layout(layout);
    make_animation(animation);

    char error[160] = {0};
    char *json = NULL;
    size_t json_size = 0;
    assert(wm_brlan_to_json(animation, sizeof(animation),
                            &json, &json_size, error, sizeof(error)));
    assert(json_size != 0 && strstr(json, "\"curveType\": 1") != NULL);
    assert(strstr(json, "\"value\": 257") != NULL);
    free(json);

    WmResourceAnimation clip = {"Focus", animation, sizeof(animation)};
    assert(wm_brlyt_to_json(layout, sizeof(layout), "Menu", "chanSel",
                             NULL, 0, &clip, 1,
                             &json, &json_size, error, sizeof(error)));
    assert(strstr(json, "\"width\": 608") != NULL);
    assert(strstr(json, "\"name\": \"RootPane\"") != NULL);
    assert(strstr(json, "\"Focus\": {") != NULL);
    free(json);

    WmResourceTexture texture = {
        "Icon.tpl", "textures/chanSel/Icon.png", 32, 32, 6,
        "layout/common/chanSel.ash/arc/timg/Icon.tpl"
    };
    assert(wm_brlyt_to_json_with_source(
        layout, sizeof(layout), "Menu", "chanSel",
        "layout/common/chanSel.ash/arc/blyt/Menu.brlyt",
        &texture, 1, NULL, 0,
        &json, &json_size, error, sizeof(error)));
    assert(strstr(json, "\"source\": \"layout/common/chanSel.ash/arc/blyt/Menu.brlyt\"") != NULL);
    assert(strstr(json, "\"source\": \"layout/common/chanSel.ash/arc/timg/Icon.tpl\"") != NULL);
    free(json);

    write_be32(layout + 40, 1024);
    assert(!wm_brlyt_to_json(layout, sizeof(layout), "Menu", "chanSel",
                              NULL, 0, NULL, 0,
                              &json, &json_size, error, sizeof(error)));
    write_be32(animation + 16 + 20, 1000);
    assert(!wm_brlan_to_json(animation, sizeof(animation),
                             &json, &json_size, error, sizeof(error)));
    puts("Resource layout tests passed.");
    return 0;
}
