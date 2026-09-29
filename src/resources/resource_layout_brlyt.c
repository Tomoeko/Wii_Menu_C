#include "resource_layout_internal.h"

#include <stdlib.h>
#include <string.h>

static bool wm_writer_srt(WmJsonWriter *writer, const uint8_t *bytes, size_t size,
                          size_t offset) {
    if (!wm_range(size, offset, 20)) {
        return false;
    }
    wm_writer_text(writer, "{\"translate\": ");
    if (!wm_writer_floats(writer, bytes, size, offset, 2)) {
        return false;
    }
    wm_writer_text(writer, ", \"rotation\": ");
    float rotation;
    if (!wm_float(bytes, size, offset + 8, &rotation)) {
        return false;
    }
    wm_writer_float(writer, rotation);
    wm_writer_text(writer, ", \"scale\": ");
    if (!wm_writer_floats(writer, bytes, size, offset + 12, 2)) {
        return false;
    }
    wm_writer_text(writer, "}");
    return !writer->failed;
}

static bool wm_writer_rgba_rows(WmJsonWriter *writer, const uint8_t *bytes, size_t size,
                                size_t offset, size_t rows) {
    if (rows > (size - (offset <= size ? offset : size)) / 4 || offset > size) {
        return false;
    }
    wm_writer_text(writer, "[");
    for (size_t index = 0; index < rows; index++) {
        if (index != 0) {
            wm_writer_text(writer, ", ");
        }
        if (!wm_writer_bytes_array(writer, bytes, size, offset + index * 4, 4)) {
            return false;
        }
    }
    wm_writer_text(writer, "]");
    return !writer->failed;
}

static bool wm_writer_material(WmJsonWriter *writer, const uint8_t *block, size_t size,
                               size_t base, unsigned depth) {
    if (!wm_range(size, base, 64)) {
        return false;
    }
    const uint8_t *name;
    size_t name_length;
    if (!wm_fixed_string(block, size, base, 20, &name, &name_length)) {
        return false;
    }
    uint32_t flags = wm_be32(block + base + 60);
    wm_writer_text(writer, "{");
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, "\"name\": ");
    wm_writer_string(writer, name, name_length);
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, ",\"colors\": [");
    for (size_t row = 0; row < 3; row++) {
        if (row != 0) {
            wm_writer_text(writer, ", ");
        }
        wm_writer_text(writer, "[");
        for (size_t channel = 0; channel < 4; channel++) {
            if (channel != 0) {
                wm_writer_text(writer, ", ");
            }
            wm_writer_format(
                writer, "%d",
                (int)wm_signed_be16(block + base + 20 + row * 8 + channel * 2));
        }
        wm_writer_text(writer, "]");
    }
    wm_writer_text(writer, "],");
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, "\"konstColors\": ");
    if (!wm_writer_rgba_rows(writer, block, size, base + 44, 4)) {
        return false;
    }
    wm_writer_format(writer, ",\n%*s\"flags\": %u,", (int)((depth + 1) * 2), "", flags);

    size_t cursor = base + 64;
    size_t count = flags & 15u;
    if (!wm_range(size, cursor, count * 4)) {
        return false;
    }
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, "\"textureMaps\": [");
    for (size_t index = 0; index < count; index++) {
        if (index != 0) {
            wm_writer_text(writer, ", ");
        }
        wm_writer_format(writer, "{\"texture\": %u, \"wrapS\": %u, \"wrapT\": %u}",
                         (unsigned)wm_be16(block + cursor + index * 4),
                         (unsigned)block[cursor + index * 4 + 2],
                         (unsigned)block[cursor + index * 4 + 3]);
    }
    wm_writer_text(writer, "],");
    cursor += count * 4;

    count = (flags >> 4) & 15u;
    if (!wm_range(size, cursor, count * 20)) {
        return false;
    }
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, "\"textureSRTs\": [");
    for (size_t index = 0; index < count; index++) {
        if (index != 0) {
            wm_writer_text(writer, ", ");
        }
        if (!wm_writer_srt(writer, block, size, cursor + index * 20)) {
            return false;
        }
    }
    wm_writer_text(writer, "],");
    cursor += count * 20;

    count = (flags >> 8) & 15u;
    if (!wm_range(size, cursor, count * 4)) {
        return false;
    }
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, "\"texCoordGens\": [");
    for (size_t index = 0; index < count; index++) {
        if (index != 0) {
            wm_writer_text(writer, ", ");
        }
        wm_writer_format(writer, "{\"type\": %u, \"source\": %u, \"matrix\": %u}",
                         (unsigned)block[cursor + index * 4],
                         (unsigned)block[cursor + index * 4 + 1],
                         (unsigned)block[cursor + index * 4 + 2]);
    }
    wm_writer_text(writer, "],");
    cursor += count * 4;

    if ((flags & (1u << 25)) != 0) {
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, "\"channelControl\": ");
        if (!wm_writer_bytes_array(writer, block, size, cursor, 4)) {
            return false;
        }
        wm_writer_text(writer, ",");
        cursor += 4;
    }
    if ((flags & (1u << 27)) != 0) {
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, "\"materialColor\": ");
        if (!wm_writer_bytes_array(writer, block, size, cursor, 4)) {
            return false;
        }
        wm_writer_text(writer, ",");
        cursor += 4;
    }
    if ((flags & (1u << 12)) != 0) {
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, "\"tevSwapTable\": ");
        if (!wm_writer_bytes_array(writer, block, size, cursor, 4)) {
            return false;
        }
        wm_writer_text(writer, ",");
        cursor += 4;
    }

    count = (flags >> 13) & 3u;
    if (!wm_range(size, cursor, count * 20)) {
        return false;
    }
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, "\"indirectSRTs\": [");
    for (size_t index = 0; index < count; index++) {
        if (index != 0) {
            wm_writer_text(writer, ", ");
        }
        if (!wm_writer_srt(writer, block, size, cursor + index * 20)) {
            return false;
        }
    }
    wm_writer_text(writer, "],");
    cursor += count * 20;

    count = (flags >> 15) & 7u;
    if (!wm_range(size, cursor, count * 4)) {
        return false;
    }
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, "\"indirectStages\": [");
    for (size_t index = 0; index < count; index++) {
        if (index != 0) {
            wm_writer_text(writer, ", ");
        }
        if (!wm_writer_bytes_array(writer, block, size, cursor + index * 4, 4)) {
            return false;
        }
    }
    wm_writer_text(writer, "],");
    cursor += count * 4;

    count = (flags >> 18) & 31u;
    if (!wm_range(size, cursor, count * 16)) {
        return false;
    }
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, "\"tevStages\": [");
    for (size_t index = 0; index < count; index++) {
        if (index != 0) {
            wm_writer_text(writer, ", ");
        }
        if (!wm_writer_bytes_array(writer, block, size, cursor + index * 16, 16)) {
            return false;
        }
    }
    wm_writer_text(writer, "]");
    cursor += count * 16;

    if ((flags & (1u << 23)) != 0) {
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, ",\"alphaCompare\": ");
        if (!wm_writer_bytes_array(writer, block, size, cursor, 4)) {
            return false;
        }
        cursor += 4;
    }
    if ((flags & (1u << 24)) != 0) {
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, ",\"blendMode\": ");
        if (!wm_writer_bytes_array(writer, block, size, cursor, 4)) {
            return false;
        }
    }
    wm_writer_indent(writer, depth);
    wm_writer_text(writer, "}");
    return !writer->failed;
}

static bool wm_writer_picture(WmJsonWriter *writer, const uint8_t *block, size_t size,
                              size_t offset, unsigned depth) {
    if (!wm_range(size, offset, 20)) {
        return false;
    }
    size_t count = block[offset + 18];
    if (count > 16 || !wm_range(size, offset + 20, count * 32)) {
        return false;
    }
    wm_writer_indent(writer, depth);
    wm_writer_text(writer, ",\"vertexColors\": ");
    if (!wm_writer_rgba_rows(writer, block, size, offset, 4)) {
        return false;
    }
    wm_writer_format(writer, ",\n%*s\"material\": %u,", (int)(depth * 2), "",
                     (unsigned)wm_be16(block + offset + 16));
    wm_writer_indent(writer, depth);
    wm_writer_text(writer, "\"texCoords\": [");
    for (size_t coord = 0; coord < count; coord++) {
        if (coord != 0) {
            wm_writer_text(writer, ", ");
        }
        wm_writer_text(writer, "[");
        for (size_t vertex = 0; vertex < 4; vertex++) {
            if (vertex != 0) {
                wm_writer_text(writer, ", ");
            }
            if (!wm_writer_floats(writer, block, size,
                                  offset + 20 + coord * 32 + vertex * 8, 2)) {
                return false;
            }
        }
        wm_writer_text(writer, "]");
    }
    wm_writer_text(writer, "]");
    return !writer->failed;
}

static void wm_writer_codepoint(WmJsonWriter *writer, uint32_t codepoint) {
    if (codepoint == '"' || codepoint == '\\') {
        char escaped[2] = {'\\', (char)codepoint};
        wm_writer_bytes(writer, escaped, 2);
    } else if (codepoint < 32) {
        wm_writer_format(writer, "\\u%04x", (unsigned)codepoint);
    } else if (codepoint < 128) {
        char value = (char)codepoint;
        wm_writer_bytes(writer, &value, 1);
    } else {
        char utf8[4];
        size_t count;
        if (codepoint < 0x800) {
            utf8[0] = (char)(0xc0u | (codepoint >> 6));
            utf8[1] = (char)(0x80u | (codepoint & 0x3fu));
            count = 2;
        } else if (codepoint < 0x10000) {
            utf8[0] = (char)(0xe0u | (codepoint >> 12));
            utf8[1] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
            utf8[2] = (char)(0x80u | (codepoint & 0x3fu));
            count = 3;
        } else {
            utf8[0] = (char)(0xf0u | (codepoint >> 18));
            utf8[1] = (char)(0x80u | ((codepoint >> 12) & 0x3fu));
            utf8[2] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
            utf8[3] = (char)(0x80u | (codepoint & 0x3fu));
            count = 4;
        }
        wm_writer_bytes(writer, utf8, count);
    }
}

static bool wm_writer_utf16(WmJsonWriter *writer, const uint8_t *bytes, size_t size,
                            size_t offset, size_t length) {
    if ((length & 1u) != 0 || !wm_range(size, offset, length)) {
        return false;
    }
    while (length >= 2 && wm_be16(bytes + offset + length - 2) == 0) {
        length -= 2;
    }
    wm_writer_text(writer, "\"");
    for (size_t index = 0; index < length; index += 2) {
        uint32_t value = wm_be16(bytes + offset + index);
        if (value >= 0xd800 && value <= 0xdbff) {
            if (index + 4 > length) {
                return false;
            }
            uint32_t low = wm_be16(bytes + offset + index + 2);
            if (low < 0xdc00 || low > 0xdfff) {
                return false;
            }
            value = 0x10000u + ((value - 0xd800u) << 10) + (low - 0xdc00u);
            index += 2;
        } else if (value >= 0xdc00 && value <= 0xdfff) {
            return false;
        }
        wm_writer_codepoint(writer, value);
    }
    wm_writer_text(writer, "\"");
    return !writer->failed;
}

static bool wm_writer_pane(WmJsonWriter *writer, const WmPaneRecord *panes,
                           size_t pane_count, int index, unsigned depth) {
    if (index < 0 || (size_t)index >= pane_count || depth > WM_RESOURCE_MAX_PANES) {
        return false;
    }
    const WmSection *section = &panes[index].section;
    const uint8_t *block = section->bytes;
    size_t size = section->size;
    if (!wm_range(size, 0, 76)) {
        return false;
    }
    const uint8_t *name;
    size_t name_length;
    if (!wm_fixed_string(block, size, 12, 16, &name, &name_length)) {
        return false;
    }

    wm_writer_text(writer, "{");
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, "\"name\": ");
    wm_writer_string(writer, name, name_length);
    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, ",\"type\": ");
    wm_writer_string(writer, (const uint8_t *)section->kind, 4);
    wm_writer_format(writer, ",\n%*s\"flags\": %u, \"origin\": %u, \"alpha\": %u,",
                     (int)((depth + 1) * 2), "", (unsigned)block[8], (unsigned)block[9],
                     (unsigned)block[10]);
    static const size_t offsets[] = {36, 48, 60, 68};
    static const size_t counts[] = {3, 3, 2, 2};
    static const char *const labels[] = {"translation", "rotation", "scale", "size"};
    for (size_t field = 0; field < 4; field++) {
        wm_writer_indent(writer, depth + 1);
        wm_writer_format(writer, "\"%s\": ", labels[field]);
        if (!wm_writer_floats(writer, block, size, offsets[field], counts[field])) {
            return false;
        }
        if (field != 3) {
            wm_writer_text(writer, ",");
        }
    }

    if (strcmp(section->kind, "pic1") == 0) {
        if (!wm_writer_picture(writer, block, size, 76, depth + 1)) {
            return false;
        }
    } else if (strcmp(section->kind, "txt1") == 0) {
        if (!wm_range(size, 76, 40)) {
            return false;
        }
        size_t length = wm_be16(block + 78);
        size_t text_offset = wm_be32(block + 88);
        wm_writer_format(writer,
                         "\n%*s,\"material\": %u, \"font\": %u, \"textPosition\": %u,",
                         (int)((depth + 1) * 2), "", (unsigned)wm_be16(block + 80),
                         (unsigned)wm_be16(block + 82), (unsigned)block[84]);
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, "\"text\": ");
        if (!wm_writer_utf16(writer, block, size, text_offset, length)) {
            return false;
        }
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, ",\"textColors\": ");
        if (!wm_writer_rgba_rows(writer, block, size, 92, 2)) {
            return false;
        }
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, ",\"fontSize\": ");
        if (!wm_writer_floats(writer, block, size, 100, 2)) {
            return false;
        }
        float char_space;
        float line_space;
        if (!wm_float(block, size, 108, &char_space) ||
            !wm_float(block, size, 112, &line_space)) {
            return false;
        }
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, ",\"charSpace\": ");
        wm_writer_float(writer, char_space);
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, ",\"lineSpace\": ");
        wm_writer_float(writer, line_space);
    } else if (strcmp(section->kind, "wnd1") == 0) {
        if (!wm_range(size, 76, 28)) {
            return false;
        }
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, ",\"inflation\": ");
        if (!wm_writer_floats(writer, block, size, 76, 4)) {
            return false;
        }
        size_t frame_count = block[92];
        size_t content_offset = wm_be32(block + 96);
        size_t frames_offset = wm_be32(block + 100);
        if (!wm_writer_picture(writer, block, size, content_offset, depth + 1) ||
            frame_count > WM_RESOURCE_MAX_ITEMS ||
            !wm_range(size, frames_offset, frame_count * 4)) {
            return false;
        }
        wm_writer_indent(writer, depth + 1);
        wm_writer_text(writer, ",\"frames\": [");
        for (size_t frame = 0; frame < frame_count; frame++) {
            size_t frame_offset = wm_be32(block + frames_offset + frame * 4);
            if (!wm_range(size, frame_offset, 3)) {
                return false;
            }
            if (frame != 0) {
                wm_writer_text(writer, ", ");
            }
            wm_writer_format(writer, "{\"material\": %u, \"flip\": %u}",
                             (unsigned)wm_be16(block + frame_offset),
                             (unsigned)block[frame_offset + 2]);
        }
        wm_writer_text(writer, "]");
    }

    wm_writer_indent(writer, depth + 1);
    wm_writer_text(writer, ",\"children\": [");
    int child = panes[index].first_child;
    while (child >= 0) {
        wm_writer_indent(writer, depth + 2);
        if (!wm_writer_pane(writer, panes, pane_count, child, depth + 2)) {
            return false;
        }
        child = panes[child].next_sibling;
        if (child >= 0) {
            wm_writer_text(writer, ",");
        }
    }
    if (panes[index].first_child >= 0) {
        wm_writer_indent(writer, depth + 1);
    }
    wm_writer_text(writer, "]");
    wm_writer_indent(writer, depth);
    wm_writer_text(writer, "}");
    return !writer->failed;
}

static const WmResourceTexture *wm_find_texture(const WmResourceTexture *textures,
                                                size_t count, const uint8_t *name,
                                                size_t name_size) {
    for (size_t index = 0; index < count; index++) {
        if (strlen(textures[index].name) == name_size &&
            memcmp(textures[index].name, name, name_size) == 0) {
            return &textures[index];
        }
    }
    return NULL;
}

static void wm_writer_texture_descriptor(WmJsonWriter *writer, const uint8_t *name,
                                         size_t name_size,
                                         const WmResourceTexture *texture) {
    wm_writer_text(writer, "{\"name\": ");
    wm_writer_string(writer, name, name_size);
    if (texture == NULL) {
        wm_writer_text(writer, ", \"missing\": true}");
        return;
    }
    wm_writer_text(writer, ", \"url\": ");
    wm_writer_string(writer, (const uint8_t *)texture->url, strlen(texture->url));
    wm_writer_format(writer, ", \"width\": %u, \"height\": %u, \"format\": %u",
                     (unsigned)texture->width, (unsigned)texture->height,
                     (unsigned)texture->format);
    if (texture->source != NULL) {
        wm_writer_text(writer, ", \"source\": ");
        wm_writer_string(writer, (const uint8_t *)texture->source,
                         strlen(texture->source));
    }
    wm_writer_text(writer, "}");
}

static bool wm_writer_name_table(WmJsonWriter *writer, const WmSection *section,
                                 const WmResourceTexture *textures,
                                 size_t texture_count, bool texture_table) {
    if (section == NULL) {
        wm_writer_text(writer, "[]");
        return true;
    }
    const uint8_t *block = section->bytes;
    size_t size = section->size;
    if (!wm_range(size, 8, 4)) {
        return false;
    }
    size_t count = wm_be16(block + 8);
    if (count > WM_RESOURCE_MAX_ITEMS || !wm_range(size, 12, count * 8)) {
        return false;
    }
    wm_writer_text(writer, "[");
    for (size_t index = 0; index < count; index++) {
        size_t relative = wm_be32(block + 12 + index * 8);
        if (relative > size - 12) {
            return false;
        }
        const uint8_t *name;
        size_t name_size;
        if (!wm_cstring(block, size, 12 + relative, &name, &name_size)) {
            return false;
        }
        if (index != 0) {
            wm_writer_text(writer, ", ");
        }
        if (texture_table) {
            wm_writer_texture_descriptor(
                writer, name, name_size,
                wm_find_texture(textures, texture_count, name, name_size));
        } else {
            wm_writer_string(writer, name, name_size);
        }
    }
    wm_writer_text(writer, "]");
    return !writer->failed;
}

static bool wm_writer_material_table(WmJsonWriter *writer, const WmSection *section) {
    if (section == NULL) {
        wm_writer_text(writer, "[]");
        return true;
    }
    const uint8_t *block = section->bytes;
    size_t size = section->size;
    if (!wm_range(size, 8, 4)) {
        return false;
    }
    size_t count = wm_be16(block + 8);
    if (count > WM_RESOURCE_MAX_ITEMS || !wm_range(size, 12, count * 4)) {
        return false;
    }
    wm_writer_text(writer, "[");
    for (size_t index = 0; index < count; index++) {
        size_t offset = wm_be32(block + 12 + index * 4);
        if (index != 0) {
            wm_writer_text(writer, ",");
        }
        wm_writer_indent(writer, 2);
        if (!wm_writer_material(writer, block, size, offset, 2)) {
            return false;
        }
    }
    if (count != 0) {
        wm_writer_indent(writer, 1);
    }
    wm_writer_text(writer, "]");
    return !writer->failed;
}

static bool wm_writer_groups(WmJsonWriter *writer, const WmSection *sections,
                             size_t section_count) {
    wm_writer_text(writer, "{");
    bool first = true;
    for (size_t section_index = 0; section_index < section_count; section_index++) {
        const WmSection *section = &sections[section_index];
        if (strcmp(section->kind, "grp1") != 0) {
            continue;
        }
        const uint8_t *block = section->bytes;
        size_t size = section->size;
        if (!wm_range(size, 8, 20)) {
            return false;
        }
        const uint8_t *name;
        size_t name_size;
        if (!wm_fixed_string(block, size, 8, 16, &name, &name_size)) {
            return false;
        }
        size_t count = wm_be16(block + 24);
        if (count > WM_RESOURCE_MAX_ITEMS || !wm_range(size, 28, count * 16)) {
            return false;
        }
        if (!first) {
            wm_writer_text(writer, ",");
        }
        wm_writer_indent(writer, 2);
        wm_writer_string(writer, name, name_size);
        wm_writer_text(writer, ": [");
        for (size_t index = 0; index < count; index++) {
            const uint8_t *member;
            size_t member_size;
            if (!wm_fixed_string(block, size, 28 + index * 16, 16, &member,
                                 &member_size)) {
                return false;
            }
            if (index != 0) {
                wm_writer_text(writer, ", ");
            }
            wm_writer_string(writer, member, member_size);
        }
        wm_writer_text(writer, "]");
        first = false;
    }
    if (!first) {
        wm_writer_indent(writer, 1);
    }
    wm_writer_text(writer, "}");
    return !writer->failed;
}

static void wm_writer_resource_textures(WmJsonWriter *writer,
                                        const WmResourceTexture *textures,
                                        size_t texture_count) {
    wm_writer_text(writer, "{");
    for (size_t index = 0; index < texture_count; index++) {
        if (index != 0)
            wm_writer_text(writer, ",");
        wm_writer_indent(writer, 2);
        wm_writer_string(writer, (const uint8_t *)textures[index].name,
                         strlen(textures[index].name));
        wm_writer_text(writer, ": ");
        wm_writer_texture_descriptor(writer, (const uint8_t *)textures[index].name,
                                     strlen(textures[index].name), &textures[index]);
    }
    if (texture_count != 0)
        wm_writer_indent(writer, 1);
    wm_writer_text(writer, "}");
}

static bool wm_writer_animations(WmJsonWriter *writer,
                                 const WmResourceAnimation *animations,
                                 size_t animation_count, char *error,
                                 size_t error_size) {
    wm_writer_text(writer, "{");
    for (size_t index = 0; index < animation_count; index++) {
        char *animation_json = NULL;
        size_t animation_size = 0;
        if (!wm_brlan_to_json(animations[index].data, animations[index].size,
                              &animation_json, &animation_size, error, error_size)) {
            free(animation_json);
            return false;
        }
        if (index != 0)
            wm_writer_text(writer, ",");
        wm_writer_indent(writer, 2);
        wm_writer_string(writer, (const uint8_t *)animations[index].name,
                         strlen(animations[index].name));
        wm_writer_text(writer, ": ");
        for (size_t byte = 0; byte < animation_size; byte++) {
            if (animation_json[byte] == '\n' && byte + 1 < animation_size)
                wm_writer_text(writer, "\n    ");
            else if (animation_json[byte] != '\n' || byte + 1 < animation_size)
                wm_writer_bytes(writer, animation_json + byte, 1);
        }
        free(animation_json);
    }
    if (animation_count != 0)
        wm_writer_indent(writer, 1);
    wm_writer_text(writer, "}");
    return true;
}

bool wm_brlyt_to_json_with_source(const uint8_t *data, size_t size, const char *name,
                                  const char *package, const char *source,
                                  const WmResourceTexture *textures,
                                  size_t texture_count,
                                  const WmResourceAnimation *animations,
                                  size_t animation_count, char **json,
                                  size_t *json_size, char *error, size_t error_size) {
    if (json == NULL || json_size == NULL || name == NULL || package == NULL ||
        (texture_count != 0 && textures == NULL) ||
        (animation_count != 0 && animations == NULL) ||
        texture_count > WM_RESOURCE_MAX_ITEMS ||
        animation_count > WM_RESOURCE_MAX_ITEMS) {
        wm_set_error(error, error_size, "Invalid BRLYT export arguments.");
        return false;
    }
    *json = NULL;
    *json_size = 0;

    WmSection *sections = calloc(WM_RESOURCE_MAX_SECTIONS, sizeof(*sections));
    WmPaneRecord *panes = calloc(WM_RESOURCE_MAX_PANES, sizeof(*panes));
    int *stack = calloc(WM_RESOURCE_MAX_PANES, sizeof(*stack));
    WmJsonWriter writer = {0};
    if (sections == NULL || panes == NULL || stack == NULL) {
        wm_set_error(error, error_size, "Out of memory reading BRLYT layout.");
        goto fail;
    }
    size_t section_count = 0;
    if (!wm_read_sections(data, size, "RLYT", sections, &section_count)) {
        wm_set_error(error, error_size, "Invalid BRLYT section table.");
        goto fail;
    }

    const WmSection *layout_header = NULL;
    const WmSection *texture_table = NULL;
    const WmSection *font_table = NULL;
    const WmSection *material_table = NULL;
    size_t pane_count = 0;
    size_t depth = 0;
    int last = -1;
    int root = -1;
    bool valid = true;
    for (size_t index = 0; index < section_count; index++) {
        const WmSection *section = &sections[index];
        if (strcmp(section->kind, "lyt1") == 0) {
            layout_header = section;
        } else if (strcmp(section->kind, "txl1") == 0) {
            texture_table = section;
        } else if (strcmp(section->kind, "fnl1") == 0) {
            font_table = section;
        } else if (strcmp(section->kind, "mat1") == 0) {
            material_table = section;
        } else if (strcmp(section->kind, "pan1") == 0 ||
                   strcmp(section->kind, "bnd1") == 0 ||
                   strcmp(section->kind, "pic1") == 0 ||
                   strcmp(section->kind, "txt1") == 0 ||
                   strcmp(section->kind, "wnd1") == 0) {
            if (pane_count >= WM_RESOURCE_MAX_PANES) {
                valid = false;
                break;
            }
            int current = (int)pane_count++;
            panes[current].section = *section;
            panes[current].first_child = -1;
            panes[current].last_child = -1;
            panes[current].next_sibling = -1;
            if (depth == 0) {
                root = current;
            } else {
                WmPaneRecord *parent = &panes[stack[depth - 1]];
                if (parent->last_child >= 0) {
                    panes[parent->last_child].next_sibling = current;
                } else {
                    parent->first_child = current;
                }
                parent->last_child = current;
            }
            last = current;
        } else if (strcmp(section->kind, "pas1") == 0) {
            if (last < 0 || depth >= WM_RESOURCE_MAX_PANES) {
                valid = false;
                break;
            }
            stack[depth++] = last;
        } else if (strcmp(section->kind, "pae1") == 0) {
            if (depth == 0) {
                valid = false;
                break;
            }
            depth--;
        }
    }
    if (!valid || depth != 0) {
        wm_set_error(error, error_size, "Invalid BRLYT pane hierarchy.");
        goto fail;
    }

    float width = 0;
    float height = 0;
    unsigned origin_type = 1;
    if (layout_header != NULL) {
        if (!wm_range(layout_header->size, 8, 12) ||
            !wm_float(layout_header->bytes, layout_header->size, 12, &width) ||
            !wm_float(layout_header->bytes, layout_header->size, 16, &height)) {
            wm_set_error(error, error_size, "Invalid BRLYT layout dimensions.");
            goto fail;
        }
        origin_type = layout_header->bytes[8];
    }

    wm_writer_text(&writer, "{\n  \"name\": ");
    wm_writer_string(&writer, (const uint8_t *)name, strlen(name));
    wm_writer_text(&writer, ",\n  \"package\": ");
    wm_writer_string(&writer, (const uint8_t *)package, strlen(package));
    if (source != NULL) {
        wm_writer_text(&writer, ",\n  \"source\": ");
        wm_writer_string(&writer, (const uint8_t *)source, strlen(source));
    }
    wm_writer_text(&writer, ",\n  \"width\": ");
    wm_writer_float(&writer, width);
    wm_writer_text(&writer, ",\n  \"height\": ");
    wm_writer_float(&writer, height);
    wm_writer_format(&writer,
                     ",\n  \"originType\": %u,\n  \"textures\": ", origin_type);
    if (!wm_writer_name_table(&writer, texture_table, textures, texture_count, true)) {
        wm_set_error(error, error_size, "Invalid BRLYT texture table.");
        goto fail;
    }
    wm_writer_text(&writer, ",\n  \"resourceTextures\": ");
    wm_writer_resource_textures(&writer, textures, texture_count);
    wm_writer_text(&writer, ",\n  \"fonts\": ");
    if (!wm_writer_name_table(&writer, font_table, NULL, 0, false)) {
        wm_set_error(error, error_size, "Invalid BRLYT font table.");
        goto fail;
    }
    wm_writer_text(&writer, ",\n  \"materials\": ");
    if (!wm_writer_material_table(&writer, material_table)) {
        wm_set_error(error, error_size, "Invalid BRLYT material table.");
        goto fail;
    }
    wm_writer_text(&writer, ",\n  \"groups\": ");
    if (!wm_writer_groups(&writer, sections, section_count)) {
        wm_set_error(error, error_size, "Invalid BRLYT group table.");
        goto fail;
    }
    wm_writer_text(&writer, ",\n  \"root\": ");
    if (root >= 0) {
        if (!wm_writer_pane(&writer, panes, pane_count, root, 1)) {
            wm_set_error(error, error_size, "Invalid BRLYT pane content.");
            goto fail;
        }
    } else {
        wm_writer_text(&writer, "null");
    }
    wm_writer_text(&writer, ",\n  \"animations\": ");
    if (!wm_writer_animations(&writer, animations, animation_count, error, error_size))
        goto fail;
    wm_writer_text(&writer, "\n}\n");
    if (writer.failed) {
        wm_set_error(error, error_size, "BRLYT JSON exceeds the memory limit.");
        goto fail;
    }
    wm_normalize_commas(&writer);
    *json = writer.text;
    *json_size = writer.length;
    free(sections);
    free(panes);
    free(stack);
    return true;

fail:
    free(writer.text);
    free(sections);
    free(panes);
    free(stack);
    return false;
}

bool wm_brlyt_to_json(const uint8_t *data, size_t size, const char *name,
                      const char *package, const WmResourceTexture *textures,
                      size_t texture_count, const WmResourceAnimation *animations,
                      size_t animation_count, char **json, size_t *json_size,
                      char *error, size_t error_size) {
    return wm_brlyt_to_json_with_source(data, size, name, package, NULL, textures,
                                        texture_count, animations, animation_count,
                                        json, json_size, error, error_size);
}
