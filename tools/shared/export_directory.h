#ifndef WII_MENU_EXPORT_DIRECTORY_H
#define WII_MENU_EXPORT_DIRECTORY_H

#include <stdbool.h>
#include <sys/types.h>

/* The caller chooses the output root. Its existing ancestors may contain
 * symlinks (for example, macOS /tmp), but the root itself must be a real
 * directory. Only a missing final root component is created. */
bool wm_export_directory_root(const char *path, mode_t mode);

/* Walk below an accepted root without following symlinks. Only the final
 * relative component is created; earlier child directories must exist. */
bool wm_export_directory_child(const char *root, const char *relative, mode_t mode);

#endif
