#ifndef WM_PREPARATION_PREPARE_RECOVERY_H
#define WM_PREPARATION_PREPARE_RECOVERY_H

#include "prepare_fs.h"

#include <stdbool.h>

/* A stage is removable only when its journal and ownership marker match. */
void recovery_identity(char kind, const char *path, char result[41]);
int lock_preparation_parent(const char *parent);
bool recover_owned_stage(const char *parent, const char *identity);
bool create_owned_stage(const char *parent, const char *identity,
                        char stage[PREPARE_PATH_CAPACITY]);

#endif
