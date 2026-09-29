#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1

#include "wii_menu/support/asset_manifest.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_file(const char *path, const char *data) {
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(data, 1, strlen(data), file) == strlen(data));
    assert(fclose(file) == 0);
}

static char *read_stream(FILE *file) {
    assert(fflush(file) == 0);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length >= 0);
    assert(fseek(file, 0, SEEK_SET) == 0);
    char *text = calloc((size_t)length + 1, 1);
    assert(text);
    assert(fread(text, 1, (size_t)length, file) == (size_t)length);
    return text;
}

int main(void) {
    char root[] = "/tmp/wm-assets-test-XXXXXX";
    assert(mkdtemp(root));
    char nested[256], first[256], second[256], state[256], manifest[256];
    assert(snprintf(nested, sizeof(nested), "%s/layouts", root) > 0);
    assert(snprintf(first, sizeof(first), "%s/channels.json", root) > 0);
    assert(snprintf(second, sizeof(second), "%s/layouts/menu.json", root) > 0);
    assert(snprintf(state, sizeof(state), "%s/.board-memos.json", root) > 0);
    assert(snprintf(manifest, sizeof(manifest), "%s/%s", root, WM_ASSET_MANIFEST_NAME) >
           0);
    assert(mkdir(nested, 0700) == 0);
    write_file(first, "catalog");
    write_file(second, "layout");
    write_file(state, "mutable");
    FILE *log = tmpfile();
    assert(log);
    assert(wm_asset_manifest_write(root, log));
    unsigned issues = 99;
    assert(wm_asset_manifest_verify(root, log, &issues));
    assert(issues == 0);

    write_file(state, "changed user state");
    assert(wm_asset_manifest_verify(root, log, &issues));
    assert(issues == 0);

    write_file(first, "CATALOG");
    assert(unlink(second) == 0);
    assert(!wm_asset_manifest_verify(root, log, &issues));
    assert(issues == 2);
    char *report = read_stream(log);
    assert(strstr(report, "SHA-1 mismatch: channels.json"));
    assert(strstr(report, "missing: layouts/menu.json"));
    free(report);

    write_file(first, "short");
    assert(!wm_asset_manifest_verify(root, log, &issues));
    assert(issues == 2);
    report = read_stream(log);
    assert(strstr(report, "size mismatch: channels.json"));
    free(report);

    assert(unlink(first) == 0);
    assert(symlink(state, first) == 0);
    assert(!wm_asset_manifest_verify(root, log, &issues));
    assert(issues == 2);
    report = read_stream(log);
    assert(strstr(report, "wrong type: channels.json"));
    free(report);

    assert(unlink(first) == 0);
    assert(unlink(state) == 0);
    assert(unlink(manifest) == 0);
    assert(rmdir(nested) == 0);
    assert(rmdir(root) == 0);
    assert(fclose(log) == 0);
    return 0;
}
