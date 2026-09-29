#define _XOPEN_SOURCE 700

#define main wm_prepare_command_main
#include "../../tools/preparation/prepare.c"
#undef main

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);            \
            abort();                                                                   \
        }                                                                              \
    } while (0)

/* Recovery tests inspect the persisted format, not recovery's C structs. */
static const char TEST_STAGE_MARKER[] = ".wm-prepare-owner";
static const char TEST_JOURNAL_NAME[] = ".wm-prepare-journal";

#define A_OLD_CONTENT "1111111111111111111111111111111111111111"
#define B_OLD_CONTENT "2222222222222222222222222222222222222222"
#define A_NEW_CONTENT "3333333333333333333333333333333333333333"
#define C_NEW_CONTENT "4444444444444444444444444444444444444444"
#define A_CHANGED_CONTENT "5555555555555555555555555555555555555555"
#define A_OLD_TMD "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define B_OLD_TMD "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#define A_NEW_TMD "cccccccccccccccccccccccccccccccccccccccc"
#define C_NEW_TMD "dddddddddddddddddddddddddddddddddddddddd"

static void write_text(const char *path, const char *value) {
    if (!make_file_parent(path)) {
        fprintf(stderr, "Could not create test parent: %s (%d)\n", path, errno);
        abort();
    }
    FILE *stream = fopen(path, "wb");
    CHECK(stream);
    CHECK(fwrite(value, 1, strlen(value), stream) == strlen(value));
    CHECK(fclose(stream) == 0);
}

static void read_text(const char *path, char *output, size_t capacity) {
    FILE *stream = fopen(path, "rb");
    CHECK(stream);
    size_t size = fread(output, 1, capacity - 1, stream);
    CHECK(!ferror(stream) && fgetc(stream) == EOF);
    output[size] = '\0';
    CHECK(fclose(stream) == 0);
}

static void replace_digest(const char *path, const char *original,
                           const char *replacement) {
    char json[4096];
    read_text(path, json, sizeof(json));
    char *found = strstr(json, original);
    CHECK(found && strlen(original) == strlen(replacement));
    memcpy(found, replacement, strlen(replacement));
    write_text(path, json);
}

static void fixture(const char *directory, bool incoming) {
    char path[PREPARE_PATH_CAPACITY];
    CHECK(mkdir(directory, 0700) == 0);
    CHECK(path_join(path, sizeof(path), directory, "channels.json"));
    if (incoming) {
        write_text(path, "{\n"
                         "  \"schemaVersion\": 1,\n"
                         "  \"language\": \"ENG\",\n"
                         "  \"channels\": [\n"
                         "    {\"id\": \"0001000141414141\", \"title\": \"A new\",\n"
                         "     \"source\": {\"tmdVersion\": 2, "
                         "\"contentSha1\": \"" A_NEW_CONTENT "\", "
                         "\"tmdSha1\": \"" A_NEW_TMD "\"}},\n"
                         "    {\"id\": \"0001000143434343\", \"title\": \"C new\",\n"
                         "     \"source\": {\"tmdVersion\": 1, "
                         "\"contentSha1\": \"" C_NEW_CONTENT "\", "
                         "\"tmdSha1\": \"" C_NEW_TMD "\"}}\n"
                         "  ],\n"
                         "  \"defaultOrder\": [\"0001000141414141\",\n"
                         "                   \"0001000143434343\"],\n"
                         "  \"savedLayout\": null\n"
                         "}\n");
    } else {
        write_text(path, "{\n"
                         "  \"schemaVersion\": 1,\n"
                         "  \"language\": \"ENG\",\n"
                         "  \"channels\": [\n"
                         "    {\"id\": \"0001000141414141\", \"title\": \"A old\",\n"
                         "     \"source\": {\"tmdVersion\": 1, "
                         "\"contentSha1\": \"" A_OLD_CONTENT "\", "
                         "\"tmdSha1\": \"" A_OLD_TMD "\"}},\n"
                         "    {\"id\": \"0001000142424242\", \"title\": \"B old\",\n"
                         "     \"source\": {\"tmdVersion\": 1, "
                         "\"contentSha1\": \"" B_OLD_CONTENT "\", "
                         "\"tmdSha1\": \"" B_OLD_TMD "\"}}\n"
                         "  ],\n"
                         "  \"defaultOrder\": [\"0001000141414141\",\n"
                         "                   \"0001000142424242\"],\n"
                         "  \"savedLayout\": {\n"
                         "    \"slots\": [{\"id\": \"0001000142424242\",\n"
                         "               \"page\": 0, \"index\": 3}]\n"
                         "  }\n"
                         "}\n");
        CHECK(path_join(path, sizeof(path), directory, "iplsave.bin"));
        write_text(path, "preserved placement");
    }
    const char *first = incoming ? "A new artwork" : "A old artwork";
    CHECK(path_join(path, sizeof(path), directory,
                    "channel-layouts/0001000141414141/icon/icon.json"));
    write_text(path, first);
    CHECK(path_join(path, sizeof(path), directory,
                    incoming ? "channel-layouts/0001000143434343/icon/icon.json"
                             : "channel-layouts/0001000142424242/icon/icon.json"));
    write_text(path, incoming ? "C new artwork" : "B old artwork");
    CHECK(
        path_join(path, sizeof(path), directory, "channel-audio/0001000141414141.wav"));
    write_text(path, incoming ? "A new audio" : "A old audio");
}

static void assert_artwork(const char *directory, const char *id,
                           const char *expected) {
    char relative[96];
    char path[PREPARE_PATH_CAPACITY];
    char actual[128];
    CHECK(snprintf(relative, sizeof(relative), "channel-layouts/%s/icon/icon.json",
                   id) > 0);
    CHECK(path_join(path, sizeof(path), directory, relative));
    read_text(path, actual, sizeof(actual));
    CHECK(strcmp(actual, expected) == 0);
}

static void assert_catalog(const char *directory, const char *first_title,
                           size_t expected_count) {
    PrepareManifest manifest;
    CHECK(prepare_open_manifest(directory, &manifest));
    CHECK(manifest.count == expected_count);
    size_t title = wm_json_member(&manifest.json, manifest.channels[0].token, "title");
    CHECK(wm_json_equals(&manifest.json, title, first_title));
    CHECK(manifest.saved_layout != WM_JSON_INVALID);
    CHECK(manifest.json.tokens[manifest.saved_layout].type == WM_JSON_OBJECT);
    prepare_close_manifest(&manifest);
}

int main(int argc, char **argv) {
    (void)argc;
    char *temporary_parent = realpath("/tmp", NULL);
    CHECK(temporary_parent);
    char root[PREPARE_PATH_CAPACITY];
    CHECK(snprintf(root, sizeof(root), "%s/wm-prepare-update-test-XXXXXX",
                   temporary_parent) > 0);
    free(temporary_parent);
    CHECK(mkdtemp(root));
    char absent_wad[PREPARE_PATH_CAPACITY];
    char absent_output[PREPARE_PATH_CAPACITY];
    CHECK(path_join(absent_wad, sizeof(absent_wad), root, "absent.wad"));
    CHECK(path_join(absent_output, sizeof(absent_output), root, "new-assets"));
    char *automatic_key_command[] = {argv[0], "--wad", absent_wad, "--output",
                                     absent_output};
    /* Omitting the key reaches normal input resolution instead of a usage
     * rejection. Explicit ticket indices follow the same optional path. */
    CHECK(wm_prepare_command_main(5, automatic_key_command) == 1);
    char *key_index_command[] = {argv[0],    "--wad",       absent_wad,
                                 "--output", absent_output, "--common-key-index",
                                 "1"};
    CHECK(wm_prepare_command_main(7, key_index_command) == 1);
    key_index_command[6] = "256";
    CHECK(wm_prepare_command_main(7, key_index_command) == 2);
    char base[PREPARE_PATH_CAPACITY], incoming[PREPARE_PATH_CAPACITY];
    char stage[PREPARE_PATH_CAPACITY], path[PREPARE_PATH_CAPACITY];
    CHECK(path_join(base, sizeof(base), root, "base"));
    CHECK(path_join(incoming, sizeof(incoming), root, "incoming"));
    fixture(base, false);
    fixture(incoming, true);

    PrepareChoices empty = {0};
    CHECK(path_join(stage, sizeof(stage), root, "stage-keep"));
    CHECK(copy_tree(base, stage));
    CHECK(prepare_update_channels(base, incoming, stage, &empty, &empty, false));
    assert_catalog(stage, "A old", 3);
    assert_artwork(stage, "0001000141414141", "A old artwork");
    assert_artwork(stage, "0001000142424242", "B old artwork");
    assert_artwork(stage, "0001000143434343", "C new artwork");
    CHECK(path_join(path, sizeof(path), stage, "iplsave.bin"));
    char placement[128];
    read_text(path, placement, sizeof(placement));
    CHECK(strcmp(placement, "preserved placement") == 0);

    PrepareChoices replace = {0};
    CHECK(prepare_add_choice(&replace, "0001000141414141"));
    CHECK(path_join(stage, sizeof(stage), root, "stage-replace"));
    CHECK(copy_tree(base, stage));
    CHECK(prepare_update_channels(base, incoming, stage, &replace, &empty, false));
    assert_catalog(stage, "A new", 3);
    assert_artwork(stage, "0001000141414141", "A new artwork");
    CHECK(path_join(path, sizeof(path), stage, "channel-audio/0001000141414141.wav"));
    read_text(path, placement, sizeof(placement));
    CHECK(strcmp(placement, "A new audio") == 0);

    PrepareChoices keep = {0};
    CHECK(prepare_add_choice(&keep, "0001000141414141"));
    CHECK(prepare_add_choice(&keep, "0001000143434343"));
    CHECK(path_join(stage, sizeof(stage), root, "stage-bulk"));
    CHECK(copy_tree(base, stage));
    CHECK(prepare_update_channels(base, incoming, stage, &empty, &keep, true));
    assert_catalog(stage, "A old", 2);
    assert_artwork(stage, "0001000141414141", "A old artwork");

    PrepareChoices absent = {0};
    CHECK(prepare_add_choice(&absent, "0001000144444444"));
    CHECK(path_join(stage, sizeof(stage), root, "stage-absent"));
    CHECK(copy_tree(base, stage));
    CHECK(!prepare_update_channels(base, incoming, stage, &absent, &empty, false));
    assert_catalog(stage, "A old", 2);

    char nand_source_path[PREPARE_PATH_CAPACITY];
    char original_nand_sha1[41];
    char changed_nand_sha1[41];
    CHECK(path_join(nand_source_path, sizeof(nand_source_path), root,
                    "identity-input.bin"));
    write_text(nand_source_path, "source NAND bytes");
    CHECK(prepare_hash_regular_file(nand_source_path, original_nand_sha1));
    CHECK(path_join(path, sizeof(path), root, "plan.json"));
    FILE *plan_stream = fopen(path, "wb+");
    CHECK(plan_stream);
    CHECK(prepare_print_update_plan(plan_stream, base, incoming, original_nand_sha1,
                                    &empty, &empty, false));
    CHECK(fclose(plan_stream) == 0);
    WmJson plan_json;
    CHECK(wm_json_load(&plan_json, path, PREPARE_MAX_MANIFEST));
    CHECK(wm_json_equals(&plan_json, wm_json_member(&plan_json, 0, "inputNandSha1"),
                         original_nand_sha1));
    CHECK(wm_json_equals(&plan_json, wm_json_member(&plan_json, 0, "sourceHashKind"),
                         "TMD-bytes-and-TMD-validated-active-content-SHA1"));
    size_t rows = wm_json_member(&plan_json, 0, "rows");
    CHECK(rows != WM_JSON_INVALID && plan_json.tokens[rows].children == 2);
    CHECK(wm_json_equals(
        &plan_json,
        wm_json_member(&plan_json, wm_json_index(&plan_json, rows, 0), "action"),
        "keep"));
    CHECK(wm_json_equals(
        &plan_json,
        wm_json_member(&plan_json, wm_json_index(&plan_json, rows, 1), "action"),
        "add"));
    size_t first_row = wm_json_index(&plan_json, rows, 0);
    CHECK(wm_json_equals(&plan_json,
                         wm_json_member(&plan_json, first_row, "incomingContentSha1"),
                         A_NEW_CONTENT));
    CHECK(wm_json_equals(&plan_json,
                         wm_json_member(&plan_json, first_row, "incomingTmdSha1"),
                         A_NEW_TMD));
    CHECK(strstr(plan_json.source, root) == NULL);
    wm_json_free(&plan_json);
    CHECK(prepare_verify_expected_plan(path, base, incoming, original_nand_sha1, &empty,
                                       &empty, false));
    write_text(nand_source_path, "source NAND byteX");
    CHECK(prepare_hash_regular_file(nand_source_path, changed_nand_sha1));
    CHECK(strcmp(original_nand_sha1, changed_nand_sha1) != 0);
    CHECK(!prepare_verify_expected_plan(path, base, incoming, changed_nand_sha1, &empty,
                                        &empty, false));
    CHECK(!prepare_verify_expected_plan(path, base, incoming, original_nand_sha1,
                                        &replace, &empty, false));

    /* A source-content change must invalidate the review even if export
     * artwork and the supplied whole-NAND identity are held constant. The
     * unrelated NAND-byte change is independently rejected above. */
    char incoming_catalog[PREPARE_PATH_CAPACITY];
    CHECK(path_join(incoming_catalog, sizeof(incoming_catalog), incoming,
                    "channels.json"));
    replace_digest(incoming_catalog, A_NEW_CONTENT, A_CHANGED_CONTENT);
    CHECK(!prepare_verify_expected_plan(path, base, incoming, original_nand_sha1,
                                        &empty, &empty, false));
    CHECK(!prepare_verify_expected_plan(path, base, incoming, changed_nand_sha1, &empty,
                                        &empty, false));
    replace_digest(incoming_catalog, A_CHANGED_CONTENT, A_NEW_CONTENT);
    CHECK(prepare_verify_expected_plan(path, base, incoming, original_nand_sha1, &empty,
                                       &empty, false));

    char artwork_path[PREPARE_PATH_CAPACITY];
    CHECK(path_join(artwork_path, sizeof(artwork_path), incoming,
                    "channel-layouts/0001000141414141/icon/icon.json"));
    write_text(artwork_path, "changed incoming artwork");
    CHECK(!prepare_verify_expected_plan(path, base, incoming, original_nand_sha1,
                                        &empty, &empty, false));
    write_text(artwork_path, "A new artwork");
    CHECK(path_join(artwork_path, sizeof(artwork_path), base,
                    "channel-layouts/0001000141414141/icon/icon.json"));
    write_text(artwork_path, "changed installed artwork");
    CHECK(!prepare_verify_expected_plan(path, base, incoming, original_nand_sha1,
                                        &empty, &empty, false));
    write_text(artwork_path, "A old artwork");
    CHECK(prepare_verify_expected_plan(path, base, incoming, original_nand_sha1, &empty,
                                       &empty, false));

    /* An interrupted preparation owns only its marked scratch directory.
     * Recovery leaves unrelated scratch and published generations alone. */
    char recovery_output[PREPARE_PATH_CAPACITY];
    char recovery_identity_hash[41];
    char recovery_stage[PREPARE_PATH_CAPACITY];
    char unrelated_stage[PREPARE_PATH_CAPACITY];
    struct stat metadata;
    CHECK(path_join(recovery_output, sizeof(recovery_output), root, "recovery-output"));
    CHECK(path_join(unrelated_stage, sizeof(unrelated_stage), root,
                    ".wm-prepare-unrelated"));
    CHECK(mkdir(unrelated_stage, 0700) == 0);
    recovery_identity('O', recovery_output, recovery_identity_hash);
    CHECK(create_owned_stage(root, recovery_identity_hash, recovery_stage));
    CHECK(path_join(path, sizeof(path), recovery_stage, "nand/private-content.bin"));
    write_text(path, "private staged bytes");
    char *recover_command[] = {argv[0], "--recover", "--output", recovery_output, NULL};
    CHECK(wm_prepare_command_main(4, recover_command) == 0);
    CHECK(lstat(recovery_stage, &metadata) != 0 && errno == ENOENT);
    CHECK(lstat(unrelated_stage, &metadata) == 0 && S_ISDIR(metadata.st_mode));
    CHECK(lstat(recovery_output, &metadata) != 0 && errno == ENOENT);

    CHECK(create_owned_stage(root, recovery_identity_hash, recovery_stage));
    char invalid_nand[PREPARE_PATH_CAPACITY];
    CHECK(path_join(invalid_nand, sizeof(invalid_nand), root,
                    "recovery-invalid-nand.bin"));
    write_text(invalid_nand, "not a BootMii dump");
    char *automatic_recovery[] = {
        argv[0],    "--update-from", base, "--nand", invalid_nand,
        "--output", recovery_output, NULL};
    CHECK(wm_prepare_command_main(7, automatic_recovery) == 1);
    CHECK(lstat(recovery_stage, &metadata) != 0 && errno == ENOENT);
    CHECK(lstat(recovery_output, &metadata) != 0 && errno == ENOENT);

    CHECK(create_owned_stage(root, recovery_identity_hash, recovery_stage));
    char wrong_identity[41];
    recovery_identity('O', unrelated_stage, wrong_identity);
    CHECK(!recover_owned_stage(root, wrong_identity));
    CHECK(lstat(recovery_stage, &metadata) == 0 && S_ISDIR(metadata.st_mode));
    CHECK(recover_owned_stage(root, recovery_identity_hash));

    CHECK(create_owned_stage(root, recovery_identity_hash, recovery_stage));
    char marker[PREPARE_PATH_CAPACITY];
    CHECK(path_join(marker, sizeof(marker), recovery_stage, TEST_STAGE_MARKER));
    write_text(marker, "not a matching ownership marker");
    CHECK(!recover_owned_stage(root, recovery_identity_hash));
    CHECK(lstat(recovery_stage, &metadata) == 0 && S_ISDIR(metadata.st_mode));
    char journal[PREPARE_PATH_CAPACITY];
    CHECK(path_join(journal, sizeof(journal), root, TEST_JOURNAL_NAME));
    CHECK(unlink(marker) == 0);
    CHECK(copy_file(journal, marker));
    CHECK(recover_owned_stage(root, recovery_identity_hash));

    CHECK(create_owned_stage(root, recovery_identity_hash, recovery_stage));
    char staged_assets[PREPARE_PATH_CAPACITY];
    CHECK(path_join(staged_assets, sizeof(staged_assets), recovery_stage, "assets"));
    CHECK(mkdir(staged_assets, 0700) == 0);
    CHECK(path_join(path, sizeof(path), staged_assets, "published.txt"));
    write_text(path, "committed generation");
    CHECK(publish_directory_no_replace(staged_assets, recovery_output));
    CHECK(wm_prepare_command_main(4, recover_command) == 0);
    CHECK(path_join(path, sizeof(path), recovery_output, "published.txt"));
    read_text(path, placement, sizeof(placement));
    CHECK(strcmp(placement, "committed generation") == 0);
    CHECK(path_join(path, sizeof(path), recovery_output, TEST_STAGE_MARKER));
    CHECK(lstat(path, &metadata) != 0 && errno == ENOENT);
    CHECK(lstat(unrelated_stage, &metadata) == 0 && S_ISDIR(metadata.st_mode));

    recovery_identity('P', base, recovery_identity_hash);
    CHECK(create_owned_stage(root, recovery_identity_hash, recovery_stage));
    char *plan_recover_command[] = {argv[0],         "--recover", "--plan",
                                    "--update-from", base,        NULL};
    CHECK(wm_prepare_command_main(5, plan_recover_command) == 0);
    CHECK(lstat(recovery_stage, &metadata) != 0 && errno == ENOENT);

    CHECK(path_join(stage, sizeof(stage), root, "stage-malformed"));
    CHECK(copy_tree(base, stage));
    CHECK(path_join(path, sizeof(path), incoming, "channels.json"));
    CHECK(unlink(path) == 0);
    write_text(path, "{\"channels\": [broken]}");
    CHECK(!prepare_update_channels(base, incoming, stage, &empty, &empty, false));
    assert_catalog(stage, "A old", 2);

    CHECK(path_join(path, sizeof(path), base, "linked"));
    CHECK(symlink("channels.json", path) == 0);
    CHECK(path_join(stage, sizeof(stage), root, "stage-symlink"));
    CHECK(!copy_tree(base, stage));
    CHECK(unlink(path) == 0);

    CHECK(path_join(path, sizeof(path), root, "fake-nand.bin"));
    write_text(path, "not a NAND");
    char existing_output[PREPARE_PATH_CAPACITY];
    CHECK(path_join(existing_output, sizeof(existing_output), root, "existing-output"));
    CHECK(mkdir(existing_output, 0700) == 0);
    char *command[] = {argv[0],    "--update-from", base, "--nand", path,
                       "--output", existing_output, NULL};
    CHECK(wm_prepare_command_main(7, command) == 1);
    CHECK(lstat(existing_output, &metadata) == 0 && S_ISDIR(metadata.st_mode));

    char nested_output[PREPARE_PATH_CAPACITY];
    CHECK(path_join(nested_output, sizeof(nested_output), base, "nested-output"));
    command[6] = nested_output;
    CHECK(wm_prepare_command_main(7, command) == 1);
    CHECK(lstat(nested_output, &metadata) != 0 && errno == ENOENT);

    char linked_parent[PREPARE_PATH_CAPACITY];
    CHECK(path_join(linked_parent, sizeof(linked_parent), root, "linked-parent"));
    CHECK(symlink(base, linked_parent) == 0);
    char linked_output[PREPARE_PATH_CAPACITY];
    CHECK(path_join(linked_output, sizeof(linked_output), linked_parent,
                    "linked-output"));
    command[6] = linked_output;
    CHECK(wm_prepare_command_main(7, command) == 1);
    CHECK(lstat(linked_output, &metadata) != 0 && errno == ENOENT);

    CHECK(remove_tree(root));
    puts("prepare update tests passed");
    return 0;
}
