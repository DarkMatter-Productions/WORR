/*Copyright (c) 2024 The DarkMatter Project
Licensed under the GNU General Public License 2.0.

menu_page_mymap.cpp (Menu Page - MyMap) migrated to cgame JSON menus.*/

#include "../g_local.hpp"
#include "../commands/command_registration.hpp"
#include "menu_ui_helpers.hpp"
#include "menu_ui_list.hpp"

namespace {

bool MyMapFeatureEnabled()
{
  return g_maps_mymap && g_maps_mymap->integer &&
         (!g_allowMymap || g_allowMymap->integer);
}

bool MyMapLoginReady(const gentity_t *ent)
{
  return ent && ent->client && ent->client->sess.socialID[0];
}

bool CanUseMyMap(const gentity_t *ent)
{
  return ent && ent->client && !Tournament_IsActive() &&
         MyMapFeatureEnabled() && MyMapLoginReady(ent);
}

std::string BuildMyMapStatus(const gentity_t *ent)
{
  if (!ent || !ent->client)
    return {};

  if (Tournament_IsActive())
    return "MyMap is unavailable during tournaments.";

  if (!MyMapFeatureEnabled())
    return "MyMap is disabled on this server.";

  if (!MyMapLoginReady(ent))
    return "Log in to queue a MyMap request on this server.";

  return "Queue a map to play next. Optional flags let you override the match rules for your request.";
}

void UpdateMyMapMenu(gentity_t *ent, bool openMenu)
{
  if (!ent || !ent->client)
    return;

  const auto &state = ent->client->ui.mymap;
  const bool hasFlags = state.enableFlags || state.disableFlags;
  const bool canUse = CanUseMyMap(ent);

  MenuUi::UiCommandBuilder cmd(ent);
  cmd.AppendCvar("ui_mymap_status", BuildMyMapStatus(ent));
  cmd.AppendCvar("ui_mymap_flags_summary",
                 fmt::format("Flags: {}", BuildMapFlagSummary(state)));
  cmd.AppendCvar("ui_mymap_can_select", canUse ? "1" : "0");
  cmd.AppendCvar("ui_mymap_can_flags", canUse ? "1" : "0");
  cmd.AppendCvar("ui_mymap_has_flags", hasFlags ? "1" : "0");
  if (openMenu)
    cmd.AppendCommand("pushmenu mymap_main");
  cmd.Flush();
}

void UpdateMyMapFlagsMenu(gentity_t *ent, bool openMenu)
{
  if (!ent || !ent->client)
    return;

  const auto &state = ent->client->ui.mymap;
  MenuUi::UiCommandBuilder cmd(ent);
  for (const auto &flag : MapFlagEntries()) {
    cmd.AppendCvar(fmt::format("ui_mymap_flag_{}", flag.code).c_str(),
                   MapFlagStateLabel(state, flag));
  }
  if (openMenu)
    cmd.AppendCommand("pushmenu mymap_flags");
  cmd.Flush();
}

} // namespace

void OpenMyMapMenu(gentity_t *ent)
{
  UpdateMyMapMenu(ent, true);
}

void OpenMyMapSelectMenu(gentity_t *ent)
{
  if (!ent || !ent->client)
    return;

  if (!Commands::CheckMyMapAllowed(ent)) {
    RefreshMyMapMenu(ent);
    return;
  }

  UiList_Open(ent, UiListKind::MyMap);
}

void RefreshMyMapMenu(gentity_t *ent)
{
  UpdateMyMapMenu(ent, false);
}

void OpenMyMapFlagsMenu(gentity_t *ent)
{
  UpdateMyMapFlagsMenu(ent, true);
}

void RefreshMyMapFlagsMenu(gentity_t *ent)
{
  UpdateMyMapFlagsMenu(ent, false);
}
