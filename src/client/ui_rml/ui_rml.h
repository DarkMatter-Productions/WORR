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

#pragma once

#include "shared/shared.h"
#include "client/ui.h"

#ifndef UI_RML_HAS_RUNTIME
#define UI_RML_HAS_RUNTIME 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_RML_AVAILABILITY_UNINITIALIZED,
    UI_RML_AVAILABILITY_DISABLED,
    UI_RML_AVAILABILITY_RUNTIME_NOT_COMPILED,
    UI_RML_AVAILABILITY_RUNTIME_UNAVAILABLE,
    UI_RML_AVAILABILITY_RENDERER_UNAVAILABLE,
    UI_RML_AVAILABILITY_READY
} ui_rml_availability_t;

typedef struct {
    int (*LoadFile)(const char *path, void **data);
    void (*FreeFile)(void *data);
} ui_rml_file_interface_t;

typedef struct {
    bool (*Init)(void);
    void (*Shutdown)(void);
    bool (*OpenRoute)(const char *route_id, const char *document_path);
    void (*CloseRoute)(void);
    bool (*Update)(int width, int height, unsigned realtime);
    bool (*Render)(void);
    bool (*KeyEvent)(int key, bool down);
    bool (*CharEvent)(int key);
    bool (*MouseEvent)(int x, int y);
    bool (*ProbeRoute)(const char *route_id, const char *document_path);
    const char *(*RuntimeName)(void);
    bool (*CanOpenRoutes)(void);
    // Optional: consume an Escape/Mouse2 back request (keybind capture,
    // document data-close-command). Returns true when handled.
    bool (*HandleBackKey)(int key);
} ui_rml_runtime_interface_t;

typedef enum {
    UI_RML_RENDERER_FAMILY_NONE,
    UI_RML_RENDERER_FAMILY_OPENGL,
    UI_RML_RENDERER_FAMILY_VULKAN,
    UI_RML_RENDERER_FAMILY_RTX_VKPT
} ui_rml_renderer_family_t;

typedef struct {
    ui_rml_renderer_family_t family;
    const char *(*RendererName)(void);
    bool (*CanRender)(void);
    void *(*NativeRenderInterface)(void);
} ui_rml_renderer_interface_t;

void UI_Rml_Init(void);
void UI_Rml_Shutdown(void);
bool UI_Rml_IsEnabled(void);
bool UI_Rml_RuntimeIsAvailable(void);
ui_rml_availability_t UI_Rml_Availability(void);
const char *UI_Rml_AvailabilityString(ui_rml_availability_t availability);
const char *UI_Rml_RuntimeName(void);
const ui_rml_renderer_interface_t *UI_Rml_RendererInterface(void);
ui_rml_renderer_family_t UI_Rml_RendererFamily(void);
const char *UI_Rml_RendererFamilyString(ui_rml_renderer_family_t family);
const char *UI_Rml_RendererName(void);
bool UI_Rml_RendererIsAvailable(void);
const ui_rml_file_interface_t *UI_Rml_FileInterface(void);
const char *UI_Rml_RouteForMenu(uiMenu_t menu);
const char *UI_Rml_DocumentForRoute(const char *route_id);
bool UI_Rml_RouteIsPopup(const char *route_id);
bool UI_Rml_ProbeRoute(const char *route_id);
bool UI_Rml_OpenRoute(const char *route_id);
bool UI_Rml_OpenPopupRoute(const char *route_id);
bool UI_Rml_OpenMenu(uiMenu_t menu);
bool UI_Rml_IsRouteActive(void);
void UI_Rml_ModeChanged(void);
bool UI_Rml_Draw(unsigned realtime);
bool UI_Rml_KeyEvent(int key, bool down);
bool UI_Rml_CharEvent(int key);
bool UI_Rml_MouseEvent(int x, int y);
void UI_Rml_CloseActiveRoute(void);

#if UI_RML_HAS_RUNTIME
// Runtime implementations register dependency-aware hooks here; keep this free of RmlUi types.
void UI_Rml_SetRuntimeInterface(const ui_rml_runtime_interface_t *runtime);
void UI_Rml_SetRendererInterface(const ui_rml_renderer_interface_t *renderer);
void UI_Rml_ClearRendererInterface(void);
void UI_Rml_RegisterCompiledRuntime(void);
#endif

#ifdef __cplusplus
}
#endif
