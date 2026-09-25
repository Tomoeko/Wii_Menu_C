#include "reader.h"

#include <stdio.h>
#include <string.h>

static int usage(void)
{
    fputs("Usage: wm-nand-extract DUMP OUTPUT_DIRECTORY [--keys KEY_FILE]\n", stderr);
    return 2;
}

int main(int argc, char **argv)
{
    const char *keys = NULL;
    if (argc == 5 && strcmp(argv[3], "--keys") == 0) {
        keys = argv[4];
    } else if (argc != 3) {
        return usage();
    }
    WmNandSummary summary = {0};
    char error[192] = {0};
    if (!wm_nand_extract_channels(argv[1], keys, argv[2], &summary,
                                   error, sizeof(error))) {
        fprintf(stderr, "NAND extraction failed: %s\n", error);
        return 1;
    }
    printf("Authenticated NAND generation %u; extracted %zu files for %zu channel titles and %zu shared font archive.\n",
           summary.generation, summary.extracted_files,
           summary.discovered_titles, summary.shared_font_archives);
    return 0;
}
