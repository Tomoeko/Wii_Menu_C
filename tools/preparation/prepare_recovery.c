#define _XOPEN_SOURCE 700
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#include "prepare_recovery.h"
#include "prepare_hash.h"
#include "wad/crypto.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

/* The journal and marker formats are persistent and must remain compatible. */
enum { PREPARE_STAGE_NAME_LENGTH = 44 };

static const char PREPARE_JOURNAL_MAGIC[] = "WM_PREPARE_RECOVERY_1\n";
static const char PREPARE_JOURNAL_NAME[] = ".wm-prepare-journal";
static const char PREPARE_JOURNAL_NEXT[] = ".wm-prepare-journal.next";
static const char PREPARE_STAGE_MARKER[] = ".wm-prepare-owner";

typedef struct PrepareRecoveryRecord {
    char identity[41];
    char stage[PREPARE_STAGE_NAME_LENGTH + 1];
} PrepareRecoveryRecord;

static bool lowercase_hex(const char *value, size_t length) {
    for (size_t index = 0; index < length; index++) {
        char digit = value[index];
        if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f')))
            return false;
    }
    return true;
}

void recovery_identity(char kind, const char *path, char result[41]) {
    WmSha1 sha1;
    uint8_t digest[20];
    wm_sha1_init(&sha1);
    wm_sha1_update(&sha1, (const uint8_t *)&kind, 1);
    wm_sha1_update(&sha1, (const uint8_t *)path, strlen(path));
    wm_sha1_final(&sha1, digest);
    prepare_format_sha1(digest, result);
}

static bool write_all(int descriptor, const char *bytes, size_t size) {
    while (size) {
        ssize_t amount = write(descriptor, bytes, size);
        if (amount < 0 && errno == EINTR)
            continue;
        if (amount <= 0)
            return false;
        bytes += amount;
        size -= (size_t)amount;
    }
    return true;
}

static bool sync_directory(const char *path) {
    int descriptor = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (descriptor < 0)
        return false;
    bool okay = fsync(descriptor) == 0;
    if (close(descriptor) != 0)
        okay = false;
    return okay;
}

int lock_preparation_parent(const char *parent) {
    char path[PREPARE_PATH_CAPACITY];
    if (!path_join(path, sizeof(path), parent, ".wm-prepare.lock"))
        return -1;
    int descriptor = open(path, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (descriptor < 0)
        return -1;
    struct stat metadata;
    if (fstat(descriptor, &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
        metadata.st_uid != getuid() || (metadata.st_mode & 0022) != 0 ||
        flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        close(descriptor);
        return -1;
    }
    return descriptor;
}

static bool recovery_record_text(const PrepareRecoveryRecord *record, char text[128],
                                 size_t *length) {
    int written = snprintf(text, 128, "%s%s\n%s\n", PREPARE_JOURNAL_MAGIC,
                           record->identity, record->stage);
    if (written < 0 || written >= 128)
        return false;
    *length = (size_t)written;
    return true;
}

static bool read_recovery_record(const char *path, PrepareRecoveryRecord *record) {
    int descriptor = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (descriptor < 0)
        return false;
    struct stat metadata;
    bool okay = fstat(descriptor, &metadata) == 0 && S_ISREG(metadata.st_mode) &&
                metadata.st_uid == getuid();
    size_t magic_size = strlen(PREPARE_JOURNAL_MAGIC);
    size_t expected_size = magic_size + 40 + 1 + PREPARE_STAGE_NAME_LENGTH + 1;
    if (okay)
        okay = metadata.st_size == (off_t)expected_size;
    char text[128];
    size_t received = 0;
    while (okay && received < expected_size) {
        ssize_t amount = read(descriptor, text + received, expected_size - received);
        if (amount < 0 && errno == EINTR)
            continue;
        if (amount <= 0)
            okay = false;
        else
            received += (size_t)amount;
    }
    if (close(descriptor) != 0)
        okay = false;
    if (!okay || memcmp(text, PREPARE_JOURNAL_MAGIC, magic_size) != 0 ||
        text[magic_size + 40] != '\n' || text[expected_size - 1] != '\n' ||
        !lowercase_hex(text + magic_size, 40) ||
        memcmp(text + magic_size + 41, ".wm-prepare-", 12) != 0 ||
        !lowercase_hex(text + magic_size + 41 + 12, 32))
        return false;
    memcpy(record->identity, text + magic_size, 40);
    record->identity[40] = '\0';
    memcpy(record->stage, text + magic_size + 41, PREPARE_STAGE_NAME_LENGTH);
    record->stage[PREPARE_STAGE_NAME_LENGTH] = '\0';
    return true;
}

static bool write_recovery_record(const char *path,
                                  const PrepareRecoveryRecord *record) {
    char text[128];
    size_t length;
    if (!recovery_record_text(record, text, &length))
        return false;
    int descriptor =
        open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (descriptor < 0)
        return false;
    bool okay = write_all(descriptor, text, length) && fsync(descriptor) == 0;
    if (close(descriptor) != 0)
        okay = false;
    if (!okay)
        unlink(path);
    return okay;
}

static bool same_record(const PrepareRecoveryRecord *left,
                        const PrepareRecoveryRecord *right) {
    return strcmp(left->identity, right->identity) == 0 &&
           strcmp(left->stage, right->stage) == 0;
}

bool recover_owned_stage(const char *parent, const char *identity) {
    char journal[PREPARE_PATH_CAPACITY];
    char next[PREPARE_PATH_CAPACITY];
    if (!path_join(journal, sizeof(journal), parent, PREPARE_JOURNAL_NAME) ||
        !path_join(next, sizeof(next), parent, PREPARE_JOURNAL_NEXT))
        return false;
    struct stat metadata;
    bool journal_exists = lstat(journal, &metadata) == 0;
    if (!journal_exists && errno != ENOENT)
        return false;
    bool next_exists = lstat(next, &metadata) == 0;
    if (!next_exists && errno != ENOENT)
        return false;
    PrepareRecoveryRecord record;
    if (journal_exists && (!read_recovery_record(journal, &record) ||
                           strcmp(record.identity, identity) != 0))
        return false;
    if (next_exists) {
        PrepareRecoveryRecord pending;
        if (!read_recovery_record(next, &pending) ||
            strcmp(pending.identity, identity) != 0 ||
            (journal_exists && !same_record(&record, &pending)))
            return false;
    }
    if (!journal_exists) {
        return !next_exists || (unlink(next) == 0 && sync_directory(parent));
    }
    char stage[PREPARE_PATH_CAPACITY];
    if (!path_join(stage, sizeof(stage), parent, record.stage))
        return false;
    if (lstat(stage, &metadata) == 0) {
        if (!S_ISDIR(metadata.st_mode) || metadata.st_uid != getuid() ||
            (metadata.st_mode & 0777) != 0700)
            return false;
        char marker[PREPARE_PATH_CAPACITY];
        PrepareRecoveryRecord owned;
        if (!path_join(marker, sizeof(marker), stage, PREPARE_STAGE_MARKER) ||
            !read_recovery_record(marker, &owned) || !same_record(&record, &owned) ||
            !remove_tree(stage))
            return false;
    } else if (errno != ENOENT) {
        return false;
    }
    if (next_exists && unlink(next) != 0)
        return false;
    return unlink(journal) == 0 && sync_directory(parent);
}

bool create_owned_stage(const char *parent, const char *identity,
                        char stage[PREPARE_PATH_CAPACITY]) {
    uint8_t random_bytes[16];
    int random_file = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (random_file < 0)
        return false;
    size_t received = 0;
    while (received < sizeof(random_bytes)) {
        ssize_t amount =
            read(random_file, random_bytes + received, sizeof(random_bytes) - received);
        if (amount < 0 && errno == EINTR)
            continue;
        if (amount <= 0)
            break;
        received += (size_t)amount;
    }
    bool okay = close(random_file) == 0 && received == sizeof(random_bytes);
    if (!okay)
        return false;
    PrepareRecoveryRecord record = {0};
    memcpy(record.identity, identity, sizeof(record.identity));
    memcpy(record.stage, ".wm-prepare-", 12);
    for (size_t index = 0; index < sizeof(random_bytes); index++) {
        snprintf(record.stage + 12 + index * 2, 3, "%02x", random_bytes[index]);
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
        !publish_directory_no_replace(next, journal) || !sync_directory(parent))
        return false;
    if (mkdir(stage, 0700) != 0) {
        unlink(journal);
        sync_directory(parent);
        return false;
    }
    char marker[PREPARE_PATH_CAPACITY];
    if (!path_join(marker, sizeof(marker), stage, PREPARE_STAGE_MARKER) ||
        !write_recovery_record(marker, &record) || !sync_directory(stage) ||
        !sync_directory(parent)) {
        if (remove_tree(stage)) {
            unlink(journal);
            sync_directory(parent);
        }
        return false;
    }
    return true;
}
