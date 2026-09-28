#include "board_model.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool leap_year(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

bool wm_board_date_valid(WmBoardDate date) {
    static const int month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (date.year < 2000 || date.year > 2035 || date.month < 1 || date.month > 12 ||
        date.day < 1) {
        return false;
    }
    int maximum = month_days[date.month - 1];
    if (date.month == 2 && leap_year(date.year)) {
        maximum++;
    }
    return date.day <= maximum;
}

/* Days since 1970-01-01. Input dates have the Board's bounded year range. */
int64_t board_model_day_number(WmBoardDate date) {
    int64_t year = date.year - (date.month <= 2);
    int64_t era = year / 400;
    int64_t year_of_era = year - era * 400;
    int64_t month = date.month + (date.month > 2 ? -3 : 9);
    int64_t day_of_year = (153 * month + 2) / 5 + date.day - 1;
    int64_t year_day =
        year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + year_day - 719468;
}

WmBoardDate board_model_date_from_day(int64_t day) {
    day += 719468;
    int64_t era = day / 146097;
    int64_t day_of_era = day - era * 146097;
    int64_t year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) /
        365;
    int64_t year = year_of_era + era * 400;
    int64_t day_of_year =
        day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    int64_t month_part = (5 * day_of_year + 2) / 153;
    int month = (int)(month_part + (month_part < 10 ? 3 : -9));
    return (WmBoardDate){.year = (int)(year + (month <= 2)),
                         .month = month,
                         .day = (int)(day_of_year - (153 * month_part + 2) / 5 + 1)};
}

bool wm_board_date_shift(WmBoardDate date, int days, WmBoardDate *result) {
    if (!result || !wm_board_date_valid(date)) {
        return false;
    }
    int64_t shifted = board_model_day_number(date) + (int64_t)days;
    WmBoardDate value = board_model_date_from_day(shifted);
    if (!wm_board_date_valid(value)) {
        return false;
    }
    *result = value;
    return true;
}

unsigned wm_board_badge_count(const WmBoardMemo *memos, size_t count,
                              WmBoardDate today) {
    if (!memos || !wm_board_date_valid(today)) {
        return 0;
    }
    unsigned found = 0;
    for (size_t index = 0; index < count; index++) {
        if (board_model_same_date(memos[index].date, today) && found < 99) {
            found++;
        }
    }
    return found;
}

float board_model_clamp_position(float value, float minimum, float maximum) {
    return fminf(fmaxf(value, minimum), maximum);
}

static char *copy_bytes(const char *value, size_t length) {
    char *copy = malloc(length + 1);
    if (copy) {
        memcpy(copy, value, length + 1);
    }
    return copy;
}

void board_model_dispose(BoardModel *model) {
    if (!model) {
        return;
    }
    for (size_t index = 0; index < model->memo_count; index++) {
        free(model->memos[index].id);
        free(model->memos[index].text);
    }
    free(model->memos);
    free(model->memo_order);
    *model = (BoardModel){0};
}

static int compare_memo_order(const void *left, const void *right) {
    const BoardMemoOrder *first = left;
    const BoardMemoOrder *second = right;
    if (first->created_at_ms > second->created_at_ms) {
        return -1;
    }
    if (first->created_at_ms < second->created_at_ms) {
        return 1;
    }
    if (first->index < second->index) {
        return -1;
    }
    if (first->index > second->index) {
        return 1;
    }
    return 0;
}

bool board_model_copy(BoardModel *model, const WmBoardMemo *memos, size_t count,
                      float settled_paste_age) {
    if (!model || (count && !memos) || count > BOARD_MODEL_MAX_MEMOS ||
        count > SIZE_MAX / sizeof(BoardMemo) ||
        count > SIZE_MAX / sizeof(BoardMemoOrder)) {
        return false;
    }

    BoardModel next = {0};
    next.memos = count ? calloc(count, sizeof(*next.memos)) : NULL;
    next.memo_order = count ? malloc(count * sizeof(*next.memo_order)) : NULL;
    if (count && (!next.memos || !next.memo_order)) {
        board_model_dispose(&next);
        return false;
    }
    next.memo_count = count;

    size_t total_bytes = 0;
    for (size_t index = 0; index < count; index++) {
        const WmBoardMemo *source = &memos[index];
        if (!source->id || !source->text || !wm_board_date_valid(source->date) ||
            source->created_at_ms < 0 ||
            source->created_at_ms > INT64_C(253402300799999)) {
            board_model_dispose(&next);
            return false;
        }
        size_t id_bytes = strlen(source->id);
        size_t text_bytes = strlen(source->text);
        if (id_bytes > BOARD_MODEL_MAX_TEXT_BYTES - total_bytes ||
            text_bytes > BOARD_MODEL_MAX_TEXT_BYTES - total_bytes - id_bytes) {
            board_model_dispose(&next);
            return false;
        }
        total_bytes += id_bytes + text_bytes;

        BoardMemo *target = &next.memos[index];
        target->id = copy_bytes(source->id, id_bytes);
        target->text = copy_bytes(source->text, text_bytes);
        if (!target->id || !target->text) {
            board_model_dispose(&next);
            return false;
        }
        target->date = source->date;
        target->created_at_ms = source->created_at_ms;
        target->x = source->has_position
                        ? board_model_clamp_position(source->x, -230.0f, 230.0f)
                        : 0.0f;
        target->y = source->has_position
                        ? board_model_clamp_position(source->y, -80.0f, 180.0f)
                        : 53.0f;
        target->read = source->read;
        target->paste_age = settled_paste_age;
        next.memo_order[index] = (BoardMemoOrder){index, source->created_at_ms};
    }
    if (count > 1) {
        qsort(next.memo_order, count, sizeof(*next.memo_order), compare_memo_order);
    }

    board_model_dispose(model);
    *model = next;
    return true;
}

bool board_model_get_memo(const BoardModel *model, size_t index, WmBoardMemo *memo) {
    if (!model || !memo || index >= model->memo_count) {
        return false;
    }
    const BoardMemo *source = &model->memos[index];
    *memo = (WmBoardMemo){.id = source->id,
                          .text = source->text,
                          .date = source->date,
                          .created_at_ms = source->created_at_ms,
                          .x = source->x,
                          .y = source->y,
                          .has_position = true,
                          .read = source->read};
    return true;
}

char *board_model_remove(BoardModel *model, size_t index) {
    if (!model || index >= model->memo_count) {
        return NULL;
    }
    char *id = model->memos[index].id;
    free(model->memos[index].text);
    if (index + 1 < model->memo_count) {
        memmove(&model->memos[index], &model->memos[index + 1],
                (model->memo_count - index - 1) * sizeof(*model->memos));
    }
    for (size_t position = 0; position < model->memo_count; position++) {
        if (model->memo_order[position].index == index) {
            memmove(&model->memo_order[position], &model->memo_order[position + 1],
                    (model->memo_count - position - 1) * sizeof(*model->memo_order));
            break;
        }
    }
    model->memo_count--;
    for (size_t position = 0; position < model->memo_count; position++) {
        if (model->memo_order[position].index > index) {
            model->memo_order[position].index--;
        }
    }
    return id;
}
