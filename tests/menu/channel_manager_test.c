#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void join(char output[4096], const char *directory, const char *name) {
    int length = snprintf(output, 4096, "%s/%s", directory, name);
    assert(length > 0 && length < 4096);
}

static void write_file(const char *path, const char *contents) {
    FILE *file = fopen(path, "wb");
    assert(file);
    size_t length = strlen(contents);
    assert(fwrite(contents, 1, length, file) == length);
    assert(fclose(file) == 0);
}

static bool exists(const char *path) {
    struct stat metadata;
    return lstat(path, &metadata) == 0;
}

static void run(const char *program, const char *assets, const char *command,
                const char *target, const char *option, int expected) {
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        char *const arguments[] = {(char *)program,
                                   "--assets",
                                   (char *)assets,
                                   (char *)command,
                                   (char *)target,
                                   (char *)option,
                                   NULL};
        execv(program, arguments);
        _exit(127);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == expected);
}

int main(int argc, char **argv) {
    assert(argc == 4);
    const char *program = argv[1];
    const char *vector_package = argv[2];
    const char *png_package = argv[3];
    char assets[] = "/tmp/wii-menu-channel-manager-XXXXXX";
    assert(mkdtemp(assets));

    char path[4096];
    join(path, assets, "channels.json");
    write_file(path, "{\"schemaVersion\":1,\"channels\":[],\"defaultOrder\":[]}");

    run(program, assets, "validate", vector_package, NULL, 0);
    run(program, assets, "remove", vector_package, NULL, 1);
    run(program, assets, "add", vector_package, NULL, 0);
    run(program, assets, "remove", vector_package, NULL, 0);
    run(program, assets, "restore", "Example Channel", NULL, 0);
    run(program, assets, "remove", "custom-example", NULL, 0);
    join(path, assets, "custom-channels/custom-example/unowned.txt");
    write_file(path, "keep this unowned file");
    run(program, assets, "purge", "custom-example", "--yes", 1);
    assert(exists(path));
    assert(unlink(path) == 0);
    run(program, assets, "purge", "custom-example", "--yes", 0);
    join(path, assets, "custom-channels/custom-example");
    assert(!exists(path));

    run(program, assets, "validate", png_package, NULL, 0);
    run(program, assets, "add", png_package, NULL, 0);
    join(path, assets, "custom-channels/custom-studio-channel-9899b686/icon.wmra");
    assert(exists(path));
    run(program, assets, "remove", "Studio Channel", NULL, 0);
    run(program, assets, "purge", "custom-studio-channel-9899b686", "--yes", 0);

    join(path, assets, "channel-audio/custom-example.wav");
    write_file(path, "preserve an unrelated existing file");
    run(program, assets, "add", vector_package, NULL, 1);
    FILE *file = fopen(path, "rb");
    assert(file);
    char contents[64] = {0};
    assert(fread(contents, 1, sizeof(contents) - 1, file) > 0);
    assert(strcmp(contents, "preserve an unrelated existing file") == 0);
    assert(fclose(file) == 0);
    assert(unlink(path) == 0);

    join(path, assets, "custom-channels/custom-example");
    assert(!exists(path));
    join(path, assets, "custom-channels");
    assert(rmdir(path) == 0);
    join(path, assets, "channel-audio");
    assert(rmdir(path) == 0);
    join(path, assets, "channels.local.json");
    assert(unlink(path) == 0);
    join(path, assets, ".channels.lock");
    assert(unlink(path) == 0);
    join(path, assets, "channels.json");
    assert(unlink(path) == 0);
    assert(rmdir(assets) == 0);
    return 0;
}
