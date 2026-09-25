#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1

#include "crypto.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define WM_MAX_CONTENTS 4096
#define WM_PATH_SIZE 4096

typedef enum WmSectionKind {
    WM_CERTIFICATES,
    WM_CRL,
    WM_TICKET,
    WM_TMD,
    WM_DATA,
    WM_FOOTER,
    WM_SECTION_COUNT
} WmSectionKind;

typedef struct WmSection {
    size_t offset;
    size_t size;
} WmSection;

typedef struct WmContent {
    uint32_t id;
    uint16_t index;
    uint16_t type;
    uint64_t size;
    uint8_t sha1[20];
    size_t encrypted_offset;
    size_t encrypted_size;
} WmContent;

typedef struct WmWad {
    uint8_t *bytes;
    size_t size;
    WmSection sections[WM_SECTION_COUNT];
    size_t ticket_body;
    size_t tmd_body;
    uint8_t title_id[8];
    uint16_t version;
    uint16_t boot_index;
    uint16_t content_count;
    uint8_t key_index;
    WmContent *contents;
} WmWad;

typedef struct WmOptions {
    const char *wad_path;
    const char *key_path;
    unsigned expected_key_index;
    bool verify_only;
} WmOptions;

static uint16_t read_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static uint64_t read_be64(const uint8_t *bytes)
{
    return ((uint64_t)read_be32(bytes) << 32) | read_be32(bytes + 4);
}

static bool aligned_size(size_t size, size_t alignment, size_t *aligned)
{
    if (size > SIZE_MAX - (alignment - 1)) {
        return false;
    }
    *aligned = (size + alignment - 1) & ~(alignment - 1);
    return true;
}

static bool slice_fits(size_t offset, size_t length, size_t total)
{
    return offset <= total && length <= total - offset;
}

static void wipe(void *memory, size_t length)
{
    volatile uint8_t *bytes = memory;
    while (length--) {
        *bytes++ = 0;
    }
}

static bool read_file(const char *path, uint8_t **bytes, size_t *size)
{
    struct stat information;
    int file = open(path, O_RDONLY);
    if (file < 0) {
        fputs("Cannot open WAD or key file.\n", stderr);
        return false;
    }
    if (fstat(file, &information) != 0 || information.st_size < 0 ||
        (uintmax_t)information.st_size > SIZE_MAX) {
        fputs("Invalid input file size.\n", stderr);
        close(file);
        return false;
    }
    size_t length = (size_t)information.st_size;
    uint8_t *data = malloc(length ? length : 1);
    if (!data) {
        fputs("Cannot allocate input buffer.\n", stderr);
        close(file);
        return false;
    }
    size_t used = 0;
    while (used < length) {
        ssize_t amount = read(file, data + used, length - used);
        if (amount < 0 && errno == EINTR) {
            continue;
        }
        if (amount <= 0) {
            fputs("Cannot read complete input file.\n", stderr);
            free(data);
            close(file);
            return false;
        }
        used += (size_t)amount;
    }
    close(file);
    *bytes = data;
    *size = length;
    return true;
}

static int hexadecimal_digit(uint8_t character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

static bool read_common_key(const char *path, uint8_t key[16])
{
    struct stat information;
    if (stat(path, &information) != 0 || information.st_size < 0 ||
        information.st_size > 256) {
        fputs("Common-key file is missing or too large.\n", stderr);
        return false;
    }
    uint8_t *data = NULL;
    size_t size = 0;
    if (!read_file(path, &data, &size)) {
        return false;
    }
    if (size == 16) {
        memcpy(key, data, 16);
        wipe(data, size);
        free(data);
        return true;
    }
    uint8_t digits[32];
    size_t digit_count = 0;
    bool valid = true;
    for (size_t index = 0; index < size; ++index) {
        uint8_t character = data[index];
        if (character == ' ' || character == '\t' || character == '\r' ||
            character == '\n') {
            continue;
        }
        if (digit_count >= sizeof(digits) || hexadecimal_digit(character) < 0) {
            valid = false;
            break;
        }
        digits[digit_count++] = character;
    }
    if (digit_count != sizeof(digits)) {
        valid = false;
    }
    if (valid) {
        for (size_t index = 0; index < 16; ++index) {
            key[index] = (uint8_t)((hexadecimal_digit(digits[index * 2]) << 4) |
                                   hexadecimal_digit(digits[index * 2 + 1]));
        }
    } else {
        fputs("Common-key file must contain 16 raw bytes or 32 hex digits.\n", stderr);
    }
    wipe(data, size);
    free(data);
    wipe(digits, sizeof(digits));
    return valid;
}

static bool signed_body(const WmWad *wad, WmSection section, const char *label,
                        size_t *body_offset)
{
    if (section.size < 4) {
        fprintf(stderr, "Truncated %s signature.\n", label);
        return false;
    }
    uint32_t signature = read_be32(wad->bytes + section.offset);
    size_t signature_size;
    switch (signature) {
        case 0x10000: signature_size = 0x240; break;
        case 0x10001: signature_size = 0x140; break;
        case 0x10002: signature_size = 0x80; break;
        default:
            fprintf(stderr, "Unsupported %s signature type.\n", label);
            return false;
    }
    if (signature_size > section.size) {
        fprintf(stderr, "Truncated %s signature.\n", label);
        return false;
    }
    *body_offset = section.offset + signature_size;
    return true;
}

static bool parse_wad(WmWad *wad)
{
    if (wad->size < 32) {
        fputs("Truncated WAD header.\n", stderr);
        return false;
    }
    uint32_t header_size = read_be32(wad->bytes);
    uint16_t kind = read_be16(wad->bytes + 4);
    if (header_size < 32 || header_size > wad->size ||
        (kind != 0x4973 && kind != 0x6962)) {
        fputs("Invalid WAD header size or type.\n", stderr);
        return false;
    }

    size_t offset;
    if (!aligned_size(header_size, 64, &offset)) {
        return false;
    }
    for (int section = 0; section < WM_SECTION_COUNT; ++section) {
        size_t size = read_be32(wad->bytes + 8 + section * 4);
        size_t stride;
        if (!slice_fits(offset, size, wad->size) ||
            !aligned_size(size, 64, &stride)) {
            fputs("Truncated or oversized WAD section.\n", stderr);
            return false;
        }
        wad->sections[section] = (WmSection){ offset, size };
        if (offset > SIZE_MAX - stride) {
            return false;
        }
        offset += stride;
    }

    if (!signed_body(wad, wad->sections[WM_TICKET], "ticket", &wad->ticket_body) ||
        !signed_body(wad, wad->sections[WM_TMD], "TMD", &wad->tmd_body)) {
        return false;
    }
    size_t ticket_length = wad->sections[WM_TICKET].offset +
                           wad->sections[WM_TICKET].size - wad->ticket_body;
    size_t tmd_length = wad->sections[WM_TMD].offset +
                        wad->sections[WM_TMD].size - wad->tmd_body;
    if (ticket_length < 0xb2 || tmd_length < 0xa4) {
        fputs("Truncated ticket or TMD header.\n", stderr);
        return false;
    }

    const uint8_t *ticket = wad->bytes + wad->ticket_body;
    const uint8_t *tmd = wad->bytes + wad->tmd_body;
    if (memcmp(ticket + 0x9c, tmd + 0x4c, 8) != 0) {
        fputs("Ticket and TMD title IDs differ.\n", stderr);
        return false;
    }
    memcpy(wad->title_id, tmd + 0x4c, sizeof(wad->title_id));
    wad->version = read_be16(tmd + 0x9c);
    wad->content_count = read_be16(tmd + 0x9e);
    wad->boot_index = read_be16(tmd + 0xa0);
    wad->key_index = ticket[0xb1];
    if (wad->content_count == 0 || wad->content_count > WM_MAX_CONTENTS ||
        wad->content_count > (tmd_length - 0xa4) / 36) {
        fputs("Invalid or truncated TMD content table.\n", stderr);
        return false;
    }
    wad->contents = calloc(wad->content_count, sizeof(*wad->contents));
    if (!wad->contents) {
        fputs("Cannot allocate content table.\n", stderr);
        return false;
    }

    size_t content_cursor = 0;
    for (uint16_t index = 0; index < wad->content_count; ++index) {
        const uint8_t *record = tmd + 0xa4 + index * 36;
        WmContent *content = wad->contents + index;
        content->id = read_be32(record);
        content->index = read_be16(record + 4);
        content->type = read_be16(record + 6);
        content->size = read_be64(record + 8);
        memcpy(content->sha1, record + 16, sizeof(content->sha1));
        for (uint16_t previous = 0; previous < index; ++previous) {
            if (wad->contents[previous].id == content->id ||
                wad->contents[previous].index == content->index) {
                fputs("Duplicate TMD content ID or index.\n", stderr);
                return false;
            }
        }
        if (content->size > SIZE_MAX) {
            fputs("Content exceeds addressable memory.\n", stderr);
            return false;
        }
        size_t encrypted_size;
        size_t stride;
        if (!aligned_size((size_t)content->size, 16, &encrypted_size) ||
            !aligned_size((size_t)content->size, 64, &stride) ||
            !slice_fits(content_cursor, encrypted_size, wad->sections[WM_DATA].size)) {
            fputs("Truncated encrypted WAD content.\n", stderr);
            return false;
        }
        content->encrypted_offset = wad->sections[WM_DATA].offset + content_cursor;
        content->encrypted_size = encrypted_size;
        if (content_cursor > SIZE_MAX - stride) {
            return false;
        }
        content_cursor += stride;
    }
    return true;
}

static bool decrypt_and_verify(WmWad *wad, const uint8_t common_key[16],
                               unsigned expected_index)
{
    if (wad->key_index != expected_index) {
        fprintf(stderr, "Ticket requires common-key index %u.\n", wad->key_index);
        return false;
    }

    const uint8_t *ticket = wad->bytes + wad->ticket_body;
    uint8_t title_key[16];
    uint8_t title_vector[16] = { 0 };
    memcpy(title_key, ticket + 0x7f, sizeof(title_key));
    memcpy(title_vector, wad->title_id, sizeof(wad->title_id));
    WmAes128 aes;
    wm_aes128_init(&aes, common_key);
    wm_aes128_cbc_decrypt(&aes, title_key, sizeof(title_key), title_vector);
    wipe(&aes, sizeof(aes));

    wm_aes128_init(&aes, title_key);
    for (uint16_t index = 0; index < wad->content_count; ++index) {
        WmContent *content = wad->contents + index;
        uint8_t vector[16] = { 0 };
        vector[0] = (uint8_t)(content->index >> 8);
        vector[1] = (uint8_t)content->index;
        uint8_t *payload = wad->bytes + content->encrypted_offset;
        wm_aes128_cbc_decrypt(&aes, payload, content->encrypted_size, vector);

        WmSha1 sha1;
        uint8_t digest[20];
        wm_sha1_init(&sha1);
        wm_sha1_update(&sha1, payload, (size_t)content->size);
        wm_sha1_final(&sha1, digest);
        if (memcmp(digest, content->sha1, sizeof(digest)) != 0) {
            fprintf(stderr, "Content %08x SHA-1 mismatch.\n", content->id);
            wipe(title_key, sizeof(title_key));
            wipe(&aes, sizeof(aes));
            return false;
        }
    }
    wipe(title_key, sizeof(title_key));
    wipe(&aes, sizeof(aes));
    return true;
}

static bool path_format(char path[WM_PATH_SIZE], const char *directory,
                        const char *filename)
{
    int length = snprintf(path, WM_PATH_SIZE, "%s/%s", directory, filename);
    return length > 0 && length < WM_PATH_SIZE;
}

static bool ensure_directory(const char *path)
{
    struct stat information;
    if (lstat(path, &information) == 0) {
        if (!S_ISDIR(information.st_mode)) {
            fputs("Output directory is not a real directory.\n", stderr);
            return false;
        }
        return true;
    }
    if (errno != ENOENT || mkdir(path, 0700) != 0) {
        fputs("Cannot create private output directory.\n", stderr);
        return false;
    }
    return true;
}

static bool write_private_file(const char *path, const uint8_t *bytes, size_t length)
{
    int file = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (file < 0) {
        return false;
    }
    size_t used = 0;
    while (used < length) {
        ssize_t amount = write(file, bytes + used, length - used);
        if (amount < 0 && errno == EINTR) {
            continue;
        }
        if (amount <= 0) {
            close(file);
            unlink(path);
            return false;
        }
        used += (size_t)amount;
    }
    bool synchronized = fsync(file) == 0;
    if (!synchronized || close(file) != 0) {
        if (!synchronized) {
            close(file);
        }
        unlink(path);
        return false;
    }
    return true;
}

static void remove_files_in_directory(const char *directory)
{
    DIR *entries = opendir(directory);
    if (!entries) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(entries)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        char path[WM_PATH_SIZE];
        if (path_format(path, directory, entry->d_name)) {
            unlink(path);
        }
    }
    closedir(entries);
    rmdir(directory);
}

static void remove_stage(const char *stage)
{
    char content_directory[WM_PATH_SIZE];
    if (path_format(content_directory, stage, "content")) {
        remove_files_in_directory(content_directory);
    }
    char path[WM_PATH_SIZE];
    const char *names[] = { "ticket.bin", "title.tmd", "import.json" };
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (path_format(path, stage, names[index])) {
            unlink(path);
        }
    }
    rmdir(stage);
}

static void print_hex(FILE *output, const uint8_t *bytes, size_t length)
{
    for (size_t index = 0; index < length; ++index) {
        fprintf(output, "%02x", bytes[index]);
    }
}

static bool write_manifest(const WmWad *wad, const char *stage)
{
    char path[WM_PATH_SIZE];
    if (!path_format(path, stage, "import.json")) {
        return false;
    }
    int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (descriptor < 0) {
        return false;
    }
    FILE *output = fdopen(descriptor, "w");
    if (!output) {
        close(descriptor);
        unlink(path);
        return false;
    }

    fputs("{\n  \"titleId\": \"", output);
    print_hex(output, wad->title_id, sizeof(wad->title_id));
    fprintf(output, "\",\n  \"version\": %u,\n  \"bootIndex\": %u,\n",
            wad->version, wad->boot_index);
    fprintf(output, "  \"commonKeyIndex\": %u,\n  \"contents\": [\n", wad->key_index);
    for (uint16_t index = 0; index < wad->content_count; ++index) {
        const WmContent *content = wad->contents + index;
        fprintf(output,
                "    { \"id\": \"%08x\", \"index\": %u, \"type\": %u, "
                "\"size\": %" PRIu64 ", \"sha1\": \"",
                content->id, content->index, content->type, content->size);
        print_hex(output, content->sha1, sizeof(content->sha1));
        fputs(index + 1 == wad->content_count ? "\" }\n" : "\" },\n", output);
    }
    fputs("  ],\n  \"integrity\": \"Every decrypted content matches its TMD "
          "SHA-1; signatures are not verified.\"\n}\n", output);
    bool successful = fflush(output) == 0 && fsync(descriptor) == 0;
    if (fclose(output) != 0) {
        successful = false;
    }
    if (!successful) {
        unlink(path);
    }
    return successful;
}

static bool extract_contents(const WmWad *wad)
{
    if (!ensure_directory(".local") || !ensure_directory(".local/wad")) {
        return false;
    }
    char title_name[17];
    for (size_t index = 0; index < sizeof(wad->title_id); ++index) {
        snprintf(title_name + index * 2, 3, "%02x", wad->title_id[index]);
    }
    char destination[WM_PATH_SIZE];
    if (!path_format(destination, ".local/wad", title_name)) {
        return false;
    }
    struct stat existing;
    if (lstat(destination, &existing) == 0) {
        fputs("Validated title already exists; existing data was preserved.\n", stderr);
        return false;
    }
    if (errno != ENOENT) {
        fputs("Cannot inspect existing output.\n", stderr);
        return false;
    }

    char stage[] = ".local/wad/.extract-XXXXXX";
    if (!mkdtemp(stage)) {
        fputs("Cannot create private staging directory.\n", stderr);
        return false;
    }
    char content_directory[WM_PATH_SIZE];
    bool successful = path_format(content_directory, stage, "content") &&
                      mkdir(content_directory, 0700) == 0;
    if (!successful) {
        remove_stage(stage);
        return false;
    }

    char path[WM_PATH_SIZE];
    for (uint16_t index = 0; index < wad->content_count; ++index) {
        const WmContent *content = wad->contents + index;
        char filename[16];
        snprintf(filename, sizeof(filename), "%08x.app", content->id);
        successful = path_format(path, content_directory, filename) &&
                     write_private_file(path, wad->bytes + content->encrypted_offset,
                                        (size_t)content->size);
        if (!successful) {
            break;
        }
    }
    if (successful) {
        successful = path_format(path, content_directory, "title.tmd") &&
                     write_private_file(path,
                                        wad->bytes + wad->sections[WM_TMD].offset,
                                        wad->sections[WM_TMD].size);
    }
    if (successful) {
        successful = path_format(path, stage, "ticket.bin") &&
                     write_private_file(path,
                                        wad->bytes + wad->sections[WM_TICKET].offset,
                                        wad->sections[WM_TICKET].size);
    }
    if (successful) {
        successful = write_manifest(wad, stage);
    }
    if (successful) {
        successful = rename(stage, destination) == 0;
    }
    if (!successful) {
        fputs("Failed to write validated contents; staging data was removed.\n", stderr);
        remove_stage(stage);
        return false;
    }

    printf("Validated %u contents and extracted title %s under .local/wad/.\n",
           wad->content_count, title_name);
    return true;
}

static void usage(FILE *output)
{
    fputs("Usage: wad_extract --wad FILE --common-key-file FILE "
          "[--common-key-index N] [--verify-only]\n"
          "Extracted title contents stay under .local/wad/ in the current project.\n",
          output);
}

static bool parse_options(int argument_count, char **arguments, WmOptions *options)
{
    *options = (WmOptions){ .expected_key_index = 0 };
    for (int index = 1; index < argument_count; ++index) {
        if (strcmp(arguments[index], "--wad") == 0 && index + 1 < argument_count) {
            options->wad_path = arguments[++index];
        } else if (strcmp(arguments[index], "--common-key-file") == 0 &&
                   index + 1 < argument_count) {
            options->key_path = arguments[++index];
        } else if (strcmp(arguments[index], "--common-key-index") == 0 &&
                   index + 1 < argument_count) {
            char *end = NULL;
            const char *text = arguments[++index];
            unsigned long value = strtoul(text, &end, 10);
            if (end == text || *end != '\0' || value > 255) {
                fputs("Common-key index must be 0 through 255.\n", stderr);
                return false;
            }
            options->expected_key_index = (unsigned)value;
        } else if (strcmp(arguments[index], "--verify-only") == 0) {
            options->verify_only = true;
        } else if (strcmp(arguments[index], "--help") == 0) {
            usage(stdout);
            exit(0);
        } else {
            usage(stderr);
            return false;
        }
    }
    if (!options->wad_path || !options->key_path) {
        usage(stderr);
        return false;
    }
    return true;
}

int main(int argument_count, char **arguments)
{
    WmOptions options;
    if (!parse_options(argument_count, arguments, &options)) {
        return 2;
    }

    WmWad wad = { 0 };
    uint8_t common_key[16] = { 0 };
    bool successful = read_common_key(options.key_path, common_key) &&
                      read_file(options.wad_path, &wad.bytes, &wad.size) &&
                      parse_wad(&wad) &&
                      decrypt_and_verify(&wad, common_key, options.expected_key_index);
    wipe(common_key, sizeof(common_key));
    if (successful && options.verify_only) {
        printf("Validated %u WAD contents against TMD SHA-1.\n", wad.content_count);
    } else if (successful) {
        successful = extract_contents(&wad);
    }
    if (wad.bytes) {
        wipe(wad.bytes, wad.size);
    }
    free(wad.bytes);
    free(wad.contents);
    return successful ? 0 : 1;
}
