#include "board_body_pane.h"

#include <string.h>

bool wm_board_body_pane(void *context, const WmLayoutPaneView *pane) {
    (void)context;
    static const char *const names[] = {
        "RootPane",   "N_Memo",     "N_MemoRoot", "N_Body",   "Body_s", "Body3",
        "Picture_11", "Picture_12", "Picture_13", "Body3_04", "B_Body"};
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        if (strcmp(pane->name, names[index]) == 0)
            return true;
    }
    return false;
}
