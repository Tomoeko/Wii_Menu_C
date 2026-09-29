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

static bool resolved_output(const char *requested, char *output, size_t capacity,
                            char *parent, size_t parent_capacity, bool create_parents) {
    size_t length = strlen(requested);
    if (!length || length >= PREPARE_PATH_CAPACITY)
        return false;
    char path[PREPARE_PATH_CAPACITY];
    memcpy(path, requested, length + 1);
    while (length > 1 && path[length - 1] == '/')
        path[--length] = '\0';
    char *separator = strrchr(path, '/');
    const char *name = separator ? separator + 1 : path;
    if (!name[0] || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return false;
    char basename[PREPARE_PATH_CAPACITY];
    strcpy(basename, name);
    if (separator) {
        if (separator == path)
            separator[1] = '\0';
        else
            *separator = '\0';
    }
    const char *parent_path = separator ? path : ".";
    if (!validate_parents(parent_path, create_parents))
        return false;
    char *canonical_parent = realpath(parent_path, NULL);
    if (!canonical_parent)
        return false;
    bool okay = strlen(canonical_parent) < parent_capacity &&
                path_join(output, capacity, canonical_parent, basename);
    if (okay)
        strcpy(parent, canonical_parent);
    free(canonical_parent);
    return okay;
}

static bool tool_path(char *result, size_t capacity, const char *binary_directory,
                      const char *name) {
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
    if (child < 0)
        return false;
    if (child == 0) {
        if (plan_output && dup2(STDERR_FILENO, STDOUT_FILENO) < 0)
            _exit(127);
        if (chdir(working_directory) != 0)
            _exit(127);
        execv(executable, arguments);
        _exit(127);
    }
    int status;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR)
            continue;
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

typedef struct {
    const char *wad;
    const char *common_key;
    const char *common_key_index;
    const char *nand;
    const char *nand_keys;
    const char *output_request;
    const char *update_from;
    const char *language;
    const char *expected_plan;
    bool replace_all;
    bool plan;
    bool recover;
    PrepareChoices replace_ids;
    PrepareChoices keep_ids;
} PrepareOptions;

typedef enum {
    PREPARE_PARSE_READY,
    PREPARE_PARSE_HELP,
    PREPARE_PARSE_INVALID,
    PREPARE_PARSE_CONFLICT
} PrepareParseResult;

static PrepareParseResult parse_options(int argc, char **argv,
                                        PrepareOptions *options) {
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
        if (strcmp(option, "--help") == 0)
            return PREPARE_PARSE_HELP;
        if (strcmp(option, "--plan") == 0) {
            if (plan)
                return PREPARE_PARSE_INVALID;
            plan = true;
            continue;
        }
        if (strcmp(option, "--recover") == 0) {
            if (recover)
                return PREPARE_PARSE_INVALID;
            recover = true;
            continue;
        }
        if (index + 1 >= argc)
            return PREPARE_PARSE_INVALID;
        const char *value = argv[++index];
        if (strcmp(option, "--wad") == 0) {
            if (wad)
                return PREPARE_PARSE_INVALID;
            wad = value;
        } else if (strcmp(option, "--common-key-file") == 0) {
            if (common_key)
                return PREPARE_PARSE_INVALID;
            common_key = value;
        } else if (strcmp(option, "--common-key-index") == 0) {
            if (common_key_index)
                return PREPARE_PARSE_INVALID;
            char *end = NULL;
            unsigned long parsed = strtoul(value, &end, 10);
            if (end == value || *end != '\0' || parsed > 255)
                return PREPARE_PARSE_INVALID;
            common_key_index = value;
        } else if (strcmp(option, "--nand") == 0) {
            if (nand)
                return PREPARE_PARSE_INVALID;
            nand = value;
        } else if (strcmp(option, "--nand-keys") == 0) {
            if (nand_keys)
                return PREPARE_PARSE_INVALID;
            nand_keys = value;
        } else if (strcmp(option, "--output") == 0) {
            if (output_request)
                return PREPARE_PARSE_INVALID;
            output_request = value;
        } else if (strcmp(option, "--language") == 0) {
            if (language)
                return PREPARE_PARSE_INVALID;
            language = value;
        } else if (strcmp(option, "--update-from") == 0) {
            if (update_from)
                return PREPARE_PARSE_INVALID;
            update_from = value;
        } else if (strcmp(option, "--expect-plan") == 0) {
            if (expected_plan)
                return PREPARE_PARSE_INVALID;
            expected_plan = value;
        } else if (strcmp(option, "--nand-policy") == 0) {
            if (policy_set)
                return PREPARE_PARSE_INVALID;
            policy_set = true;
            if (strcmp(value, "keep") == 0)
                replace_all = false;
            else if (strcmp(value, "replace") == 0)
                replace_all = true;
            else
                return PREPARE_PARSE_INVALID;
        } else if (strcmp(option, "--replace-channel") == 0) {
            if (!prepare_add_choice(&replace_ids, value))
                return PREPARE_PARSE_INVALID;
        } else if (strcmp(option, "--keep-channel") == 0) {
            if (!prepare_add_choice(&keep_ids, value))
                return PREPARE_PARSE_INVALID;
        } else
            return PREPARE_PARSE_INVALID;
    }
    bool invalid_recovery =
        recover &&
        (wad || common_key || common_key_index || nand || nand_keys || expected_plan ||
         language || replace_ids.count || keep_ids.count || policy_set ||
         (plan ? (!update_from || output_request) : (!output_request || update_from)));
    bool invalid_preparation =
        !recover &&
        ((plan ? output_request != NULL : output_request == NULL) ||
         (plan && (!update_from || expected_plan)) || (expected_plan && !update_from) ||
         (nand_keys && !nand) ||
         (update_from ? (!nand || wad || common_key || common_key_index)
                      : (!wad || replace_ids.count || keep_ids.count || policy_set)));
    if (invalid_recovery || invalid_preparation) {
        return PREPARE_PARSE_INVALID;
    }
    if (language) {
        if (strlen(language) != 3)
            return PREPARE_PARSE_INVALID;
        for (size_t index = 0; index < 3; index++) {
            char character = language[index];
            if (character < 'A' || character > 'Z')
                return PREPARE_PARSE_INVALID;
        }
    }
    for (size_t index = 0; index < keep_ids.count; index++) {
        if (prepare_choice_contains(&replace_ids, keep_ids.ids[index])) {
            fputs("A title cannot be both kept and replaced.\n", stderr);
            return PREPARE_PARSE_CONFLICT;
        }
    }

    options->wad = wad;
    options->common_key = common_key;
    options->common_key_index = common_key_index;
    options->nand = nand;
    options->nand_keys = nand_keys;
    options->output_request = output_request;
    options->update_from = update_from;
    options->language = language;
    options->expected_plan = expected_plan;
    options->replace_all = replace_all;
    options->plan = plan;
    options->recover = recover;
    options->replace_ids = replace_ids;
    options->keep_ids = keep_ids;
    return PREPARE_PARSE_READY;
}

typedef struct {
    char assets[PREPARE_PATH_CAPACITY];
    char nand_output[PREPARE_PATH_CAPACITY];
    char incoming_assets[PREPARE_PATH_CAPACITY];
    char resource10[PREPARE_PATH_CAPACITY];
    char resource97[PREPARE_PATH_CAPACITY];
    char resource98[PREPARE_PATH_CAPACITY];
    char content_directory[PREPARE_PATH_CAPACITY];
} PrepareStagePaths;

static bool stage_paths(PrepareStagePaths *paths, const char *temporary) {
    return path_join(paths->assets, sizeof(paths->assets), temporary, "assets") &&
           path_join(paths->nand_output, sizeof(paths->nand_output), temporary,
                     "nand") &&
           path_join(paths->incoming_assets, sizeof(paths->incoming_assets), temporary,
                     "incoming-assets") &&
           path_join(paths->resource10, sizeof(paths->resource10), temporary,
                     ".local/wad/0000000100000002/content/0000000a.app") &&
           path_join(paths->resource97, sizeof(paths->resource97), temporary,
                     ".local/wad/0000000100000002/content/00000097.app") &&
           path_join(paths->resource98, sizeof(paths->resource98), temporary,
                     ".local/wad/0000000100000002/content/00000098.app") &&
           path_join(paths->content_directory, sizeof(paths->content_directory),
                     temporary, ".local/wad/0000000100000002/content");
}

static bool create_stage_assets(const PrepareStagePaths *paths, const char *base_path,
                                bool plan) {
    if (base_path && plan)
        return regular_tree(base_path) && mkdir(paths->incoming_assets, 0700) == 0;
    if (base_path)
        return copy_tree(base_path, paths->assets) &&
               mkdir(paths->incoming_assets, 0700) == 0;
    return mkdir(paths->assets, 0700) == 0;
}

static bool export_wad_assets(const char *binary_directory, const char *temporary,
                              const PrepareStagePaths *paths, const char *wad_path,
                              const char *common_key_path, const char *common_key_index,
                              const char *language) {
    char *wad_arguments[8] = {"wm-wad-extract", "--wad", (char *)wad_path};
    size_t argument_count = 3;
    if (common_key_path) {
        wad_arguments[argument_count++] = "--common-key-file";
        wad_arguments[argument_count++] = (char *)common_key_path;
    }
    if (common_key_index) {
        wad_arguments[argument_count++] = "--common-key-index";
        wad_arguments[argument_count++] = (char *)common_key_index;
    }
    wad_arguments[argument_count] = NULL;
    if (!run_tool(binary_directory, "wm-wad-extract", temporary, wad_arguments, false))
        return false;

    char *dictionary_arguments[] = {"wm-keyboard-dictionary-export",
                                    (char *)paths->content_directory,
                                    (char *)paths->assets, NULL};
    if (!run_tool(binary_directory, "wm-keyboard-dictionary-export", temporary,
                  dictionary_arguments, false))
        return false;

    char *layout_arguments[] = {"wm-layout-export", (char *)paths->resource97,
                                (char *)paths->assets, (char *)language, NULL};
    if (!run_tool(binary_directory, "wm-layout-export", temporary, layout_arguments,
                  false))
        return false;

    char *settings_arguments[] = {"wm-settings-export", (char *)paths->resource97,
                                  (char *)paths->assets, NULL};
    if (!run_tool(binary_directory, "wm-settings-export", temporary, settings_arguments,
                  false))
        return false;

    char *outline_font_arguments[] = {"wm-outline-font-export",
                                      (char *)paths->resource10, (char *)paths->assets,
                                      NULL};
    if (!run_tool(binary_directory, "wm-outline-font-export", temporary,
                  outline_font_arguments, false))
        return false;

    char *audio_arguments[] = {"wm-audio-export", (char *)paths->resource97,
                               (char *)paths->resource98, (char *)paths->assets, NULL};
    if (!run_tool(binary_directory, "wm-audio-export", temporary, audio_arguments,
                  false))
        return false;

    char *restart_arguments[] = {"wm-restart-export", (char *)paths->resource98,
                                 (char *)paths->assets, NULL};
    return run_tool(binary_directory, "wm-restart-export", temporary, restart_arguments,
                    false);
}

static bool export_nand_assets(const char *binary_directory, const char *temporary,
                               const PrepareStagePaths *paths, const char *nand_path,
                               const char *nand_keys_path, const char *base_path,
                               const char *language, bool plan) {
    char *nand_arguments[] = {"wm-nand-extract",
                              (char *)nand_path,
                              (char *)paths->nand_output,
                              NULL,
                              NULL,
                              NULL};
    if (nand_keys_path) {
        nand_arguments[3] = "--keys";
        nand_arguments[4] = (char *)nand_keys_path;
    }
    if (!run_tool(binary_directory, "wm-nand-extract", temporary, nand_arguments, plan))
        return false;

    char *channel_arguments[] = {
        "wm-channel-export", (char *)paths->nand_output,
        (char *)(base_path ? paths->incoming_assets : paths->assets), (char *)language,
        NULL};
    if (!run_tool(binary_directory, "wm-channel-export", temporary, channel_arguments,
                  plan))
        return false;
    if (base_path)
        return true;

    char *font_arguments[] = {"wm-shared-font-export", (char *)paths->nand_output,
                              (char *)paths->assets, NULL};
    return run_tool(binary_directory, "wm-shared-font-export", temporary,
                    font_arguments, false);
}

int main(int argc, char **argv) {
    PrepareOptions options;
    PrepareParseResult parse_result = parse_options(argc, argv, &options);
    if (parse_result == PREPARE_PARSE_HELP)
        return usage(argv[0], 0);
    if (parse_result == PREPARE_PARSE_INVALID)
        return usage(argv[0], 2);
    if (parse_result == PREPARE_PARSE_CONFLICT)
        return 2;

    const char *wad = options.wad;
    const char *common_key = options.common_key;
    const char *common_key_index = options.common_key_index;
    const char *nand = options.nand;
    const char *nand_keys = options.nand_keys;
    const char *output_request = options.output_request;
    const char *update_from = options.update_from;
    const char *language = options.language;
    const char *expected_plan = options.expected_plan;
    bool replace_all = options.replace_all;
    bool plan = options.plan;
    bool recover = options.recover;
    PrepareChoices replace_ids = options.replace_ids;
    PrepareChoices keep_ids = options.keep_ids;

    char *wad_path = wad ? realpath(wad, NULL) : NULL;
    char *common_key_path = common_key ? realpath(common_key, NULL) : NULL;
    char *nand_path = nand ? realpath(nand, NULL) : NULL;
    char *nand_keys_path = nand_keys ? realpath(nand_keys, NULL) : NULL;
    char *base_path = update_from ? realpath(update_from, NULL) : NULL;
    char *self = realpath(argv[0], NULL);
    int result = 1;
    struct stat base_metadata;
    if ((wad && !wad_path) || (common_key && !common_key_path) ||
        (nand && !nand_path) || (nand_keys && !nand_keys_path) ||
        (update_from && (!base_path || !validate_parents(update_from, false) ||
                         lstat(update_from, &base_metadata) != 0 ||
                         !S_ISDIR(base_metadata.st_mode))) ||
        !self) {
        fputs("A specified input or this executable could not be resolved.\n", stderr);
        goto release_paths;
    }
    char *binary_separator = strrchr(self, '/');
    if (!binary_separator)
        goto release_paths;
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
            if (!separator)
                output_valid = false;
            else if (separator == parent)
                separator[1] = '\0';
            else
                *separator = '\0';
        }
    } else {
        output_valid =
            resolved_output(output_request, output, sizeof(output), parent,
                            sizeof(parent), !update_from && !recover) &&
            !(base_path && (strcmp(parent, base_path) == 0 ||
                            (strncmp(parent, base_path, strlen(base_path)) == 0 &&
                             parent[strlen(base_path)] == '/')));
    }
    if (!output_valid) {
        fputs("Output path is invalid or already exists.\n", stderr);
        goto release_paths;
    }

    char identity[41];
    recovery_identity(plan ? 'P' : 'O', plan ? base_path : output, identity);
    if (recover) {
        int lock = lock_preparation_parent(parent);
        bool cleaned = lock >= 0 && recover_owned_stage(parent, identity);
        if (lock >= 0)
            close(lock);
        if (!cleaned) {
            fputs("Recovery found an active operation or ambiguous staging; "
                  "no source or published output was changed.\n",
                  stderr);
        }
        result = cleaned ? 0 : 1;
        goto release_paths;
    }

    PrepareManifest existing_manifest;
    if (base_path) {
        if (!prepare_open_manifest(base_path, &existing_manifest)) {
            fputs("The existing channel catalog is invalid.\n", stderr);
            goto release_paths;
        }
        if (language && strcmp(language, existing_manifest.language) != 0) {
            fputs("Update language must match the existing catalog.\n", stderr);
            prepare_close_manifest(&existing_manifest);
            goto release_paths;
        }
        language = existing_manifest.language;
    } else if (!language) {
        language = "ENG";
    }

    int lock = lock_preparation_parent(parent);
    bool ready = lock >= 0 && recover_owned_stage(parent, identity);
    if (ready && !plan)
        ready = lstat(output, &existing) != 0 && errno == ENOENT;
    if (!ready) {
        fputs("Output exists, preparation is active, or an ambiguous "
              "recovery journal needs attention.\n",
              stderr);
        if (lock >= 0)
            close(lock);
        if (base_path)
            prepare_close_manifest(&existing_manifest);
        goto release_paths;
    }

    char temporary[PREPARE_PATH_CAPACITY];
    if (!create_owned_stage(parent, identity, temporary)) {
        fputs("Could not create private staging directory.\n", stderr);
        close(lock);
        if (base_path)
            prepare_close_manifest(&existing_manifest);
        goto release_paths;
    }
    PrepareStagePaths paths;
    bool okay =
        stage_paths(&paths, temporary) && create_stage_assets(&paths, base_path, plan);
    if (okay && !base_path) {
        okay = export_wad_assets(self, temporary, &paths, wad_path, common_key_path,
                                 common_key_index, language);
    }
    if (okay && nand_path) {
        okay = export_nand_assets(self, temporary, &paths, nand_path, nand_keys_path,
                                  base_path, language, plan);
    }
    char input_nand_sha1[41];
    if (okay && base_path) {
        okay = prepare_hash_regular_file(nand_path, input_nand_sha1);
        if (!okay)
            fputs("Could not hash the NAND input for the update plan.\n", stderr);
    }
    if (okay && plan) {
        okay = prepare_print_update_plan(stdout, base_path, paths.incoming_assets,
                                         input_nand_sha1, &replace_ids, &keep_ids,
                                         replace_all);
    } else if (okay && base_path) {
        if (expected_plan) {
            okay = prepare_verify_expected_plan(expected_plan, paths.assets,
                                                paths.incoming_assets, input_nand_sha1,
                                                &replace_ids, &keep_ids, replace_all);
            if (!okay)
                fputs("The reviewed channel update plan no longer "
                      "matches these inputs and choices.\n",
                      stderr);
        }
        if (okay)
            okay = prepare_update_channels(paths.assets, paths.incoming_assets,
                                           paths.assets, &replace_ids, &keep_ids,
                                           replace_all);
    }
    if (okay && !plan) {
        okay = wm_asset_manifest_write(paths.assets, stderr);
    }
    if (okay && !plan) {
        okay = publish_directory_no_replace(paths.assets, output);
    }
    if (!okay)
        fputs("Preparation failed; the destination was not published.\n", stderr);
    bool cleaned = recover_owned_stage(parent, identity);
    if (!cleaned)
        fputs("Could not recover all private staging files; run --recover "
              "for this output or plan.\n",
              stderr);
    close(lock);
    if (base_path)
        prepare_close_manifest(&existing_manifest);
    result = okay && cleaned ? 0 : 1;

release_paths:
    free(wad_path);
    free(common_key_path);
    free(nand_path);
    free(nand_keys_path);
    free(base_path);
    free(self);
    return result;
}
