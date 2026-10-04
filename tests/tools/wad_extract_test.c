#define main wm_wad_command_main
#include "extract.c"
#undef main

#include <assert.h>
#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#define chdir _chdir
#endif

enum {
    FIXTURE_TICKET_OFFSET = 64,
    FIXTURE_TMD_OFFSET = 64 + 0x200,
    FIXTURE_DATA_OFFSET = 64 + 0x200 + 0x240,
    FIXTURE_SIZE = FIXTURE_DATA_OFFSET + 64
};

static const uint8_t fixture_title[8] = {0x00, 0x00, 0x00, 0x01,
                                         0x00, 0x00, 0x00, 0x02};
static const char fixture_text[] = "Local WAD test\n";

/* Authored AES-CBC fixtures, not extracted title content. The title key is
 * the test sequence 00 through 0f; index 9 uses that sequence as its override.
 * Ciphertext was independently produced by Node's built-in crypto. */
static const uint8_t encrypted_title_keys[3][16] = {
    {0x25, 0x71, 0xe4, 0x86, 0x7a, 0xfa, 0x09, 0x5a, 0x7c, 0x72, 0x04, 0xf8, 0x9a, 0xc9,
     0x99, 0xb7},
    {0xa2, 0xe5, 0xed, 0xfb, 0x09, 0xfe, 0x5f, 0xb5, 0x3b, 0xfc, 0xe5, 0xe1, 0xef, 0xdb,
     0xae, 0xb9},
    {0x3c, 0x83, 0x8f, 0x35, 0x06, 0x45, 0xa6, 0xe0, 0xaf, 0x4a, 0xec, 0x31, 0x25, 0x50,
     0x27, 0x79}};
static const uint8_t encrypted_content[16] = {0x70, 0xfd, 0x7e, 0x5c, 0x55, 0x52,
                                              0xf1, 0xeb, 0x07, 0xb5, 0xa3, 0x9c,
                                              0xab, 0xf3, 0x6d, 0xc7};

static void write_be16(uint8_t *bytes, unsigned value) {
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void write_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static WmWad make_fixture(unsigned key_index) {
    assert(key_index == 0 || key_index == 1 || key_index == 9);
    WmWad wad = {.bytes = calloc(1, FIXTURE_SIZE), .size = FIXTURE_SIZE};
    assert(wad.bytes);
    write_be32(wad.bytes, 32);
    write_be16(wad.bytes + 4, 0x4973);
    write_be32(wad.bytes + 16, 0x1f2);
    write_be32(wad.bytes + 20, 0x208);
    write_be32(wad.bytes + 24, 64);
    write_be32(wad.bytes + FIXTURE_TICKET_OFFSET, 0x10001);
    uint8_t *ticket = wad.bytes + FIXTURE_TICKET_OFFSET + 0x140;
    memcpy(ticket + 0x7f, encrypted_title_keys[key_index == 9 ? 2 : key_index], 16);
    memcpy(ticket + 0x9c, fixture_title, sizeof(fixture_title));
    ticket[0xb1] = (uint8_t)key_index;
    write_be32(wad.bytes + FIXTURE_TMD_OFFSET, 0x10001);
    uint8_t *tmd = wad.bytes + FIXTURE_TMD_OFFSET + 0x140;
    memcpy(tmd + 0x4c, fixture_title, sizeof(fixture_title));
    write_be16(tmd + 0x9c, 1);
    write_be16(tmd + 0x9e, 1);
    uint8_t *record = tmd + 0xa4;
    write_be32(record, 1);
    write_be16(record + 6, 1);
    write_be32(record + 12, sizeof(fixture_text) - 1);
    WmSha1 digest;
    wm_sha1_init(&digest);
    wm_sha1_update(&digest, (const uint8_t *)fixture_text, sizeof(fixture_text) - 1);
    wm_sha1_final(&digest, record + 16);
    memcpy(wad.bytes + FIXTURE_DATA_OFFSET, encrypted_content,
           sizeof(encrypted_content));
    return wad;
}

static void free_fixture(WmWad *wad) {
    wipe(wad->bytes, wad->size);
    free(wad->contents);
    free(wad->bytes);
    *wad = (WmWad){0};
}

static void write_fixture_file(const char *path, const void *data, size_t size) {
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(data, 1, size, file) == size);
    assert(fclose(file) == 0);
}

static void test_retail_defaults(void) {
    char *arguments[] = {"wm-wad-extract", "--wad", "fixture.wad"};
    WmOptions options;
    assert(parse_options(3, arguments, &options));
    assert(!options.key_path && !options.key_index_explicit);
    for (unsigned index = 0; index < 2; index++) {
        WmWad wad = make_fixture(index);
        assert(parse_wad(&wad));
        uint8_t key[16] = {0};
        unsigned expected = 255;
        assert(resolve_common_key(&wad, &options, key, &expected));
        assert(expected == index);
        assert(decrypt_and_verify(&wad, key, expected));
        assert(memcmp(wad.bytes + FIXTURE_DATA_OFFSET, fixture_text,
                      sizeof(fixture_text) - 1) == 0);
        wipe(key, sizeof(key));
        free_fixture(&wad);
    }
    uint8_t key[16] = {0};
    assert(!wm_wad_retail_common_key(2, key));
    assert(!wm_wad_retail_common_key(9, key));
    assert(!wm_wad_retail_common_key(0, NULL));
    WmWad unsupported = make_fixture(9);
    assert(parse_wad(&unsupported));
    unsigned expected;
    assert(!resolve_common_key(&unsupported, &options, key, &expected));
    free_fixture(&unsupported);
    WmWad korean = make_fixture(1);
    assert(parse_wad(&korean));
    options.key_index_explicit = true;
    options.expected_key_index = 0;
    assert(!resolve_common_key(&korean, &options, key, &expected));
    free_fixture(&korean);
}

static void test_override_and_output(void) {
    char saved_directory[WM_PATH_SIZE];
    assert(getcwd(saved_directory, sizeof(saved_directory)));
    char temporary[] = "wm-wad-default-test-XXXXXX";
    assert(mkdtemp(temporary) && chdir(temporary) == 0);
    WmWad fixture = make_fixture(9);
    write_fixture_file("fixture.wad", fixture.bytes, fixture.size);
    free_fixture(&fixture);
    char *without_key[] = {"wm-wad-extract", "--wad", "fixture.wad"};
    assert(wm_wad_command_main(3, without_key) == 1);
    struct stat information;
    assert(lstat("Files/.local", &information) != 0 && errno == ENOENT);

    uint8_t key[16];
    for (unsigned byte = 0; byte < 16; byte++)
        key[byte] = (uint8_t)byte;
    write_fixture_file("override.key", key, sizeof(key));
    char *binary_override[] = {"wm-wad-extract",    "--wad",        "fixture.wad",
                               "--common-key-file", "override.key", "--verify-only"};
    assert(wm_wad_command_main(6, binary_override) == 0);
    char *wrong_index[] = {"wm-wad-extract",
                           "--wad",
                           "fixture.wad",
                           "--common-key-file",
                           "override.key",
                           "--common-key-index",
                           "0",
                           "--verify-only"};
    assert(wm_wad_command_main(8, wrong_index) == 1);
    static const char hexadecimal_key[] =
        "00 01 02 03 04 05 06 07\n08 09 0a 0b 0c 0d 0e 0f\n";
    write_fixture_file("override.key", hexadecimal_key, sizeof(hexadecimal_key) - 1);
    char *hexadecimal_override[] = {"wm-wad-extract",
                                    "--wad",
                                    "fixture.wad",
                                    "--common-key-file",
                                    "override.key",
                                    "--common-key-index",
                                    "9",
                                    "--verify-only"};
    assert(wm_wad_command_main(8, hexadecimal_override) == 0);
    memset(key, 0, sizeof(key));
    write_fixture_file("override.key", key, sizeof(key));
    assert(wm_wad_command_main(6, binary_override) == 1);
    assert(lstat("Files/.local", &information) != 0 && errno == ENOENT);
    static const char malformed_key[] = "not a hexadecimal key";
    write_fixture_file("override.key", malformed_key, sizeof(malformed_key));
    assert(wm_wad_command_main(6, binary_override) == 1);
    assert(lstat("Files/.local", &information) != 0 && errno == ENOENT);

    fixture = make_fixture(0);
    fixture.bytes[FIXTURE_DATA_OFFSET] ^= 1;
    write_fixture_file("fixture.wad", fixture.bytes, fixture.size);
    free_fixture(&fixture);
    assert(wm_wad_command_main(3, without_key) == 1);
    assert(lstat("Files/.local", &information) != 0 && errno == ENOENT);
    fixture = make_fixture(0);
    write_fixture_file("fixture.wad", fixture.bytes, 31);
    assert(wm_wad_command_main(3, without_key) == 1);
    assert(lstat("Files/.local", &information) != 0 && errno == ENOENT);
    write_fixture_file("fixture.wad", fixture.bytes, fixture.size);
    free_fixture(&fixture);
    assert(wm_wad_command_main(3, without_key) == 0);
    assert(wm_wad_command_main(3, without_key) == 1);
    uint8_t *content = NULL;
    size_t content_size = 0;
    assert(read_file("Files/.local/wad/0000000100000002/content/00000001.app", &content,
                     &content_size));
    assert(content_size == sizeof(fixture_text) - 1);
    assert(memcmp(content, fixture_text, content_size) == 0);
    free(content);
    remove_stage("Files/.local/wad/0000000100000002");
    assert(rmdir("Files/.local/wad") == 0 && rmdir("Files/.local") == 0);
    assert(rmdir("Files") == 0);
    assert(unlink("override.key") == 0 && unlink("fixture.wad") == 0);
    assert(chdir(saved_directory) == 0 && rmdir(temporary) == 0);
}

int main(void) {
    test_retail_defaults();
    test_override_and_output();
    puts("Automatic retail keys, overrides, WAD integrity, and output passed.");
    return 0;
}
