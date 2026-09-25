#if defined(__linux__)
#define _GNU_SOURCE 1
#endif
#define _XOPEN_SOURCE 700
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#include "wii_menu/json.h"
#include "wad/crypto.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <sys/stdio.h>
#elif defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#endif

/* Work stays beside the destination so publishing uses a same-filesystem
 * rename. Updates publish a new generation; existing assets are never edited. */
enum {
    PREPARE_PATH_CAPACITY = 4096,
    PREPARE_MAX_CHANNELS = 2048,
    PREPARE_MAX_MANIFEST = 8 * 1024 * 1024,
    PREPARE_STAGE_NAME_LENGTH = 44
};

static const char PREPARE_JOURNAL_MAGIC[] = "WM_PREPARE_RECOVERY_1\n";
static const char PREPARE_JOURNAL_NAME[] = ".wm-prepare-journal";
static const char PREPARE_JOURNAL_NEXT[] = ".wm-prepare-journal.next";
static const char PREPARE_STAGE_MARKER[] = ".wm-prepare-owner";

typedef struct PrepareRecoveryRecord {
    char identity[41];
    char stage[PREPARE_STAGE_NAME_LENGTH + 1];
} PrepareRecoveryRecord;

typedef struct PrepareChoices {
    char ids[PREPARE_MAX_CHANNELS][17];
    size_t count;
} PrepareChoices;

typedef struct PrepareChannel {
    char id[17];
    char content_sha1[41];
    char tmd_sha1[41];
    size_t token;
    bool incoming;
} PrepareChannel;

typedef struct PrepareManifest {
    WmJson json;
    PrepareChannel *channels;
    size_t count;
    size_t default_order;
    size_t saved_layout;
    char language[4];
} PrepareManifest;

static size_t find_channel(const PrepareManifest *manifest, const char *id);

static bool path_join(char *result, size_t capacity,
                      const char *directory, const char *name) {
    int length = snprintf(result, capacity, "%s/%s", directory, name);
    return length > 0 && (size_t)length < capacity;
}

static bool publish_directory_no_replace(const char *source,
                                         const char *destination) {
#if defined(__APPLE__)
    return renamex_np(source, destination, RENAME_EXCL) == 0;
#elif defined(__linux__) && defined(SYS_renameat2) && \
      defined(RENAME_NOREPLACE)
    return syscall(SYS_renameat2, AT_FDCWD, source, AT_FDCWD,
                   destination, RENAME_NOREPLACE) == 0;
#else
    (void)source;
    (void)destination;
    errno = ENOTSUP;
    return false;
#endif
}

static bool validate_parents(const char *path, bool create_missing) {
    size_t length = strlen(path);
    if (length == 0 || length >= PREPARE_PATH_CAPACITY) return false;
    char current[PREPARE_PATH_CAPACITY];
    memcpy(current, path, length + 1);
    for (size_t index = 1; index <= length; index++) {
        if (current[index] != '/' && current[index] != '\0') continue;
        char saved = current[index];
        current[index] = '\0';
        if (current[0] && create_missing && mkdir(current, 0700) != 0 &&
            errno != EEXIST) return false;
        struct stat metadata;
        if (current[0] && (lstat(current, &metadata) != 0 ||
                           !S_ISDIR(metadata.st_mode))) return false;
        current[index] = saved;
    }
    return true;
}

static bool make_parents(const char *path) {
    return validate_parents(path, true);
}

static bool make_file_parent(const char *path) {
    char parent[PREPARE_PATH_CAPACITY];
    size_t length = strlen(path);
    if (length == 0 || length >= sizeof(parent)) return false;
    memcpy(parent, path, length + 1);
    char *separator = strrchr(parent, '/');
    if (!separator) return true;
    if (separator == parent) separator[1] = '\0';
    else *separator = '\0';
    return make_parents(parent);
}

static bool remove_tree(const char *path) {
    struct stat metadata;
    if (lstat(path, &metadata) != 0) return errno == ENOENT;
    if (!S_ISDIR(metadata.st_mode)) return unlink(path) == 0;
    DIR *directory = opendir(path);
    if (!directory) return false;
    bool okay = true;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) continue;
        char child[PREPARE_PATH_CAPACITY];
        if (!path_join(child, sizeof(child), path, entry->d_name) ||
            !remove_tree(child)) okay = false;
    }
    if (closedir(directory) != 0) okay = false;
    if (okay && rmdir(path) != 0) okay = false;
    return okay;
}

static bool copy_file(const char *source, const char *destination) {
    int input = open(source, O_RDONLY | O_NOFOLLOW);
    if (input < 0) return false;
    struct stat metadata;
    bool okay = fstat(input, &metadata) == 0 && S_ISREG(metadata.st_mode);
    int output = okay ? open(destination, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW,
                            0600) : -1;
    if (output < 0) okay = false;
    char bytes[65536];
    while (okay) {
        ssize_t count = read(input, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) okay = false;
        if (count <= 0) break;
        size_t written = 0;
        while (written < (size_t)count) {
            ssize_t amount = write(output, bytes + written, (size_t)count - written);
            if (amount < 0 && errno == EINTR) continue;
            if (amount <= 0) {
                okay = false;
                break;
            }
            written += (size_t)amount;
        }
    }
    if (output >= 0 && close(output) != 0) okay = false;
    if (close(input) != 0) okay = false;
    if (!okay && output >= 0) unlink(destination);
    return okay;
}

static bool copy_tree(const char *source, const char *destination) {
    struct stat metadata;
    if (lstat(source, &metadata) != 0) return false;
    if (S_ISREG(metadata.st_mode)) return copy_file(source, destination);
    if (!S_ISDIR(metadata.st_mode) || mkdir(destination, 0700) != 0) return false;
    DIR *directory = opendir(source);
    if (!directory) return false;
    bool okay = true;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) continue;
        char from[PREPARE_PATH_CAPACITY];
        char to[PREPARE_PATH_CAPACITY];
        if (!path_join(from, sizeof(from), source, entry->d_name) ||
            !path_join(to, sizeof(to), destination, entry->d_name) ||
            !copy_tree(from, to)) okay = false;
        if (!okay) break;
    }
    if (closedir(directory) != 0) okay = false;
    return okay;
}

static bool regular_tree(const char *path) {
    struct stat metadata;
    if (lstat(path, &metadata) != 0) return false;
    if (S_ISREG(metadata.st_mode)) return true;
    if (!S_ISDIR(metadata.st_mode)) return false;
    DIR *directory = opendir(path);
    if (!directory) return false;
    bool okay = true;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) continue;
        char child[PREPARE_PATH_CAPACITY];
        if (!path_join(child, sizeof(child), path, entry->d_name) ||
            !regular_tree(child)) okay = false;
        if (!okay) break;
    }
    if (closedir(directory) != 0) okay = false;
    return okay;
}

static bool normalize_title_id(const char *value, char normalized[17]) {
    if (strlen(value) != 16) return false;
    for (size_t index = 0; index < 16; index++) {
        unsigned char character = (unsigned char)value[index];
        if (!isxdigit(character)) return false;
        normalized[index] = (char)tolower(character);
    }
    normalized[16] = '\0';
    return true;
}

static bool source_hash(const WmJson *json, size_t source,
                        const char *name, char value[41]) {
    size_t token = wm_json_member(json, source, name);
    if (token == WM_JSON_INVALID) return true;
    if (!wm_json_copy(json, token, value, 41) || strlen(value) != 40)
        return false;
    for (size_t index = 0; index < 40; index++) {
        if (!((value[index] >= '0' && value[index] <= '9') ||
              (value[index] >= 'a' && value[index] <= 'f'))) return false;
    }
    return true;
}

static bool choice_contains(const PrepareChoices *choices, const char *id) {
    for (size_t index = 0; index < choices->count; index++) {
        if (strcmp(choices->ids[index], id) == 0) return true;
    }
    return false;
}

static bool add_choice(PrepareChoices *choices, const char *value) {
    char id[17];
    if (!normalize_title_id(value, id)) return false;
    if (choice_contains(choices, id)) return true;
    if (choices->count >= PREPARE_MAX_CHANNELS) return false;
    strcpy(choices->ids[choices->count++], id);
    return true;
}

static void close_manifest(PrepareManifest *manifest) {
    free(manifest->channels);
    wm_json_free(&manifest->json);
    memset(manifest, 0, sizeof(*manifest));
}

static bool open_manifest(const char *directory, PrepareManifest *manifest) {
    memset(manifest, 0, sizeof(*manifest));
    char path[PREPARE_PATH_CAPACITY];
    struct stat metadata;
    if (!path_join(path, sizeof(path), directory, "channels.json") ||
        lstat(path, &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
        !wm_json_load(&manifest->json, path, PREPARE_MAX_MANIFEST)) return false;
    const WmJson *json = &manifest->json;
    int version = 0;
    size_t channels = wm_json_member(json, 0, "channels");
    manifest->default_order = wm_json_member(json, 0, "defaultOrder");
    manifest->saved_layout = wm_json_member(json, 0, "savedLayout");
    bool okay = wm_json_integer(json, wm_json_member(json, 0, "schemaVersion"),
                               &version) && version == 1 &&
                channels != WM_JSON_INVALID &&
                json->tokens[channels].type == WM_JSON_ARRAY &&
                json->tokens[channels].children <= PREPARE_MAX_CHANNELS &&
                manifest->default_order != WM_JSON_INVALID &&
                json->tokens[manifest->default_order].type == WM_JSON_ARRAY &&
                wm_json_copy(json, wm_json_member(json, 0, "language"),
                             manifest->language, sizeof(manifest->language));
    if (!okay) {
        close_manifest(manifest);
        return false;
    }
    manifest->count = json->tokens[channels].children;
    manifest->channels = calloc(manifest->count ? manifest->count : 1,
                                sizeof(*manifest->channels));
    if (!manifest->channels) {
        close_manifest(manifest);
        return false;
    }
    for (size_t index = 0; index < manifest->count; index++) {
        size_t entry = wm_json_index(json, channels, index);
        char original[32];
        if (entry == WM_JSON_INVALID || json->tokens[entry].type != WM_JSON_OBJECT ||
            !wm_json_copy(json, wm_json_member(json, entry, "id"),
                          original, sizeof(original)) ||
            !normalize_title_id(original, manifest->channels[index].id) ||
            !wm_json_equals(json, wm_json_member(json, entry, "id"),
                            manifest->channels[index].id)) {
            okay = false;
            break;
        }
        manifest->channels[index].token = entry;
        size_t source = wm_json_member(json, entry, "source");
        if (!source_hash(json, source, "contentSha1",
                         manifest->channels[index].content_sha1) ||
            !source_hash(json, source, "tmdSha1",
                         manifest->channels[index].tmd_sha1)) {
            okay = false;
            break;
        }
        for (size_t previous = 0; previous < index; previous++) {
            if (strcmp(manifest->channels[previous].id,
                       manifest->channels[index].id) == 0) okay = false;
        }
        if (!okay) break;
    }
    for (size_t index = 0;
         okay && index < json->tokens[manifest->default_order].children;
         index++) {
        char original[32];
        char id[17];
        size_t token = wm_json_index(json, manifest->default_order, index);
        okay = wm_json_copy(json, token, original, sizeof(original)) &&
               normalize_title_id(original, id) &&
               wm_json_equals(json, token, id) &&
               find_channel(manifest, id) != SIZE_MAX;
    }
    if (!okay) close_manifest(manifest);
    return okay;
}

static size_t find_channel(const PrepareManifest *manifest, const char *id) {
    for (size_t index = 0; index < manifest->count; index++) {
        if (strcmp(manifest->channels[index].id, id) == 0) return index;
    }
    return SIZE_MAX;
}

static bool write_json_token(FILE *stream, const WmJson *json, size_t token) {
    if (token == WM_JSON_INVALID || token >= json->count) return false;
    const WmJsonToken *item = &json->tokens[token];
    size_t start = item->start;
    size_t end = item->end;
    if (item->type == WM_JSON_STRING) {
        if (start == 0 || end >= json->length) return false;
        start--;
        end++;
    }
    return end >= start && end <= json->length &&
           fwrite(json->source + start, 1, end - start, stream) == end - start;
}

static bool resolved_output(const char *requested, char *output,
                            size_t capacity, char *parent,
                            size_t parent_capacity, bool create_parents) {
    size_t length = strlen(requested);
    if (!length || length >= PREPARE_PATH_CAPACITY) return false;
    char path[PREPARE_PATH_CAPACITY];
    memcpy(path, requested, length + 1);
    while (length > 1 && path[length - 1] == '/') path[--length] = '\0';
    char *separator = strrchr(path, '/');
    const char *name = separator ? separator + 1 : path;
    if (!name[0] || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0) return false;
    char basename[PREPARE_PATH_CAPACITY];
    strcpy(basename, name);
    if (separator) {
        if (separator == path) separator[1] = '\0';
        else *separator = '\0';
    }
    const char *parent_path = separator ? path : ".";
    if (!validate_parents(parent_path, create_parents)) return false;
    char *canonical_parent = realpath(parent_path, NULL);
    if (!canonical_parent) return false;
    bool okay = strlen(canonical_parent) < parent_capacity &&
                path_join(output, capacity, canonical_parent, basename);
    if (okay) strcpy(parent, canonical_parent);
    free(canonical_parent);
    return okay;
}

static bool tool_path(char *result, size_t capacity,
                      const char *binary_directory, const char *name) {
    return path_join(result, capacity, binary_directory, name);
}

static bool run_tool(const char *binary_directory, const char *name,
                     const char *working_directory, char *const arguments[],
                     bool plan_output) {
    char executable[PREPARE_PATH_CAPACITY];
    if (!tool_path(executable, sizeof(executable), binary_directory, name))
        return false;
    fprintf(plan_output ? stderr : stdout, "Preparing: %s\n", name);
    fflush(plan_output ? stderr : stdout);
    pid_t child = fork();
    if (child < 0) return false;
    if (child == 0) {
        if (plan_output && dup2(STDERR_FILENO, STDOUT_FILENO) < 0) _exit(127);
        if (chdir(working_directory) != 0) _exit(127);
        execv(executable, arguments);
        _exit(127);
    }
    int status;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool copy_selected_channel(const char *incoming_assets,
                                  const char *staged_assets, const char *id) {
    char source[PREPARE_PATH_CAPACITY];
    char destination[PREPARE_PATH_CAPACITY];
    char relative[96];
    int length = snprintf(relative, sizeof(relative), "channel-layouts/%s", id);
    if (length < 0 || (size_t)length >= sizeof(relative) ||
        !path_join(source, sizeof(source), incoming_assets, relative) ||
        !path_join(destination, sizeof(destination), staged_assets, relative) ||
        !remove_tree(destination)) return false;
    struct stat metadata;
    if (lstat(source, &metadata) == 0) {
        if (!S_ISDIR(metadata.st_mode) || !make_file_parent(destination) ||
            !copy_tree(source, destination)) return false;
    } else if (errno != ENOENT) {
        return false;
    }
    length = snprintf(relative, sizeof(relative), "channel-audio/%s.wav", id);
    if (length < 0 || (size_t)length >= sizeof(relative) ||
        !path_join(source, sizeof(source), incoming_assets, relative) ||
        !path_join(destination, sizeof(destination), staged_assets, relative) ||
        !remove_tree(destination)) return false;
    if (lstat(source, &metadata) == 0) {
        if (!S_ISREG(metadata.st_mode) || !make_file_parent(destination) ||
            !copy_file(source, destination)) return false;
    } else if (errno != ENOENT) {
        return false;
    }
    return true;
}

static void hash_name(WmSha1 *sha1, char kind, const char *relative) {
    wm_sha1_update(sha1, (const uint8_t *)&kind, 1);
    wm_sha1_update(sha1, (const uint8_t *)relative, strlen(relative) + 1);
}

static void format_sha1(const uint8_t digest[20], char hexadecimal[41]) {
    for (size_t byte = 0; byte < 20; byte++) {
        snprintf(hexadecimal + byte * 2, 3, "%02x", digest[byte]);
    }
    hexadecimal[40] = '\0';
}

static bool lowercase_hex(const char *value, size_t length) {
    for (size_t index = 0; index < length; index++) {
        char digit = value[index];
        if (!((digit >= '0' && digit <= '9') ||
              (digit >= 'a' && digit <= 'f'))) return false;
    }
    return true;
}

static void recovery_identity(char kind, const char *path, char result[41]) {
    WmSha1 sha1;
    uint8_t digest[20];
    wm_sha1_init(&sha1);
    wm_sha1_update(&sha1, (const uint8_t *)&kind, 1);
    wm_sha1_update(&sha1, (const uint8_t *)path, strlen(path));
    wm_sha1_final(&sha1, digest);
    format_sha1(digest, result);
}

static bool write_all(int descriptor, const char *bytes, size_t size) {
    while (size) {
        ssize_t amount = write(descriptor, bytes, size);
        if (amount < 0 && errno == EINTR) continue;
        if (amount <= 0) return false;
        bytes += amount;
        size -= (size_t)amount;
    }
    return true;
}

static bool sync_directory(const char *path) {
    int descriptor = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (descriptor < 0) return false;
    bool okay = fsync(descriptor) == 0;
    if (close(descriptor) != 0) okay = false;
    return okay;
}

static int lock_preparation_parent(const char *parent) {
    char path[PREPARE_PATH_CAPACITY];
    if (!path_join(path, sizeof(path), parent, ".wm-prepare.lock")) return -1;
    int descriptor = open(path, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC,
                          0600);
    if (descriptor < 0) return -1;
    struct stat metadata;
    if (fstat(descriptor, &metadata) != 0 ||
        !S_ISREG(metadata.st_mode) || metadata.st_uid != getuid() ||
        (metadata.st_mode & 0022) != 0 ||
        flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        close(descriptor);
        return -1;
    }
    return descriptor;
}

static bool recovery_record_text(const PrepareRecoveryRecord *record,
                                 char text[128], size_t *length) {
    int written = snprintf(text, 128, "%s%s\n%s\n", PREPARE_JOURNAL_MAGIC,
                           record->identity, record->stage);
    if (written < 0 || written >= 128) return false;
    *length = (size_t)written;
    return true;
}

static bool read_recovery_record(const char *path,
                                 PrepareRecoveryRecord *record) {
    int descriptor = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (descriptor < 0) return false;
    struct stat metadata;
    bool okay = fstat(descriptor, &metadata) == 0 &&
                S_ISREG(metadata.st_mode) && metadata.st_uid == getuid();
    size_t magic_size = strlen(PREPARE_JOURNAL_MAGIC);
    size_t expected_size = magic_size + 40 + 1 +
                           PREPARE_STAGE_NAME_LENGTH + 1;
    if (okay) okay = metadata.st_size == (off_t)expected_size;
    char text[128];
    size_t received = 0;
    while (okay && received < expected_size) {
        ssize_t amount = read(descriptor, text + received,
                              expected_size - received);
        if (amount < 0 && errno == EINTR) continue;
        if (amount <= 0) okay = false;
        else received += (size_t)amount;
    }
    if (close(descriptor) != 0) okay = false;
    if (!okay || memcmp(text, PREPARE_JOURNAL_MAGIC, magic_size) != 0 ||
        text[magic_size + 40] != '\n' ||
        text[expected_size - 1] != '\n' ||
        !lowercase_hex(text + magic_size, 40) ||
        memcmp(text + magic_size + 41, ".wm-prepare-", 12) != 0 ||
        !lowercase_hex(text + magic_size + 41 + 12, 32)) return false;
    memcpy(record->identity, text + magic_size, 40);
    record->identity[40] = '\0';
    memcpy(record->stage, text + magic_size + 41,
           PREPARE_STAGE_NAME_LENGTH);
    record->stage[PREPARE_STAGE_NAME_LENGTH] = '\0';
    return true;
}

static bool write_recovery_record(const char *path,
                                  const PrepareRecoveryRecord *record) {
    char text[128];
    size_t length;
    if (!recovery_record_text(record, text, &length)) return false;
    int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL |
                             O_NOFOLLOW | O_CLOEXEC, 0600);
    if (descriptor < 0) return false;
    bool okay = write_all(descriptor, text, length) && fsync(descriptor) == 0;
    if (close(descriptor) != 0) okay = false;
    if (!okay) unlink(path);
    return okay;
}

static bool same_record(const PrepareRecoveryRecord *left,
                        const PrepareRecoveryRecord *right) {
    return strcmp(left->identity, right->identity) == 0 &&
           strcmp(left->stage, right->stage) == 0;
}

static bool recover_owned_stage(const char *parent, const char *identity) {
    char journal[PREPARE_PATH_CAPACITY];
    char next[PREPARE_PATH_CAPACITY];
    if (!path_join(journal, sizeof(journal), parent, PREPARE_JOURNAL_NAME) ||
        !path_join(next, sizeof(next), parent, PREPARE_JOURNAL_NEXT))
        return false;
    struct stat metadata;
    bool journal_exists = lstat(journal, &metadata) == 0;
    if (!journal_exists && errno != ENOENT) return false;
    bool next_exists = lstat(next, &metadata) == 0;
    if (!next_exists && errno != ENOENT) return false;
    PrepareRecoveryRecord record;
    if (journal_exists && (!read_recovery_record(journal, &record) ||
                           strcmp(record.identity, identity) != 0))
        return false;
    if (next_exists) {
        PrepareRecoveryRecord pending;
        if (!read_recovery_record(next, &pending) ||
            strcmp(pending.identity, identity) != 0 ||
            (journal_exists && !same_record(&record, &pending))) return false;
    }
    if (!journal_exists) {
        return !next_exists || (unlink(next) == 0 && sync_directory(parent));
    }
    char stage[PREPARE_PATH_CAPACITY];
    if (!path_join(stage, sizeof(stage), parent, record.stage)) return false;
    if (lstat(stage, &metadata) == 0) {
        if (!S_ISDIR(metadata.st_mode) || metadata.st_uid != getuid() ||
            (metadata.st_mode & 0777) != 0700) return false;
        char marker[PREPARE_PATH_CAPACITY];
        PrepareRecoveryRecord owned;
        if (!path_join(marker, sizeof(marker), stage,
                       PREPARE_STAGE_MARKER) ||
            !read_recovery_record(marker, &owned) ||
            !same_record(&record, &owned) || !remove_tree(stage)) return false;
    } else if (errno != ENOENT) {
        return false;
    }
    if (next_exists && unlink(next) != 0) return false;
    return unlink(journal) == 0 && sync_directory(parent);
}

static bool create_owned_stage(const char *parent, const char *identity,
                               char stage[PREPARE_PATH_CAPACITY]) {
    uint8_t random_bytes[16];
    int random_file = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (random_file < 0) return false;
    size_t received = 0;
    while (received < sizeof(random_bytes)) {
        ssize_t amount = read(random_file, random_bytes + received,
                              sizeof(random_bytes) - received);
        if (amount < 0 && errno == EINTR) continue;
        if (amount <= 0) break;
        received += (size_t)amount;
    }
    bool okay = close(random_file) == 0 && received == sizeof(random_bytes);
    if (!okay) return false;
    PrepareRecoveryRecord record = {0};
    memcpy(record.identity, identity, sizeof(record.identity));
    memcpy(record.stage, ".wm-prepare-", 12);
    for (size_t index = 0; index < sizeof(random_bytes); index++) {
        snprintf(record.stage + 12 + index * 2, 3, "%02x",
                 random_bytes[index]);
    }
    char journal[PREPARE_PATH_CAPACITY];
    char next[PREPARE_PATH_CAPACITY];
    if (!path_join(stage, PREPARE_PATH_CAPACITY, parent, record.stage) ||
        !path_join(journal, sizeof(journal), parent, PREPARE_JOURNAL_NAME) ||
        !path_join(next, sizeof(next), parent, PREPARE_JOURNAL_NEXT))
        return false;
    struct stat metadata;
    if (lstat(stage, &metadata) == 0 || errno != ENOENT ||
        !write_recovery_record(next, &record) ||
        !publish_directory_no_replace(next, journal) ||
        !sync_directory(parent)) return false;
    if (mkdir(stage, 0700) != 0) {
        unlink(journal);
        sync_directory(parent);
        return false;
    }
    char marker[PREPARE_PATH_CAPACITY];
    if (!path_join(marker, sizeof(marker), stage, PREPARE_STAGE_MARKER) ||
        !write_recovery_record(marker, &record) ||
        !sync_directory(stage) || !sync_directory(parent)) {
        if (remove_tree(stage)) {
            unlink(journal);
            sync_directory(parent);
        }
        return false;
    }
    return true;
}

static bool hash_regular_file(const char *path, char hexadecimal[41]) {
    int file = open(path, O_RDONLY | O_NOFOLLOW);
    if (file < 0) return false;
    struct stat metadata;
    bool okay = fstat(file, &metadata) == 0 && S_ISREG(metadata.st_mode);
    WmSha1 sha1;
    wm_sha1_init(&sha1);
    uint8_t bytes[65536];
    while (okay) {
        ssize_t count = read(file, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) okay = false;
        if (count <= 0) break;
        wm_sha1_update(&sha1, bytes, (size_t)count);
    }
    if (close(file) != 0) okay = false;
    if (!okay) return false;
    uint8_t digest[20];
    wm_sha1_final(&sha1, digest);
    format_sha1(digest, hexadecimal);
    return true;
}

static bool hash_asset_tree(WmSha1 *sha1, const char *root,
                            const char *relative) {
    char path[PREPARE_PATH_CAPACITY];
    if (!path_join(path, sizeof(path), root, relative)) return false;
    struct stat metadata;
    if (lstat(path, &metadata) != 0) {
        if (errno != ENOENT) return false;
        hash_name(sha1, 'M', relative);
        return true;
    }
    if (S_ISREG(metadata.st_mode)) {
        hash_name(sha1, 'F', relative);
        int file = open(path, O_RDONLY | O_NOFOLLOW);
        if (file < 0 || fstat(file, &metadata) != 0 ||
            !S_ISREG(metadata.st_mode)) {
            if (file >= 0) close(file);
            return false;
        }
        char bytes[65536];
        bool okay = true;
        while (okay) {
            ssize_t count = read(file, bytes, sizeof(bytes));
            if (count < 0 && errno == EINTR) continue;
            if (count < 0) okay = false;
            if (count <= 0) break;
            wm_sha1_update(sha1, (const uint8_t *)bytes, (size_t)count);
        }
        if (close(file) != 0) okay = false;
        return okay;
    }
    if (!S_ISDIR(metadata.st_mode)) return false;
    hash_name(sha1, 'D', relative);
    struct dirent **entries = NULL;
    int count = scandir(path, &entries, NULL, alphasort);
    if (count < 0) return false;
    bool okay = true;
    for (int index = 0; index < count; index++) {
        const char *name = entries[index]->d_name;
        if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0 && okay) {
            char child[PREPARE_PATH_CAPACITY];
            okay = path_join(child, sizeof(child), relative, name) &&
                   hash_asset_tree(sha1, root, child);
        }
        free(entries[index]);
    }
    free(entries);
    return okay;
}

static bool channel_export_hash(const PrepareManifest *manifest,
                                size_t index, const char *assets,
                                char hexadecimal[41]) {
    const PrepareChannel *channel = &manifest->channels[index];
    WmSha1 sha1;
    wm_sha1_init(&sha1);
    const WmJsonToken *record = &manifest->json.tokens[channel->token];
    wm_sha1_update(&sha1, (const uint8_t *)manifest->json.source + record->start,
                   record->end - record->start);
    char relative[96];
    int length = snprintf(relative, sizeof(relative), "channel-layouts/%s",
                          channel->id);
    if (length < 0 || (size_t)length >= sizeof(relative) ||
        !hash_asset_tree(&sha1, assets, relative)) return false;
    length = snprintf(relative, sizeof(relative), "channel-audio/%s.wav",
                      channel->id);
    if (length < 0 || (size_t)length >= sizeof(relative) ||
        !hash_asset_tree(&sha1, assets, relative)) return false;
    uint8_t digest[20];
    wm_sha1_final(&sha1, digest);
    format_sha1(digest, hexadecimal);
    return true;
}

static bool print_channel_version(FILE *stream,
                                  const PrepareManifest *manifest,
                                  size_t index) {
    if (index == SIZE_MAX) return fputs("null", stream) != EOF;
    const WmJson *json = &manifest->json;
    size_t source = wm_json_member(json, manifest->channels[index].token,
                                   "source");
    int version = 0;
    if (!wm_json_integer(json, wm_json_member(json, source, "tmdVersion"),
                         &version)) return fputs("null", stream) != EOF;
    return fprintf(stream, "%d", version) >= 0;
}

static bool print_source_hash(FILE *stream, const char value[41]) {
    if (!value[0]) return fputs("null", stream) != EOF;
    return fprintf(stream, "\"%s\"", value) >= 0;
}

static bool choices_present(const PrepareManifest *incoming,
                            const PrepareChoices *replace_ids,
                            const PrepareChoices *keep_ids) {
    for (size_t index = 0; index < replace_ids->count; index++) {
        if (find_channel(incoming, replace_ids->ids[index]) == SIZE_MAX)
            return false;
    }
    for (size_t index = 0; index < keep_ids->count; index++) {
        if (find_channel(incoming, keep_ids->ids[index]) == SIZE_MAX ||
            choice_contains(replace_ids, keep_ids->ids[index])) return false;
    }
    return true;
}

static bool print_update_plan(FILE *stream, const char *base_assets,
                              const char *incoming_assets,
                              const char input_nand_sha1[41],
                              const PrepareChoices *replace_ids,
                              const PrepareChoices *keep_ids,
                              bool replace_all) {
    PrepareManifest base;
    PrepareManifest incoming;
    if (!open_manifest(base_assets, &base)) return false;
    if (!open_manifest(incoming_assets, &incoming)) {
        close_manifest(&base);
        return false;
    }
    bool okay = strcmp(base.language, incoming.language) == 0 &&
                choices_present(&incoming, replace_ids, keep_ids);
    typedef struct PreparePlanRow {
        size_t existing;
        char existing_hash[41];
        char incoming_hash[41];
        bool keep;
    } PreparePlanRow;
    PreparePlanRow *rows = calloc(incoming.count ? incoming.count : 1,
                                  sizeof(*rows));
    if (!rows) okay = false;
    for (size_t index = 0; index < incoming.count && okay; index++) {
        const char *id = incoming.channels[index].id;
        rows[index].existing = find_channel(&base, id);
        okay = incoming.channels[index].content_sha1[0] &&
               incoming.channels[index].tmd_sha1[0] &&
               channel_export_hash(&incoming, index, incoming_assets,
                                   rows[index].incoming_hash) &&
               (rows[index].existing == SIZE_MAX ||
                channel_export_hash(&base, rows[index].existing, base_assets,
                                    rows[index].existing_hash));
        rows[index].keep = choice_contains(keep_ids, id) ||
                           (rows[index].existing != SIZE_MAX && !replace_all &&
                            !choice_contains(replace_ids, id));
    }
    if (okay) okay = fprintf(stream,
                            "{\n  \"schemaVersion\": 3,\n"
                            "  \"inputNandSha1\": \"%s\",\n"
                            "  \"hashKind\": \"exported-channel-SHA1\",\n"
                            "  \"sourceHashKind\": "
                            "\"TMD-bytes-and-TMD-validated-active-content-SHA1\",\n"
                            "  \"rows\": [\n", input_nand_sha1) >= 0;
    for (size_t index = 0; index < incoming.count && okay; index++) {
        const char *id = incoming.channels[index].id;
        const PreparePlanRow *row = &rows[index];
        const char *change = row->existing == SIZE_MAX ? "new" :
                             strcmp(row->existing_hash,
                                    row->incoming_hash) == 0 ? "unchanged" :
                             "different";
        if (index && fputs(",\n", stream) == EOF) okay = false;
        if (okay && fprintf(stream, "    {\n"
                            "      \"id\": \"%s\",\n"
                            "      \"change\": \"%s\",\n"
                            "      \"action\": \"%s\",\n"
                            "      \"existingVersion\": ",
                            id, change, row->keep ? "keep" :
                            row->existing == SIZE_MAX ? "add" : "replace") < 0)
            okay = false;
        if (okay) okay = print_channel_version(stream, &base, row->existing);
        if (okay) okay = fputs(",\n      \"incomingVersion\": ",
                             stream) != EOF;
        if (okay) okay = print_channel_version(stream, &incoming, index);
        if (okay && row->existing != SIZE_MAX) {
            okay = fprintf(stream,
                           ",\n      \"existingExportSha1\": \"%s\"",
                           row->existing_hash) >= 0;
        } else if (okay) {
            okay = fputs(",\n      \"existingExportSha1\": null",
                         stream) != EOF;
        }
        if (okay) okay = fprintf(stream,
            ",\n      \"incomingExportSha1\": \"%s\",\n"
            "      \"existingContentSha1\": ",
            row->incoming_hash) >= 0;
        if (okay) okay = print_source_hash(stream,
            row->existing == SIZE_MAX ? "" :
            base.channels[row->existing].content_sha1);
        if (okay) okay = fputs(",\n      \"incomingContentSha1\": ",
                             stream) != EOF;
        if (okay) okay = print_source_hash(stream,
            incoming.channels[index].content_sha1);
        if (okay) okay = fputs(",\n      \"existingTmdSha1\": ",
                             stream) != EOF;
        if (okay) okay = print_source_hash(stream,
            row->existing == SIZE_MAX ? "" :
            base.channels[row->existing].tmd_sha1);
        if (okay) okay = fputs(",\n      \"incomingTmdSha1\": ",
                             stream) != EOF;
        if (okay) okay = print_source_hash(stream,
            incoming.channels[index].tmd_sha1);
        if (okay) okay = fputs("\n    }", stream) != EOF;
    }
    if (okay) okay = fputs("\n  ]\n}\n", stream) != EOF &&
                     fflush(stream) == 0;
    free(rows);
    close_manifest(&incoming);
    close_manifest(&base);
    return okay;
}

/* Match the exact plan emitted by this build. The plan contains both input
 * export hashes and the selected actions, so a stale or differently selected
 * review cannot silently publish an update. */
static bool verify_expected_plan(const char *expected_path,
                                 const char *base_assets,
                                 const char *incoming_assets,
                                 const char input_nand_sha1[41],
                                 const PrepareChoices *replace_ids,
                                 const PrepareChoices *keep_ids,
                                 bool replace_all) {
    int descriptor = open(expected_path, O_RDONLY | O_NOFOLLOW);
    if (descriptor < 0) return false;
    struct stat metadata;
    bool okay = fstat(descriptor, &metadata) == 0 &&
                S_ISREG(metadata.st_mode) &&
                metadata.st_size <= PREPARE_MAX_MANIFEST;
    FILE *expected = okay ? fdopen(descriptor, "rb") : NULL;
    if (!expected) {
        close(descriptor);
        return false;
    }
    FILE *actual = tmpfile();
    if (!actual) okay = false;
    if (okay) okay = print_update_plan(actual, base_assets, incoming_assets,
                                       input_nand_sha1,
                                       replace_ids, keep_ids, replace_all) &&
                     fseek(actual, 0, SEEK_SET) == 0;
    while (okay) {
        int reviewed = fgetc(expected);
        int current = fgetc(actual);
        if (reviewed != current) okay = false;
        if (reviewed == EOF || current == EOF) {
            if (ferror(expected) || ferror(actual)) okay = false;
            break;
        }
    }
    if (actual && fclose(actual) != 0) okay = false;
    if (fclose(expected) != 0) okay = false;
    return okay;
}

static size_t merged_index(const PrepareChannel *channels, size_t count,
                           const char *id) {
    for (size_t index = 0; index < count; index++) {
        if (strcmp(channels[index].id, id) == 0) return index;
    }
    return SIZE_MAX;
}

static bool append_default_order(FILE *stream, const PrepareManifest *manifest,
                                 const PrepareManifest *base,
                                 const PrepareChannel *merged, size_t count,
                                 bool *emitted, bool incoming, bool *first) {
    const WmJson *json = &manifest->json;
    size_t order = manifest->default_order;
    for (size_t index = 0; index < json->tokens[order].children; index++) {
        size_t token = wm_json_index(json, order, index);
        char original[32];
        char id[17];
        if (!wm_json_copy(json, token, original, sizeof(original)) ||
            !normalize_title_id(original, id)) return false;
        size_t target = merged_index(merged, count, id);
        if (target == SIZE_MAX || emitted[target]) continue;
        if (incoming && (find_channel(base, id) != SIZE_MAX ||
                         !merged[target].incoming)) continue;
        if (!*first && fputs(", ", stream) == EOF) return false;
        if (fprintf(stream, "\"%s\"", id) < 0) return false;
        emitted[target] = true;
        *first = false;
    }
    return true;
}

static bool write_merged_manifest(const char *staged_assets,
                                  const PrepareManifest *base,
                                  const PrepareManifest *incoming,
                                  const PrepareChannel *merged, size_t count) {
    char temporary[PREPARE_PATH_CAPACITY];
    char destination[PREPARE_PATH_CAPACITY];
    if (!path_join(temporary, sizeof(temporary), staged_assets,
                   "channels.json.tmp") ||
        !path_join(destination, sizeof(destination), staged_assets,
                   "channels.json")) return false;
    FILE *stream = fopen(temporary, "wb");
    if (!stream) return false;
    bool okay = fputs("{\n  \"schemaVersion\": 1,\n"
                     "  \"source\": {\"kind\": \"local-prepared-channel-update\"},\n"
                     "  \"language\": ", stream) != EOF &&
                write_json_token(stream, &base->json,
                                 wm_json_member(&base->json, 0, "language")) &&
                fputs(",\n  \"channels\": [\n", stream) != EOF;
    for (size_t index = 0; okay && index < count; index++) {
        const PrepareManifest *source = merged[index].incoming ? incoming : base;
        if (index && fputs(",\n", stream) == EOF) okay = false;
        if (okay) okay = write_json_token(stream, &source->json,
                                         merged[index].token);
    }
    if (okay) okay = fputs("\n  ],\n  \"defaultOrder\": [", stream) != EOF;
    bool *emitted = calloc(count ? count : 1, sizeof(*emitted));
    if (!emitted) okay = false;
    bool first = true;
    if (okay) okay = append_default_order(stream, base, base, merged, count,
                                         emitted, false, &first);
    if (okay) okay = append_default_order(stream, incoming, base, merged, count,
                                         emitted, true, &first);
    free(emitted);
    if (okay) okay = fputs("],\n  \"savedLayout\": ", stream) != EOF;
    if (okay && base->saved_layout != WM_JSON_INVALID) {
        okay = write_json_token(stream, &base->json, base->saved_layout);
    } else if (okay) {
        okay = fputs("null", stream) != EOF;
    }
    if (okay) okay = fputs(",\n  \"notes\": ["
                         "\"Installed title resources are local exports; "
                         "native modules and scripts are not executed.\"]\n}\n",
                         stream) != EOF;
    if (fclose(stream) != 0) okay = false;
    if (!okay) {
        unlink(temporary);
        return false;
    }
    /* The existing catalog is still in place at this point. Replace it only
     * after validating the newly written document itself. */
    if (okay) {
        WmJson parsed;
        okay = wm_json_load(&parsed, temporary, PREPARE_MAX_MANIFEST);
        if (okay) wm_json_free(&parsed);
    }
    if (okay) okay = rename(temporary, destination) == 0;
    if (!okay) unlink(temporary);
    return okay;
}

static bool update_channels(const char *base_assets,
                            const char *incoming_assets,
                            const char *staged_assets,
                            const PrepareChoices *replace_ids,
                            const PrepareChoices *keep_ids,
                            bool replace_all) {
    PrepareManifest base;
    PrepareManifest incoming;
    if (!open_manifest(base_assets, &base)) {
        fputs("The existing channel catalog is invalid.\n", stderr);
        return false;
    }
    if (!open_manifest(incoming_assets, &incoming)) {
        fputs("The incoming channel catalog is invalid.\n", stderr);
        close_manifest(&base);
        return false;
    }
    bool okay = strcmp(base.language, incoming.language) == 0;
    if (!okay) fputs("Channel catalog languages do not match.\n", stderr);
    if (okay) okay = choices_present(&incoming, replace_ids, keep_ids);
    if (!okay) fputs("A selected title is absent or both kept and replaced.\n",
                     stderr);
    PrepareChannel *merged = calloc(PREPARE_MAX_CHANNELS, sizeof(*merged));
    if (!merged) okay = false;
    size_t count = 0;
    if (okay) {
        for (size_t index = 0; index < base.count; index++) {
            merged[count++] = base.channels[index];
        }
        for (size_t index = 0; index < incoming.count && okay; index++) {
            const PrepareChannel *candidate = &incoming.channels[index];
            size_t existing = merged_index(merged, count, candidate->id);
            bool selected = !choice_contains(keep_ids, candidate->id) &&
                            (existing == SIZE_MAX || replace_all ||
                             choice_contains(replace_ids, candidate->id));
            if (!selected) continue;
            if (existing == SIZE_MAX && count >= PREPARE_MAX_CHANNELS) {
                okay = false;
                break;
            }
            PrepareChannel chosen = *candidate;
            chosen.incoming = true;
            if (existing == SIZE_MAX) merged[count++] = chosen;
            else merged[existing] = chosen;
            okay = copy_selected_channel(incoming_assets, staged_assets,
                                         candidate->id);
        }
        if (okay) okay = write_merged_manifest(staged_assets, &base,
                                               &incoming, merged, count);
    }
    free(merged);
    close_manifest(&incoming);
    close_manifest(&base);
    return okay;
}

static int usage(const char *program, int result) {
    fprintf(stderr,
            "Usage: %s --wad FILE --common-key-file FILE --output DIRECTORY "
            "[--nand FILE] [--nand-keys FILE] [--language ENG]\n"
            "       %s --update-from DIRECTORY --nand FILE --output DIRECTORY "
            "[--nand-keys FILE] [--nand-policy keep|replace] "
            "[--replace-channel ID] [--keep-channel ID] "
            "[--expect-plan FILE]\n"
            "       %s --plan --update-from DIRECTORY --nand FILE "
            "[--nand-keys FILE] [--nand-policy keep|replace] "
            "[--replace-channel ID] [--keep-channel ID]\n"
            "       %s --recover --output DIRECTORY\n"
            "       %s --recover --plan --update-from DIRECTORY\n",
            program, program, program, program, program);
    fputs("Output must be a new directory. Updates preserve the source "
          "directory and its saved placement. Inputs stay local.\n", stderr);
    return result;
}

int main(int argc, char **argv) {
    const char *wad = NULL, *common_key = NULL, *nand = NULL;
    const char *nand_keys = NULL, *output_request = NULL;
    const char *update_from = NULL, *language = NULL;
    const char *expected_plan = NULL;
    bool replace_all = false, policy_set = false;
    bool plan = false, recover = false;
    PrepareChoices replace_ids = {0};
    PrepareChoices keep_ids = {0};
    for (int index = 1; index < argc; index++) {
        const char *option = argv[index];
        if (strcmp(option, "--help") == 0) return usage(argv[0], 0);
        if (strcmp(option, "--plan") == 0) {
            if (plan) return usage(argv[0], 2);
            plan = true;
            continue;
        }
        if (strcmp(option, "--recover") == 0) {
            if (recover) return usage(argv[0], 2);
            recover = true;
            continue;
        }
        if (index + 1 >= argc) return usage(argv[0], 2);
        const char *value = argv[++index];
        if (strcmp(option, "--wad") == 0) {
            if (wad) return usage(argv[0], 2);
            wad = value;
        } else if (strcmp(option, "--common-key-file") == 0) {
            if (common_key) return usage(argv[0], 2);
            common_key = value;
        } else if (strcmp(option, "--nand") == 0) {
            if (nand) return usage(argv[0], 2);
            nand = value;
        } else if (strcmp(option, "--nand-keys") == 0) {
            if (nand_keys) return usage(argv[0], 2);
            nand_keys = value;
        } else if (strcmp(option, "--output") == 0) {
            if (output_request) return usage(argv[0], 2);
            output_request = value;
        } else if (strcmp(option, "--language") == 0) {
            if (language) return usage(argv[0], 2);
            language = value;
        } else if (strcmp(option, "--update-from") == 0) {
            if (update_from) return usage(argv[0], 2);
            update_from = value;
        } else if (strcmp(option, "--expect-plan") == 0) {
            if (expected_plan) return usage(argv[0], 2);
            expected_plan = value;
        } else if (strcmp(option, "--nand-policy") == 0) {
            if (policy_set) return usage(argv[0], 2);
            policy_set = true;
            if (strcmp(value, "keep") == 0) replace_all = false;
            else if (strcmp(value, "replace") == 0) replace_all = true;
            else return usage(argv[0], 2);
        } else if (strcmp(option, "--replace-channel") == 0) {
            if (!add_choice(&replace_ids, value)) return usage(argv[0], 2);
        } else if (strcmp(option, "--keep-channel") == 0) {
            if (!add_choice(&keep_ids, value)) return usage(argv[0], 2);
        }
        else return usage(argv[0], 2);
    }
    bool invalid_recovery = recover &&
        (wad || common_key || nand || nand_keys || expected_plan || language ||
         replace_ids.count || keep_ids.count || policy_set ||
         (plan ? (!update_from || output_request) :
                 (!output_request || update_from)));
    bool invalid_preparation = !recover &&
        ((plan ? output_request != NULL : output_request == NULL) ||
         (plan && (!update_from || expected_plan)) ||
         (expected_plan && !update_from) || (nand_keys && !nand) ||
         (update_from ? (!nand || wad || common_key) :
                        (!wad || !common_key || replace_ids.count ||
                         keep_ids.count || policy_set)));
    if (invalid_recovery || invalid_preparation) {
        return usage(argv[0], 2);
    }
    if (language) {
        if (strlen(language) != 3) return usage(argv[0], 2);
        for (size_t index = 0; index < 3; index++) {
            char character = language[index];
            if (character < 'A' || character > 'Z') return usage(argv[0], 2);
        }
    }
    for (size_t index = 0; index < keep_ids.count; index++) {
        if (choice_contains(&replace_ids, keep_ids.ids[index])) {
            fputs("A title cannot be both kept and replaced.\n", stderr);
            return 2;
        }
    }

    char *wad_path = wad ? realpath(wad, NULL) : NULL;
    char *common_key_path = common_key ? realpath(common_key, NULL) : NULL;
    char *nand_path = nand ? realpath(nand, NULL) : NULL;
    char *nand_keys_path = nand_keys ? realpath(nand_keys, NULL) : NULL;
    char *base_path = update_from ? realpath(update_from, NULL) : NULL;
    char *self = realpath(argv[0], NULL);
    struct stat base_metadata;
    if ((wad && !wad_path) || (common_key && !common_key_path) ||
        (nand && !nand_path) || (nand_keys && !nand_keys_path) ||
        (update_from && (!base_path || !validate_parents(update_from, false) ||
                         lstat(update_from, &base_metadata) != 0 ||
                         !S_ISDIR(base_metadata.st_mode))) || !self) {
        fputs("A specified input or this executable could not be resolved.\n",
              stderr);
        free(wad_path); free(common_key_path); free(nand_path);
        free(nand_keys_path); free(base_path); free(self);
        return 1;
    }
    char *binary_separator = strrchr(self, '/');
    if (!binary_separator) {
        free(wad_path); free(common_key_path); free(nand_path);
        free(nand_keys_path); free(base_path); free(self);
        return 1;
    }
    *binary_separator = '\0';
    char output[PREPARE_PATH_CAPACITY];
    char parent[PREPARE_PATH_CAPACITY];
    struct stat existing;
    bool output_valid = true;
    if (plan) {
        size_t base_length = strlen(base_path);
        output_valid = base_length < sizeof(parent);
        if (output_valid) {
            memcpy(parent, base_path, base_length + 1);
            char *separator = strrchr(parent, '/');
            if (!separator) output_valid = false;
            else if (separator == parent) separator[1] = '\0';
            else *separator = '\0';
        }
    } else {
        output_valid = resolved_output(output_request, output, sizeof(output),
                                       parent, sizeof(parent),
                                       !update_from && !recover) &&
            !(base_path && (strcmp(parent, base_path) == 0 ||
                            (strncmp(parent, base_path, strlen(base_path)) == 0 &&
                             parent[strlen(base_path)] == '/')));
    }
    if (!output_valid) {
        fputs("Output path is invalid or already exists.\n", stderr);
        free(wad_path); free(common_key_path); free(nand_path);
        free(nand_keys_path); free(base_path); free(self);
        return 1;
    }

    char identity[41];
    recovery_identity(plan ? 'P' : 'O', plan ? base_path : output,
                      identity);
    if (recover) {
        int lock = lock_preparation_parent(parent);
        bool cleaned = lock >= 0 && recover_owned_stage(parent, identity);
        if (lock >= 0) close(lock);
        if (!cleaned) {
            fputs("Recovery found an active operation or ambiguous staging; "
                  "no source or published output was changed.\n", stderr);
        }
        free(wad_path); free(common_key_path); free(nand_path);
        free(nand_keys_path); free(base_path); free(self);
        return cleaned ? 0 : 1;
    }

    PrepareManifest existing_manifest;
    if (base_path) {
        if (!open_manifest(base_path, &existing_manifest)) {
            fputs("The existing channel catalog is invalid.\n", stderr);
            free(wad_path); free(common_key_path); free(nand_path);
            free(nand_keys_path); free(base_path); free(self);
            return 1;
        }
        if (language && strcmp(language, existing_manifest.language) != 0) {
            fputs("Update language must match the existing catalog.\n", stderr);
            close_manifest(&existing_manifest);
            free(wad_path); free(common_key_path); free(nand_path);
            free(nand_keys_path); free(base_path); free(self);
            return 1;
        }
        language = existing_manifest.language;
    } else if (!language) {
        language = "ENG";
    }

    int lock = lock_preparation_parent(parent);
    bool ready = lock >= 0 && recover_owned_stage(parent, identity);
    if (ready && !plan) ready = lstat(output, &existing) != 0 &&
                                errno == ENOENT;
    if (!ready) {
        fputs("Output exists, preparation is active, or an ambiguous "
              "recovery journal needs attention.\n", stderr);
        if (lock >= 0) close(lock);
        if (base_path) close_manifest(&existing_manifest);
        free(wad_path); free(common_key_path); free(nand_path);
        free(nand_keys_path); free(base_path); free(self);
        return 1;
    }

    char temporary[PREPARE_PATH_CAPACITY];
    if (!create_owned_stage(parent, identity, temporary)) {
        fputs("Could not create private staging directory.\n", stderr);
        close(lock);
        if (base_path) close_manifest(&existing_manifest);
        free(wad_path); free(common_key_path); free(nand_path);
        free(nand_keys_path); free(base_path); free(self);
        return 1;
    }
    char assets[PREPARE_PATH_CAPACITY], nand_output[PREPARE_PATH_CAPACITY];
    char resource10[PREPARE_PATH_CAPACITY];
    char resource97[PREPARE_PATH_CAPACITY], resource98[PREPARE_PATH_CAPACITY];
    char content_directory[PREPARE_PATH_CAPACITY];
    char incoming_assets[PREPARE_PATH_CAPACITY];
    bool okay = path_join(assets, sizeof(assets), temporary, "assets") &&
        path_join(nand_output, sizeof(nand_output), temporary, "nand") &&
        path_join(incoming_assets, sizeof(incoming_assets), temporary,
                  "incoming-assets") &&
        path_join(resource10, sizeof(resource10), temporary,
                  ".local/wad/0000000100000002/content/0000000a.app") &&
        path_join(resource97, sizeof(resource97), temporary,
                  ".local/wad/0000000100000002/content/00000097.app") &&
        path_join(resource98, sizeof(resource98), temporary,
                  ".local/wad/0000000100000002/content/00000098.app") &&
        path_join(content_directory, sizeof(content_directory), temporary,
                  ".local/wad/0000000100000002/content");
    if (okay && base_path && plan) okay = regular_tree(base_path);
    if (okay && base_path && !plan) okay = copy_tree(base_path, assets) &&
                                           mkdir(incoming_assets, 0700) == 0;
    else if (okay && plan) okay = mkdir(incoming_assets, 0700) == 0;
    else if (okay) okay = mkdir(assets, 0700) == 0;
    if (okay && !base_path) {
        char *wad_arguments[] = {"wm-wad-extract", "--wad", wad_path,
                                 "--common-key-file", common_key_path, NULL};
        okay = run_tool(self, "wm-wad-extract", temporary, wad_arguments, false);
    }
    if (okay && !base_path) {
        char *dictionary_arguments[] = {
            "wm-keyboard-dictionary-export", content_directory, assets, NULL
        };
        okay = run_tool(self, "wm-keyboard-dictionary-export", temporary,
                        dictionary_arguments, false);
    }
    if (okay && !base_path) {
        char *layout_arguments[] = {"wm-layout-export", resource97,
                                    assets, (char *)language, NULL};
        okay = run_tool(self, "wm-layout-export", temporary,
                        layout_arguments, false);
    }
    if (okay && !base_path) {
        char *settings_arguments[] = {"wm-settings-export", resource97,
                                      assets, NULL};
        okay = run_tool(self, "wm-settings-export", temporary,
                        settings_arguments, false);
    }
    if (okay && !base_path) {
        char *outline_font_arguments[] = {"wm-outline-font-export",
                                          resource10, assets, NULL};
        okay = run_tool(self, "wm-outline-font-export", temporary,
                        outline_font_arguments, false);
    }
    if (okay && !base_path) {
        char *audio_arguments[] = {"wm-audio-export", resource97,
                                   resource98, assets, NULL};
        okay = run_tool(self, "wm-audio-export", temporary,
                        audio_arguments, false);
    }
    if (okay && !base_path) {
        char *restart_arguments[] = {"wm-restart-export", resource98,
                                     assets, NULL};
        okay = run_tool(self, "wm-restart-export", temporary,
                        restart_arguments, false);
    }
    if (okay && nand_path) {
        char *nand_arguments[] = {"wm-nand-extract", nand_path,
                                  nand_output, NULL, NULL, NULL};
        if (nand_keys_path) {
            nand_arguments[3] = "--keys";
            nand_arguments[4] = nand_keys_path;
        }
        okay = run_tool(self, "wm-nand-extract", temporary, nand_arguments,
                        plan);
    }
    if (okay && nand_path) {
        char *channel_arguments[] = {"wm-channel-export", nand_output,
                                     base_path ? incoming_assets : assets,
                                     (char *)language, NULL};
        okay = run_tool(self, "wm-channel-export", temporary,
                        channel_arguments, plan);
    }
    if (okay && nand_path && !base_path) {
        char *font_arguments[] = {"wm-shared-font-export", nand_output,
                                  assets, NULL};
        okay = run_tool(self, "wm-shared-font-export", temporary,
                        font_arguments, false);
    }
    char input_nand_sha1[41];
    if (okay && base_path) {
        okay = hash_regular_file(nand_path, input_nand_sha1);
        if (!okay) fputs("Could not hash the NAND input for the update plan.\n",
                         stderr);
    }
    if (okay && plan) {
        okay = print_update_plan(stdout, base_path, incoming_assets,
                                 input_nand_sha1,
                                 &replace_ids,
                                 &keep_ids, replace_all);
    } else if (okay && base_path) {
        if (expected_plan) {
            okay = verify_expected_plan(expected_plan, assets,
                                        incoming_assets, input_nand_sha1,
                                        &replace_ids,
                                        &keep_ids, replace_all);
            if (!okay) fputs("The reviewed channel update plan no longer "
                             "matches these inputs and choices.\n", stderr);
        }
        if (okay) okay = update_channels(assets, incoming_assets, assets,
                                        &replace_ids, &keep_ids, replace_all);
    }
    if (okay && !plan) {
        okay = publish_directory_no_replace(assets, output);
    }
    if (!okay) fputs("Preparation failed; the destination was not published.\n",
                     stderr);
    bool cleaned = recover_owned_stage(parent, identity);
    if (!cleaned)
        fputs("Could not recover all private staging files; run --recover "
              "for this output or plan.\n", stderr);
    close(lock);
    if (base_path) close_manifest(&existing_manifest);
    free(wad_path); free(common_key_path); free(nand_path);
    free(nand_keys_path); free(base_path); free(self);
    return okay && cleaned ? 0 : 1;
}
