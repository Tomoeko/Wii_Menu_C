#define _XOPEN_SOURCE 700

#define main wm_prepare_command_main
#include "../tools/prepare.c"
#undef main

#include <assert.h>

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
    assert(stream);
    assert(fwrite(value, 1, strlen(value), stream) == strlen(value));
    assert(fclose(stream) == 0);
}

static void read_text(const char *path, char *output, size_t capacity) {
    FILE *stream = fopen(path, "rb");
    assert(stream);
    size_t size = fread(output, 1, capacity - 1, stream);
    assert(!ferror(stream) && fgetc(stream) == EOF);
    output[size] = '\0';
    assert(fclose(stream) == 0);
}

static void replace_digest(const char *path, const char *original,
                           const char *replacement) {
    char json[4096];
    read_text(path, json, sizeof(json));
    char *found = strstr(json, original);
    assert(found && strlen(original) == strlen(replacement));
    memcpy(found, replacement, strlen(replacement));
    write_text(path, json);
}

static void fixture(const char *directory, bool incoming) {
    char path[PREPARE_PATH_CAPACITY];
    assert(mkdir(directory, 0700) == 0);
    assert(path_join(path, sizeof(path), directory, "channels.json"));
    if (incoming) {
        write_text(path,
            "{\n"
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
        write_text(path,
            "{\n"
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
        assert(path_join(path, sizeof(path), directory, "iplsave.bin"));
        write_text(path, "preserved placement");
    }
    const char *first = incoming ? "A new artwork" : "A old artwork";
    assert(path_join(path, sizeof(path), directory,
                     "channel-layouts/0001000141414141/icon/icon.json"));
    write_text(path, first);
    assert(path_join(path, sizeof(path), directory,
                     incoming ? "channel-layouts/0001000143434343/icon/icon.json" :
                                "channel-layouts/0001000142424242/icon/icon.json"));
    write_text(path, incoming ? "C new artwork" : "B old artwork");
    assert(path_join(path, sizeof(path), directory,
                     "channel-audio/0001000141414141.wav"));
    write_text(path, incoming ? "A new audio" : "A old audio");
}

static void assert_artwork(const char *directory, const char *id,
                           const char *expected) {
    char relative[96];
    char path[PREPARE_PATH_CAPACITY];
    char actual[128];
    assert(snprintf(relative, sizeof(relative),
                    "channel-layouts/%s/icon/icon.json", id) > 0);
    assert(path_join(path, sizeof(path), directory, relative));
    read_text(path, actual, sizeof(actual));
    assert(strcmp(actual, expected) == 0);
}

static void assert_catalog(const char *directory, const char *first_title,
                           size_t expected_count) {
    PrepareManifest manifest;
    assert(open_manifest(directory, &manifest));
    assert(manifest.count == expected_count);
    size_t title = wm_json_member(&manifest.json, manifest.channels[0].token,
                                  "title");
    assert(wm_json_equals(&manifest.json, title, first_title));
    assert(manifest.saved_layout != WM_JSON_INVALID);
    assert(manifest.json.tokens[manifest.saved_layout].type == WM_JSON_OBJECT);
    close_manifest(&manifest);
}

int main(int argc, char **argv) {
    (void)argc;
    char *temporary_parent = realpath("/tmp", NULL);
    assert(temporary_parent);
    char root[PREPARE_PATH_CAPACITY];
    assert(snprintf(root, sizeof(root), "%s/wm-prepare-update-test-XXXXXX",
                    temporary_parent) > 0);
    free(temporary_parent);
    assert(mkdtemp(root));
    char base[PREPARE_PATH_CAPACITY], incoming[PREPARE_PATH_CAPACITY];
    char stage[PREPARE_PATH_CAPACITY], path[PREPARE_PATH_CAPACITY];
    assert(path_join(base, sizeof(base), root, "base"));
    assert(path_join(incoming, sizeof(incoming), root, "incoming"));
    fixture(base, false);
    fixture(incoming, true);

    PrepareChoices empty = {0};
    assert(path_join(stage, sizeof(stage), root, "stage-keep"));
    assert(copy_tree(base, stage));
    assert(update_channels(base, incoming, stage, &empty, &empty, false));
    assert_catalog(stage, "A old", 3);
    assert_artwork(stage, "0001000141414141", "A old artwork");
    assert_artwork(stage, "0001000142424242", "B old artwork");
    assert_artwork(stage, "0001000143434343", "C new artwork");
    assert(path_join(path, sizeof(path), stage, "iplsave.bin"));
    char placement[128];
    read_text(path, placement, sizeof(placement));
    assert(strcmp(placement, "preserved placement") == 0);

    PrepareChoices replace = {0};
    assert(add_choice(&replace, "0001000141414141"));
    assert(path_join(stage, sizeof(stage), root, "stage-replace"));
    assert(copy_tree(base, stage));
    assert(update_channels(base, incoming, stage, &replace, &empty, false));
    assert_catalog(stage, "A new", 3);
    assert_artwork(stage, "0001000141414141", "A new artwork");
    assert(path_join(path, sizeof(path), stage,
                     "channel-audio/0001000141414141.wav"));
    read_text(path, placement, sizeof(placement));
    assert(strcmp(placement, "A new audio") == 0);

    PrepareChoices keep = {0};
    assert(add_choice(&keep, "0001000141414141"));
    assert(add_choice(&keep, "0001000143434343"));
    assert(path_join(stage, sizeof(stage), root, "stage-bulk"));
    assert(copy_tree(base, stage));
    assert(update_channels(base, incoming, stage, &empty, &keep, true));
    assert_catalog(stage, "A old", 2);
    assert_artwork(stage, "0001000141414141", "A old artwork");

    PrepareChoices absent = {0};
    assert(add_choice(&absent, "0001000144444444"));
    assert(path_join(stage, sizeof(stage), root, "stage-absent"));
    assert(copy_tree(base, stage));
    assert(!update_channels(base, incoming, stage, &absent, &empty, false));
    assert_catalog(stage, "A old", 2);

    PrepareManifest base_manifest, incoming_manifest;
    assert(open_manifest(base, &base_manifest));
    assert(open_manifest(incoming, &incoming_manifest));
    char old_hash[41], new_hash[41];
    assert(channel_export_hash(&base_manifest, 0, base, old_hash));
    assert(channel_export_hash(&incoming_manifest, 0, incoming, new_hash));
    assert(strcmp(old_hash, new_hash) != 0);
    close_manifest(&incoming_manifest);
    close_manifest(&base_manifest);

    char nand_source_path[PREPARE_PATH_CAPACITY];
    char original_nand_sha1[41];
    char changed_nand_sha1[41];
    assert(path_join(nand_source_path, sizeof(nand_source_path), root,
                     "identity-input.bin"));
    write_text(nand_source_path, "source NAND bytes");
    assert(hash_regular_file(nand_source_path, original_nand_sha1));
    assert(path_join(path, sizeof(path), root, "plan.json"));
    FILE *plan_stream = fopen(path, "wb+");
    assert(plan_stream);
    assert(print_update_plan(plan_stream, base, incoming,
                             original_nand_sha1,
                             &empty, &empty, false));
    assert(fclose(plan_stream) == 0);
    WmJson plan_json;
    assert(wm_json_load(&plan_json, path, PREPARE_MAX_MANIFEST));
    assert(wm_json_equals(&plan_json,
                          wm_json_member(&plan_json, 0, "inputNandSha1"),
                          original_nand_sha1));
    assert(wm_json_equals(&plan_json,
                          wm_json_member(&plan_json, 0, "sourceHashKind"),
                          "TMD-bytes-and-TMD-validated-active-content-SHA1"));
    size_t rows = wm_json_member(&plan_json, 0, "rows");
    assert(rows != WM_JSON_INVALID && plan_json.tokens[rows].children == 2);
    assert(wm_json_equals(&plan_json,
                          wm_json_member(&plan_json, wm_json_index(&plan_json,
                                                                  rows, 0), "action"),
                          "keep"));
    assert(wm_json_equals(&plan_json,
                          wm_json_member(&plan_json, wm_json_index(&plan_json,
                                                                  rows, 1), "action"),
                          "add"));
    size_t first_row = wm_json_index(&plan_json, rows, 0);
    assert(wm_json_equals(&plan_json,
                          wm_json_member(&plan_json, first_row,
                                         "incomingContentSha1"),
                          A_NEW_CONTENT));
    assert(wm_json_equals(&plan_json,
                          wm_json_member(&plan_json, first_row,
                                         "incomingTmdSha1"),
                          A_NEW_TMD));
    assert(strstr(plan_json.source, root) == NULL);
    wm_json_free(&plan_json);
    assert(verify_expected_plan(path, base, incoming, original_nand_sha1,
                                &empty, &empty, false));
    write_text(nand_source_path, "source NAND byteX");
    assert(hash_regular_file(nand_source_path, changed_nand_sha1));
    assert(strcmp(original_nand_sha1, changed_nand_sha1) != 0);
    assert(!verify_expected_plan(path, base, incoming, changed_nand_sha1,
                                 &empty, &empty, false));
    assert(!verify_expected_plan(path, base, incoming, original_nand_sha1,
                                 &replace, &empty, false));

    /* A source-content change must invalidate the review even if export
     * artwork and the supplied whole-NAND identity are held constant. The
     * unrelated NAND-byte change is independently rejected above. */
    char incoming_catalog[PREPARE_PATH_CAPACITY];
    assert(path_join(incoming_catalog, sizeof(incoming_catalog), incoming,
                     "channels.json"));
    replace_digest(incoming_catalog, A_NEW_CONTENT, A_CHANGED_CONTENT);
    assert(!verify_expected_plan(path, base, incoming, original_nand_sha1,
                                 &empty, &empty, false));
    assert(!verify_expected_plan(path, base, incoming, changed_nand_sha1,
                                 &empty, &empty, false));
    replace_digest(incoming_catalog, A_CHANGED_CONTENT, A_NEW_CONTENT);
    assert(verify_expected_plan(path, base, incoming, original_nand_sha1,
                                &empty, &empty, false));

    char artwork_path[PREPARE_PATH_CAPACITY];
    assert(path_join(artwork_path, sizeof(artwork_path), incoming,
                     "channel-layouts/0001000141414141/icon/icon.json"));
    write_text(artwork_path, "changed incoming artwork");
    assert(!verify_expected_plan(path, base, incoming, original_nand_sha1,
                                 &empty, &empty, false));
    write_text(artwork_path, "A new artwork");
    assert(path_join(artwork_path, sizeof(artwork_path), base,
                     "channel-layouts/0001000141414141/icon/icon.json"));
    write_text(artwork_path, "changed installed artwork");
    assert(!verify_expected_plan(path, base, incoming, original_nand_sha1,
                                 &empty, &empty, false));
    write_text(artwork_path, "A old artwork");
    assert(verify_expected_plan(path, base, incoming, original_nand_sha1,
                                &empty, &empty, false));

    /* An interrupted preparation owns only its marked scratch directory.
     * Recovery leaves unrelated scratch and published generations alone. */
    char recovery_output[PREPARE_PATH_CAPACITY];
    char recovery_identity_hash[41];
    char recovery_stage[PREPARE_PATH_CAPACITY];
    char unrelated_stage[PREPARE_PATH_CAPACITY];
    struct stat metadata;
    assert(path_join(recovery_output, sizeof(recovery_output), root,
                     "recovery-output"));
    assert(path_join(unrelated_stage, sizeof(unrelated_stage), root,
                     ".wm-prepare-unrelated"));
    assert(mkdir(unrelated_stage, 0700) == 0);
    recovery_identity('O', recovery_output, recovery_identity_hash);
    assert(create_owned_stage(root, recovery_identity_hash, recovery_stage));
    assert(path_join(path, sizeof(path), recovery_stage,
                     "nand/private-content.bin"));
    write_text(path, "private staged bytes");
    char *recover_command[] = {argv[0], "--recover", "--output",
                               recovery_output, NULL};
    assert(wm_prepare_command_main(4, recover_command) == 0);
    assert(lstat(recovery_stage, &metadata) != 0 && errno == ENOENT);
    assert(lstat(unrelated_stage, &metadata) == 0 && S_ISDIR(metadata.st_mode));
    assert(lstat(recovery_output, &metadata) != 0 && errno == ENOENT);

    assert(create_owned_stage(root, recovery_identity_hash, recovery_stage));
    char invalid_nand[PREPARE_PATH_CAPACITY];
    assert(path_join(invalid_nand, sizeof(invalid_nand), root,
                     "recovery-invalid-nand.bin"));
    write_text(invalid_nand, "not a BootMii dump");
    char *automatic_recovery[] = {argv[0], "--update-from", base,
                                  "--nand", invalid_nand, "--output",
                                  recovery_output, NULL};
    assert(wm_prepare_command_main(7, automatic_recovery) == 1);
    assert(lstat(recovery_stage, &metadata) != 0 && errno == ENOENT);
    assert(lstat(recovery_output, &metadata) != 0 && errno == ENOENT);

    assert(create_owned_stage(root, recovery_identity_hash, recovery_stage));
    char wrong_identity[41];
    recovery_identity('O', unrelated_stage, wrong_identity);
    assert(!recover_owned_stage(root, wrong_identity));
    assert(lstat(recovery_stage, &metadata) == 0 && S_ISDIR(metadata.st_mode));
    assert(recover_owned_stage(root, recovery_identity_hash));

    assert(create_owned_stage(root, recovery_identity_hash, recovery_stage));
    char marker[PREPARE_PATH_CAPACITY];
    assert(path_join(marker, sizeof(marker), recovery_stage,
                     PREPARE_STAGE_MARKER));
    write_text(marker, "not a matching ownership marker");
    assert(!recover_owned_stage(root, recovery_identity_hash));
    assert(lstat(recovery_stage, &metadata) == 0 && S_ISDIR(metadata.st_mode));
    char journal[PREPARE_PATH_CAPACITY];
    PrepareRecoveryRecord record;
    assert(path_join(journal, sizeof(journal), root, PREPARE_JOURNAL_NAME));
    assert(read_recovery_record(journal, &record));
    assert(unlink(marker) == 0);
    assert(write_recovery_record(marker, &record));
    assert(recover_owned_stage(root, recovery_identity_hash));

    assert(create_owned_stage(root, recovery_identity_hash, recovery_stage));
    char staged_assets[PREPARE_PATH_CAPACITY];
    assert(path_join(staged_assets, sizeof(staged_assets), recovery_stage,
                     "assets"));
    assert(mkdir(staged_assets, 0700) == 0);
    assert(path_join(path, sizeof(path), staged_assets, "published.txt"));
    write_text(path, "committed generation");
    assert(publish_directory_no_replace(staged_assets, recovery_output));
    assert(wm_prepare_command_main(4, recover_command) == 0);
    assert(path_join(path, sizeof(path), recovery_output, "published.txt"));
    read_text(path, placement, sizeof(placement));
    assert(strcmp(placement, "committed generation") == 0);
    assert(path_join(path, sizeof(path), recovery_output,
                     PREPARE_STAGE_MARKER));
    assert(lstat(path, &metadata) != 0 && errno == ENOENT);
    assert(lstat(unrelated_stage, &metadata) == 0 && S_ISDIR(metadata.st_mode));

    recovery_identity('P', base, recovery_identity_hash);
    assert(create_owned_stage(root, recovery_identity_hash, recovery_stage));
    char *plan_recover_command[] = {argv[0], "--recover", "--plan",
                                    "--update-from", base, NULL};
    assert(wm_prepare_command_main(5, plan_recover_command) == 0);
    assert(lstat(recovery_stage, &metadata) != 0 && errno == ENOENT);

    assert(path_join(stage, sizeof(stage), root, "stage-malformed"));
    assert(copy_tree(base, stage));
    assert(path_join(path, sizeof(path), incoming, "channels.json"));
    assert(unlink(path) == 0);
    write_text(path, "{\"channels\": [broken]}");
    assert(!update_channels(base, incoming, stage, &empty, &empty, false));
    assert_catalog(stage, "A old", 2);

    assert(path_join(path, sizeof(path), base, "linked"));
    assert(symlink("channels.json", path) == 0);
    assert(path_join(stage, sizeof(stage), root, "stage-symlink"));
    assert(!copy_tree(base, stage));
    assert(unlink(path) == 0);

    assert(path_join(path, sizeof(path), root, "fake-nand.bin"));
    write_text(path, "not a NAND");
    char existing_output[PREPARE_PATH_CAPACITY];
    assert(path_join(existing_output, sizeof(existing_output), root,
                     "existing-output"));
    assert(mkdir(existing_output, 0700) == 0);
    char *command[] = {argv[0], "--update-from", base, "--nand", path,
                       "--output", existing_output, NULL};
    assert(wm_prepare_command_main(7, command) == 1);
    assert(lstat(existing_output, &metadata) == 0 && S_ISDIR(metadata.st_mode));

    char nested_output[PREPARE_PATH_CAPACITY];
    assert(path_join(nested_output, sizeof(nested_output), base, "nested-output"));
    command[6] = nested_output;
    assert(wm_prepare_command_main(7, command) == 1);
    assert(lstat(nested_output, &metadata) != 0 && errno == ENOENT);

    char linked_parent[PREPARE_PATH_CAPACITY];
    assert(path_join(linked_parent, sizeof(linked_parent), root, "linked-parent"));
    assert(symlink(base, linked_parent) == 0);
    char linked_output[PREPARE_PATH_CAPACITY];
    assert(path_join(linked_output, sizeof(linked_output), linked_parent,
                     "linked-output"));
    command[6] = linked_output;
    assert(wm_prepare_command_main(7, command) == 1);
    assert(lstat(linked_output, &metadata) != 0 && errno == ENOENT);

    assert(remove_tree(root));
    puts("prepare update tests passed");
    return 0;
}
