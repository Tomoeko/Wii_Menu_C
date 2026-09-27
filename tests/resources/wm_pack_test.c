#define _XOPEN_SOURCE 700
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#include "wii_menu/resources/wm_pack.h"

#include <assert.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void join(char *result, const char *root, const char *relative) {
    int length = snprintf(result, 4096, "%s/%s", root, relative);
    assert(length > 0 && length < 4096);
}

static void write_bytes(const char *path, const void *bytes, size_t size) {
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(bytes, 1, size, file) == size);
    assert(fclose(file) == 0);
}

static void assert_bytes(const char *path, const void *bytes, size_t size) {
    FILE *file = fopen(path, "rb");
    assert(file);
    unsigned char buffer[256];
    assert(size <= sizeof(buffer));
    assert(fread(buffer, 1, size, file) == size);
    assert(fgetc(file) == EOF && !ferror(file));
    assert(memcmp(buffer, bytes, size) == 0);
    assert(fclose(file) == 0);
}

static void remove_tree(const char *path) {
    struct stat metadata;
    assert(lstat(path, &metadata) == 0);
    if (!S_ISDIR(metadata.st_mode)) { assert(unlink(path) == 0); return; }
    DIR *directory = opendir(path);
    assert(directory);
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char child[4096];
        join(child, path, entry->d_name);
        remove_tree(child);
    }
    assert(closedir(directory) == 0);
    assert(rmdir(path) == 0);
}

static void put_u32(unsigned char *bytes, uint32_t value) {
    for (unsigned index = 0; index < 4; index++) bytes[index] = (unsigned char)(value >> (8 * index));
}

static void put_u64(unsigned char *bytes, uint64_t value) {
    put_u32(bytes, (uint32_t)value);
    put_u32(bytes + 4, (uint32_t)(value >> 32));
}

static void empty_records(const char *path, const char *first, const char *second) {
    FILE *file = fopen(path, "wb");
    assert(file);
    unsigned char header[32] = {0};
    memcpy(header, "WMPACK1", 7);
    put_u32(header + 8, 1);
    put_u32(header + 12, second ? 2 : 1);
    assert(fwrite(header, 1, sizeof(header), file) == sizeof(header));
    const char *names[2] = {first, second};
    for (size_t index = 0; index < (second ? 2u : 1u); index++) {
        unsigned char record[16] = {0};
        put_u32(record, (uint32_t)strlen(names[index]));
        assert(fwrite(record, 1, sizeof(record), file) == sizeof(record));
        assert(fwrite(names[index], 1, strlen(names[index]), file) == strlen(names[index]));
    }
    assert(fclose(file) == 0);
}

static void assert_rejected(const char *package, const char *destination) {
    char error[256];
    assert(!wm_pack_extract(package, destination, error, sizeof(error)));
    assert(error[0]);
    struct stat metadata;
    assert(lstat(destination, &metadata) != 0);
    char parent[4096];
    strcpy(parent, destination);
    char *basename = strrchr(parent, '/');
    assert(basename);
    *basename++ = '\0';
    char prefix[4096];
    int length = snprintf(prefix, sizeof(prefix), "%s.tmp.", basename);
    assert(length > 0 && (size_t)length < sizeof(prefix));
    DIR *directory = opendir(parent);
    assert(directory);
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL)
        assert(strncmp(entry->d_name, prefix, strlen(prefix)) != 0);
    assert(closedir(directory) == 0);
}

int main(void) {
    char *temporary_parent = realpath("/tmp", NULL);
    assert(temporary_parent);
    char root[4096];
    join(root, temporary_parent, "wm-pack-test-XXXXXX");
    free(temporary_parent);
    assert(mkdtemp(root));
    char assets[4096], package[4096], other[4096], extracted[4096], path[4096];
    join(assets, root, "assets");
    join(package, root, "menu.wm");
    join(other, root, "other.wm");
    join(extracted, root, "extracted");
    assert(mkdir(assets, 0700) == 0);
    join(path, assets, "psvr2");
    assert(mkdir(path, 0700) == 0);
    join(path, assets, "psvr2/vr.conf");
    static const char calibration[] = "presentation = seated\nconvergence = calibrated\n";
    write_bytes(path, calibration, sizeof(calibration));
    join(path, assets, "iplsave.bin");
    static const unsigned char binary[] = {0, 255, 1, 2, 0};
    write_bytes(path, binary, sizeof(binary));
    join(path, assets, "empty");
    write_bytes(path, "", 0);

    char error[256];
    assert(wm_pack_create(assets, package, error, sizeof(error)));
    assert(wm_pack_create(assets, other, error, sizeof(error)));
    FILE *first = fopen(package, "rb"), *second = fopen(other, "rb");
    assert(first && second);
    int a, b;
    do { a = fgetc(first); b = fgetc(second); assert(a == b); } while (a != EOF);
    assert(fclose(first) == 0 && fclose(second) == 0);
    assert(wm_pack_extract(package, extracted, error, sizeof(error)));
    join(path, extracted, "psvr2/vr.conf");
    assert_bytes(path, calibration, sizeof(calibration));
    join(path, extracted, "iplsave.bin");
    assert_bytes(path, binary, sizeof(binary));
    join(path, extracted, "empty");
    assert_bytes(path, "", 0);
    assert(!wm_pack_extract(package, extracted, error, sizeof(error)));

    join(path, assets, "linked");
    assert(symlink("iplsave.bin", path) == 0);
    assert(!wm_pack_create(assets, package, error, sizeof(error)));
    assert(unlink(path) == 0);
    join(path, assets, "hard-linked");
    char original[4096];
    join(original, assets, "iplsave.bin");
    assert(link(original, path) == 0);
    assert(!wm_pack_create(assets, package, error, sizeof(error)));
    assert(unlink(path) == 0);
    join(path, assets, "too-large");
    FILE *large = fopen(path, "wb");
    assert(large);
    assert(ftruncate(fileno(large), (off_t)WM_PACK_MAX_FILE_BYTES + 1) == 0);
    assert(fclose(large) == 0);
    assert(!wm_pack_create(assets, package, error, sizeof(error)));
    assert(unlink(path) == 0);
    /* Failed publication attempts preserve the original valid package. */
    char preserved[4096];
    join(preserved, root, "preserved");
    assert(wm_pack_extract(package, preserved, error, sizeof(error)));

    char rejected[4096];
    join(rejected, root, "rejected");
    const char *unsafe[] = {"../escape", "/escape", "a/../escape", "a//b",
                            "a\\b", "a:b", "a/./b", "a/", "", "a\nb"};
    for (size_t index = 0; index < sizeof(unsafe) / sizeof(unsafe[0]); index++) {
        empty_records(other, unsafe[index], NULL);
        assert_rejected(other, rejected);
    }
    empty_records(other, "same", "same");
    assert_rejected(other, rejected);
    empty_records(other, "z", "a");
    assert_rejected(other, rejected);
    empty_records(other, "a", "a/b");
    assert_rejected(other, rejected);
    empty_records(other, "good", NULL);
    FILE *damaged = fopen(other, "r+b");
    assert(damaged);
    assert(fseek(damaged, 24, SEEK_SET) == 0 && fputc(1, damaged) == 1);
    assert(fclose(damaged) == 0);
    assert_rejected(other, rejected);
    unsigned char excessive[32] = {0};
    memcpy(excessive, "WMPACK1", 7);
    put_u32(excessive + 8, 1);
    put_u32(excessive + 12, WM_PACK_MAX_FILES + 1);
    write_bytes(other, excessive, sizeof(excessive));
    assert_rejected(other, rejected);
    put_u32(excessive + 12, 1);
    put_u64(excessive + 16, (uint64_t)WM_PACK_MAX_BYTES + 1);
    write_bytes(other, excessive, sizeof(excessive));
    assert_rejected(other, rejected);
    assert(wm_pack_create(assets, other, error, sizeof(error)));
    damaged = fopen(other, "r+b");
    assert(damaged);
    assert(fseek(damaged, -1, SEEK_END) == 0);
    int last = fgetc(damaged);
    assert(last != EOF && fseek(damaged, -1, SEEK_END) == 0);
    assert(fputc(last ^ 1, damaged) != EOF);
    assert(fclose(damaged) == 0);
    assert_rejected(other, rejected);
    assert(wm_pack_create(assets, other, error, sizeof(error)));
    damaged = fopen(other, "ab");
    assert(damaged && fputc(1, damaged) != EOF && fclose(damaged) == 0);
    assert_rejected(other, rejected);
    write_bytes(other, "WMPACK1", 7);
    assert_rejected(other, rejected);

    /* Check the wire checksum against the standard CRC-32 test vector. */
    char crc_assets[4096];
    join(crc_assets, root, "crc-assets");
    assert(mkdir(crc_assets, 0700) == 0);
    join(path, crc_assets, "crc");
    write_bytes(path, "123456789", 9);
    assert(wm_pack_create(crc_assets, other, error, sizeof(error)));
    FILE *known = fopen(other, "rb");
    assert(known && fseek(known, 36, SEEK_SET) == 0);
    unsigned char checksum[4];
    assert(fread(checksum, 1, sizeof(checksum), known) == sizeof(checksum));
    static const unsigned char expected_checksum[4] = {0x26, 0x39, 0xf4, 0xcb};
    assert(memcmp(checksum, expected_checksum, sizeof(checksum)) == 0);
    assert(fclose(known) == 0);

    join(path, root, "linked-package.wm");
    assert(symlink("menu.wm", path) == 0);
    assert_rejected(path, rejected);
    assert(!wm_pack_create(assets, path, error, sizeof(error)));
    join(path, root, "linked-assets");
    assert(symlink("assets", path) == 0);
    assert(!wm_pack_create(path, other, error, sizeof(error)));
    char linked_child[4096];
    join(linked_child, path, "bad-destination");
    assert_rejected(package, linked_child);

    char adjacent[256];
    assert(wm_pack_adjacent("/ram/wii-menu", adjacent, sizeof(adjacent)));
    assert(strcmp(adjacent, "/ram/wii-menu.wm") == 0);
    assert(wm_pack_adjacent("/ram/wii-menu.elf", adjacent, sizeof(adjacent)));
    assert(strcmp(adjacent, "/ram/wii-menu.wm") == 0);
    assert(wm_pack_adjacent("menu.bin", adjacent, sizeof(adjacent)));
    assert(strcmp(adjacent, "menu.wm") == 0);
    assert(!wm_pack_adjacent("menu", adjacent, 4));
    assert(!wm_pack_adjacent("/ram/", adjacent, sizeof(adjacent)));
    remove_tree(root);
    return 0;
}
