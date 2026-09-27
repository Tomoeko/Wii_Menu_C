#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE 1

#include "../../src/app/packed_assets.h"
#include "wii_menu/resources/wm_pack.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void) {
    char root[4096] = "/tmp/wm-packed-app-test-XXXXXX";
    assert(mkdtemp(root));
    char *canonical = realpath(root, NULL);
    assert(canonical && strlen(canonical) < sizeof(root));
    strcpy(root, canonical);
    free(canonical);
    char source[512], input[512], package[512];
    snprintf(source, sizeof(source), "%s/source", root);
    snprintf(input, sizeof(input), "%s/source/channels.json", root);
    snprintf(package, sizeof(package), "%s/wii-menu.wm", root);
    assert(mkdir(source, 0700) == 0);
    FILE *file = fopen(input, "wb");
    assert(file);
    assert(fputs("{\"channels\":[]}\n", file) >= 0);
    assert(fclose(file) == 0);
    char error[256];
    assert(wm_pack_create(source, package, error, sizeof(error)));
    char *arguments[] = {"wii-menu", "--assets", package, NULL};
    WmPackedAssets assets;
    assert(wm_packed_assets_open(&assets, 3, arguments, error, sizeof(error)));
    assert(assets.argc == 3);
    assert(!strcmp(assets.argv[2], assets.assets_path));
    char extracted[4096];
    snprintf(extracted, sizeof(extracted), "%s", assets.temporary_root);
    struct stat status;
    assert(stat(extracted, &status) == 0 && S_ISDIR(status.st_mode));
    wm_packed_assets_close(&assets);
    assert(stat(extracted, &status) != 0);

    char *help[] = {"wii-menu", "--assets", "/missing.wm", "--help", NULL};
    assert(wm_packed_assets_open(&assets, 4, help, error, sizeof(error)));
    assert(!assets.temporary_root[0]);
    wm_packed_assets_close(&assets);

    char *missing[] = {"wii-menu", "--assets", "/missing.wm", NULL};
    assert(!wm_packed_assets_open(&assets, 3, missing, error, sizeof(error)));
    snprintf(extracted, sizeof(extracted), "%s", assets.temporary_root);
    wm_packed_assets_close(&assets);
    assert(!extracted[0] || stat(extracted, &status) != 0);

    assert(unlink(package) == 0);
    assert(unlink(input) == 0);
    assert(rmdir(source) == 0);
    assert(rmdir(root) == 0);
    return 0;
}
