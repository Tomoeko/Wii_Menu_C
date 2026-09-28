#ifndef WII_MENU_BOARD_MODEL_H
#define WII_MENU_BOARD_MODEL_H

#include "wii_menu/board/board_scene.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { BOARD_MODEL_MAX_MEMOS = 4096, BOARD_MODEL_MAX_TEXT_BYTES = 16 * 1024 * 1024 };

typedef struct BoardMemo {
    char *id;
    char *text;
    WmBoardDate date;
    int64_t created_at_ms;
    float x;
    float y;
    bool read;
    WmBoardPinKind pin_kind;
    bool pin_sampled;
    float pin_start_clock;
    float paste_age;
} BoardMemo;

typedef struct BoardMemoOrder {
    size_t index;
    int64_t created_at_ms;
} BoardMemoOrder;

/* The model owns its memo strings and sorted index. The scene borrows both
 * arrays until the model is replaced or destroyed. */
typedef struct BoardModel {
    BoardMemo *memos;
    BoardMemoOrder *memo_order;
    size_t memo_count;
} BoardModel;

/* An initialized model is replaced only after the full input validates and
 * every allocation succeeds. Failure leaves the old model untouched. */
bool board_model_copy(BoardModel *model, const WmBoardMemo *memos, size_t count,
                      float settled_paste_age);
void board_model_dispose(BoardModel *model);
bool board_model_get_memo(const BoardModel *model, size_t index, WmBoardMemo *memo);
/* Returns the removed ID to the caller, which must free it. */
char *board_model_remove(BoardModel *model, size_t index);

static inline bool board_model_same_date(WmBoardDate first, WmBoardDate second) {
    return first.year == second.year && first.month == second.month &&
           first.day == second.day;
}
int64_t board_model_day_number(WmBoardDate date);
WmBoardDate board_model_date_from_day(int64_t day);
float board_model_clamp_position(float value, float minimum, float maximum);

#endif
