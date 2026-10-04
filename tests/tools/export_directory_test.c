#define _XOPEN_SOURCE 700
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#include "atomic_file.h"
#include "export_directory.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include "console_common/support/tool_io.h"
#include "platform/windows/junction.h"
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static bool join(char output[256], const char *directory, const char *name) {
    int length = snprintf(output, 256, "%s/%s", directory, name);
    return length > 0 && length < 256;
}

static bool publish_probe(const char *root, const char *relative) {
    char directory[256], path[256];
    return wm_export_directory_child(root, relative, 0700) &&
           join(directory, root, relative) && join(path, directory, "probe.bin") &&
           wm_atomic_file_replace(path, "new", 3);
}

int main(void) {
    char root[] = "export-directory-XXXXXX";
    assert(mkdtemp(root));
    assert(wm_export_directory_root(".", 0700));

    char outside[256], linked[256], linked_slash[256], linked_dot[256];
    char sentinel[256];
    assert(join(outside, root, "outside"));
    assert(join(linked, root, "linked"));
    assert(snprintf(linked_slash, sizeof(linked_slash), "%s/", linked) > 0);
    assert(join(linked_dot, linked, "."));
    assert(join(sentinel, outside, "probe.bin"));
    assert(mkdir(outside, 0700) == 0);
    assert(wm_atomic_file_replace(sentinel, "keep", 4));
#ifdef _WIN32
    assert(cc_test_junction(linked, outside));
#else
    assert(symlink("outside", linked) == 0);
#endif

    assert(!wm_export_directory_root(linked, 0700));
    assert(!wm_export_directory_root(linked_slash, 0700));
    assert(!wm_export_directory_root(linked_dot, 0700));
    assert(!wm_export_directory_child(linked_slash, "child", 0700));
    assert(!wm_export_directory_child(linked_dot, "child", 0700));
    assert(!publish_probe(root, "linked"));
    assert(!wm_export_directory_child(root, "linked/child", 0700));
    char outside_child[256];
    assert(join(outside_child, outside, "child"));
    struct stat metadata;
    assert(lstat(outside_child, &metadata) != 0 && errno == ENOENT);
    FILE *file = fopen(sentinel, "rb");
    char retained[4];
    assert(file && fread(retained, 1, sizeof(retained), file) == sizeof(retained));
    assert(fclose(file) == 0);
    assert(memcmp(retained, "keep", sizeof(retained)) == 0);

    char safe[256], nested[256];
    assert(join(safe, root, "safe"));
    assert(join(nested, safe, "nested"));
    assert(wm_export_directory_child(root, "safe", 0700));
    assert(wm_export_directory_child(root, "safe/nested", 0700));
    assert(wm_export_directory_child(root, "safe/nested", 0700));
    assert(!wm_export_directory_child(root, "missing/child", 0700));

#ifndef _WIN32
    /* macOS routes /tmp through /private/tmp. This is a trusted ancestor
     * of the selected output root, not a symlink inside that root. */
    char system_root[] = "/tmp/wm-export-directory-XXXXXX";
    assert(mkdtemp(system_root));
    assert(wm_export_directory_root(system_root, 0700));
    assert(wm_export_directory_child(system_root, "assets", 0700));
    char system_child[256];
    assert(join(system_child, system_root, "assets"));
    assert(rmdir(system_child) == 0);
    assert(rmdir(system_root) == 0);

#endif

    assert(rmdir(nested) == 0);
    assert(rmdir(safe) == 0);
#ifdef _WIN32
    assert(rmdir(linked) == 0);
#else
    assert(unlink(linked) == 0);
#endif
    assert(unlink(sentinel) == 0);
    assert(rmdir(outside) == 0);
    assert(rmdir(root) == 0);
    return 0;
}
