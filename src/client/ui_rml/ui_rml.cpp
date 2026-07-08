/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "ui_rml.h"

#include "common/common.h"
#include "common/cvar.h"
#include "common/files.h"
#include "common/prompt.h"
#include "client/keys.h"
#include "renderer/renderer.h"

#if UI_RML_HAS_RUNTIME
#define UI_RML_ENABLE_DEFAULT "1"
#else
#define UI_RML_ENABLE_DEFAULT "0"
#endif

static cvar_t *ui_rml_enable;
static cvar_t *ui_rml_debug;
static cvar_t *ui_rml_asset_root;
static bool ui_rml_initialized;
static bool ui_rml_commands_registered;

#if UI_RML_HAS_RUNTIME
static ui_rml_runtime_interface_t ui_rml_runtime;
static bool ui_rml_runtime_registered;
static bool ui_rml_runtime_started;
static bool ui_rml_runtime_failed;
static bool ui_rml_route_active;
static char ui_rml_active_route[MAX_QPATH];
#define UI_RML_ROUTE_HISTORY_MAX 16
static char ui_rml_route_history[UI_RML_ROUTE_HISTORY_MAX][MAX_QPATH];
static size_t ui_rml_route_history_count;
typedef struct {
    unsigned opens;
    unsigned closes;
    unsigned close_requests;
    unsigned synthetic_inputs;
    unsigned updates;
    unsigned renders;
    unsigned key_events;
    unsigned char_events;
    unsigned mouse_moves;
    unsigned mouse_buttons;
    unsigned mouse_wheels;
    unsigned last_realtime;
    int width;
    int height;
    int last_mouse_x;
    int last_mouse_y;
} ui_rml_route_metrics_t;

static ui_rml_route_metrics_t ui_rml_route_metrics;
static ui_rml_renderer_interface_t ui_rml_renderer;
static bool ui_rml_renderer_registered;
static qhandle_t ui_rml_cursor_handle;
static int ui_rml_cursor_width;
static int ui_rml_cursor_height;
static int ui_rml_mouse_x;
static int ui_rml_mouse_y;
static bool ui_rml_mouse_valid;
static constexpr float UI_RML_REFERENCE_WIDTH = 960.0f;
static constexpr float UI_RML_REFERENCE_HEIGHT = 720.0f;
#endif

typedef struct {
    const char *id;
    const char *document;
} ui_rml_route_t;

typedef struct {
    const char *route_id;
    uiMenu_t menu;
    const char *menu_name;
} ui_rml_menu_route_t;

static const ui_rml_route_t ui_rml_routes[] = {
    { "main", "shell/main.rml" },
    { "game", "shell/game.rml" },
    { "options", "shell/options.rml" },
    { "video", "settings/video.rml" },
    { "multimonitor", "settings/multimonitor.rml" },
    { "performance", "settings/performance.rml" },
    { "accessibility", "settings/accessibility.rml" },
    { "sound", "settings/sound.rml" },
    { "railtrail", "settings/railtrail.rml" },
    { "effects", "settings/effects.rml" },
    { "crosshair", "settings/crosshair.rml" },
    { "screen", "settings/screen.rml" },
    { "language", "settings/language.rml" },
    { "downloads", "shell/downloads.rml" },
    { "download_status", "shell/download_status.rml" },
    { "addressbook", "utility/addressbook.rml" },
    { "input", "settings/input.rml" },
    { "keys", "utility/keys.rml" },
    { "legacykeys", "utility/legacykeys.rml" },
    { "weapons", "utility/weapons.rml" },
    { "quit_confirm", "shell/quit_confirm.rml" },
    { "gameflags", "singleplayer/gameflags.rml" },
    { "startserver", "singleplayer/startserver.rml" },
    { "multiplayer", "multiplayer/multiplayer.rml" },
    { "singleplayer", "singleplayer/singleplayer.rml" },
    { "skill_select", "singleplayer/skill_select.rml" },
    { "loadgame", "singleplayer/loadgame.rml" },
    { "savegame", "singleplayer/savegame.rml" },
    { "servers", "utility/servers.rml" },
    { "demos", "utility/demos.rml" },
    { "players", "utility/players.rml" },
    { "ui_list", "utility/ui_list.rml" },
    { "dm_welcome", "session/dm_welcome.rml" },
    { "dm_join", "session/dm_join.rml" },
    { "join", "session/join.rml" },
    { "dm_hostinfo", "session/dm_hostinfo.rml" },
    { "dm_matchinfo", "session/dm_matchinfo.rml" },
    { "callvote_main", "session/callvote_main.rml" },
    { "callvote_ruleset", "session/callvote_ruleset.rml" },
    { "callvote_timelimit", "session/callvote_timelimit.rml" },
    { "callvote_scorelimit", "session/callvote_scorelimit.rml" },
    { "callvote_unlagged", "session/callvote_unlagged.rml" },
    { "callvote_random", "session/callvote_random.rml" },
    { "callvote_map_flags", "session/callvote_map_flags.rml" },
    { "mymap_main", "session/mymap_main.rml" },
    { "mymap_flags", "session/mymap_flags.rml" },
    { "forfeit_confirm", "session/forfeit_confirm.rml" },
    { "leave_match_confirm", "session/leave_match_confirm.rml" },
    { "admin_menu", "session/admin_menu.rml" },
    { "admin_commands", "session/admin_commands.rml" },
    { "tourney_info", "session/tourney_info.rml" },
    { "tourney_mapchoices", "session/tourney_mapchoices.rml" },
    { "tourney_veto", "session/tourney_veto.rml" },
    { "tourney_replay_confirm", "session/tourney_replay_confirm.rml" },
    { "vote_menu", "session/vote_menu.rml" },
    { "map_selector", "session/map_selector.rml" },
    { "match_stats", "session/match_stats.rml" },
    { "core.runtime_smoke", "core/runtime_smoke.rml" },
};

static const ui_rml_menu_route_t ui_rml_runtime_menu_routes[] = {
    { "main", UIMENU_MAIN, "UIMENU_MAIN" },
    { "game", UIMENU_GAME, "UIMENU_GAME" },
    { "download_status", UIMENU_DOWNLOAD, "UIMENU_DOWNLOAD" },
};

static int UI_Rml_DefaultLoadFile(const char *path, void **data)
{
    return FS_LoadFileEx(path, data, 0, TAG_FILESYSTEM);
}

static void UI_Rml_DefaultFreeFile(void *data)
{
    FS_FreeFile(data);
}

// Narrow engine-system boundary for future runtime and test hooks.
static const ui_rml_file_interface_t ui_rml_file_interface = {
    UI_Rml_DefaultLoadFile,
    UI_Rml_DefaultFreeFile
};

static const ui_rml_route_t *UI_Rml_FindRoute(const char *route_id)
{
    if (!route_id || !route_id[0]) {
        return NULL;
    }

    for (size_t i = 0; i < q_countof(ui_rml_routes); i++) {
        if (!strcmp(route_id, ui_rml_routes[i].id)) {
            return &ui_rml_routes[i];
        }
    }

    return NULL;
}

static const ui_rml_menu_route_t *UI_Rml_FindRuntimeMenuRoute(const char *route_id)
{
    if (!route_id || !route_id[0]) {
        return NULL;
    }

    for (size_t i = 0; i < q_countof(ui_rml_runtime_menu_routes); i++) {
        if (!strcmp(route_id, ui_rml_runtime_menu_routes[i].route_id)) {
            return &ui_rml_runtime_menu_routes[i];
        }
    }

    return NULL;
}

static bool UI_Rml_PopupRouteIsKnown(const char *route_id)
{
    return route_id &&
           (!strcmp(route_id, "quit_confirm") ||
            !strcmp(route_id, "forfeit_confirm") ||
            !strcmp(route_id, "leave_match_confirm") ||
            !strcmp(route_id, "tourney_replay_confirm"));
}

bool UI_Rml_RouteIsPopup(const char *route_id)
{
    return UI_Rml_PopupRouteIsKnown(route_id);
}

static bool UI_Rml_RuntimeRouteIsAllowed(const char *route_id)
{
    return UI_Rml_FindRoute(route_id) != NULL;
}

static const char *UI_Rml_AssetRoot(void)
{
    if (ui_rml_asset_root && ui_rml_asset_root->string[0]) {
        return ui_rml_asset_root->string;
    }

    return "ui/rml";
}

static bool UI_Rml_BuildDocumentPath(const ui_rml_route_t *route,
                                     char *path, size_t size)
{
    const char *root = UI_Rml_AssetRoot();
    size_t root_len = strlen(root);
    size_t len;

    if (!root_len) {
        len = Q_strlcpy(path, route->document, size);
    } else if (root[root_len - 1] == '/' || root[root_len - 1] == '\\') {
        len = Q_concat(path, size, root, route->document);
    } else {
        len = Q_concat(path, size, root, "/", route->document);
    }

    return len < size;
}

#if UI_RML_HAS_RUNTIME
static float UI_Rml_CanvasPixelScale(void)
{
    const int framebuffer_width =
        r_config.width > 0 ? r_config.width : (int)UI_RML_REFERENCE_WIDTH;
    const int framebuffer_height =
        r_config.height > 0 ? r_config.height : (int)UI_RML_REFERENCE_HEIGHT;
    const float scale_x = (float)framebuffer_width / UI_RML_REFERENCE_WIDTH;
    const float scale_y = (float)framebuffer_height / UI_RML_REFERENCE_HEIGHT;
    float scale = min(scale_x, scale_y);

    if (scale < 1.0f) {
        scale = 1.0f;
    }

    return scale;
}

static int UI_Rml_RendererBaseScaleInt(void)
{
    const int framebuffer_width =
        r_config.width > 0 ? r_config.width : VIRTUAL_SCREEN_WIDTH;
    const int framebuffer_height =
        r_config.height > 0 ? r_config.height : VIRTUAL_SCREEN_HEIGHT;
    float scale_x = (float)framebuffer_width / VIRTUAL_SCREEN_WIDTH;
    float scale_y = (float)framebuffer_height / VIRTUAL_SCREEN_HEIGHT;
    float base_scale = max(scale_x, scale_y);
    int base_scale_int;

    if (base_scale < 1.0f) {
        base_scale = 1.0f;
    }

    base_scale_int = (int)base_scale;
    if (base_scale_int < 1) {
        base_scale_int = 1;
    }

    return base_scale_int;
}

static float UI_Rml_RendererDrawScale(void)
{
    const float canvas_scale = UI_Rml_CanvasPixelScale();

    if (canvas_scale <= 0.0f) {
        return (float)UI_Rml_RendererBaseScaleInt();
    }

    return (float)UI_Rml_RendererBaseScaleInt() / canvas_scale;
}

static void UI_Rml_GetCanvas(int *width, int *height, float *pixel_scale)
{
    int framebuffer_width =
        r_config.width > 0 ? r_config.width : (int)UI_RML_REFERENCE_WIDTH;
    int framebuffer_height =
        r_config.height > 0 ? r_config.height : (int)UI_RML_REFERENCE_HEIGHT;
    float scale = UI_Rml_CanvasPixelScale();

    if (scale <= 0.0f) {
        scale = 1.0f;
    }

    if (width) {
        *width = max(1, Q_rint((float)framebuffer_width / scale));
    }
    if (height) {
        *height = max(1, Q_rint((float)framebuffer_height / scale));
    }
    if (pixel_scale) {
        *pixel_scale = scale;
    }
}

static void UI_Rml_ClampMouseToCanvas(void)
{
    int width;
    int height;

    UI_Rml_GetCanvas(&width, &height, NULL);
    ui_rml_mouse_x = Q_clip(ui_rml_mouse_x, 0, max(0, width - 1));
    ui_rml_mouse_y = Q_clip(ui_rml_mouse_y, 0, max(0, height - 1));

    if (ui_rml_route_active) {
        ui_rml_route_metrics.last_mouse_x = ui_rml_mouse_x;
        ui_rml_route_metrics.last_mouse_y = ui_rml_mouse_y;
    }
}

static void UI_Rml_EnsureMousePosition(void)
{
    if (!ui_rml_mouse_valid) {
        int width;
        int height;

        UI_Rml_GetCanvas(&width, &height, NULL);
        ui_rml_mouse_x = width / 2;
        ui_rml_mouse_y = height / 2;
        ui_rml_mouse_valid = true;
    }

    UI_Rml_ClampMouseToCanvas();
}

static void UI_Rml_MouseFromFramebuffer(int x, int y, int *out_x, int *out_y)
{
    int width;
    int height;
    float pixel_scale;

    UI_Rml_GetCanvas(&width, &height, &pixel_scale);
    if (pixel_scale <= 0.0f) {
        pixel_scale = 1.0f;
    }

    if (out_x) {
        *out_x = Q_clip(Q_rint((float)x / pixel_scale), 0, max(0, width - 1));
    }
    if (out_y) {
        *out_y = Q_clip(Q_rint((float)y / pixel_scale), 0, max(0, height - 1));
    }
}

static void UI_Rml_RegisterCursor(void)
{
    if (ui_rml_cursor_handle) {
        return;
    }

    ui_rml_cursor_handle = R_RegisterPic("/gfx/cursor.png");
    ui_rml_cursor_width = 12;
    ui_rml_cursor_height = 12;
}

static void UI_Rml_DrawCursor(void)
{
    if (!ui_rml_cursor_handle) {
        UI_Rml_RegisterCursor();
    }
    if (!ui_rml_cursor_handle) {
        return;
    }

    UI_Rml_EnsureMousePosition();
    R_SetClipRect(NULL);
    R_SetScale(UI_Rml_RendererDrawScale());
    R_DrawStretchPic(ui_rml_mouse_x, ui_rml_mouse_y,
                     ui_rml_cursor_width, ui_rml_cursor_height,
                     COLOR_WHITE, ui_rml_cursor_handle);
}
#endif

static bool UI_Rml_RuntimeInterfaceAvailable(void)
{
#if UI_RML_HAS_RUNTIME
    return ui_rml_runtime_registered &&
           ui_rml_runtime.OpenRoute != NULL &&
           !ui_rml_runtime_failed;
#else
    return false;
#endif
}

static bool UI_Rml_RuntimeCanOpenRoutes(void)
{
#if UI_RML_HAS_RUNTIME
    if (!UI_Rml_RuntimeInterfaceAvailable()) {
        return false;
    }

    if (!UI_Rml_RendererIsAvailable()) {
        return false;
    }

    if (ui_rml_runtime.CanOpenRoutes) {
        return ui_rml_runtime.CanOpenRoutes();
    }

    return true;
#else
    return false;
#endif
}

static void UI_Rml_StopRuntime(void);
static void UI_Rml_ClearActiveRoute(bool notify_runtime);
static void UI_Rml_PrintRuntimeStatus(void);
static bool UI_Rml_OpenRouteInternalEx(const char *route_id, bool record_history);
static bool UI_Rml_OpenPopupRouteInternal(const char *route_id);

#if UI_RML_HAS_RUNTIME
static void UI_Rml_ClearRouteHistory(void)
{
    ui_rml_route_history_count = 0;
}

static void UI_Rml_PushRouteHistory(const char *route_id)
{
    if (!route_id || !route_id[0]) {
        return;
    }

    if (ui_rml_route_history_count > 0 &&
        !strcmp(ui_rml_route_history[ui_rml_route_history_count - 1], route_id)) {
        return;
    }

    if (ui_rml_route_history_count == q_countof(ui_rml_route_history)) {
        memmove(ui_rml_route_history,
                ui_rml_route_history + 1,
                sizeof(ui_rml_route_history[0]) * (q_countof(ui_rml_route_history) - 1));
        ui_rml_route_history_count--;
    }

    Q_strlcpy(ui_rml_route_history[ui_rml_route_history_count++],
              route_id,
              sizeof(ui_rml_route_history[0]));
}

static bool UI_Rml_PopRouteHistory(char *route_id, size_t size)
{
    if (!route_id || !size || ui_rml_route_history_count == 0) {
        return false;
    }

    ui_rml_route_history_count--;
    Q_strlcpy(route_id, ui_rml_route_history[ui_rml_route_history_count], size);
    ui_rml_route_history[ui_rml_route_history_count][0] = 0;
    return route_id[0] != 0;
}
#endif

static bool UI_Rml_StartRuntime(void)
{
#if UI_RML_HAS_RUNTIME
    if (!UI_Rml_RuntimeInterfaceAvailable()) {
        return false;
    }

    if (ui_rml_runtime_started) {
        return true;
    }

    if (ui_rml_runtime.Init && !ui_rml_runtime.Init()) {
        ui_rml_runtime_failed = true;
        return false;
    }

    ui_rml_runtime_started = true;
    return true;
#else
    return false;
#endif
}

static void UI_Rml_StopRuntime(void)
{
#if UI_RML_HAS_RUNTIME
    UI_Rml_ClearActiveRoute(true);
    UI_Rml_ClearRouteHistory();

    if (ui_rml_runtime_started && ui_rml_runtime.Shutdown) {
        ui_rml_runtime.Shutdown();
    }

    ui_rml_runtime_started = false;
#endif
}

static void UI_Rml_ClaimMenuKeyDest(void)
{
    keydest_t dest = Key_GetDest();

    Key_SetDest((keydest_t)((dest & ~KEY_CONSOLE) | KEY_MENU));
}

static void UI_Rml_ReleaseMenuKeyDest(void)
{
    Key_SetDest((keydest_t)(Key_GetDest() & ~KEY_MENU));
}

static void UI_Rml_MarkRouteActive(const char *route_id)
{
#if UI_RML_HAS_RUNTIME
    ui_rml_route_active = true;
    ui_rml_route_metrics = {};
    ui_rml_route_metrics.opens++;
    Q_strlcpy(ui_rml_active_route, route_id ? route_id : "", sizeof(ui_rml_active_route));
    UI_Rml_ClaimMenuKeyDest();
#else
    (void)route_id;
#endif
}

static void UI_Rml_ClearActiveRoute(bool notify_runtime)
{
#if UI_RML_HAS_RUNTIME
    if (!ui_rml_route_active) {
        return;
    }

    if (notify_runtime && ui_rml_runtime_started && ui_rml_runtime.CloseRoute) {
        ui_rml_runtime.CloseRoute();
    }

    ui_rml_route_metrics.closes++;
    ui_rml_route_active = false;
    ui_rml_active_route[0] = 0;
    UI_Rml_ReleaseMenuKeyDest();
#else
    (void)notify_runtime;
#endif
}

static bool UI_Rml_KeyIsMouseButton(int key)
{
    return key >= K_MOUSEFIRST && key <= K_MOUSE8;
}

static bool UI_Rml_KeyIsMouseWheel(int key)
{
    return key == K_MWHEELUP || key == K_MWHEELDOWN ||
           key == K_MWHEELLEFT || key == K_MWHEELRIGHT;
}

static bool UI_Rml_RuntimeProbeRoute(const char *route_id)
{
#if UI_RML_HAS_RUNTIME
    const ui_rml_route_t *route = UI_Rml_FindRoute(route_id);
    char path[MAX_QPATH];
    bool was_started = ui_rml_runtime_started;
    bool ok;

    if (!route) {
        Com_Printf("RmlUi route '%s' is not registered.\n",
                   route_id ? route_id : "<null>");
        return false;
    }

    if (!ui_rml_runtime_registered || !ui_rml_runtime.ProbeRoute) {
        Com_Printf("RmlUi runtime probe is unavailable; runtime='%s', availability='%s'.\n",
                   UI_Rml_RuntimeName(),
                   UI_Rml_AvailabilityString(UI_Rml_Availability()));
        return false;
    }

    if (!UI_Rml_BuildDocumentPath(route, path, sizeof(path))) {
        Com_Printf("RmlUi route '%s' document path is too long: root='%s', document='%s'.\n",
                   route->id, UI_Rml_AssetRoot(), route->document);
        return false;
    }

    if (!UI_Rml_StartRuntime()) {
        Com_Printf("RmlUi runtime '%s' failed to start for route probe.\n",
                   UI_Rml_RuntimeName());
        return false;
    }

    ok = ui_rml_runtime.ProbeRoute(route->id, path);

    if (!was_started) {
        UI_Rml_StopRuntime();
    }

    return ok;
#else
    Com_Printf("RmlUi runtime probe is unavailable; runtime support is not compiled.\n");
    return false;
#endif
}

static bool UI_Rml_OpenRouteInternal(const char *route_id)
{
    return UI_Rml_OpenRouteInternalEx(route_id, true);
}

static bool UI_Rml_OpenRouteInternalEx(const char *route_id, bool record_history)
{
#if UI_RML_HAS_RUNTIME
    const ui_rml_route_t *route = UI_Rml_FindRoute(route_id);
    const char *document;
    bool document_found;
    bool had_previous_route = false;
    char previous_route[MAX_QPATH];

    if (!route) {
        Com_Printf("RmlUi route '%s' is not registered.\n",
                   route_id ? route_id : "<null>");
        return false;
    }

    if (!UI_Rml_IsEnabled()) {
        Com_Printf("RmlUi route '%s' requested, but ui_rml_enable is disabled.\n",
                   route->id);
        return false;
    }

    if (!UI_Rml_RuntimeRouteIsAllowed(route->id)) {
        Com_Printf("RmlUi route '%s' is not registered for runtime drawing; keeping legacy UI fallback active.\n",
                   route->id);
        return false;
    }

    Com_Printf("RmlUi route '%s' requested; probing document.\n", route->id);
    document_found = UI_Rml_ProbeRoute(route->id);

    if (!UI_Rml_RuntimeIsAvailable()) {
        Com_Printf("RmlUi availability is '%s' with renderer='%s' family='%s'; falling back to legacy UI.\n",
                   UI_Rml_AvailabilityString(UI_Rml_Availability()),
                   UI_Rml_RendererName(),
                   UI_Rml_RendererFamilyString(UI_Rml_RendererFamily()));
        return false;
    }

    if (!document_found) {
        return false;
    }

    document = UI_Rml_DocumentForRoute(route->id);
    if (!document) {
        Com_Printf("RmlUi route '%s' document path is unavailable; falling back to legacy UI.\n",
                   route->id);
        return false;
    }

    if (!UI_Rml_StartRuntime()) {
        Com_Printf("RmlUi runtime '%s' failed to start; falling back to legacy UI.\n",
                   UI_Rml_RuntimeName());
        return false;
    }

    if (ui_rml_route_active && ui_rml_active_route[0] &&
        strcmp(ui_rml_active_route, route->id)) {
        had_previous_route = true;
        Q_strlcpy(previous_route, ui_rml_active_route, sizeof(previous_route));
    }

    if (ui_rml_runtime.OpenRoute(route->id, document)) {
        if (record_history && had_previous_route) {
            UI_Rml_PushRouteHistory(previous_route);
        }
        UI_Rml_MarkRouteActive(route->id);
        UI_Rml_GetCanvas(&ui_rml_route_metrics.width,
                         &ui_rml_route_metrics.height,
                         NULL);
        UI_Rml_EnsureMousePosition();
        return true;
    }

    Com_Printf("RmlUi runtime '%s' declined route '%s'; falling back to legacy UI.\n",
               UI_Rml_RuntimeName(), route->id);
    return false;
#else
    (void)route_id;
    return false;
#endif
}

static void UI_Rml_ProbeRoute_c(genctx_t *ctx, int argnum)
{
    if (argnum != 1) {
        return;
    }

    for (size_t i = 0; i < q_countof(ui_rml_routes); i++) {
        Prompt_AddMatch(ctx, ui_rml_routes[i].id);
    }
}

static void UI_Rml_ProbeRoute_f(void)
{
    if (Cmd_Argc() > 2) {
        Com_Printf("Usage: %s [route_id]\n", Cmd_Argv(0));
        return;
    }

    if (Cmd_Argc() == 2) {
        UI_Rml_ProbeRoute(Cmd_Argv(1));
        return;
    }

    for (size_t i = 0; i < q_countof(ui_rml_routes); i++) {
        UI_Rml_ProbeRoute(ui_rml_routes[i].id);
    }
}

static void UI_Rml_RuntimeProbeRoute_c(genctx_t *ctx, int argnum)
{
    UI_Rml_ProbeRoute_c(ctx, argnum);
}

static void UI_Rml_RuntimeProbeRoute_f(void)
{
    if (Cmd_Argc() > 2) {
        Com_Printf("Usage: %s [route_id]\n", Cmd_Argv(0));
        return;
    }

    if (Cmd_Argc() == 2) {
        UI_Rml_RuntimeProbeRoute(Cmd_Argv(1));
        return;
    }

    for (size_t i = 0; i < q_countof(ui_rml_routes); i++) {
        UI_Rml_RuntimeProbeRoute(ui_rml_routes[i].id);
    }
}

static void UI_Rml_RuntimeOpenRoute_c(genctx_t *ctx, int argnum)
{
    if (argnum != 1) {
        return;
    }

    for (size_t i = 0; i < q_countof(ui_rml_routes); i++) {
        if (UI_Rml_RuntimeRouteIsAllowed(ui_rml_routes[i].id)) {
            Prompt_AddMatch(ctx, ui_rml_routes[i].id);
        }
    }
}

static void UI_Rml_RuntimeMenuRoute_c(genctx_t *ctx, int argnum)
{
    if (argnum != 1) {
        return;
    }

    for (size_t i = 0; i < q_countof(ui_rml_runtime_menu_routes); i++) {
        Prompt_AddMatch(ctx, ui_rml_runtime_menu_routes[i].route_id);
    }
}

static void UI_Rml_RuntimeOpenRoute_f(void)
{
    const char *route_id = "core.runtime_smoke";

    if (Cmd_Argc() > 2) {
        Com_Printf("Usage: %s [route_id]\n", Cmd_Argv(0));
        return;
    }

    if (Cmd_Argc() == 2) {
        route_id = Cmd_Argv(1);
    }

    UI_Rml_OpenRouteInternal(route_id);
}

static bool UI_Rml_OpenPopupRouteInternal(const char *route_id)
{
#if UI_RML_HAS_RUNTIME
    Com_Printf("RmlUi popup route '%s' requested.\n",
               route_id && route_id[0] ? route_id : "<null>");
    return UI_Rml_OpenRouteInternalEx(route_id, true);
#else
    (void)route_id;
    return false;
#endif
}

bool UI_Rml_OpenPopupRoute(const char *route_id)
{
    return UI_Rml_OpenPopupRouteInternal(route_id);
}

static void UI_Rml_RuntimePopupRoute_f(void)
{
    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: %s <route_id>\n", Cmd_Argv(0));
        return;
    }

    UI_Rml_OpenPopupRouteInternal(Cmd_Argv(1));
}

static void UI_Rml_RuntimeCaptureMenu_f(void)
{
#if UI_RML_HAS_RUNTIME
    const ui_rml_menu_route_t *menu_route;

    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: %s <main|game|download_status>\n", Cmd_Argv(0));
        return;
    }

    menu_route = UI_Rml_FindRuntimeMenuRoute(Cmd_Argv(1));
    if (!menu_route) {
        Com_Printf("RmlUi runtime menu capture route '%s' is not in the guarded menu entrypoint set.\n",
                   Cmd_Argv(1));
        return;
    }

    UI_OpenMenu(menu_route->menu);
    if (!UI_Rml_IsRouteActive()) {
        Com_Printf("RmlUi guarded menu capture route '%s' could not be opened through %s.\n",
                   menu_route->route_id,
                   menu_route->menu_name);
        return;
    }

    Com_Printf("RmlUi guarded menu capture route '%s' is active through %s; capture after the next rendered frame and attach the current ui_rml_runtime_status output to the evidence note.\n",
               menu_route->route_id,
               menu_route->menu_name);
    UI_Rml_PrintRuntimeStatus();
#else
    Com_Printf("RmlUi runtime menu capture is unavailable; runtime support is not compiled.\n");
#endif
}

static void UI_Rml_RuntimeCloseRoute_f(void)
{
    UI_Rml_CloseActiveRoute();
}

static void UI_Rml_RuntimeBackRoute_f(void)
{
#if UI_RML_HAS_RUNTIME
    char route_id[MAX_QPATH];

    if (UI_Rml_PopRouteHistory(route_id, sizeof(route_id))) {
        if (UI_Rml_OpenRouteInternalEx(route_id, false)) {
            return;
        }

        Com_Printf("RmlUi back route '%s' failed to reopen; closing active route.\n",
                   route_id);
    }
#endif

    UI_Rml_CloseActiveRoute();
}

static void UI_Rml_PrintRuntimeStatus(void)
{
#if UI_RML_HAS_RUNTIME
    Com_Printf("RmlUi runtime status: active=%s route='%s' availability='%s' runtime='%s' renderer='%s' family='%s'.\n",
               ui_rml_route_active ? "yes" : "no",
               ui_rml_route_active && ui_rml_active_route[0] ? ui_rml_active_route : "<none>",
               UI_Rml_AvailabilityString(UI_Rml_Availability()),
               UI_Rml_RuntimeName(),
               UI_Rml_RendererName(),
               UI_Rml_RendererFamilyString(UI_Rml_RendererFamily()));
    Com_Printf("RmlUi runtime frames: updates=%u renders=%u last_realtime=%u dimensions=%dx%d.\n",
               ui_rml_route_metrics.updates,
               ui_rml_route_metrics.renders,
               ui_rml_route_metrics.last_realtime,
               ui_rml_route_metrics.width,
               ui_rml_route_metrics.height);
    Com_Printf("RmlUi runtime route counters: opens=%u closes=%u close_requests=%u synthetic_inputs=%u.\n",
               ui_rml_route_metrics.opens,
               ui_rml_route_metrics.closes,
               ui_rml_route_metrics.close_requests,
               ui_rml_route_metrics.synthetic_inputs);
    Com_Printf("RmlUi runtime input: keys=%u chars=%u mouse_moves=%u mouse_buttons=%u mouse_wheels=%u last_mouse=%d,%d.\n",
               ui_rml_route_metrics.key_events,
               ui_rml_route_metrics.char_events,
               ui_rml_route_metrics.mouse_moves,
               ui_rml_route_metrics.mouse_buttons,
               ui_rml_route_metrics.mouse_wheels,
               ui_rml_route_metrics.last_mouse_x,
               ui_rml_route_metrics.last_mouse_y);
#else
    Com_Printf("RmlUi runtime status: runtime support is not compiled.\n");
#endif
}

static void UI_Rml_RuntimeStatus_f(void)
{
    UI_Rml_PrintRuntimeStatus();
}

static void UI_Rml_RuntimeSyntheticInput_f(void)
{
#if UI_RML_HAS_RUNTIME
    if (!ui_rml_route_active) {
        Com_Printf("RmlUi synthetic input smoke skipped: no active route.\n");
        return;
    }

    if (!UI_Rml_RuntimeRouteIsAllowed(ui_rml_active_route)) {
        Com_Printf("RmlUi synthetic input smoke skipped: route '%s' is not in the guarded runtime route set.\n",
                   ui_rml_active_route[0] ? ui_rml_active_route : "<none>");
        return;
    }

    ui_rml_route_metrics.synthetic_inputs++;
    UI_Rml_MouseEvent(128, 192);
    UI_Rml_CharEvent('w');
    UI_Rml_KeyEvent(K_MWHEELUP, true);
    UI_Rml_KeyEvent(K_MOUSE2, true);

    Com_Printf("RmlUi synthetic input smoke: keys=%u chars=%u mouse_moves=%u mouse_buttons=%u mouse_wheels=%u close_requests=%u closes=%u active=%s.\n",
               ui_rml_route_metrics.key_events,
               ui_rml_route_metrics.char_events,
               ui_rml_route_metrics.mouse_moves,
               ui_rml_route_metrics.mouse_buttons,
               ui_rml_route_metrics.mouse_wheels,
               ui_rml_route_metrics.close_requests,
               ui_rml_route_metrics.closes,
               ui_rml_route_active ? "yes" : "no");
#else
    Com_Printf("RmlUi synthetic input smoke is unavailable; runtime support is not compiled.\n");
#endif
}

static void UI_Rml_RuntimeCapture_f(void)
{
#if UI_RML_HAS_RUNTIME
    if (!UI_Rml_IsRouteActive() && !UI_Rml_OpenRouteInternal("core.runtime_smoke")) {
        Com_Printf("RmlUi guarded capture route could not be opened.\n");
        return;
    }

    Com_Printf("RmlUi guarded capture route is active; capture after the next rendered frame and attach the current ui_rml_runtime_status output to the evidence note.\n");
    UI_Rml_PrintRuntimeStatus();
#else
    Com_Printf("RmlUi runtime capture is unavailable; runtime support is not compiled.\n");
#endif
}

static const cmdreg_t ui_rml_commands[] = {
    { "ui_rml_probe", UI_Rml_ProbeRoute_f, UI_Rml_ProbeRoute_c },
    { "ui_rml_runtime_probe", UI_Rml_RuntimeProbeRoute_f, UI_Rml_RuntimeProbeRoute_c },
    { "ui_rml_runtime_open", UI_Rml_RuntimeOpenRoute_f, UI_Rml_RuntimeOpenRoute_c },
    { "ui_rml_runtime_popup", UI_Rml_RuntimePopupRoute_f, UI_Rml_RuntimeOpenRoute_c },
    { "ui_rml_runtime_back", UI_Rml_RuntimeBackRoute_f },
    { "ui_rml_runtime_close", UI_Rml_RuntimeCloseRoute_f },
    { "ui_rml_runtime_status", UI_Rml_RuntimeStatus_f },
    { "ui_rml_runtime_capture", UI_Rml_RuntimeCapture_f },
    { "ui_rml_runtime_capture_menu", UI_Rml_RuntimeCaptureMenu_f, UI_Rml_RuntimeMenuRoute_c },
    { "ui_rml_runtime_synthetic_input", UI_Rml_RuntimeSyntheticInput_f },
    { NULL }
};

void UI_Rml_Init(void)
{
    if (ui_rml_initialized) {
        return;
    }

    ui_rml_enable = Cvar_Get("ui_rml_enable", UI_RML_ENABLE_DEFAULT, 0);
    ui_rml_debug = Cvar_Get("ui_rml_debug", "0", 0);
    ui_rml_asset_root = Cvar_Get("ui_rml_asset_root", "ui/rml", 0);

#if UI_RML_HAS_RUNTIME
    UI_Rml_RegisterCompiledRuntime();
    UI_Rml_RegisterCursor();
#endif

    if (!Cmd_Exists("ui_rml_probe")) {
        Cmd_Register(ui_rml_commands);
        ui_rml_commands_registered = true;
    }

    ui_rml_initialized = true;

    if (ui_rml_debug->integer) {
        Com_Printf("RmlUi menu scaffold initialized; availability='%s', runtime='%s', renderer='%s', renderer_family='%s', asset_root='%s'.\n",
                   UI_Rml_AvailabilityString(UI_Rml_Availability()),
                   UI_Rml_RuntimeName(),
                   UI_Rml_RendererName(),
                   UI_Rml_RendererFamilyString(UI_Rml_RendererFamily()),
                   UI_Rml_AssetRoot());
    }
}

void UI_Rml_Shutdown(void)
{
    if (!ui_rml_initialized) {
        return;
    }

    if (ui_rml_debug && ui_rml_debug->integer) {
        Com_Printf("RmlUi menu scaffold shut down.\n");
    }

    if (ui_rml_commands_registered) {
        Cmd_Deregister(ui_rml_commands);
        ui_rml_commands_registered = false;
    }

    UI_Rml_StopRuntime();

#if UI_RML_HAS_RUNTIME
    ui_rml_cursor_handle = 0;
    ui_rml_cursor_width = 0;
    ui_rml_cursor_height = 0;
    ui_rml_mouse_valid = false;
#endif

    ui_rml_initialized = false;
    ui_rml_enable = NULL;
    ui_rml_debug = NULL;
    ui_rml_asset_root = NULL;
}

bool UI_Rml_IsEnabled(void)
{
    return ui_rml_initialized && ui_rml_enable && ui_rml_enable->integer != 0;
}

bool UI_Rml_RuntimeIsAvailable(void)
{
    return UI_Rml_Availability() == UI_RML_AVAILABILITY_READY;
}

ui_rml_availability_t UI_Rml_Availability(void)
{
    if (!ui_rml_initialized) {
        return UI_RML_AVAILABILITY_UNINITIALIZED;
    }

    if (!ui_rml_enable || ui_rml_enable->integer == 0) {
        return UI_RML_AVAILABILITY_DISABLED;
    }

#if UI_RML_HAS_RUNTIME
    if (!UI_Rml_RuntimeInterfaceAvailable()) {
        return UI_RML_AVAILABILITY_RUNTIME_UNAVAILABLE;
    }

    if (!UI_Rml_RuntimeCanOpenRoutes()) {
        return UI_RML_AVAILABILITY_RENDERER_UNAVAILABLE;
    }

    return UI_RML_AVAILABILITY_READY;
#else
    return UI_RML_AVAILABILITY_RUNTIME_NOT_COMPILED;
#endif
}

const char *UI_Rml_AvailabilityString(ui_rml_availability_t availability)
{
    switch (availability) {
    case UI_RML_AVAILABILITY_UNINITIALIZED:
        return "uninitialized";
    case UI_RML_AVAILABILITY_DISABLED:
        return "disabled";
    case UI_RML_AVAILABILITY_RUNTIME_NOT_COMPILED:
        return "runtime_not_compiled";
    case UI_RML_AVAILABILITY_RUNTIME_UNAVAILABLE:
        return "runtime_unavailable";
    case UI_RML_AVAILABILITY_RENDERER_UNAVAILABLE:
        return "renderer_unavailable";
    case UI_RML_AVAILABILITY_READY:
        return "ready";
    default:
        return "unknown";
    }
}

const char *UI_Rml_RuntimeName(void)
{
#if UI_RML_HAS_RUNTIME
    if (ui_rml_runtime_registered && ui_rml_runtime.RuntimeName) {
        const char *name = ui_rml_runtime.RuntimeName();

        if (name && name[0]) {
            return name;
        }
    }

    return ui_rml_runtime_registered ? "registered" : "unregistered";
#else
    return "stub";
#endif
}

const ui_rml_renderer_interface_t *UI_Rml_RendererInterface(void)
{
#if UI_RML_HAS_RUNTIME
    return ui_rml_renderer_registered ? &ui_rml_renderer : NULL;
#else
    return NULL;
#endif
}

ui_rml_renderer_family_t UI_Rml_RendererFamily(void)
{
#if UI_RML_HAS_RUNTIME
    if (ui_rml_renderer_registered) {
        return ui_rml_renderer.family;
    }
#endif

    return UI_RML_RENDERER_FAMILY_NONE;
}

const char *UI_Rml_RendererFamilyString(ui_rml_renderer_family_t family)
{
    switch (family) {
    case UI_RML_RENDERER_FAMILY_OPENGL:
        return "opengl";
    case UI_RML_RENDERER_FAMILY_VULKAN:
        return "vulkan";
    case UI_RML_RENDERER_FAMILY_RTX_VKPT:
        return "rtx_vkpt";
    case UI_RML_RENDERER_FAMILY_NONE:
    default:
        return "none";
    }
}

const char *UI_Rml_RendererName(void)
{
#if UI_RML_HAS_RUNTIME
    if (ui_rml_renderer_registered && ui_rml_renderer.RendererName) {
        const char *name = ui_rml_renderer.RendererName();

        if (name && name[0]) {
            return name;
        }
    }

    if (ui_rml_renderer_registered) {
        return UI_Rml_RendererFamilyString(ui_rml_renderer.family);
    }
#endif

    return "unregistered";
}

bool UI_Rml_RendererIsAvailable(void)
{
#if UI_RML_HAS_RUNTIME
    if (!ui_rml_renderer_registered ||
        ui_rml_renderer.family == UI_RML_RENDERER_FAMILY_NONE ||
        !ui_rml_renderer.CanRender ||
        !ui_rml_renderer.NativeRenderInterface) {
        return false;
    }

    if (!ui_rml_renderer.CanRender()) {
        return false;
    }

    return ui_rml_renderer.NativeRenderInterface() != NULL;
#else
    return false;
#endif
}

const ui_rml_file_interface_t *UI_Rml_FileInterface(void)
{
    return &ui_rml_file_interface;
}

const char *UI_Rml_RouteForMenu(uiMenu_t menu)
{
    switch (menu) {
    case UIMENU_DEFAULT:
    case UIMENU_MAIN:
        return "main";
    case UIMENU_GAME:
        return "game";
    case UIMENU_DOWNLOAD:
        return "download_status";
    case UIMENU_NONE:
    default:
        return NULL;
    }
}

const char *UI_Rml_DocumentForRoute(const char *route_id)
{
    static char path[MAX_QPATH];
    const ui_rml_route_t *route = UI_Rml_FindRoute(route_id);

    if (!route) {
        return NULL;
    }

    if (!UI_Rml_BuildDocumentPath(route, path, sizeof(path))) {
        return NULL;
    }

    return path;
}

bool UI_Rml_ProbeRoute(const char *route_id)
{
    const ui_rml_route_t *route = UI_Rml_FindRoute(route_id);
    const ui_rml_file_interface_t *files = UI_Rml_FileInterface();
    char path[MAX_QPATH];
    void *data = NULL;
    int len;

    if (!route) {
        Com_Printf("RmlUi route '%s' is not registered.\n",
                   route_id ? route_id : "<null>");
        return false;
    }

    if (!UI_Rml_BuildDocumentPath(route, path, sizeof(path))) {
        Com_Printf("RmlUi route '%s' document path is too long: root='%s', document='%s'.\n",
                   route->id, UI_Rml_AssetRoot(), route->document);
        return false;
    }

    len = files->LoadFile(path, &data);
    if (!data) {
        Com_Printf("RmlUi route '%s' document probe failed: %s (%s).\n",
                   route->id, path, Q_ErrorString(len));
        return false;
    }

    Com_Printf("RmlUi route '%s' document probe OK: %s (%d bytes).\n",
               route->id, path, len);
    files->FreeFile(data);
    return true;
}

bool UI_Rml_OpenRoute(const char *route_id)
{
    return UI_Rml_OpenRouteInternal(route_id);
}

bool UI_Rml_OpenMenu(uiMenu_t menu)
{
    const char *route = UI_Rml_RouteForMenu(menu);

    if (menu == UIMENU_NONE) {
        UI_Rml_CloseActiveRoute();
        return false;
    }

    if (!route || !UI_Rml_IsEnabled()) {
        return false;
    }

    if (!UI_Rml_RuntimeRouteIsAllowed(route)) {
        if (UI_Rml_IsRouteActive()) {
            UI_Rml_CloseActiveRoute();
        }

        if (ui_rml_debug && ui_rml_debug->integer) {
            Com_Printf("RmlUi route '%s' is not registered for runtime drawing; keeping legacy UI fallback active.\n",
                       route);
        }
        return false;
    }

    return UI_Rml_OpenRouteInternal(route);
}

bool UI_Rml_IsRouteActive(void)
{
#if UI_RML_HAS_RUNTIME
    return ui_rml_route_active;
#else
    return false;
#endif
}

void UI_Rml_ModeChanged(void)
{
#if UI_RML_HAS_RUNTIME
    int width;
    int height;

    UI_Rml_ClampMouseToCanvas();

    if (!ui_rml_route_active || !ui_rml_runtime_started ||
        !ui_rml_runtime.Update) {
        return;
    }

    UI_Rml_GetCanvas(&width, &height, NULL);
    if (!ui_rml_runtime.Update(width, height, ui_rml_route_metrics.last_realtime)) {
        UI_Rml_ClearActiveRoute(true);
        return;
    }

    ui_rml_route_metrics.width = width;
    ui_rml_route_metrics.height = height;
#endif
}

bool UI_Rml_Draw(unsigned realtime)
{
#if UI_RML_HAS_RUNTIME
    int width;
    int height;

    if (!ui_rml_route_active || !ui_rml_runtime_started ||
        !ui_rml_runtime.Update || !ui_rml_runtime.Render) {
        return false;
    }

    if (!UI_Rml_RuntimeIsAvailable()) {
        UI_Rml_ClearActiveRoute(true);
        return false;
    }

    UI_Rml_GetCanvas(&width, &height, NULL);

    if (!ui_rml_runtime.Update(width, height, realtime) ||
        !ui_rml_runtime.Render()) {
        Com_Printf("RmlUi route '%s' failed to render; falling back to legacy UI.\n",
                   ui_rml_active_route[0] ? ui_rml_active_route : "<unknown>");
        UI_Rml_ClearActiveRoute(true);
        return false;
    }

    UI_Rml_DrawCursor();

    ui_rml_route_metrics.updates++;
    ui_rml_route_metrics.renders++;
    ui_rml_route_metrics.last_realtime = realtime;
    ui_rml_route_metrics.width = width;
    ui_rml_route_metrics.height = height;
    return true;
#else
    (void)realtime;
    return false;
#endif
}

bool UI_Rml_KeyEvent(int key, bool down)
{
#if UI_RML_HAS_RUNTIME
    if (!ui_rml_route_active) {
        return false;
    }

    if (ui_rml_runtime.KeyEvent) {
        (void)ui_rml_runtime.KeyEvent(key, down);
    }

    ui_rml_route_metrics.key_events++;
    if (UI_Rml_KeyIsMouseButton(key)) {
        ui_rml_route_metrics.mouse_buttons++;
    } else if (down && UI_Rml_KeyIsMouseWheel(key)) {
        ui_rml_route_metrics.mouse_wheels++;
    }

    if (down && (key == K_ESCAPE || key == K_MOUSE2)) {
        UI_StartFeedbackSound(UI_FEEDBACK_CLOSE);
        UI_Rml_RuntimeBackRoute_f();
    }

    return true;
#else
    (void)key;
    (void)down;
    return false;
#endif
}

bool UI_Rml_CharEvent(int key)
{
#if UI_RML_HAS_RUNTIME
    if (!ui_rml_route_active) {
        return false;
    }

    if (ui_rml_runtime.CharEvent) {
        (void)ui_rml_runtime.CharEvent(key);
    }

    ui_rml_route_metrics.char_events++;
    return true;
#else
    (void)key;
    return false;
#endif
}

bool UI_Rml_MouseEvent(int x, int y)
{
#if UI_RML_HAS_RUNTIME
    int canvas_x;
    int canvas_y;

    if (!ui_rml_route_active) {
        return false;
    }

    UI_Rml_MouseFromFramebuffer(x, y, &canvas_x, &canvas_y);
    ui_rml_mouse_x = canvas_x;
    ui_rml_mouse_y = canvas_y;
    ui_rml_mouse_valid = true;

    if (ui_rml_runtime.MouseEvent) {
        (void)ui_rml_runtime.MouseEvent(canvas_x, canvas_y);
    }

    ui_rml_route_metrics.mouse_moves++;
    ui_rml_route_metrics.last_mouse_x = canvas_x;
    ui_rml_route_metrics.last_mouse_y = canvas_y;
    return true;
#else
    (void)x;
    (void)y;
    return false;
#endif
}

void UI_Rml_CloseActiveRoute(void)
{
#if UI_RML_HAS_RUNTIME
    if (ui_rml_route_active) {
        ui_rml_route_metrics.close_requests++;
    }
    UI_Rml_ClearRouteHistory();
#endif
    UI_Rml_ClearActiveRoute(true);
}

#if UI_RML_HAS_RUNTIME
void UI_Rml_SetRuntimeInterface(const ui_rml_runtime_interface_t *runtime)
{
    UI_Rml_StopRuntime();
    ui_rml_runtime_failed = false;

    if (runtime) {
        ui_rml_runtime = *runtime;
        ui_rml_runtime_registered = runtime->OpenRoute != NULL &&
                                    runtime->CloseRoute != NULL &&
                                    runtime->Update != NULL &&
                                    runtime->Render != NULL &&
                                    runtime->KeyEvent != NULL &&
                                    runtime->CharEvent != NULL &&
                                    runtime->MouseEvent != NULL;
    } else {
        ui_rml_runtime = {};
        ui_rml_runtime_registered = false;
    }
}

void UI_Rml_SetRendererInterface(const ui_rml_renderer_interface_t *renderer)
{
    if (ui_rml_runtime_started) {
        UI_Rml_StopRuntime();
    }

    if (renderer && renderer->family != UI_RML_RENDERER_FAMILY_NONE) {
        ui_rml_renderer = *renderer;
        ui_rml_renderer_registered = true;
    } else {
        UI_Rml_ClearRendererInterface();
    }
}

void UI_Rml_ClearRendererInterface(void)
{
    if (ui_rml_runtime_started) {
        UI_Rml_StopRuntime();
    }

    ui_rml_renderer = {};
    ui_rml_renderer_registered = false;
}
#endif
