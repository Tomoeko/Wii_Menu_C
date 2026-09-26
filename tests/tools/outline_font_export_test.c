#define _XOPEN_SOURCE 700
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void put32(uint8_t *bytes, size_t offset, uint32_t value) {
    bytes[offset] = (uint8_t)(value >> 24);
    bytes[offset + 1] = (uint8_t)(value >> 16);
    bytes[offset + 2] = (uint8_t)(value >> 8);
    bytes[offset + 3] = (uint8_t)value;
}

static int run_exporter(const char *tool, const char *source,
                        const char *output) {
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        execl(tool, tool, source, output, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    return WEXITSTATUS(status);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    char temporary[] = "outline-font-export-XXXXXX";
    assert(mkdtemp(temporary));
    char source_path[256], output_path[256];
    assert(snprintf(source_path, sizeof(source_path), "%s/invalid.app",
                    temporary) > 0);
    assert(snprintf(output_path, sizeof(output_path), "%s/assets",
                    temporary) > 0);

    uint8_t archive[160] = {0};
    const char name[] = "\0WiiNTLG-Regular.ttc";
    memcpy(archive, "U\xaa" "8-", 4);
    put32(archive, 4, 32);
    put32(archive, 8, 24 + sizeof(name));
    put32(archive, 12, 128);
    put32(archive, 32, 0x01000000);
    put32(archive, 40, 2);
    put32(archive, 44, 1); /* file name begins after root NUL */
    put32(archive, 48, 128);
    put32(archive, 52, 32);
    memcpy(archive + 56, name, sizeof(name));
    memcpy(archive + 128, "ttcf", 4);
    put32(archive, 128 + 4, 0x00010000);
    put32(archive, 128 + 8, 2);
    put32(archive, 128 + 12, 20);
    put32(archive, 128 + 16, 32); /* face 1 outside member */

    FILE *file = fopen(source_path, "wb");
    assert(file);
    assert(fwrite(archive, 1, sizeof(archive), file) == sizeof(archive));
    assert(fclose(file) == 0);
    assert(run_exporter(argv[1], source_path, output_path) == 1);
    struct stat metadata;
    assert(lstat(output_path, &metadata) != 0 && errno == ENOENT);

    /* An ignored local source can also exercise the exporter against real
     * font data without putting any proprietary bytes in this fixture. */
    const char *valid_source = getenv("WM_OUTLINE_TEST_SOURCE");
    if (valid_source && valid_source[0]) {
        char linked[256], fonts[256], font_path[256], sentinel[256];
        assert(snprintf(linked, sizeof(linked), "%s/linked", temporary) > 0);
        assert(snprintf(fonts, sizeof(fonts), "%s/fonts", output_path) > 0);
        assert(snprintf(font_path, sizeof(font_path),
                        "%s/settings-latin.ttc", fonts) > 0);
        assert(snprintf(sentinel, sizeof(sentinel), "%s/sentinel",
                        temporary) > 0);
        assert(mkdir(output_path, 0700) == 0);
        assert(symlink("assets", linked) == 0);
        assert(run_exporter(argv[1], valid_source, linked) == 1);
        assert(lstat(fonts, &metadata) != 0 && errno == ENOENT);
        assert(unlink(linked) == 0);

        assert(run_exporter(argv[1], valid_source, output_path) == 0);
        assert(lstat(font_path, &metadata) == 0 &&
               S_ISREG(metadata.st_mode));
        assert(run_exporter(argv[1], valid_source, output_path) == 1);
        assert(unlink(font_path) == 0);
        FILE *marker = fopen(sentinel, "wb");
        assert(marker && fwrite("keep", 1, 4, marker) == 4);
        assert(fclose(marker) == 0);
        assert(symlink("../../sentinel", font_path) == 0);
        assert(run_exporter(argv[1], valid_source, output_path) == 1);
        marker = fopen(sentinel, "rb");
        char retained[4];
        assert(marker && fread(retained, 1, 4, marker) == 4);
        assert(fclose(marker) == 0);
        assert(memcmp(retained, "keep", 4) == 0);
        assert(unlink(font_path) == 0);
        assert(unlink(sentinel) == 0);
        assert(rmdir(fonts) == 0);
        assert(rmdir(output_path) == 0);
    }

    assert(remove(source_path) == 0);
    assert(rmdir(temporary) == 0);
    puts("Malformed TTC was rejected before creating output.");
    return 0;
}
