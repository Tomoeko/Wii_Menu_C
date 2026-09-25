#ifndef WII_MENU_BOARD_SCENE_H
#define WII_MENU_BOARD_SCENE_H

#include "wii_menu/board_contact_store.h"

#include "wii_menu/font_cache.h"
#include "wii_menu/board_keyboard.h"
#include "wii_menu/platform.h"
#include "wii_menu/texture_cache.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct WmBoardScene WmBoardScene;

typedef struct WmBoardDate {
    int year;
    int month;
    int day;
} WmBoardDate;

/* Records are caller-owned on input. The board copies text and IDs. Positions
 * are source BoardObject coordinates, constrained to x [-230,230] and
 * y [-80,180]; a missing position uses the original default (0,53). */
typedef struct WmBoardMemo {
    const char *id;
    const char *text;
    WmBoardDate date;
    /* UTC milliseconds since 1970. Zero means a legacy date-only record;
     * those records retain their saved order after timestamped records. */
    int64_t created_at_ms;
    float x;
    float y;
    bool has_position;
    bool read;
} WmBoardMemo;

typedef enum WmBoardPinKind {
    WM_BOARD_PIN_NONE,
    WM_BOARD_PIN_NEW,
    WM_BOARD_PIN_DEFAULT
} WmBoardPinKind;

/* BoardObject samples the clock once when a card first appears. A caller may
 * supply a console clock; NULL restores the local system clock. */
typedef int64_t (*WmBoardTimeNow)(void *context);
void wm_board_scene_set_pin_clock(WmBoardScene *board,
                                   WmBoardTimeNow time_now, void *context);

typedef enum WmBoardPhase {
    WM_BOARD_CLOSED,
    WM_BOARD_ENTER,
    WM_BOARD_READY,
    WM_BOARD_DATE_SCROLL,
    WM_BOARD_MEMO_PAGE,
    WM_BOARD_MEMO_OPEN,
    WM_BOARD_MEMO_READ,
    WM_BOARD_MEMO_BACK_SELECT,
    WM_BOARD_MEMO_CLOSE,
    WM_BOARD_MEMO_TRASH_SELECT,
    WM_BOARD_MEMO_DIALOG,
    WM_BOARD_MEMO_TRASH_CANCEL,
    WM_BOARD_MEMO_ERASE_CLOSE,
    WM_BOARD_EXIT
} WmBoardPhase;

typedef enum WmBoardControl {
    WM_BOARD_CONTROL_NONE,
    WM_BOARD_CONTROL_BACK,
    WM_BOARD_CONTROL_CALENDAR,
    WM_BOARD_CONTROL_CREATE,
    WM_BOARD_CONTROL_PREVIOUS,
    WM_BOARD_CONTROL_NEXT,
    WM_BOARD_CONTROL_MEMO,
    WM_BOARD_CONTROL_MEMO_BACK,
    WM_BOARD_CONTROL_MEMO_TRASH,
    WM_BOARD_CONTROL_MEMO_SCROLL_UP,
    WM_BOARD_CONTROL_MEMO_SCROLL_DOWN,
    WM_BOARD_CONTROL_CALENDAR_BACK,
    WM_BOARD_CONTROL_CALENDAR_PREVIOUS,
    WM_BOARD_CONTROL_CALENDAR_NEXT,
    WM_BOARD_CONTROL_CALENDAR_DAY,
    WM_BOARD_CONTROL_COMPOSE_BACK,
    WM_BOARD_CONTROL_COMPOSE_MEMO,
    WM_BOARD_CONTROL_COMPOSE_LETTER,
    WM_BOARD_CONTROL_COMPOSE_ADDRESS,
    WM_BOARD_CONTROL_COMPOSE_EDIT,
    WM_BOARD_CONTROL_COMPOSE_POST,
    WM_BOARD_CONTROL_COMPOSE_MII,
    WM_BOARD_CONTROL_ERASE_QUIT,
    WM_BOARD_CONTROL_ERASE_OK,
    WM_BOARD_CONTROL_COMPOSE_SCROLL_UP,
    WM_BOARD_CONTROL_COMPOSE_SCROLL_DOWN,
    WM_BOARD_CONTROL_COMPOSE_ADDRESS_PREVIOUS,
    WM_BOARD_CONTROL_COMPOSE_ADDRESS_NEXT,
    WM_BOARD_CONTROL_COMPOSE_ADDRESS_WII,
    WM_BOARD_CONTROL_COMPOSE_ADDRESS_OTHERS,
    WM_BOARD_CONTROL_COMPOSE_ADDRESS_EDIT,
    WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_OK,
    WM_BOARD_CONTROL_COMPOSE_ADDRESS_MII,
    WM_BOARD_CONTROL_COMPOSE_ADDRESS_INFO,
    WM_BOARD_CONTROL_COMPOSE_ADDRESS_CHANGE_NICKNAME,
    WM_BOARD_CONTROL_COMPOSE_ADDRESS_ERASE,
    WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_YES,
    WM_BOARD_CONTROL_COMPOSE_ADDRESS_DIALOG_NO,
    WM_BOARD_CONTROL_COMPOSE_NETWORK_QUIT,
    WM_BOARD_CONTROL_COMPOSE_NETWORK_SETTINGS,
    WM_BOARD_CONTROL_COMPOSE_ADDRESS_ENTRY_FIRST,
    WM_BOARD_CONTROL_COMPOSE_ADDRESS_ENTRY_LAST =
        WM_BOARD_CONTROL_COMPOSE_ADDRESS_ENTRY_FIRST + 4,
    WM_BOARD_CONTROL_COMPOSE_KEY_FIRST = 100,
    WM_BOARD_CONTROL_COMPOSE_KEY_LAST =
        WM_BOARD_CONTROL_COMPOSE_KEY_FIRST + WM_KEYBOARD_CONTROL_LAST - 1
} WmBoardControl;

/* Consume each Memo keyboard cue after activating a Board control. */
const char *wm_board_scene_take_compose_key_cue(WmBoardScene *board);
bool wm_board_scene_hold_compose_control(WmBoardScene *board,
                                          WmBoardControl control);
void wm_board_scene_release_compose_control(WmBoardScene *board);

typedef struct WmBoardHit {
    WmBoardControl control;
    size_t memo_index; /* Memo index, or Calendar's visible day index. */
} WmBoardHit;

enum { WM_BOARD_MAX_PRESENTED_MEMOS = 20 };

/* Draw order and parent-layout coordinates for the current and entering Memo
 * pages. Existing records are already at the settled PasteLetter pose; only
 * a newly posted Memo progresses through that clip. An entering card ignores
 * outgoing page, hover, selection, and drag state. */
typedef struct WmBoardMemoPresentation {
    size_t memo_index;
    float x;
    float y;
    float paste_frame;
    float next_page_frame; /* -1 when the outgoing NextPage clip is inactive. */
    WmBoardPinKind pin_kind;
    bool entering;
} WmBoardMemoPresentation;

typedef enum WmBoardAction {
    WM_BOARD_ACTION_NONE,
    WM_BOARD_ACTION_EXITED,
    WM_BOARD_ACTION_CALENDAR,
    WM_BOARD_ACTION_CREATE,
    WM_BOARD_ACTION_ERASE_MEMO,
    WM_BOARD_ACTION_MEMO_POSTED,
    WM_BOARD_ACTION_OPEN_SETTINGS,
    WM_BOARD_ACTION_MEMO_READ,
    WM_BOARD_ACTION_MEMO_MOVED
} WmBoardAction;

typedef enum WmBoardDragCue {
    WM_BOARD_DRAG_CUE_NONE,
    WM_BOARD_DRAG_CUE_HOLD,
    WM_BOARD_DRAG_CUE_RELEASE
} WmBoardDragCue;

typedef enum WmBoardChild {
    WM_BOARD_CHILD_NONE,
    WM_BOARD_CHILD_CALENDAR,
    WM_BOARD_CHILD_COMPOSE,
    WM_BOARD_CHILD_ERASE
} WmBoardChild;

/* These pure helpers use the source Board calendar's inclusive 2000–2035
 * range and count cap of 99. A failed shift leaves the input unchanged. */
bool wm_board_date_valid(WmBoardDate date);
bool wm_board_date_shift(WmBoardDate date, int days, WmBoardDate *result);
unsigned wm_board_badge_count(const WmBoardMemo *memos, size_t count,
                              WmBoardDate today);

/* The scene owns parsed layouts and memo strings. All GPU objects remain owned
 * by the caller's platform and caches. Source assets are created locally from
 * the supplied System Menu WAD; no extracted resource is embedded in C. */
WmBoardScene *wm_board_scene_create(WmPlatform *platform,
                                     const char *assets_directory,
                                     WmTextureCache *textures,
                                     WmFontCache *fonts);
void wm_board_scene_destroy(WmBoardScene *board);
WmBoardContactStoreStatus wm_board_scene_load_contacts(
    WmBoardScene *board, const char *path,
    char *error, size_t error_capacity);
bool wm_board_scene_set_memos(WmBoardScene *board,
                               const WmBoardMemo *memos, size_t count);
bool wm_board_scene_open(WmBoardScene *board, WmBoardDate date);
/* Immediate HOME restart handoff. Keeps loaded memos and source layouts. */
void wm_board_scene_reset(WmBoardScene *board);
/* ChannelSelect page remains relevant during the Board footer's first ten
 * entry frames and last twenty exit frames. */
void wm_board_scene_set_grid_page(WmBoardScene *board, int page);
bool wm_board_scene_sd_visible(const WmBoardScene *board);
bool wm_board_scene_back(WmBoardScene *board);
void wm_board_scene_advance(WmBoardScene *board, float frames);
WmBoardPhase wm_board_scene_phase(const WmBoardScene *board);
WmBoardChild wm_board_scene_child(const WmBoardScene *board);
/* True for an active Memo or Address Book text editor. */
bool wm_board_scene_compose_editor_active(const WmBoardScene *board);
bool wm_board_scene_compose_keyboard_overlay_visible(
    const WmBoardScene *board);
/* Narrow route for physical Enter in an Address Book text editor. */
bool wm_board_scene_address_editor_active(const WmBoardScene *board);
WmBoardDate wm_board_scene_date(const WmBoardScene *board);
size_t wm_board_scene_memo_page(const WmBoardScene *board);
size_t wm_board_scene_memo_page_count(const WmBoardScene *board);
unsigned wm_board_scene_today_count(const WmBoardScene *board);
unsigned wm_board_scene_today_unread_count(const WmBoardScene *board);
/* Refresh the Home badge's calendar day while the Board is closed. An open
 * Board keeps its selected date and current-day reference through navigation. */
bool wm_board_scene_refresh_today(WmBoardScene *board, WmBoardDate today);
size_t wm_board_scene_memo_count(const WmBoardScene *board);
/* Returned ID/text pointers remain Board-owned until set_memos, a new post,
 * erase, or destroy. The caller copies them before persisting. */
bool wm_board_scene_get_memo(const WmBoardScene *board, size_t index,
                              WmBoardMemo *memo);
/* The deleted ID is Board-owned until the next completed erase or destroy. */
const char *wm_board_scene_last_erased_id(const WmBoardScene *board);
WmBoardAction wm_board_scene_take_action(WmBoardScene *board,
                                         size_t *memo_index);
bool wm_board_scene_insert_text(WmBoardScene *board, const char *utf8);
bool wm_board_scene_backspace(WmBoardScene *board);
bool wm_board_scene_finish_edit(WmBoardScene *board);

/* Pointer coordinates use the 640 x 456 raster. A card becomes pointer-owned
 * on down, draws last while held, and commits its clamped position on up. */
bool wm_board_scene_pointer_down(WmBoardScene *board, WmBoardHit hit,
                                  int x, int y);
bool wm_board_scene_pointer_move(WmBoardScene *board, int x, int y);
bool wm_board_scene_pointer_up(WmBoardScene *board, int x, int y);
/* Commit the last held position when the pointer leaves the Board without
 * another in-bounds position sample. */
bool wm_board_scene_pointer_finish(WmBoardScene *board);
bool wm_board_scene_cancel_pointer(WmBoardScene *board);
bool wm_board_scene_dragging(const WmBoardScene *board);
WmBoardDragCue wm_board_scene_take_drag_cue(WmBoardScene *board,
                                             float *pan);
bool wm_board_scene_drag_mix(const WmBoardScene *board, float *gain,
                              float *pan);

/* Source reader scroll arrows move the text/card by 300 units with a
 * 20-frame Hermite curve. Sound is the WIPL_SE_MESSAGE_SCROLL loop. */
float wm_board_scene_reader_scroll_offset(const WmBoardScene *board);
float wm_board_scene_reader_scroll_limit(const WmBoardScene *board);
bool wm_board_scene_reader_scroll_sound_active(const WmBoardScene *board);
/* The erase dialog emits its opening cue once when its transition starts. */
const char *wm_board_scene_take_reader_cue(WmBoardScene *board);
/* The arrow's requested visibility stays true during the reader's Back/Trash
 * press; the authored Lost clip begins at the close-phase handoff. */
bool wm_board_scene_reader_arrow_target_visible(const WmBoardScene *board,
                                                 WmBoardControl control);

/* During entry, draw the grid over the board body at layout frames 70–90 for
 * the first 20 updates. During exit, use frames 100–120 for all 40 updates.
 * The caller draws body, optional grid overlay, then footer, in that order. */
bool wm_board_scene_grid_overlay(const WmBoardScene *board, float *grid_frame);
/* Returns up to WM_BOARD_MAX_PRESENTED_MEMOS cards in render order. During a
 * date slide, this includes settled cards for the date moving onto the Board. */
size_t wm_board_scene_memo_presentation(
    WmBoardScene *board,
    WmBoardMemoPresentation cards[WM_BOARD_MAX_PRESENTED_MEMOS]);
/* The closed Board leaves today's first Memo page parked behind ChannelSelect.
 * Cards have a neutral focus and completed PasteLetter pose. The caller's
 * date tracks the Home Menu wall clock, including a day change while idle. */
size_t wm_board_scene_parked_memo_presentation(
    WmBoardScene *board, WmBoardDate today,
    WmBoardMemoPresentation cards[WM_BOARD_MAX_PRESENTED_MEMOS]);
/* Draw parked cards in Home Menu world coordinates. An optional camera is the
 * ChannelSelect zoom transform, applied to the Memo translation as well. */
void wm_board_scene_draw_parked_memos(WmBoardScene *board,
                                       WmBoardDate today,
                                       const float camera[12]);
void wm_board_scene_draw_body(WmBoardScene *board);
void wm_board_scene_draw_footer(WmBoardScene *board);
/* Read a footer button's WAD world anchor after draw_footer or hit has posed
 * it. Page-arrow visual anchors remain available during posted-Memo phases. */
bool wm_board_scene_footer_button_anchor(const WmBoardScene *board,
                                          WmBoardControl control,
                                          float *x, float *y);
/* Read the authored focus visual's horizontal scale after draw_footer or hit
 * has posed it. Board Back is available during posted-Memo transitions. */
bool wm_board_scene_footer_button_visual_scale(const WmBoardScene *board,
                                                WmBoardControl control,
                                                float *scale);

/* Hit rectangles come from the posed source button and memo layouts. The
 * caller routes pointer and keyboard actions through the same controls. */
WmBoardHit wm_board_scene_hit(WmBoardScene *board, int x, int y);
void wm_board_scene_hover(WmBoardScene *board, WmBoardHit hit);
bool wm_board_scene_activate(WmBoardScene *board, WmBoardHit hit);
bool wm_board_scene_activate_secondary(WmBoardScene *board, WmBoardHit hit);

#endif
