#include "resource_layout_internal.h"

#include <stdlib.h>
#include <string.h>

static bool wm_writer_animation_target(WmJsonWriter *writer, const uint8_t *block,
                                       size_t size, size_t base, unsigned depth) {
    if (!wm_range(size, base, 24)) {
        return false;
    }
    const uint8_t *name;
    size_t name_length;
    if (!wm_fixed_string(block, size, base, 20, &name, &name_length)) {
        return false;
    }
    size_t tag_count = block[base + 20];
    if (tag_count > WM_RESOURCE_MAX_ITEMS ||
        !wm_range(size, base + 24, tag_count * 4)) {
        return false;
    }

    wm_writer_text(writer, "{\"name\": ");
    wm_writer_string(writer, name, name_length);
    wm_writer_format(writer, ", \"type\": %u, \"tracks\": [",
                     (unsigned)block[base + 21]);
    bool first_track = true;
    for (size_t tag_index = 0; tag_index < tag_count; tag_index++) {
        size_t relative = wm_be32(block + base + 24 + tag_index * 4);
        if (relative > size - base) {
            return false;
        }
        size_t tag = base + relative;
        if (!wm_range(size, tag, 8)) {
            return false;
        }
        size_t track_count = block[tag + 4];
        if (!wm_range(size, tag + 8, track_count * 4)) {
            return false;
        }
        for (size_t track_index = 0; track_index < track_count; track_index++) {
            size_t track_relative = wm_be32(block + tag + 8 + track_index * 4);
            if (track_relative > size - tag) {
                return false;
            }
            size_t track = tag + track_relative;
            if (!wm_range(size, track, 12)) {
                return false;
            }
            uint8_t curve = block[track + 2];
            size_t key_count = wm_be16(block + track + 4);
            size_t key_offset = wm_be32(block + track + 8);
            size_t stride = curve == 2 ? 12 : 8;
            if (key_count > WM_RESOURCE_MAX_ITEMS || key_offset > size - track ||
                !wm_range(size, track + key_offset, key_count * stride)) {
                return false;
            }
            if (!first_track) {
                wm_writer_text(writer, ",");
            }
            wm_writer_indent(writer, depth + 1);
            wm_writer_text(writer, "{\"kind\": ");
            wm_writer_string(writer, block + tag, 4);
            wm_writer_format(
                writer, ", \"id\": %u, \"target\": %u, \"curveType\": %u, \"keys\": [",
                (unsigned)block[track], (unsigned)block[track + 1], (unsigned)curve);
            for (size_t key_index = 0; key_index < key_count; key_index++) {
                size_t key = track + key_offset + key_index * stride;
                float frame;
                if (!wm_float(block, size, key, &frame)) {
                    return false;
                }
                if (key_index != 0) {
                    wm_writer_text(writer, ", ");
                }
                wm_writer_text(writer, "{\"frame\": ");
                wm_writer_float(writer, frame);
                wm_writer_text(writer, ", \"value\": ");
                if (curve == 2) {
                    float value;
                    float slope;
                    if (!wm_float(block, size, key + 4, &value) ||
                        !wm_float(block, size, key + 8, &slope)) {
                        return false;
                    }
                    wm_writer_float(writer, value);
                    wm_writer_text(writer, ", \"slope\": ");
                    wm_writer_float(writer, slope);
                } else {
                    wm_writer_format(writer, "%u", (unsigned)wm_be16(block + key + 4));
                }
                wm_writer_text(writer, "}");
            }
            wm_writer_text(writer, "]}");
            first_track = false;
        }
    }
    if (!first_track) {
        wm_writer_indent(writer, depth);
    }
    wm_writer_text(writer, "]}");
    return !writer->failed;
}

bool wm_brlan_to_json(const uint8_t *data, size_t size, char **json, size_t *json_size,
                      char *error, size_t error_size) {
    if (json == NULL || json_size == NULL) {
        wm_set_error(error, error_size, "Invalid BRLAN output.");
        return false;
    }
    *json = NULL;
    *json_size = 0;

    WmSection *sections = calloc(WM_RESOURCE_MAX_SECTIONS, sizeof(*sections));
    if (sections == NULL) {
        wm_set_error(error, error_size, "Out of memory reading BRLAN sections.");
        return false;
    }
    size_t section_count = 0;
    if (!wm_read_sections(data, size, "RLAN", sections, &section_count)) {
        wm_set_error(error, error_size, "Invalid BRLAN section table.");
        free(sections);
        return false;
    }

    const WmSection *last_pai = NULL;
    for (size_t index = 0; index < section_count; index++) {
        if (strcmp(sections[index].kind, "pai1") == 0) {
            last_pai = &sections[index];
        }
    }
    WmJsonWriter writer = {0};
    unsigned frames = 0;
    bool loop = false;
    if (last_pai != NULL) {
        if (!wm_range(last_pai->size, 8, 12)) {
            wm_set_error(error, error_size, "Truncated BRLAN animation header.");
            free(sections);
            return false;
        }
        frames = wm_be16(last_pai->bytes + 8);
        loop = last_pai->bytes[10] != 0;
    }
    wm_writer_format(&writer,
                     "{\n  \"frames\": %u,\n  \"loop\": %s,\n  \"textures\": [", frames,
                     loop ? "true" : "false");
    if (last_pai != NULL) {
        const uint8_t *block = last_pai->bytes;
        size_t file_count = wm_be16(block + 12);
        if (file_count > WM_RESOURCE_MAX_ITEMS ||
            !wm_range(last_pai->size, 20, file_count * 4)) {
            wm_set_error(error, error_size, "Invalid BRLAN texture table.");
            goto fail;
        }
        for (size_t index = 0; index < file_count; index++) {
            size_t relative = wm_be32(block + 20 + index * 4);
            if (relative > last_pai->size - 20) {
                wm_set_error(error, error_size, "Invalid BRLAN texture name offset.");
                goto fail;
            }
            const uint8_t *name;
            size_t name_length;
            if (!wm_cstring(block, last_pai->size, 20 + relative, &name,
                            &name_length)) {
                wm_set_error(error, error_size, "Invalid BRLAN texture name.");
                goto fail;
            }
            if (index != 0) {
                wm_writer_text(&writer, ", ");
            }
            wm_writer_string(&writer, name, name_length);
        }
    }
    wm_writer_text(&writer, "],\n  \"targets\": [");

    bool first_target = true;
    for (size_t section_index = 0; section_index < section_count; section_index++) {
        const WmSection *section = &sections[section_index];
        if (strcmp(section->kind, "pai1") != 0) {
            continue;
        }
        const uint8_t *block = section->bytes;
        if (!wm_range(section->size, 8, 12)) {
            wm_set_error(error, error_size, "Truncated BRLAN animation header.");
            goto fail;
        }
        size_t target_count = wm_be16(block + 14);
        size_t table = wm_be32(block + 16);
        if (target_count > WM_RESOURCE_MAX_ITEMS ||
            !wm_range(section->size, table, target_count * 4)) {
            wm_set_error(error, error_size, "Invalid BRLAN target table.");
            goto fail;
        }
        for (size_t target_index = 0; target_index < target_count; target_index++) {
            size_t base = wm_be32(block + table + target_index * 4);
            if (!first_target) {
                wm_writer_text(&writer, ",");
            }
            wm_writer_indent(&writer, 2);
            if (!wm_writer_animation_target(&writer, block, section->size, base, 2)) {
                wm_set_error(error, error_size, "Invalid BRLAN target or key track.");
                goto fail;
            }
            first_target = false;
        }
    }
    if (!first_target) {
        wm_writer_indent(&writer, 1);
    }
    wm_writer_text(&writer, "]\n}\n");
    if (writer.failed) {
        wm_set_error(error, error_size, "BRLAN JSON exceeds the memory limit.");
        goto fail;
    }
    wm_normalize_commas(&writer);
    *json = writer.text;
    *json_size = writer.length;
    free(sections);
    return true;

fail:
    free(writer.text);
    free(sections);
    return false;
}
