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

#include "client/cgame_ui.h"
#include "client/sound/sound.h"
#include "client/ui.h"
#include "ui_rml/ui_rml.h"

extern "C" void SCR_NotifyMouseEvent(int x, int y);

static const cgame_ui_export_t *UI_GetAPI(void)
{
    return CG_UI_GetExport();
}

void UI_Init(void)
{
    UI_Rml_Init();

    const cgame_ui_export_t *api = UI_GetAPI();
    if (api && api->Init)
        api->Init();
}

void UI_Shutdown(void)
{
    const cgame_ui_export_t *api = UI_GetAPI();
    if (api && api->Shutdown)
        api->Shutdown();

    UI_Rml_Shutdown();
}

void UI_ModeChanged(void)
{
    UI_Rml_ModeChanged();

    const cgame_ui_export_t *api = UI_GetAPI();
    if (api && api->ModeChanged)
        api->ModeChanged();
}

void UI_KeyEvent(int key, bool down)
{
    if (UI_Rml_KeyEvent(key, down))
        return;

    const cgame_ui_export_t *api = UI_GetAPI();
    if (api && api->KeyEvent)
        api->KeyEvent(key, down);
}

void UI_CharEvent(int key)
{
    if (UI_Rml_CharEvent(key))
        return;

    const cgame_ui_export_t *api = UI_GetAPI();
    if (api && api->CharEvent)
        api->CharEvent(key);
}

void UI_Draw(unsigned realtime)
{
    if (UI_Rml_Draw(realtime))
        return;

    const cgame_ui_export_t *api = UI_GetAPI();
    if (api && api->Draw)
        api->Draw(realtime);
}

void UI_OpenMenu(uiMenu_t menu)
{
    if (UI_Rml_OpenMenu(menu))
        return;

    const cgame_ui_export_t *api = UI_GetAPI();
    if (api && api->OpenMenu)
        api->OpenMenu(menu);
}

void UI_StartFeedbackSound(uiFeedbackSound_t sound)
{
    switch (sound) {
    case UI_FEEDBACK_OPEN:
        S_StartLocalSound("misc/menu1.wav");
        break;
    case UI_FEEDBACK_MOVE:
        S_StartLocalSound("misc/menu2.wav");
        break;
    case UI_FEEDBACK_CLOSE:
        S_StartLocalSound("misc/menu3.wav");
        break;
    case UI_FEEDBACK_ALERT:
        S_StartLocalSound("misc/talk1.wav");
        break;
    default:
        break;
    }
}

void UI_Frame(int msec)
{
    if (UI_Rml_IsRouteActive())
        return;

    const cgame_ui_export_t *api = UI_GetAPI();
    if (api && api->Frame)
        api->Frame(msec);
}

void UI_StatusEvent(const serverStatus_t *status)
{
    const cgame_ui_export_t *api = UI_GetAPI();
    if (api && api->StatusEvent)
        api->StatusEvent(status);
}

void UI_ErrorEvent(const netadr_t *from)
{
    const cgame_ui_export_t *api = UI_GetAPI();
    if (api && api->ErrorEvent)
        api->ErrorEvent(from);
}

void UI_MouseEvent(int x, int y)
{
    if (Key_GetDest() & KEY_MESSAGE)
        SCR_NotifyMouseEvent(x, y);
    if (UI_Rml_MouseEvent(x, y))
        return;
    const cgame_ui_export_t *api = UI_GetAPI();
    if (api && api->MouseEvent)
        api->MouseEvent(x, y);
}

bool UI_IsTransparent(void)
{
    const cgame_ui_export_t *api = UI_GetAPI();
    if (api && api->IsTransparent)
        return api->IsTransparent();
    return true;
}
