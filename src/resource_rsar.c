#include "wii_menu/resource_rsar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    WM_RSAR_MAX_SOUNDS = 65536,
    WM_RSAR_MAX_SAMPLES = 20000000
};

static void set_error(char *error, size_t capacity, const char *message)
{
    if (error && capacity) snprintf(error, capacity, "%s", message);
}

static bool fits(const WmRsar *archive, size_t offset, size_t length)
{
    return archive && offset <= archive->size &&
           length <= archive->size - offset;
}

static bool read_u32(const WmRsar *archive, size_t offset, uint32_t *value)
{
    if (!fits(archive, offset, 4)) return false;
    const uint8_t *source = archive->data + offset;
    *value = ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
             ((uint32_t)source[2] << 8) | source[3];
    return true;
}

static bool read_reference(const WmRsar *archive, size_t offset,
                           size_t base, size_t *target)
{
    uint32_t relative;
    if (!fits(archive, offset, 8) ||
        !read_u32(archive, offset + 4, &relative) ||
        relative > SIZE_MAX - base || !fits(archive, base + relative, 1)) {
        return false;
    }
    *target = base + relative;
    return true;
}

static bool table_item(const WmRsar *archive, size_t table, size_t base,
                       uint32_t index, size_t *item)
{
    uint32_t count;
    if (!read_u32(archive, table, &count) || count > WM_RSAR_MAX_SOUNDS ||
        index >= count || index > (SIZE_MAX - table - 4) / 8) {
        return false;
    }
    return read_reference(archive, table + 4 + (size_t)index * 8, base, item);
}

static bool checked_sum(size_t left, uint32_t right, size_t *result)
{
    if ((size_t)right > SIZE_MAX - left) return false;
    *result = left + right;
    return true;
}

bool wm_rsar_open(const uint8_t *data, size_t size, WmRsar *archive,
                  char *error, size_t error_capacity)
{
    set_error(error, error_capacity, "");
    if (!archive) return false;
    *archive = (WmRsar){0};
    if (!data || size < 64 || size > 128u * 1024u * 1024u ||
        memcmp(data, "RSAR\xfe\xff\x01\x01", 8) != 0) {
        set_error(error, error_capacity, "Expected bounded big-endian RSAR 1.1.");
        return false;
    }
    WmRsar parsed = {.data = data, .size = size};
    uint32_t recorded_size, symbol_offset, symbol_length;
    uint32_t info_offset, info_length;
    if (!read_u32(&parsed, 8, &recorded_size) || recorded_size != size ||
        !read_u32(&parsed, 16, &symbol_offset) ||
        !read_u32(&parsed, 20, &symbol_length) ||
        !read_u32(&parsed, 24, &info_offset) ||
        !read_u32(&parsed, 28, &info_length) ||
        !fits(&parsed, symbol_offset, symbol_length) ||
        !fits(&parsed, info_offset, info_length) ||
        symbol_length < 8 || info_length < 48 ||
        memcmp(data + symbol_offset, "SYMB", 4) != 0 ||
        memcmp(data + info_offset, "INFO", 4) != 0) {
        set_error(error, error_capacity, "Invalid RSAR block directory.");
        return false;
    }
    parsed.symbol_block = (size_t)symbol_offset + 8;
    parsed.info_block = (size_t)info_offset + 8;
    uint32_t symbol_table_relative;
    if (!read_u32(&parsed, parsed.symbol_block, &symbol_table_relative) ||
        !checked_sum(parsed.symbol_block, symbol_table_relative,
                     &parsed.symbol_table) ||
        !read_reference(&parsed, parsed.info_block, parsed.info_block,
                        &parsed.sound_table) ||
        !read_reference(&parsed, parsed.info_block + 8, parsed.info_block,
                        &parsed.bank_table) ||
        !read_reference(&parsed, parsed.info_block + 24, parsed.info_block,
                        &parsed.file_table) ||
        !read_reference(&parsed, parsed.info_block + 32, parsed.info_block,
                        &parsed.group_table) ||
        !fits(&parsed, parsed.symbol_table, 4)) {
        set_error(error, error_capacity, "Invalid RSAR symbol or INFO references.");
        return false;
    }
    uint32_t name_count, sound_count, bank_count, file_count, group_count;
    if (!read_u32(&parsed, parsed.symbol_table, &name_count) ||
        !read_u32(&parsed, parsed.sound_table, &sound_count) ||
        !read_u32(&parsed, parsed.bank_table, &bank_count) ||
        !read_u32(&parsed, parsed.file_table, &file_count) ||
        !read_u32(&parsed, parsed.group_table, &group_count) ||
        name_count > WM_RSAR_MAX_SOUNDS || sound_count > name_count ||
        bank_count > WM_RSAR_MAX_SOUNDS || file_count > WM_RSAR_MAX_SOUNDS ||
        group_count > WM_RSAR_MAX_SOUNDS ||
        !fits(&parsed, parsed.symbol_table + 4, (size_t)name_count * 4) ||
        !fits(&parsed, parsed.sound_table + 4, (size_t)sound_count * 8) ||
        !fits(&parsed, parsed.bank_table + 4, (size_t)bank_count * 8) ||
        !fits(&parsed, parsed.file_table + 4, (size_t)file_count * 8) ||
        !fits(&parsed, parsed.group_table + 4, (size_t)group_count * 8)) {
        set_error(error, error_capacity, "Invalid RSAR table counts.");
        return false;
    }
    *archive = parsed;
    return true;
}

static bool symbol_name(const WmRsar *archive, uint32_t index,
                        const char **name, size_t *length)
{
    uint32_t count, offset;
    if (!read_u32(archive, archive->symbol_table, &count) || index >= count ||
        !read_u32(archive, archive->symbol_table + 4 + (size_t)index * 4,
                  &offset)) return false;
    size_t absolute;
    if (!checked_sum(archive->symbol_block, offset, &absolute) ||
        !fits(archive, absolute, 1)) return false;
    const uint8_t *end = memchr(archive->data + absolute, 0,
                               archive->size - absolute);
    if (!end || end == archive->data + absolute) return false;
    *name = (const char *)(archive->data + absolute);
    *length = (size_t)(end - (archive->data + absolute));
    return true;
}

size_t wm_rsar_sound_count(const WmRsar *archive)
{
    uint32_t count;
    return archive && read_u32(archive, archive->sound_table, &count) ?
           count : 0;
}

bool wm_rsar_sound_at(const WmRsar *archive, size_t index,
                      char *symbol, size_t symbol_capacity,
                      WmRsarSound *sound)
{
    if (!archive || !symbol || !symbol_capacity || !sound ||
        index >= wm_rsar_sound_count(archive)) return false;
    size_t entry;
    uint32_t name_index, file_index;
    const char *name;
    size_t length;
    if (!table_item(archive, archive->sound_table, archive->info_block,
                    (uint32_t)index, &entry) || !fits(archive, entry, 32) ||
        !read_u32(archive, entry, &name_index) ||
        !read_u32(archive, entry + 4, &file_index) ||
        !symbol_name(archive, name_index, &name, &length) ||
        length >= symbol_capacity) return false;
    size_t extra;
    if (!read_reference(archive, entry + 24, archive->info_block, &extra))
        return false;
    memcpy(symbol, name, length);
    symbol[length] = '\0';
    *sound = (WmRsarSound){
        .file_index = file_index,
        .archive_index = (uint32_t)index,
        .volume = archive->data[entry + 20],
        .type = archive->data[entry + 22],
        .extra = extra
    };
    return true;
}

bool wm_rsar_find_sound(const WmRsar *archive, const char *symbol,
                        WmRsarSound *sound)
{
    if (!archive || !symbol || !sound) return false;
    size_t count = wm_rsar_sound_count(archive);
    for (size_t index = 0; index < count; index++) {
        char candidate[128];
        WmRsarSound current;
        if (!wm_rsar_sound_at(archive, index, candidate,
                              sizeof(candidate), &current)) return false;
        if (strcmp(candidate, symbol) != 0) continue;
        *sound = current;
        return true;
    }
    return false;
}

static bool file_locations(const WmRsar *archive, uint32_t file_index,
                           size_t *header, size_t *wave)
{
    size_t file_info, membership, group, item;
    uint32_t group_index, item_index, header_base, header_relative;
    uint32_t wave_base, wave_relative;
    if (!table_item(archive, archive->file_table, archive->info_block,
                    file_index, &file_info) || !fits(archive, file_info, 28) ||
        !read_reference(archive, file_info + 20, archive->info_block,
                        &membership) ||
        !table_item(archive, membership, archive->info_block, 0, &membership) ||
        !read_u32(archive, membership, &group_index) ||
        !read_u32(archive, membership + 4, &item_index) ||
        !table_item(archive, archive->group_table, archive->info_block,
                    group_index, &group) || !fits(archive, group, 40) ||
        !read_reference(archive, group + 32, archive->info_block, &item) ||
        !table_item(archive, item, archive->info_block, item_index, &item) ||
        !read_u32(archive, group + 16, &header_base) ||
        !read_u32(archive, item + 4, &header_relative) ||
        !read_u32(archive, group + 24, &wave_base) ||
        !read_u32(archive, item + 12, &wave_relative) ||
        !checked_sum(header_base, header_relative, header) ||
        !checked_sum(wave_base, wave_relative, wave) ||
        !fits(archive, *header, 32) || !fits(archive, *wave, 1)) {
        return false;
    }
    return true;
}

static bool decode_wave(const WmRsar *archive, size_t info, size_t wave_base,
                        WmAudioPcm *output, char *error, size_t error_capacity)
{
    if (!fits(archive, info, 24)) {
        set_error(error, error_capacity, "Truncated RSAR wave metadata.");
        return false;
    }
    uint8_t codec = archive->data[info];
    uint8_t looping = archive->data[info + 1];
    uint8_t channels = archive->data[info + 2];
    uint32_t rate = ((uint32_t)archive->data[info + 3] << 16) |
                    ((uint32_t)archive->data[info + 4] << 8) |
                    archive->data[info + 5];
    uint32_t first_address, last_address, channel_table_relative, data_relative;
    if (!read_u32(archive, info + 8, &first_address) ||
        !read_u32(archive, info + 12, &last_address) ||
        !read_u32(archive, info + 16, &channel_table_relative) ||
        !read_u32(archive, info + 20, &data_relative)) return false;
    int64_t frames = codec == 2
                         ? (int64_t)(last_address / 16) * 14 +
                               (last_address % 16) - 1
                         : (int64_t)last_address + 1;
    if ((codec != 0 && codec != 1 && codec != 2) || looping > 1 ||
        (channels != 1 && channels != 2) || rate == 0 || rate > 192000 ||
        frames <= 0 || frames > WM_RSAR_MAX_SAMPLES ||
        (size_t)frames > SIZE_MAX / (sizeof(int16_t) * channels)) {
        set_error(error, error_capacity, "Unsupported or excessive RSAR wave.");
        return false;
    }
    size_t channel_table, data_base;
    if (!checked_sum(info, channel_table_relative, &channel_table) ||
        !checked_sum(wave_base, data_relative, &data_base) ||
        !fits(archive, channel_table, (size_t)channels * 4)) {
        set_error(error, error_capacity, "Invalid RSAR wave channel table.");
        return false;
    }
    int16_t *pcm = malloc((size_t)frames * channels * sizeof(*pcm));
    if (!pcm) {
        set_error(error, error_capacity, "Out of memory decoding RSAR wave.");
        return false;
    }
    bool valid = true;
    for (uint8_t channel = 0; channel < channels && valid; channel++) {
        uint32_t meta_relative, sample_relative;
        size_t meta, sample;
        if (!read_u32(archive, channel_table + (size_t)channel * 4,
                      &meta_relative) ||
            !checked_sum(info, meta_relative, &meta) ||
            !fits(archive, meta, 8) ||
            !read_u32(archive, meta, &sample_relative) ||
            !checked_sum(data_base, sample_relative, &sample)) {
            valid = false;
            break;
        }
        size_t encoded_bytes = codec == 2
                                   ? (((size_t)frames + 13) / 14) * 8
                                   : (size_t)frames * (codec == 1 ? 2 : 1);
        if (!fits(archive, sample, encoded_bytes)) {
            valid = false;
            break;
        }
        if (codec == 2) {
            uint32_t context_relative;
            size_t context;
            if (!read_u32(archive, meta + 4, &context_relative) ||
                !checked_sum(info, context_relative, &context) ||
                !fits(archive, context, 40)) {
                valid = false;
                break;
            }
            int16_t coefficients[16];
            for (size_t index = 0; index < 16; index++) {
                const uint8_t *source = archive->data + context + index * 2;
                coefficients[index] = (int16_t)(((uint16_t)source[0] << 8) |
                                                source[1]);
            }
            const uint8_t *history = archive->data + context + 36;
            int16_t history1 = (int16_t)(((uint16_t)history[0] << 8) | history[1]);
            int16_t history2 = (int16_t)(((uint16_t)history[2] << 8) | history[3]);
            int16_t *single = malloc((size_t)frames * sizeof(*single));
            if (!single || !wm_dsp_decode(archive->data + sample, encoded_bytes,
                                          (uint32_t)frames, coefficients,
                                          history1, history2, single,
                                          error, error_capacity)) {
                free(single);
                valid = false;
                break;
            }
            for (size_t frame = 0; frame < (size_t)frames; frame++) {
                pcm[frame * channels + channel] = single[frame];
            }
            free(single);
        } else {
            for (size_t frame = 0; frame < (size_t)frames; frame++) {
                const uint8_t *source = archive->data + sample +
                                        frame * (codec == 1 ? 2u : 1u);
                pcm[frame * channels + channel] = codec == 1
                    ? (int16_t)(((uint16_t)source[0] << 8) | source[1])
                    : (int16_t)((int16_t)(int8_t)source[0] * 256);
            }
        }
    }
    if (!valid) {
        free(pcm);
        set_error(error, error_capacity, "Invalid or truncated RSAR wave data.");
        return false;
    }
    output->samples = pcm;
    output->sample_rate = rate;
    output->frame_count = (uint32_t)frames;
    output->channels = channels;
    output->looping = looping != 0;
    output->loop_end = (uint32_t)frames;
    if (looping) {
        int64_t start = codec == 2
                            ? (int64_t)(first_address / 16) * 14 +
                                  first_address % 16 - 2
                            : first_address;
        output->loop_start = start >= 0 && start < frames ?
                             (uint32_t)start : 0;
    }
    return true;
}

bool wm_rsar_decode_direct_wave(const WmRsar *archive,
                                const WmRsarSound *sound, WmAudioPcm *output,
                                char *error, size_t error_capacity)
{
    if (!archive || !sound || !output || sound->type != 3) {
        set_error(error, error_capacity, "Expected direct-wave RSAR sound.");
        return false;
    }
    *output = (WmAudioPcm){0};
    size_t header, wave_base;
    uint32_t data_relative, wave_relative, note_index, sound_index;
    size_t data, wsd, note_table, note, wave_table, info;
    if (!file_locations(archive, sound->file_index, &header, &wave_base) ||
        !read_u32(archive, header + 16, &data_relative) ||
        !checked_sum(header, data_relative + 8u, &data) ||
        !read_u32(archive, sound->extra, &sound_index) ||
        !table_item(archive, data, data, sound_index, &wsd) ||
        !read_reference(archive, wsd + 16, data, &note_table) ||
        !table_item(archive, note_table, data, 0, &note) ||
        !read_u32(archive, note, &note_index) ||
        !read_u32(archive, header + 24, &wave_relative) ||
        !checked_sum(header, wave_relative, &wave_table) ||
        note_index > (SIZE_MAX - wave_table - 12) / 4 ||
        !read_u32(archive, wave_table + 12 + (size_t)note_index * 4,
                  &wave_relative) ||
        !checked_sum(wave_table, wave_relative, &info)) {
        set_error(error, error_capacity, "Invalid RSAR direct-wave references.");
        return false;
    }
    return decode_wave(archive, info, wave_base, output,
                       error, error_capacity);
}

bool wm_rsar_get_sequence(const WmRsar *archive, const WmRsarSound *sound,
                          WmRsarSequence *sequence,
                          char *error, size_t error_capacity)
{
    if (!archive || !sound || !sequence || sound->type != 1) {
        set_error(error, error_capacity, "Expected sequenced RSAR sound.");
        return false;
    }
    size_t header, wave, data, base;
    uint32_t data_relative, base_relative, start_offset, bank_index;
    uint32_t file_size;
    if (!file_locations(archive, sound->file_index, &header, &wave) ||
        memcmp(archive->data + header, "RSEQ", 4) != 0 ||
        !read_u32(archive, header + 8, &file_size) ||
        !fits(archive, header, file_size) || file_size < 44 ||
        !read_u32(archive, header + 16, &data_relative) ||
        !checked_sum(header, data_relative, &data) ||
        !read_u32(archive, data + 8, &base_relative) ||
        !checked_sum(data, base_relative, &base) ||
        !read_u32(archive, sound->extra, &start_offset) ||
        !read_u32(archive, sound->extra + 4, &bank_index) ||
        base >= header + file_size ||
        (size_t)start_offset >= header + file_size - base) {
        set_error(error, error_capacity, "Invalid RSAR sequence references.");
        return false;
    }
    *sequence = (WmRsarSequence){
        .data = archive->data + base,
        .size = header + file_size - base,
        .start_offset = start_offset,
        .bank_index = bank_index
    };
    return true;
}

static bool bank_locations(const WmRsar *archive, uint32_t bank_index,
                           size_t *header, size_t *wave)
{
    size_t entry;
    uint32_t file_index;
    return table_item(archive, archive->bank_table, archive->info_block,
                      bank_index, &entry) &&
           read_u32(archive, entry + 4, &file_index) &&
           file_locations(archive, file_index, header, wave);
}

bool wm_rsar_decode_bank_wave(const WmRsar *archive, uint32_t bank_index,
                              uint32_t wave_index, WmAudioPcm *output,
                              char *error, size_t error_capacity)
{
    if (!archive || !output) return false;
    *output = (WmAudioPcm){0};
    size_t header, wave_base, table, info;
    uint32_t relative;
    if (!bank_locations(archive, bank_index, &header, &wave_base) ||
        !read_u32(archive, header + 24, &relative) ||
        !checked_sum(header, relative + 8u, &table) ||
        wave_index > (SIZE_MAX - table - 4) / 8 ||
        !read_reference(archive, table + 4 + (size_t)wave_index * 8,
                        table, &info)) {
        set_error(error, error_capacity, "Invalid RSAR bank wave index.");
        return false;
    }
    return decode_wave(archive, info, wave_base, output,
                       error, error_capacity);
}

bool wm_rsar_get_instrument(const WmRsar *archive, uint32_t bank_index,
                            uint32_t program, uint8_t key, uint8_t velocity,
                            WmRsarInstrument *instrument,
                            char *error, size_t error_capacity)
{
    if (!archive || !instrument || program > 1024) return false;
    size_t header, wave, bank_base, reference, region;
    uint32_t relative;
    if (!bank_locations(archive, bank_index, &header, &wave) ||
        !read_u32(archive, header + 16, &relative) ||
        !checked_sum(header, relative + 8u, &bank_base) ||
        program > (SIZE_MAX - bank_base - 4) / 8) return false;
    reference = bank_base + 4 + (size_t)program * 8;
    for (unsigned dimension = 0; dimension < 3; dimension++) {
        if (!fits(archive, reference, 8) ||
            !read_reference(archive, reference, bank_base, &region)) break;
        uint8_t kind = archive->data[reference + 1];
        if (kind == 1) {
            uint32_t bits;
            if (!fits(archive, region, 20) ||
                !read_u32(archive, region, &instrument->wave_index) ||
                !read_u32(archive, region + 16, &bits)) break;
            memcpy(instrument->envelope, archive->data + region + 4, 4);
            instrument->root_key = archive->data[region + 12];
            instrument->volume = archive->data[region + 13];
            instrument->pan = archive->data[region + 14];
            memcpy(&instrument->pitch, &bits, sizeof(instrument->pitch));
            return true;
        }
        uint8_t selected = dimension == 0 ? key : velocity;
        if (kind == 2) {
            if (!fits(archive, region, 1)) break;
            uint8_t count = archive->data[region];
            if (!count || !fits(archive, region + 1, count)) break;
            uint8_t slot = 0;
            while (slot < count && selected > archive->data[region + 1 + slot])
                slot++;
            if (slot == count) break;
            reference = region + ((size_t)count + 4) / 4 * 4 +
                        (size_t)slot * 8;
        } else if (kind == 3) {
            if (!fits(archive, region, 2)) break;
            uint8_t low = archive->data[region];
            uint8_t high = archive->data[region + 1];
            if (selected < low || selected > high) break;
            reference = region + 4 + (size_t)(selected - low) * 8;
        } else {
            break;
        }
    }
    set_error(error, error_capacity, "Unsupported RSAR instrument region.");
    return false;
}

bool wm_rsar_resolve_instrument(const WmRsar *archive, uint32_t bank_index,
                                uint32_t program, uint8_t key,
                                uint8_t velocity, uint32_t *wave_index,
                                uint8_t *root_key, float *pitch,
                                char *error, size_t error_capacity)
{
    if (!wave_index || !root_key || !pitch) return false;
    WmRsarInstrument instrument;
    if (!wm_rsar_get_instrument(archive, bank_index, program, key, velocity,
                                &instrument, error, error_capacity)) return false;
    *wave_index = instrument.wave_index;
    *root_key = instrument.root_key;
    *pitch = instrument.pitch;
    return true;
}
