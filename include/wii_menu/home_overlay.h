#ifndef WII_MENU_HOME_OVERLAY_H
#define WII_MENU_HOME_OVERLAY_H

#include "wii_menu/font_cache.h"
#include "wii_menu/platform.h"
#include "wii_menu/texture_cache.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct WmHomeOverlay WmHomeOverlay;

typedef enum WmHomePhase {
    WM_HOME_CLOSED,
    WM_HOME_ENTER,
    WM_HOME_IDLE,
    WM_HOME_LEAVE,
    WM_HOME_RETURN_FADE
} WmHomePhase;

typedef enum WmHomeControl {
    WM_HOME_CONTROL_NONE,
    WM_HOME_CONTROL_CLOSE,
    WM_HOME_CONTROL_RETURN,
    WM_HOME_CONTROL_OPTIONS,
    WM_HOME_CONTROL_VOLUME_DOWN,
    WM_HOME_CONTROL_VOLUME_UP,
    WM_HOME_CONTROL_RUMBLE_ON,
    WM_HOME_CONTROL_RUMBLE_OFF,
    WM_HOME_CONTROL_RECONNECT,
    WM_HOME_CONTROL_YES,
    WM_HOME_CONTROL_NO
} WmHomeControl;

typedef enum WmHomeOutcome {
    WM_HOME_OUTCOME_NONE,
    WM_HOME_OUTCOME_CLOSED,
    WM_HOME_OUTCOME_RETURN_MENU
} WmHomeOutcome;

typedef struct WmHomeRemote {
    bool connected;
    unsigned battery; /* Zero through four. */
} WmHomeRemote;

typedef struct WmHomeRemoteState {
    float volume; /* Zero through one, in ten source steps. */
    bool muted;
    bool rumble;
    WmHomeRemote controllers[4];
} WmHomeRemoteState;

typedef enum WmHomeReconnectMode {
    WM_HOME_RECONNECT_MANUAL,
    WM_HOME_RECONNECT_AUTOMATIC,
    WM_HOME_RECONNECT_TIMEOUT
} WmHomeReconnectMode;

/* Local WPAD substitute. No physical connection or private identifier is read.
 * Automatic fixtures connect players in the supplied order after delay_frames,
 * then interval_frames between later players. The default matches the HTML
 * fixture: automatic player one after 180 updates. */
typedef struct WmHomeReconnectFixture {
    WmHomeReconnectMode mode;
    unsigned players[4];
    size_t player_count;
    float delay_frames;
    float interval_frames;
    unsigned start_failures;
    unsigned stop_failures;
} WmHomeReconnectFixture;

typedef void (*WmHomeCueCallback)(void *context, const char *symbol);
typedef void (*WmHomeRemoteCallback)(void *context,
                                     const WmHomeRemoteState *state);

/* The overlay owns its parsed layout. Platform, texture cache, and font cache
 * remain caller-owned. The source JSON and .wmra textures come from the local
 * WAD export. Returns NULL if the HOME layout is unavailable or incomplete. */
WmHomeOverlay *wm_home_overlay_create(WmPlatform *platform,
                                       const char *assets_directory,
                                       WmTextureCache *textures,
                                       WmFontCache *fonts,
                                       WmHomeCueCallback cue,
                                       WmHomeRemoteCallback remote_changed,
                                       void *callback_context);
void wm_home_overlay_destroy(WmHomeOverlay *home);

bool wm_home_overlay_set_remote_state(WmHomeOverlay *home,
                                      const WmHomeRemoteState *state);
WmHomeRemoteState wm_home_overlay_remote_state(const WmHomeOverlay *home);
bool wm_home_overlay_set_reconnect_fixture(
    WmHomeOverlay *home, const WmHomeReconnectFixture *fixture);

bool wm_home_overlay_open(WmHomeOverlay *home);
void wm_home_overlay_reset(WmHomeOverlay *home);
/* Advances in native 60 Hz updates. A completed close/return yields unused
 * fractional updates for the next scene; consume the outcome separately. */
float wm_home_overlay_advance(WmHomeOverlay *home, float frames);
WmHomeOutcome wm_home_overlay_take_outcome(WmHomeOverlay *home);

bool wm_home_overlay_active(const WmHomeOverlay *home);
bool wm_home_overlay_ready(const WmHomeOverlay *home);
WmHomePhase wm_home_overlay_phase(const WmHomeOverlay *home);
float wm_home_overlay_frame(const WmHomeOverlay *home);
float wm_home_overlay_fade_alpha(const WmHomeOverlay *home);

/* Coordinates use the common 640 x 456 logical framebuffer. Hover is ignored
 * until the source controller is ready; hit returns NONE for hidden controls. */
WmHomeControl wm_home_overlay_hit(WmHomeOverlay *home, int x, int y);
void wm_home_overlay_hover(WmHomeOverlay *home, WmHomeControl control);
bool wm_home_overlay_activate(WmHomeOverlay *home, WmHomeControl control);
bool wm_home_overlay_back(WmHomeOverlay *home);
bool wm_home_overlay_reconnect_key(WmHomeOverlay *home, char key, bool down);
bool wm_home_overlay_connect(WmHomeOverlay *home, unsigned player);

/* Draw after the retained grid/preview underlay and before the pointer. The
 * confirmed-return fade is a separate final pass drawn after the pointer. */
bool wm_home_overlay_draw(WmHomeOverlay *home);
void wm_home_overlay_draw_fade(WmHomeOverlay *home);

#endif
