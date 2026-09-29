#if defined(__linux__)
#define _GNU_SOURCE 1
#endif
#define _XOPEN_SOURCE 700
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#include "preparation/prepare_fs.h"
#include "preparation/prepare_recovery.h"
#include "preparation/prepare_update.h"
#include "wii_menu/support/asset_manifest.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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

static int usage(const char *program, int result) {
    fprintf(stderr,
            "Usage: %s --wad FILE --output DIRECTORY "
            "[--common-key-file FILE] [--common-key-index N] "
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
          "directory and its saved placement. Inputs stay local.\n"
          "WAD retail ticket indices 0 and 1 select a built-in common key.\n",
          stderr);
    return result;
}

int main(int argc, char **argv) {
    const char *wad = NULL, *common_key = NULL, *nand = NULL;
    const char *common_key_index = NULL;
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
        } else if (strcmp(option, "--common-key-index") == 0) {
            if (common_key_index) return usage(argv[0], 2);
            char *end = NULL;
            unsigned long parsed = strtoul(value, &end, 10);
            if (end == value || *end != '\0' || parsed > 255)
                return usage(argv[0], 2);
            common_key_index = value;
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
            if (!prepare_add_choice(&replace_ids, value)) return usage(argv[0], 2);
        } else if (strcmp(option, "--keep-channel") == 0) {
            if (!prepare_add_choice(&keep_ids, value)) return usage(argv[0], 2);
        }
        else return usage(argv[0], 2);
    }
    bool invalid_recovery = recover &&
        (wad || common_key || common_key_index || nand || nand_keys ||
         expected_plan || language ||
         replace_ids.count || keep_ids.count || policy_set ||
         (plan ? (!update_from || output_request) :
                 (!output_request || update_from)));
    bool invalid_preparation = !recover &&
        ((plan ? output_request != NULL : output_request == NULL) ||
         (plan && (!update_from || expected_plan)) ||
         (expected_plan && !update_from) || (nand_keys && !nand) ||
         (update_from ? (!nand || wad || common_key || common_key_index) :
                        (!wad || replace_ids.count ||
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
        if (prepare_choice_contains(&replace_ids, keep_ids.ids[index])) {
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
        if (!prepare_open_manifest(base_path, &existing_manifest)) {
            fputs("The existing channel catalog is invalid.\n", stderr);
            free(wad_path); free(common_key_path); free(nand_path);
            free(nand_keys_path); free(base_path); free(self);
            return 1;
        }
        if (language && strcmp(language, existing_manifest.language) != 0) {
            fputs("Update language must match the existing catalog.\n", stderr);
            prepare_close_manifest(&existing_manifest);
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
        if (base_path) prepare_close_manifest(&existing_manifest);
        free(wad_path); free(common_key_path); free(nand_path);
        free(nand_keys_path); free(base_path); free(self);
        return 1;
    }

    char temporary[PREPARE_PATH_CAPACITY];
    if (!create_owned_stage(parent, identity, temporary)) {
        fputs("Could not create private staging directory.\n", stderr);
        close(lock);
        if (base_path) prepare_close_manifest(&existing_manifest);
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
        char *wad_arguments[8] = {"wm-wad-extract", "--wad", wad_path};
        size_t argument_count = 3;
        if (common_key_path) {
            wad_arguments[argument_count++] = "--common-key-file";
            wad_arguments[argument_count++] = common_key_path;
        }
        if (common_key_index) {
            wad_arguments[argument_count++] = "--common-key-index";
            wad_arguments[argument_count++] = (char *)common_key_index;
        }
        wad_arguments[argument_count] = NULL;
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
        okay = prepare_hash_regular_file(nand_path, input_nand_sha1);
        if (!okay) fputs("Could not hash the NAND input for the update plan.\n",
                         stderr);
    }
    if (okay && plan) {
        okay = prepare_print_update_plan(stdout, base_path, incoming_assets,
                                 input_nand_sha1,
                                 &replace_ids,
                                 &keep_ids, replace_all);
    } else if (okay && base_path) {
        if (expected_plan) {
            okay = prepare_verify_expected_plan(expected_plan, assets,
                                        incoming_assets, input_nand_sha1,
                                        &replace_ids,
                                        &keep_ids, replace_all);
            if (!okay) fputs("The reviewed channel update plan no longer "
                             "matches these inputs and choices.\n", stderr);
        }
        if (okay) okay = prepare_update_channels(assets, incoming_assets, assets,
                                        &replace_ids, &keep_ids, replace_all);
    }
    if (okay && !plan) {
        okay = wm_asset_manifest_write(assets, stderr);
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
    if (base_path) prepare_close_manifest(&existing_manifest);
    free(wad_path); free(common_key_path); free(nand_path);
    free(nand_keys_path); free(base_path); free(self);
    return okay && cleaned ? 0 : 1;
}
