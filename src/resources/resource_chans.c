#include "wii_menu/resources/resource_chans.h"

#include "wii_menu/support/bounds.h"
#include "wii_menu/support/endian.h"
#include "wii_menu/support/error.h"

#include <stdlib.h>
#include <string.h>

enum { CHANS_MAX_SYMBOLS = 256, CHANS_MAX_STACK = 128, CHANS_STEP_LIMIT = 8192 };

typedef enum ValueKind {
    VALUE_NUMBER,
    VALUE_STRING,
    VALUE_SYMBOL,
    VALUE_PANE,
    VALUE_DEBUG
} ValueKind;

typedef struct Value {
    ValueKind kind;
    int number;
    const char *text;
} Value;

typedef struct Script {
    const uint8_t *bytes;
    size_t size;
    const uint8_t *code;
    size_t code_size;
    char symbols[CHANS_MAX_SYMBOLS][128];
    char members[CHANS_MAX_SYMBOLS][128];
    char strings[CHANS_MAX_SYMBOLS][WM_CHANS_TEXT_BYTES];
    size_t symbol_count;
    size_t member_count;
    size_t string_count;
    Value globals[CHANS_MAX_SYMBOLS];
    size_t setup_start;
    size_t title_start;
    size_t title_end;
} Script;

static bool span(const Script *script, size_t offset, size_t length) {
    return wm_bounds_contains(script->size, offset, length);
}

static bool table_offset(const Script *script, size_t field, size_t *offset) {
    if (!span(script, field, 4))
        return false;
    uint32_t relative = wm_read_be32(script->bytes + field);
    if (relative > script->size || script->size - relative < 0x20)
        return false;
    *offset = (size_t)relative + 0x20;
    return true;
}

static bool read_names(const Script *script, size_t field, size_t count,
                       char names[][128]) {
    size_t table;
    if (!table_offset(script, field, &table) || !span(script, table, count * 4))
        return false;
    for (size_t index = 0; index < count; index++) {
        uint32_t entry = wm_read_be32(script->bytes + table + index * 4);
        size_t length = entry >> 24;
        size_t relative = entry & 0xffffff;
        if (length >= 128 || relative > script->size - table ||
            !span(script, table + relative, length))
            return false;
        const uint8_t *name = script->bytes + table + relative;
        for (size_t byte = 0; byte < length; byte++) {
            if (name[byte] < 0x20 || name[byte] > 0x7e)
                return false;
        }
        memcpy(names[index], name, length);
        names[index][length] = '\0';
    }
    return true;
}

static bool decode_utf16(const uint8_t *bytes, size_t length, char *output) {
    size_t used = 0;
    if (length % 2)
        return false;
    for (size_t offset = 0; offset < length; offset += 2) {
        uint32_t scalar = wm_read_be16(bytes + offset);
        if (!scalar)
            return false;
        if (scalar >= 0xd800 && scalar <= 0xdbff) {
            if (length - offset < 4)
                return false;
            uint32_t low = wm_read_be16(bytes + offset + 2);
            if (low < 0xdc00 || low > 0xdfff)
                return false;
            scalar = 0x10000 + ((scalar - 0xd800) << 10) + low - 0xdc00;
            offset += 2;
        } else if (scalar >= 0xdc00 && scalar <= 0xdfff) {
            return false;
        }
        unsigned count = scalar < 0x80      ? 1
                         : scalar < 0x800   ? 2
                         : scalar < 0x10000 ? 3
                                            : 4;
        if (used + count >= WM_CHANS_TEXT_BYTES)
            return false;
        if (count == 1) {
            output[used++] = (char)scalar;
        } else {
            output[used++] =
                (char)((0xffu << (8 - count)) | (scalar >> (6 * (count - 1))));
            for (unsigned part = count - 1; part > 0; part--)
                output[used++] = (char)(0x80 | ((scalar >> (6 * (part - 1))) & 0x3f));
        }
    }
    output[used] = '\0';
    return true;
}

static bool read_strings(Script *script) {
    size_t cursor;
    if (!table_offset(script, 0x54, &cursor))
        return false;
    for (size_t index = 0; index < script->string_count; index++) {
        if (!span(script, cursor, 2))
            return false;
        size_t length = wm_read_be16(script->bytes + cursor);
        cursor += 2;
        if (!span(script, cursor, length) ||
            !decode_utf16(script->bytes + cursor, length, script->strings[index]))
            return false;
        cursor += length;
    }
    return true;
}

static bool read_methods(Script *script) {
    size_t table;
    size_t count = wm_read_be32(script->bytes + 0x40);
    if (count > CHANS_MAX_SYMBOLS || !table_offset(script, 0x44, &table) ||
        !span(script, table, count * 8))
        return false;
    script->setup_start = script->title_start = script->code_size;
    script->title_end = script->code_size;
    for (size_t index = 0; index < count; index++) {
        const uint8_t *entry = script->bytes + table + index * 8;
        size_t start = wm_read_be32(entry);
        size_t symbol = wm_read_be16(entry + 4);
        if (start >= script->code_size || symbol >= script->symbol_count)
            return false;
        if (strcmp(script->symbols[symbol], "setRegionPanes") == 0 && entry[6] == 1)
            script->setup_start = start;
        if (strcmp(script->symbols[symbol], "setTitle") == 0 && entry[6] == 4) {
            script->title_start = start;
            if (index + 1 < count)
                script->title_end = wm_read_be32(entry + 8);
        }
    }
    return script->setup_start < script->title_start &&
           script->title_start < script->title_end &&
           script->title_end <= script->code_size;
}

/* Verify the recovered TextWriter recipe before replacing it with the runtime
 * font measurement callback. No text, font sizes or pane names live in C. */
static bool title_recipe(const Script *script) {
    static const char *const calls[] = {
        "GetFont",      "SetFont",      "SetFontSize",  "GetLineSpace",
        "SetLineSpace", "GetCharSpace", "SetCharSpace", "CalcStringRect",
        "SetFontSize",  "GetWidth",     "GetHeight",    "SetSize",
        "SetString",    "GetString"};
    static const uint8_t argument_counts[] = {0, 1, 2, 0, 1, 0, 1, 2, 2, 0, 0, 2, 1, 0};
    size_t found = 0;
    for (size_t pc = script->title_start; pc < script->title_end;) {
        uint8_t opcode = script->code[pc++];
        size_t operands = opcode == 0x30                                       ? 3
                          : opcode == 0x2c                                     ? 2
                          : opcode == 0x02 || opcode == 0x31 || opcode >= 0x40 ? 1
                                                                               : 0;
        if (operands > script->title_end - pc)
            return false;
        if (opcode == 0x30) {
            size_t member = wm_read_be16(script->code + pc);
            if (member >= script->member_count ||
                found >= sizeof(calls) / sizeof(*calls) ||
                strcmp(script->members[member], calls[found]) != 0 ||
                script->code[pc + 2] != argument_counts[found])
                return false;
            found++;
        } else if (opcode != 0 && opcode != 0x02 && opcode != 0x04 && opcode != 0x06 &&
                   opcode != 0x2c && opcode != 0x31 &&
                   !(opcode >= 0x40 && opcode < 0x80)) {
            return false;
        }
        pc += operands;
    }
    return found == sizeof(calls) / sizeof(*calls);
}

static bool initialize_globals(Script *script) {
    Value value = {0};
    for (size_t index = 0; index < script->symbol_count; index++)
        script->globals[index] =
            (Value){VALUE_SYMBOL, (int)index, script->symbols[index]};
    /* RCHE reserves byte zero; entryPoint begins at byte one. Stop before
     * the service-dependent entryPoint, after its constant initializers. */
    size_t assignments = 0;
    for (size_t pc = 1; pc < script->setup_start;) {
        uint8_t opcode = script->code[pc++];
        if (opcode == 0x18 && pc < script->code_size) {
            value = (Value){.number = (int8_t)script->code[pc++]};
        } else if (opcode == 0x2c && script->code_size - pc >= 2) {
            size_t literal = wm_read_be16(script->code + pc);
            pc += 2;
            if (literal >= script->string_count)
                return false;
            value = (Value){VALUE_STRING, 0, script->strings[literal]};
        } else if (opcode >= 0x60 && opcode < 0x70 && pc < script->code_size) {
            size_t symbol = (size_t)(opcode & 15) * 256 + script->code[pc++];
            if (symbol >= script->symbol_count)
                return false;
            script->globals[symbol] = value;
            assignments++;
        } else {
            return assignments > 0;
        }
    }
    return false;
}

static bool append_text(WmChansLanguages *result, const char *language,
                        const Value *args) {
    if (result->count == WM_CHANS_MAX_TEXTS || args[0].kind != VALUE_PANE ||
        args[1].kind != VALUE_NUMBER || args[2].kind != VALUE_NUMBER ||
        args[3].kind != VALUE_STRING || args[1].number <= 0 || args[1].number > 256 ||
        args[2].number <= 0 || args[2].number > 256)
        return false;
    for (size_t index = 0; index < result->count; index++) {
        if (strcmp(result->texts[index].language, language) == 0 &&
            strcmp(result->texts[index].pane, args[0].text) == 0)
            return false;
    }
    WmChansText *text = &result->texts[result->count++];
    if (strlen(args[0].text) >= sizeof(text->pane))
        return false;
    strcpy(text->language, language);
    strcpy(text->pane, args[0].text);
    strcpy(text->text, args[3].text);
    text->font_size[0] = (float)args[1].number;
    text->font_size[1] = (float)args[2].number;
    return true;
}

static bool call_member(const char *name, unsigned count, Value *value,
                        const Value *args, int language) {
    if (strcmp(name, "SCGetLanguage") == 0 && count == 0 &&
        value->kind == VALUE_SYMBOL && strcmp(value->text, "System") == 0) {
        *value = (Value){.number = language};
        return true;
    }
    if (strcmp(name, "FindPaneByName") == 0 && count == 1 &&
        args[0].kind == VALUE_STRING) {
        *value = (Value){VALUE_PANE, 1, args[0].text};
        return true;
    }
    return count == 0 && value->kind == VALUE_PANE &&
           (strcmp(name, "show") == 0 || strcmp(name, "hide") == 0);
}

static bool run_setup(const Script *script, const char *language, int language_number,
                      WmChansLanguages *result) {
    Value temporaries[32] = {{0}};
    Value stack[CHANS_MAX_STACK];
    size_t depth = 0;
    Value value = {0};
    temporaries[0] = (Value){VALUE_SYMBOL, 0, "Layout"};
    size_t pc = script->setup_start;
    for (unsigned step = 0; step < CHANS_STEP_LIMIT && pc < script->title_start;
         step++) {
        size_t begin = pc;
        uint8_t opcode = script->code[pc++];
        if (!opcode)
            return true;
        if (opcode == 4) {
            if (depth == CHANS_MAX_STACK)
                return false;
            stack[depth++] = value;
            continue;
        }
        if (opcode == 5 || opcode == 6 || opcode == 0x0b) {
            if (!depth)
                return false;
            Value left = stack[--depth];
            if (opcode == 5)
                value = left;
            else if (opcode == 0x0b)
                value = (Value){.number = left.number & value.number};
            else
                value = (Value){VALUE_DEBUG, 0, ""}; /* Debug concatenation only. */
            continue;
        }
        if (pc >= script->title_start)
            return false;
        if (opcode == 0x18) {
            value = (Value){.number = (int8_t)script->code[pc++]};
        } else if (opcode == 0x2c || opcode == 0x30 || opcode == 0x32) {
            if (script->title_start - pc < (opcode == 0x30 ? 3u : 2u))
                return false;
            size_t index = wm_read_be16(script->code + pc);
            pc += 2;
            if (opcode == 0x2c) {
                if (index >= script->string_count)
                    return false;
                value = (Value){VALUE_STRING, 0, script->strings[index]};
            } else if (index >= script->member_count) {
                return false;
            } else if (opcode == 0x32) {
                const char *name = script->members[index];
                int number = strcmp(name, "SC_LANG_ENGLISH") == 0   ? 1
                             : strcmp(name, "SC_LANG_FRENCH") == 0  ? 2
                             : strcmp(name, "SC_LANG_SPANISH") == 0 ? 3
                                                                    : 0;
                if (!number || value.kind != VALUE_SYMBOL ||
                    strcmp(value.text, "System"))
                    return false;
                value = (Value){.number = number};
            } else {
                unsigned count = script->code[pc++];
                if (count > depth ||
                    !call_member(script->members[index], count, &value,
                                 stack + depth - count, language_number))
                    return false;
                depth -= count;
            }
        } else if (opcode == 0x31) {
            unsigned count = script->code[pc++];
            if (count > depth || value.kind != VALUE_SYMBOL)
                return false;
            if (strcmp(value.text, "setTitle") == 0) {
                if (count != 4 || !append_text(result, language, stack + depth - count))
                    return false;
            } else if (strcmp(value.text, "debugPrint") != 0 || count != 1) {
                return false;
            }
            depth -= count;
        } else if (opcode >= 0x40 && opcode < 0x80) {
            size_t index = (size_t)(opcode & 15) * 256 + script->code[pc++];
            if (opcode < 0x50) {
                if (index >= script->symbol_count)
                    return false;
                value = script->globals[index];
                if (value.kind == VALUE_SYMBOL && strcmp(value.text, "flagEULA") == 0)
                    value = (Value){.number = 1};
            } else {
                if (index > 0xfff || index < 0xfff - 31 ||
                    (opcode >= 0x60 && opcode < 0x70))
                    return false;
                if (opcode < 0x60)
                    value = temporaries[0xfff - index];
                else
                    temporaries[0xfff - index] = value;
            }
        } else if (opcode >= 0x80) {
            size_t distance = (size_t)(opcode & 31) * 256 + script->code[pc++];
            bool take = opcode >= 0xe0 ||
                        (opcode >= 0xa0 && opcode < 0xc0 && !value.number) ||
                        (opcode >= 0xc0 && opcode < 0xe0 && value.number);
            if (opcode < 0xa0) {
                if (!depth)
                    return false;
                take = stack[depth - 1].number == value.number;
                if (take)
                    depth--;
            }
            if (take) {
                ptrdiff_t target =
                    distance < 0x1000 ? (ptrdiff_t)(pc + distance)
                                      : (ptrdiff_t)begin + (ptrdiff_t)distance - 0x1fff;
                if (target < (ptrdiff_t)script->setup_start ||
                    target >= (ptrdiff_t)script->title_start)
                    return false;
                pc = (size_t)target;
            }
        } else {
            return false;
        }
    }
    return false;
}

bool wm_chans_languages_decode(const uint8_t *data, size_t size,
                               WmChansLanguages *result, char *error,
                               size_t error_capacity) {
    wm_error_set(error, error_capacity,
                 "Invalid or unsupported channel language script");
    if (!data || !result || size < 0x80 || size > 1024 * 1024 ||
        memcmp(data, "RCHE\x03\0\x01\0", 8) || wm_read_be32(data + 8) != size)
        return false;
    Script *script = calloc(1, sizeof(*script));
    WmChansLanguages *decoded = calloc(1, sizeof(*decoded));
    bool valid = false;
    if (!script || !decoded)
        goto release_script;
    script->bytes = data;
    script->size = size;
    script->symbol_count = wm_read_be32(data + 0x34);
    script->member_count = wm_read_be32(data + 0x48);
    script->string_count = wm_read_be32(data + 0x50);
    script->code_size = wm_read_be32(data + 0x2c);
    size_t code_offset;
    if (script->symbol_count > CHANS_MAX_SYMBOLS ||
        script->member_count > CHANS_MAX_SYMBOLS ||
        script->string_count > CHANS_MAX_SYMBOLS ||
        !table_offset(script, 0x30, &code_offset) ||
        !span(script, code_offset, script->code_size))
        goto release_script;
    script->code = data + code_offset;
    valid = read_names(script, 0x60, script->symbol_count, script->symbols) &&
            read_names(script, 0x4c, script->member_count, script->members) &&
            read_strings(script) && read_methods(script) && title_recipe(script) &&
            initialize_globals(script) && run_setup(script, "ENG", 1, decoded) &&
            run_setup(script, "FRA", 2, decoded) &&
            run_setup(script, "SPA", 3, decoded) && decoded->count > 0;
    if (valid) {
        *result = *decoded;
        wm_error_set(error, error_capacity, "");
    }
release_script:
    free(script);
    free(decoded);
    return valid;
}
