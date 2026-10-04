#define _POSIX_C_SOURCE 200809L

#include "console_common/support/tool_io.h"
#include "atomic_file.h"
#include "export_directory.h"

#include "wii_menu/board/keyboard_dictionary.h"
#include "wii_menu/resources/resource_u8.h"
#include "wii_menu/support/regular_file.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { EXPORT_PATH_CAPACITY = 4096, EXPORT_MAX_APP_BYTES = 16 * 1024 * 1024 };

static const char *const oem_names[3] = {"eZTNintendoENAM.znd", "eZTNintendoFRCA.znd",
                                         "eZTNintendoESSA.znd"};

static bool app_name(const char *name) {
    if (strlen(name) != 12 || strcmp(name + 8, ".app") != 0)
        return false;
    for (unsigned index = 0; index < 8; index++) {
        char character = name[index];
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f')))
            return false;
    }
    return true;
}

static bool join(char output[EXPORT_PATH_CAPACITY], const char *left,
                 const char *right) {
    int length = snprintf(output, EXPORT_PATH_CAPACITY, "%s/%s", left, right);
    return length > 0 && length < EXPORT_PATH_CAPACITY;
}

static bool write_file(const char *path, const uint8_t *data, size_t size) {
    return wm_atomic_file_replace(path, data, size);
}

static bool find_oem_archive(const uint8_t *app, size_t size, uint8_t *oem[3],
                             size_t oem_size[3], bool *system_found, bool *oem_found) {
    if (memcmp(app,
               "U\xaa"
               "8-",
               4) != 0)
        return true;
    char error[160] = {0};
    WmU8Archive outer = {0};
    if (!wm_u8_parse(app, size, &outer, error, sizeof(error))) {
        fprintf(stderr, "Dictionary source U8 parse failed: %s\n", error);
        return false;
    }
    if (wm_u8_find(&outer, "eZTSystemNA.arc"))
        *system_found = true;
    const WmU8Entry *entry = wm_u8_find(&outer, "eZTNintendoNA.arc");
    if (!entry) {
        wm_u8_free(&outer);
        return true;
    }
    if (*oem_found) {
        wm_u8_free(&outer);
        fputs("More than one OEM dictionary archive was found.\n", stderr);
        return false;
    }
    *oem_found = true;
    WmU8Archive inner = {0};
    if (!wm_u8_parse(entry->data, entry->size, &inner, error, sizeof(error))) {
        fprintf(stderr, "OEM dictionary U8 parse failed: %s\n", error);
        wm_u8_free(&outer);
        return false;
    }
    bool okay = true;
    for (unsigned language = 0; language < 3 && okay; language++) {
        const WmU8Entry *word_file = wm_u8_find(&inner, oem_names[language]);
        if (!word_file) {
            fprintf(stderr, "Missing OEM dictionary %s.\n", oem_names[language]);
            okay = false;
            break;
        }
        WmKeyboardWordList words = {0};
        okay = wm_keyboard_oem_decode(word_file->data, word_file->size, &words, error,
                                      sizeof(error));
        if (!okay) {
            fprintf(stderr, "%s: %s\n", oem_names[language], error);
        } else {
            oem[language] = malloc(word_file->size);
            okay = oem[language] != NULL;
            if (okay) {
                memcpy(oem[language], word_file->data, word_file->size);
                oem_size[language] = word_file->size;
            }
        }
        wm_keyboard_word_list_free(&words);
    }
    wm_u8_free(&inner);
    wm_u8_free(&outer);
    return okay;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fputs("Usage: wm-keyboard-dictionary-export CONTENT_DIRECTORY "
              "LOCAL_OUTPUT_DIRECTORY\n",
              stderr);
        return 2;
    }
    DIR *directory = opendir(argv[1]);
    if (!directory) {
        fputs("Could not read decrypted WAD content directory.\n", stderr);
        return 1;
    }
    uint8_t *oem[3] = {0};
    size_t oem_size[3] = {0};
    bool system_found = false, oem_found = false, okay = true;
    struct dirent *item;
    while ((item = readdir(directory)) != NULL && okay) {
        if (!app_name(item->d_name))
            continue;
        char path[EXPORT_PATH_CAPACITY];
        size_t size = 0;
        if (!join(path, argv[1], item->d_name)) {
            okay = false;
            break;
        }
        uint8_t *app = NULL;
        if (wm_regular_file_read_bytes(path, 4, EXPORT_MAX_APP_BYTES, &app, &size) !=
            WM_REGULAR_FILE_OK) {
            fprintf(stderr, "Could not read %s.\n", item->d_name);
            okay = false;
            break;
        }
        okay = find_oem_archive(app, size, oem, oem_size, &system_found, &oem_found);
        free(app);
    }
    if (closedir(directory) != 0)
        okay = false;
    if (okay && system_found && oem_found) {
        char destination[EXPORT_PATH_CAPACITY];
        okay = join(destination, argv[2], "keyboard-dictionary") &&
               wm_export_directory_child(argv[2], "keyboard-dictionary", 0700);
        for (unsigned language = 0; language < 3 && okay; language++) {
            char path[EXPORT_PATH_CAPACITY];
            okay = join(path, destination, oem_names[language]) &&
                   write_file(path, oem[language], oem_size[language]);
        }
        if (okay)
            puts("Prepared local OEM keyboard word containers.");
    } else if (okay) {
        puts("OEM keyboard word containers unavailable; built-in fallback remains "
             "active.");
    }
    for (unsigned language = 0; language < 3; language++)
        free(oem[language]);
    return okay ? 0 : 1;
}
