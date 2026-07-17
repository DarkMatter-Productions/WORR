project('worr', 'c', 'cpp',
  license: 'GPL-2.0-or-later',
  version: run_command(find_program('python3'), 'version.py', '--version', check: true).stdout().strip(),
  meson_version: '>= 0.60.0',
  default_options: [
    meson.version().version_compare('>=1.3.0') ? 'c_std=gnu11,c11' : 'c_std=gnu11',
    'cpp_std=c++17',
    'buildtype=debugoptimized',
    'b_vscrt=from_buildtype',
  ],
)

common_src = [
  'src/common/bsp.c',
  'src/common/cmd.c',
  'src/common/cmodel.c',
  'src/common/common.c',
  'src/common/crc.c',
  'src/common/cvar.c',
  'src/common/error.c',
  'src/common/bootstrap.c',
  'src/common/field.c',
  'src/common/fifo.c',
  'src/common/files.c',
  'src/common/game3_convert.c',
  'src/common/game3_pmove/new.c',
  'src/common/game3_pmove/old.c',
  'src/common/natsort.c',
  'src/common/hash_map.c',
  'src/common/loc.c',
  'src/common/math.c',
  'src/common/mdfour.c',
  'src/common/msg.c',
  'src/common/net/chan.c',
  'src/common/net/command_canonical.c',
  'src/common/net/impair.c',
  'src/common/net/impair_queue.c',
  'src/common/net/net.c',
  'src/common/net/sequence.c',
  'src/common/net/usercmd_delta.c',
  'src/common/pmove.c',
  'src/common/prompt.c',
  'src/common/q2proto_shared.c',
  'src/common/sizebuf.c',
  'src/common/steam.c',
  'src/common/utils.c',
  'src/common/zone.c',
  'src/common/features.h',
  'src/common/mapdb.c',
  'src/shared/shared.c',

  'src/common/q2proto-glue.c',
  'src/common/q2proto-packing.c',

  'inc/shared/atomic.h',
  'inc/shared/base85.h',
  'inc/shared/files.h',
  'inc/shared/game.h',
  'inc/shared/game3_shared.h',
  'inc/shared/list.h',
  'inc/shared/m_flash.h',
  'inc/shared/platform.h',
  'inc/shared/shared.h',

  'inc/common/natsort.h',
  'inc/common/async.h',
  'inc/common/bsp.h',
  'inc/common/bootstrap.h',
  'inc/common/cmd.h',
  'inc/common/cmodel.h',
  'inc/common/common.h',
  'inc/common/cvar.h',
  'inc/common/error.h',
  'inc/common/field.h',
  'inc/common/fifo.h',
  'inc/common/files.h',
  'inc/common/gamedll.h',
  'inc/common/hash_map.h',
  'inc/common/intreadwrite.h',
  'inc/common/loc.h',
  'inc/common/math.h',
  'inc/common/mdfour.h',
  'inc/common/msg.h',
  'inc/common/pmove.h',
  'inc/common/prompt.h',
  'inc/common/protocol.h',
  'inc/common/sizebuf.h',
  'inc/common/steam.h',
  'inc/common/tests.h',
  'inc/common/utils.h',
  'inc/common/zone.h',

  'inc/common/net/chan.h',
  'inc/common/net/impair.h',
  'inc/common/net/impair_queue.h',
  'inc/common/net/net.h',
  'inc/common/net/sequence.h',
  'inc/common/net/command_canonical.h',
  'inc/common/net/usercmd_delta.h',

  'inc/format/bsp.h',
  'inc/format/md2.h',
  'inc/format/md3.h',
  'inc/format/pak.h',
  'inc/format/pcx.h',
  'inc/format/sp2.h',
  'inc/format/wal.h',

  'inc/system/hunk.h',
  'inc/system/pthread.h',
  'inc/system/system.h',
]

q2proto_src = [
  'q2proto/src/q2proto_client.c',
  'q2proto/src/q2proto_coords.c',
  'q2proto/src/q2proto_crc.c',
  'q2proto/src/q2proto_error.c',
  'q2proto/src/q2proto_internal_common.c',
  'q2proto/src/q2proto_internal_debug.c',
  'q2proto/src/q2proto_internal_download.c',
  'q2proto/src/q2proto_internal_fmt.c',
  'q2proto/src/q2proto_internal_maybe_zpacket.c',
  'q2proto/src/q2proto_internal_packing.c',
  'q2proto/src/q2proto_multicast.c',
  'q2proto/src/q2proto_proto_kex.c',
  'q2proto/src/q2proto_proto_q2pro_extdemo.c',
  'q2proto/src/q2proto_proto_q2pro.c',
  'q2proto/src/q2proto_proto_q2repro.c',
  'q2proto/src/q2proto_proto_r1q2.c',
  'q2proto/src/q2proto_proto_vanilla.c',
  'q2proto/src/q2proto_protocol.c',
  'q2proto/src/q2proto_server.c',
  'q2proto/src/q2proto_solid.c',
  'q2proto/src/q2proto_sound.c',
  'q2proto/src/q2proto_string.c',
]

client_src = [
  'src/client/ascii.cpp',
  'src/client/cgame.cpp',
  'src/client/cgame_event_runtime.cpp',
  'src/client/cgame_classic.cpp',
  'src/client/command_identity.cpp',
  'src/client/consumed_cursor.cpp',
  'src/client/demo_clock.cpp',
  'src/client/console.cpp',
  'src/client/demo.cpp',
  'src/client/download.cpp',
  'src/client/effects.cpp',
  'src/client/entities.cpp',
  'src/client/event_shadow.cpp',
  'src/client/font.cpp',
  'src/client/gameplay_light.c',
  'src/client/ui_font.cpp',
  'src/client/ui_rml/ui_rml.cpp',
  'src/client/ui_rml/ui_rml_runtime.cpp',
  'src/client/input.cpp',
  'src/client/keys.cpp',
  'src/client/locs.cpp',
  'src/client/main.cpp',
  'src/client/newfx.cpp',
  'src/client/net_capability.cpp',
  'src/client/native_readiness_pilot.cpp',
  'src/client/parse.cpp',
  'src/client/snapshot_recovery.cpp',
  'src/client/snapshot_shadow.cpp',
  'src/client/precache.cpp',
  'src/client/predict.cpp',
  'src/client/renderer.cpp',
  'src/client/screen.cpp',
  'src/client/sound/main.cpp',
  'src/client/sound/mem.cpp',
  'src/client/sound/qal.h',
  'src/client/sound/sound.h',
  'src/client/tent.cpp',
  'src/client/ui_bridge.cpp',
  'src/client/view.cpp',
  'src/client/client.h',
  'src/client/cgame_classic.h',
  'inc/client/native_readiness_pilot.h',
  'src/common/async.c',
  'src/common/gamedll.c',
  'src/common/net/cgame_event_shadow.c',
  'src/server/commands.c',
  'src/server/command_context.c',
  'src/server/entities.c',
  'src/server/event_shadow.c',
  'src/server/game.c',
  'src/server/local_interaction_authority.c',
  'src/server/rewind_collision.c',
  'src/server/game3_proxy/game3_proxy.c',
  'src/server/init.c',
  'src/server/main.c',
  'src/server/native_shadow.c',
  'src/server/send.c',
  'src/server/snapshot_event_candidates.c',
  'src/server/snapshot_shadow.c',
  'src/server/user.c',
  'src/server/nav.c',
  'src/server/world.c',
  'src/server/server.h',
  'src/shared/base85.c',
  'src/shared/m_flash.c',

  'inc/server/server.h',
  'inc/server/native_shadow.h',
  'inc/server/snapshot_event_candidates.h',
  'inc/server/snapshot_shadow.h',

  'inc/client/client.h',
  'inc/client/gameplay_light.h',
  'inc/client/input.h',
  'inc/client/keys.h',
  'inc/client/ui.h',
  'inc/client/video.h',

  'inc/client/sound/dma.h',
  'inc/client/sound/sound.h',
]

ui_src = [
  'src/game/cgame/ui/ui_cgame_access.cpp',
  'src/game/cgame/ui/ui_core.cpp',
  'src/game/cgame/ui/ui_menu.cpp',
  'src/game/cgame/ui/ui_hints.cpp',
  'src/game/cgame/ui/ui_widgets.cpp',
  'src/game/cgame/ui/ui_json.cpp',
  'src/game/cgame/ui/ui_conditions.cpp',
  'src/game/cgame/ui/ui_list.cpp',
  'src/game/cgame/ui/ui_page_demos.cpp',
  'src/game/cgame/ui/ui_page_servers.cpp',
  'src/game/cgame/ui/ui_page_player.cpp',
  'src/game/cgame/ui/ui_page_welcome.cpp',
  'src/game/cgame/ui/ui_mapdb.cpp',
  'src/game/cgame/ui/ui_playermodels.cpp',
]

renderer_src = [
  'src/renderer/dds.c',
  'src/renderer/rmlui_bridge.cpp',
  'src/renderer/shadow_frontend.c',
  'src/renderer/ui_scale.c',
  'src/renderer/view_setup.c',
  'src/rend_gl/draw.c',
  'src/rend_gl/hq2x.c',
  'src/rend_gl/images.c',
  'src/rend_gl/legacy.c',
  'src/rend_gl/main.c',
  'src/rend_gl/mesh.c',
  'src/rend_gl/models.c',
  'src/rend_gl/profile.c',
  'src/rend_gl/qgl.c',
  'src/rend_gl/shadow.c',
  'src/rend_gl/shader.c',
  'src/rend_gl/sky.c',
  'src/rend_gl/state.c',
  'src/rend_gl/surf.c',
  'src/rend_gl/tess.c',
  'src/rend_gl/texture.c',
  'src/rend_gl/world.c',
  'src/rend_gl/debug.c',
  'src/rend_gl/debug_text.c',
  'src/rend_gl/arbfp.h',
  'src/rend_gl/gl.h',
  'src/rend_gl/images.h',
  'src/rend_gl/qgl.h',

  'inc/renderer/renderer.h',
  'inc/renderer/shadow_frontend.h',
]

renderer_vk_rtx_src = [
  'src/renderer/dds.c',
  'src/renderer/rmlui_bridge.cpp',
  'src/renderer/shadow_frontend.c',
  'src/renderer/ui_scale.c',
  'src/renderer/view_setup.c',
  'src/rend_rtx/vkpt/asvgf.c',
  'src/rend_rtx/vkpt/bloom.c',
  'src/rend_rtx/vkpt/bsp_mesh.c',
  'src/rend_rtx/vkpt/buddy_allocator.c',
  'src/rend_rtx/vkpt/cameras.c',
  'src/rend_rtx/vkpt/conversion.c',
  'src/rend_rtx/vkpt/debug.c',
  'src/rend_rtx/vkpt/device_memory_allocator.c',
  'src/rend_rtx/vkpt/draw.c',
  'src/rend_rtx/vkpt/fog.c',
  'src/rend_rtx/vkpt/freecam.c',
  'src/rend_rtx/vkpt/fsr.c',
  'src/rend_rtx/vkpt/god_rays.c',
  'src/rend_rtx/vkpt/main.c',
  'src/rend_rtx/vkpt/material.c',
  'src/rend_rtx/vkpt/matrix.c',
  'src/rend_rtx/vkpt/mgpu.c',
  'src/rend_rtx/vkpt/models.c',
  'src/rend_rtx/vkpt/path_tracer.c',
  'src/rend_rtx/vkpt/physical_sky.c',
  'src/rend_rtx/vkpt/precomputed_sky.c',
  'src/rend_rtx/vkpt/profiler.c',
  'src/rend_rtx/vkpt/shadow_map.c',
  'src/rend_rtx/vkpt/textures.c',
  'src/rend_rtx/vkpt/tone_mapping.c',
  'src/rend_rtx/vkpt/transparency.c',
  'src/rend_rtx/vkpt/uniform_buffer.c',
  'src/rend_rtx/vkpt/vertex_buffer.c',
  'src/rend_rtx/vkpt/vk_util.c',

  'src/rend_rtx/refresh/debug.c',
  'src/rend_rtx/refresh/debug_text.c',
  'src/rend_rtx/refresh/images.c',
  'src/rend_rtx/refresh/model_iqm.c',
  'src/rend_rtx/refresh/models.c',
  'src/rend_rtx/refresh/refresh_ptrs.c',
  'src/rend_vk/refresh/stb/stb.c',

  'src/rend_rtx/vkpt/vkpt.h',
  'src/rend_rtx/vkpt/vk_util.h',
  'inc/renderer/renderer.h',
  'inc/renderer/shadow_frontend.h',
]

renderer_vk_src = [
  'src/renderer/dds.c',
  'src/renderer/rmlui_bridge.cpp',
  'src/renderer/shadow_frontend.c',
  'src/renderer/ui_scale.c',
  'src/renderer/view_setup.c',
  'src/rend_rtx/refresh/debug.c',
  'src/rend_rtx/refresh/debug_text.c',
  'src/rend_vk/vk_debug.c',
  'src/rend_vk/vk_debug.h',
  'src/rend_vk/vk_debug_spv.h',
  'src/rend_vk/vk_main.c',
  'src/rend_vk/vk_postprocess.c',
  'src/rend_vk/vk_postprocess.h',
  'src/rend_vk/vk_postprocess_spv.h',
  'src/rend_vk/vk_bloom_spv.h',
  'src/rend_vk/vk_crt_spv.h',
  'src/rend_vk/vk_dof_spv.h',
  'src/rend_vk/vk_entity.c',
  'src/rend_vk/vk_entity.h',
  'src/rend_vk/vk_entity_spv.h',
  'src/rend_vk/vk_shadow.c',
  'src/rend_vk/vk_shadow.h',
  'src/rend_vk/vk_ui.c',
  'src/rend_vk/vk_ui.h',
  'src/rend_vk/vk_ui2d_spv.h',
  'src/rend_vk/vk_world.c',
  'src/rend_vk/vk_world.h',
  'src/rend_vk/vk_world_spv.h',
  'src/rend_vk/refresh/stb/stb.c',
  'src/rend_vk/vk_local.h',
  'inc/renderer/renderer.h',
  'inc/renderer/shadow_frontend.h',
  'inc/renderer/view_setup.h',
]

server_src = [
  'src/client/null.c',
  'src/common/gamedll.c',
  'src/server/commands.c',
  'src/server/command_context.c',
  'src/server/entities.c',
  'src/server/event_shadow.c',
  'src/server/game.c',
  'src/server/local_interaction_authority.c',
  'src/server/rewind_collision.c',
  'src/server/game3_proxy/game3_proxy.c',
  'src/server/init.c',
  'src/server/main.c',
  'src/server/native_shadow.c',
  'src/server/send.c',
  'src/server/snapshot_event_candidates.c',
  'src/server/snapshot_shadow.c',
  'src/server/user.c',
  'src/server/nav.c',
  'src/server/world.c',
  'src/server/server.h',
  'src/shared/base85.c',

  'inc/server/server.h',
  'inc/server/native_shadow.h',
  'inc/server/snapshot_event_candidates.h',
  'inc/server/snapshot_shadow.h',
]

bgame_src = [
  'src/game/bgame/bg_local.hpp',
  'src/game/bgame/char_array_utils.hpp',
  'src/game/bgame/game.hpp',
  'src/game/bgame/logger.cpp',
  'src/game/bgame/logger.hpp',
  'src/game/bgame/map_validation.hpp',
  'src/game/bgame/m_flash.hpp',
  'src/game/bgame/q_std.hpp',
  'src/game/bgame/q_vec3.hpp',
  'src/game/bgame/string_compat.hpp',
  'src/game/bgame/version_autogen.hpp',
  'src/game/bgame/version.hpp',
  'src/game/bgame/weapon_pref_utils.hpp',
]

prediction_core_src = [
  'src/common/net/command_canonical.c',
  'inc/shared/prediction_abi.h',
  'src/game/bgame/prediction_abi.cpp',
  'src/game/sgame/player/p_move.cpp',
]

sgame_src = bgame_src + [
  'src/game/sgame/bots/bot_actions.cpp',
  'src/game/sgame/bots/bot_brain.cpp',
  'src/game/sgame/bots/bot_combat.cpp',
  'src/game/sgame/bots/bot_items.cpp',
  'src/game/sgame/bots/bot_nav.cpp',
  'src/game/sgame/bots/bot_objectives.cpp',
  'src/game/sgame/bots/botlib_adapter.cpp',
  'src/game/sgame/bots/bot_runtime.cpp',
  'src/game/sgame/bots/bot_think.cpp',
  'src/game/sgame/bots/q3a/q3a_botlib_boundary.cpp',
  'src/game/sgame/client/client_session_service_impl.cpp',
  'src/game/sgame/client/client_stats_service.cpp',
  'src/game/sgame/commands/command_admin.cpp',
  'src/game/sgame/commands/command_cheats.cpp',
  'src/game/sgame/commands/command_client.cpp',
  'src/game/sgame/commands/command_system.cpp',
  'src/game/sgame/commands/command_voting.cpp',
  'src/game/sgame/gameplay/g_ai_new.cpp',
  'src/game/sgame/gameplay/g_ai.cpp',
  'src/game/sgame/gameplay/g_capture.cpp',
  'src/game/sgame/gameplay/g_client_cfg.cpp',
  'src/game/sgame/gameplay/g_clients.cpp',
  'src/game/sgame/gameplay/g_combat_heatmap.cpp',
  'src/game/sgame/gameplay/g_combat.cpp',
  'src/game/sgame/gameplay/g_domination.cpp',
  'src/game/sgame/gameplay/g_func.cpp',
  'src/game/sgame/gameplay/g_hud_blob.cpp',
  'src/game/sgame/gameplay/g_harvester.cpp',
  'src/game/sgame/gameplay/g_headhunters.cpp',
  'src/game/sgame/gameplay/g_horde.cpp',
  'src/game/sgame/gameplay/g_item_list.cpp',
  'src/game/sgame/gameplay/g_items.cpp',
  'src/game/sgame/gameplay/g_main.cpp',
  'src/game/sgame/gameplay/g_map_manager.cpp',
  'src/game/sgame/gameplay/g_misc.cpp',
  'src/game/sgame/gameplay/g_monster_spawn.cpp',
  'src/game/sgame/gameplay/g_monster.cpp',
  'src/game/sgame/gameplay/g_phys.cpp',
  'src/game/sgame/gameplay/g_qu3e_physics.cpp',
  'src/game/sgame/gameplay/g_proball.cpp',
  'src/game/sgame/gameplay/g_save.cpp',
  'src/game/sgame/gameplay/g_spawn_points.cpp',
  'src/game/sgame/gameplay/g_spawn.cpp',
  'src/game/sgame/gameplay/g_spectator.cpp',
  'src/game/sgame/gameplay/g_statusbar.cpp',
  'src/game/sgame/gameplay/g_svcmds.cpp',
  'src/game/sgame/gameplay/g_target.cpp',
  'src/game/sgame/gameplay/g_teamplay.cpp',
  'src/game/sgame/gameplay/g_trigger.cpp',
  'src/game/sgame/gameplay/g_turret.cpp',
  'src/game/sgame/gameplay/g_utilities.cpp',
  'src/game/sgame/gameplay/g_weapon.cpp',
  'src/game/sgame/match/match_logging.cpp',
  'src/game/sgame/match/match_state.cpp',
  'src/game/sgame/match/match_setup.cpp',
  'src/game/sgame/match/tournament.cpp',
  'src/game/sgame/menu/menu_page_admin_commands.cpp',
  'src/game/sgame/menu/menu_page_admin.cpp',
  'src/game/sgame/menu/menu_page_callvote.cpp',
  'src/game/sgame/menu/menu_page_forfeit.cpp',
  'src/game/sgame/menu/menu_page_hostinfo.cpp',
  'src/game/sgame/menu/menu_page_mapselector.cpp',
  'src/game/sgame/menu/menu_page_matchinfo.cpp',
  'src/game/sgame/menu/menu_page_matchstats.cpp',
  'src/game/sgame/menu/menu_page_mymap.cpp',
  'src/game/sgame/menu/menu_page_tournament.cpp',
  'src/game/sgame/menu/menu_page_voting.cpp',
  'src/game/sgame/menu/menu_page_welcome.cpp',
  'src/game/sgame/menu/menu_ui_helpers.cpp',
  'src/game/sgame/menu/menu_ui_list.cpp',
  'src/game/sgame/network/lag_compensation.cpp',
  'src/game/sgame/network/lag_compensation.hpp',
  'src/game/sgame/network/rewind_collision_import.hpp',
  'src/game/sgame/network/local_action_observation.cpp',
  'src/game/sgame/network/local_action_observation.hpp',
  'src/common/net/event_shadow_map.c',
  'src/game/sgame/network/event_shadow.cpp',
  'src/game/sgame/network/event_shadow.hpp',
  'src/game/sgame/monsters/m_actor.cpp',
  'src/game/sgame/monsters/m_arachnid.cpp',
  'src/game/sgame/monsters/m_berserk.cpp',
  'src/game/sgame/monsters/m_brain.cpp',
  'src/game/sgame/monsters/m_carrier.cpp',
  'src/game/sgame/monsters/m_centroid.cpp',
  'src/game/sgame/monsters/m_chick.cpp',
  'src/game/sgame/monsters/m_chthon.cpp',
  'src/game/sgame/monsters/m_dog.cpp',
  'src/game/sgame/monsters/m_dragon.cpp',
  'src/game/sgame/monsters/m_eel.cpp',
  'src/game/sgame/monsters/m_enforcer.cpp',
  'src/game/sgame/monsters/m_fiend.cpp',
  'src/game/sgame/monsters/m_fixbot.cpp',
  'src/game/sgame/monsters/m_float.cpp',
  'src/game/sgame/monsters/m_flyer.cpp',
  'src/game/sgame/monsters/m_gekk.cpp',
  'src/game/sgame/monsters/m_gladiator.cpp',
  'src/game/sgame/monsters/m_grunt.cpp',
  'src/game/sgame/monsters/m_guardian.cpp',
  'src/game/sgame/monsters/m_guncmdr.cpp',
  'src/game/sgame/monsters/m_gunner.cpp',
  'src/game/sgame/monsters/m_hellknight.cpp',
  'src/game/sgame/monsters/m_hornet.cpp',
  'src/game/sgame/monsters/m_hover.cpp',
  'src/game/sgame/monsters/m_infantry.cpp',
  'src/game/sgame/monsters/m_insane.cpp',
  'src/game/sgame/monsters/m_jorg.cpp',
  'src/game/sgame/monsters/m_knight.cpp',
  'src/game/sgame/monsters/m_makron.cpp',
  'src/game/sgame/monsters/m_medic.cpp',
  'src/game/sgame/monsters/m_move.cpp',
  'src/game/sgame/monsters/m_mutant.cpp',
  'src/game/sgame/monsters/m_ogre.cpp',
  'src/game/sgame/monsters/m_overlord.cpp',
  'src/game/sgame/monsters/m_parasite.cpp',
  'src/game/sgame/monsters/m_rotfish.cpp',
  'src/game/sgame/monsters/m_scrag.cpp',
  'src/game/sgame/monsters/m_shambler.cpp',
  'src/game/sgame/monsters/m_shark.cpp',
  'src/game/sgame/monsters/m_shub.cpp',
  'src/game/sgame/monsters/m_soldier.cpp',
  'src/game/sgame/monsters/m_spawn.cpp',
  'src/game/sgame/monsters/m_spike.cpp',
  'src/game/sgame/monsters/m_stalker.cpp',
  'src/game/sgame/monsters/m_supertank.cpp',
  'src/game/sgame/monsters/m_sword.cpp',
  'src/game/sgame/monsters/m_tank.cpp',
  'src/game/sgame/monsters/m_turret.cpp',
  'src/game/sgame/monsters/m_vore.cpp',
  'src/game/sgame/monsters/m_widow.cpp',
  'src/game/sgame/monsters/m_widow2.cpp',
  'src/game/sgame/monsters/m_wrath.cpp',
  'src/game/sgame/monsters/m_wyvern.cpp',
  'src/game/sgame/monsters/m_zombie.cpp',
  'src/game/sgame/monsters/q1_support.cpp',
  'src/game/sgame/player/p_client.cpp',
  'src/game/sgame/player/p_hud_main.cpp',
  'src/game/sgame/player/p_hud_scoreboard.cpp',
  'src/game/sgame/player/p_trail.cpp',
  'src/game/sgame/player/p_view.cpp',
  'src/game/sgame/player/p_weapon.cpp',
  'src/game/sgame/q_std.cpp',
  'src/game/sgame/third_party/qu3e/broadphase/q3BroadPhase.cpp',
  'src/game/sgame/third_party/qu3e/broadphase/q3DynamicAABBTree.cpp',
  'src/game/sgame/third_party/qu3e/collision/q3Box.cpp',
  'src/game/sgame/third_party/qu3e/collision/q3Collide.cpp',
  'src/game/sgame/third_party/qu3e/common/q3Geometry.cpp',
  'src/game/sgame/third_party/qu3e/common/q3Memory.cpp',
  'src/game/sgame/third_party/qu3e/dynamics/q3Body.cpp',
  'src/game/sgame/third_party/qu3e/dynamics/q3Contact.cpp',
  'src/game/sgame/third_party/qu3e/dynamics/q3ContactManager.cpp',
  'src/game/sgame/third_party/qu3e/dynamics/q3ContactSolver.cpp',
  'src/game/sgame/third_party/qu3e/dynamics/q3Island.cpp',
  'src/game/sgame/third_party/qu3e/math/q3Mat3.cpp',
  'src/game/sgame/third_party/qu3e/math/q3Quaternion.cpp',
  'src/game/sgame/third_party/qu3e/math/q3Vec3.cpp',
  'src/game/sgame/third_party/qu3e/scene/q3Scene.cpp',
]

q3a_botlib_utility_src = [
  'src/game/sgame/bots/q3a/q3a_botlib_import.c',
  'src/game/sgame/bots/q3a/game/q_shared.h',
  'src/game/sgame/bots/q3a/game/surfaceflags.h',
  'src/game/sgame/bots/q3a/game/botlib.h',
  'src/game/sgame/bots/q3a/game/be_aas.h',
  'src/game/sgame/bots/q3a/botlib/aasfile.h',
  'src/game/sgame/bots/q3a/botlib/be_aas_bsp.h',
  'src/game/sgame/bots/q3a/botlib/be_aas_cluster.c',
  'src/game/sgame/bots/q3a/botlib/be_aas_cluster.h',
  'src/game/sgame/bots/q3a/botlib/be_aas_debug.c',
  'src/game/sgame/bots/q3a/botlib/be_aas_debug.h',
  'src/game/sgame/bots/q3a/botlib/be_aas_def.h',
  'src/game/sgame/bots/q3a/botlib/be_aas_entity.c',
  'src/game/sgame/bots/q3a/botlib/be_aas_entity.h',
  'src/game/sgame/bots/q3a/botlib/be_aas_file.c',
  'src/game/sgame/bots/q3a/botlib/be_aas_file.h',
  'src/game/sgame/bots/q3a/botlib/be_aas_funcs.h',
  'src/game/sgame/bots/q3a/botlib/be_aas_main.c',
  'src/game/sgame/bots/q3a/botlib/be_aas_main.h',
  'src/game/sgame/bots/q3a/botlib/be_aas_move.c',
  'src/game/sgame/bots/q3a/botlib/be_aas_move.h',
  'src/game/sgame/bots/q3a/botlib/be_aas_optimize.c',
  'src/game/sgame/bots/q3a/botlib/be_aas_optimize.h',
  'src/game/sgame/bots/q3a/botlib/be_aas_reach.c',
  'src/game/sgame/bots/q3a/botlib/be_aas_reach.h',
  'src/game/sgame/bots/q3a/botlib/be_aas_route.c',
  'src/game/sgame/bots/q3a/botlib/be_aas_route.h',
  'src/game/sgame/bots/q3a/botlib/be_aas_routealt.c',
  'src/game/sgame/bots/q3a/botlib/be_aas_routealt.h',
  'src/game/sgame/bots/q3a/botlib/be_aas_sample.c',
  'src/game/sgame/bots/q3a/botlib/be_aas_sample.h',
  'src/game/sgame/bots/q3a/botlib/be_interface.h',
  'src/game/sgame/bots/q3a/botlib/l_crc.c',
  'src/game/sgame/bots/q3a/botlib/l_crc.h',
  'src/game/sgame/bots/q3a/botlib/l_log.h',
  'src/game/sgame/bots/q3a/botlib/l_memory.c',
  'src/game/sgame/bots/q3a/botlib/l_memory.h',
  'src/game/sgame/bots/q3a/botlib/l_libvar.c',
  'src/game/sgame/bots/q3a/botlib/l_libvar.h',
  'src/game/sgame/bots/q3a/botlib/l_precomp.h',
  'src/game/sgame/bots/q3a/botlib/l_script.h',
  'src/game/sgame/bots/q3a/botlib/l_struct.h',
  'src/game/sgame/bots/q3a/botlib/l_utils.h',
]

cgame_src = bgame_src + [
  'src/game/cgame/cg_local.h',
  'src/game/cgame/cg_entity_local.h',
  'src/game/cgame/cg_event_shadow.hpp',
  'src/game/cgame/cg_event_shadow.cpp',
  'src/game/cgame/cg_event_runtime.hpp',
  'src/game/cgame/cg_event_runtime.cpp',
  'src/game/cgame/cg_local_interaction.hpp',
  'src/game/cgame/cg_local_interaction.cpp',
  'src/game/cgame/cg_canonical_snapshot_timeline.hpp',
  'src/game/cgame/cg_canonical_snapshot_timeline.cpp',
  'src/game/cgame/cg_prediction_authority.hpp',
  'src/game/cgame/cg_prediction_authority.cpp',
  'src/game/cgame/cg_main.cpp',
  'src/game/cgame/cg_entity_api.cpp',
  'src/game/cgame/cg_entities.cpp',
  'src/game/cgame/cg_snapshot_timeline.hpp',
  'src/game/cgame/cg_snapshot_timeline.cpp',
  'src/game/cgame/cg_view.cpp',
  'src/game/cgame/cg_predict.cpp',
  'src/game/cgame/cg_effects.cpp',
  'src/game/cgame/cg_newfx.cpp',
  'src/game/cgame/cg_tent.cpp',
  'src/game/cgame/cg_draw.cpp',
  'src/game/cgame/cg_ui_export.cpp',
  'src/game/cgame/cg_ui_sys.cpp',
  'src/game/cgame/cg_fmt.cpp',
  'src/game/cgame/cg_parse.cpp',
  'src/game/cgame/cg_std.cpp',
  'src/game/cgame/cg_wheel.cpp',
]

updater_src = []
updater_worker_src = []
client_launcher_src = []
server_launcher_src = []
updater_deps = []

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

win32 = host_machine.system() == 'windows'
x86 = host_machine.cpu_family() == 'x86'

cpuremap = {
  'x86': win32 ? 'x86' : 'i386',
  'aarch64': 'arm64',
}

cpu = host_machine.cpu_family()
if cpu in cpuremap
  cpu = cpuremap[cpu]
endif

if win32
  sys = cpu.endswith('64') ? 'Win64' : 'Win32'
else
  sys = host_machine.system()
  if sys.endswith('bsd')
    sys = sys.substring(0, -3) + sys.substring(-3).to_upper()
  endif
  sys = sys[0].to_upper() + sys.substring(1)
endif

bindir = '.'
datadir = '.'
libdir = '.'
homedir = ''
default_prefix = ''

system_wide = not win32 and get_option('system-wide')
if system_wide
  bindir = get_option('prefix') / get_option('bindir')
  datadir = get_option('prefix') / get_option('datadir') / meson.project_name()
  libdir = get_option('prefix') / get_option('libdir') / meson.project_name()
  homedir = get_option('homedir')
else
  default_prefix = get_option('prefix')
endif

common_args = ['-DHAVE_CONFIG_H', '-DQ2PROTO_CONFIG_H="common/q2proto_config.h"']
if win32
  common_args += '-D_USE_MATH_DEFINES'
else
  common_args += '-D_GNU_SOURCE'
endif

engine_args = []
engine_c_warn_args = []
if win32
  engine_args += '-D_WIN32_WINNT=0x0601'
endif
engine_args += ['-DREF_GL=1', '-DREF_VKPT=2']

common_link_args = []
exe_link_args = []
dll_link_args = []
win_subsystem_client = 'windows'
win_subsystem_server = 'console'

if cc.get_argument_syntax() == 'gcc'
  if x86
    add_global_arguments('-msse2', '-mfpmath=sse', language: 'c')
  endif

  test_args = [
    '-Werror=vla',
    '-Wformat-security',
    '-Wno-microsoft-anon-tag',
    '-Wpointer-arith',
    '-Wstrict-prototypes',
    '-fms-extensions',
    '-fno-math-errno',
    '-fno-trapping-math',
    '-fsigned-char',
  ]

  common_args += cc.get_supported_arguments(test_args)
  engine_c_warn_args += cc.get_supported_arguments(['-Wmissing-prototypes'])

  if win32
    common_args += '-D__USE_MINGW_ANSI_STDIO=1'
    common_args += '-D_CRT_SECURE_NO_WARNINGS'
    common_args += '-D_CRT_NONSTDC_NO_WARNINGS'
    if cc.get_id() == 'clang'
      common_link_args += '-fuse-ld=lld'
    endif
    linker_id = cc.get_linker_id()
    if linker_id == 'link' or linker_id == 'lld-link'
      common_link_args += ['-Wl,/NXCOMPAT', '-Wl,/DYNAMICBASE']
      if cpu == 'x86_64'
        exe_link_args += ['-Wl,/HIGHENTROPYVA', '-Wl,/BASE:0x140000000']
        dll_link_args += ['-Wl,/HIGHENTROPYVA', '-Wl,/BASE:0x180000000']
      endif
    else
      common_link_args += '-Wl,--nxcompat,--dynamicbase'
      if cpu == 'x86_64'
        exe_link_args += '-Wl,--high-entropy-va,--image-base=0x140000000'
        dll_link_args += '-Wl,--high-entropy-va,--image-base=0x180000000'
      endif
      dll_link_args += '-static-libgcc'
    endif
  endif
elif cc.get_id() == 'msvc'
  common_args += ['/wd4146', '/wd4244', '/wd4305', '/D_CRT_SECURE_NO_WARNINGS', '/D_CRT_NONSTDC_NO_WARNINGS', '/wd4267', '/wd4018']
endif

# Propagate CRT deprecation suppressions to subprojects (e.g. HarfBuzz).
if win32
  if cc.get_argument_syntax() == 'msvc'
    add_global_arguments('/D_CRT_SECURE_NO_WARNINGS', '/D_CRT_NONSTDC_NO_WARNINGS', language: ['c', 'cpp'])
  else
    add_global_arguments('-D_CRT_SECURE_NO_WARNINGS', '-D_CRT_NONSTDC_NO_WARNINGS', language: ['c', 'cpp'])
  endif
endif

# Silence known third-party C++ warnings from fallback dependencies on Windows.
if win32 and cpp.get_argument_syntax() == 'gcc'
  add_global_arguments(cpp.get_supported_arguments([
    '-Wno-deprecated-literal-operator',
    '-Wno-invalid-offsetof',
    '-Wno-unused-function',
  ]), language: 'cpp')
endif
if win32 and cc.get_argument_syntax() == 'gcc'
  add_global_arguments(cc.get_supported_arguments(['-Wno-unused-function']), language: 'c')
endif

client_cpp_args = [
  '-DHAVE_CONFIG_H',
  '-DQ2PROTO_CONFIG_H="common/q2proto_config.h"',
  '-DUSE_CLIENT=1',
  '-DUSE_REF=REF_GL',
]
if win32
  client_cpp_args += '-D_USE_MATH_DEFINES'
else
  client_cpp_args += '-D_GNU_SOURCE'
endif
client_cpp_args += engine_args
engine_c_args = engine_args + engine_c_warn_args

if cpp.get_argument_syntax() == 'gcc'
  if x86
    client_cpp_args += cpp.get_supported_arguments(['-msse2', '-mfpmath=sse'])
  endif
  client_cpp_warn_args = [
    '-Wformat-security',
    '-Wno-microsoft-anon-tag',
    '-Wpointer-arith',
    '-fms-extensions',
    '-fno-math-errno',
    '-fno-trapping-math',
    '-fsigned-char',
  ]
  client_cpp_args += cpp.get_supported_arguments(client_cpp_warn_args)

  if win32
    client_cpp_args += ['-D_CRT_SECURE_NO_WARNINGS', '-D_CRT_NONSTDC_NO_WARNINGS']
  endif
elif cpp.get_id() == 'msvc'
  client_cpp_args += ['/D_CRT_SECURE_NO_WARNINGS', '/D_CRT_NONSTDC_NO_WARNINGS']
endif

game_cpp_args = [
  '-DHAVE_CONFIG_H',
  '-DQ2PROTO_CONFIG_H="common/q2proto_config.h"',
  '-DNO_FMT_SOURCE',
  '-DNOMINMAX',
]

game_c_args = [
  '-DHAVE_CONFIG_H',
  '-DQ2PROTO_CONFIG_H="common/q2proto_config.h"',
]

game_link_args = []

if cpp.get_argument_syntax() == 'gcc'
  if x86
    game_cpp_args += cpp.get_supported_arguments(['-msse2', '-mfpmath=sse'])
  endif

  game_warn_args = [
    '-Wformat-security',
    '-Wno-class-memaccess',
    '-Wno-narrowing',
    '-Wno-sign-compare',
    '-Wpointer-arith',
    '-Wno-array-bounds',
  ]

  game_cpp_args += cpp.get_supported_arguments(game_warn_args)

  if win32
    game_cpp_args += ['-D_CRT_SECURE_NO_WARNINGS', '-D_CRT_NONSTDC_NO_WARNINGS']
    linker_id = cpp.get_linker_id()
    if linker_id == 'link' or linker_id == 'lld-link'
      game_link_args += ['-Wl,/NXCOMPAT', '-Wl,/DYNAMICBASE']
      if cpu == 'x86_64'
        game_link_args += ['-Wl,/HIGHENTROPYVA', '-Wl,/BASE:0x180000000']
      endif
    else
      game_link_args += [
        '-Wl,--nxcompat,--dynamicbase',
        '-static-libgcc',
        '-static-libstdc++',
      ]
      if cpu == 'x86_64'
        game_link_args += '-Wl,--high-entropy-va,--image-base=0x180000000'
      endif
    endif
  endif
elif cpp.get_id() == 'msvc'
  game_cpp_args += '/wd4244'

  d = get_option('buildtype') == 'debug' ? 'd' : ''
  game_link_args += f'/nodefaultlib:libucrt@d@.lib'
  game_link_args += f'/defaultlib:ucrt@d@.lib'
endif

if win32 and cc.get_argument_syntax() == 'msvc'
  win_subsystem_client += ',6.0'
  win_subsystem_server += ',6.0'
endif

add_project_arguments(common_args, language: 'c')
add_project_link_arguments(common_link_args, language: 'c')
add_project_link_arguments(common_link_args, language: 'cpp')

if get_option('q2aas')
  subdir('tools/q2aas')
endif

config = configuration_data()

texture_formats = []

fallback_opt = ['default_library=static']
quiet_fallback_opt = fallback_opt + ['warning_level=0', 'c_args=-w', 'cpp_args=-w']

game_fallback_opt = fallback_opt + [
  'b_vscrt=from_buildtype',
  'buildtype=' + get_option('buildtype'),
  'cpp_std=c++17',
]

fmt_dep = dependency('fmt', default_options: game_fallback_opt)
jsoncpp_dep = dependency('jsoncpp', default_options: game_fallback_opt)

zlib = dependency('zlib',
  fallback:        'zlib-ng',
  required:        get_option('zlib'),
  default_options: fallback_opt + [ 'tests=disabled', 'zlib-compat=true', 'sse2=enabled' ],
)

if not zlib.found()
  warning('zlib not found, client will be unable to connect to protocol 35 servers')
endif

png = dependency('libpng',
  version:         '>= 1.6.11',
  required:        get_option('libpng'),
  default_options: fallback_opt,
)
use_stb_png = not png.found()
if png.found() or use_stb_png
  texture_formats += 'png'
endif

config.set10('USE_ZLIB', zlib.found())
config.set10('USE_PNG', png.found())
config.set10('USE_STB_PNG', use_stb_png)

if use_stb_png
  renderer_src += 'src/rend_vk/refresh/stb/stb.c'
endif

curl_required = get_option('libcurl')
if get_option('bootstrapper')
  curl_required = true
endif

if win32
  curl = dependency('libcurl',
    version:         '>= 7.68.0',
    required:        curl_required,
    default_options: fallback_opt,
    fallback:        ['libcurl', 'libcurl_dep'],
  )
else
  curl = dependency('libcurl',
    version:         '>= 7.68.0',
    required:        curl_required,
    default_options: fallback_opt,
  )
endif

if curl.found()
  client_src += 'src/client/http.cpp'
  config.set10('USE_CURL', true)
endif

sdl3_required = get_option('sdl3')
if get_option('bootstrapper')
  sdl3_required = true
endif

sdl3 = dependency('sdl3',
  version:         '>= 3.4.0',
  required:        sdl3_required,
  fallback:        ['sdl3', 'sdl3_dep'],
  default_options: quiet_fallback_opt,
)
if sdl3.found()
  client_src += 'src/unix/video/sdl.c'
  config.set('USE_SDL', 'USE_CLIENT')
endif

freetype_fallback_opt = quiet_fallback_opt + [
  'harfbuzz=disabled',
  'brotli=disabled',
  'bzip2=disabled',
  'png=disabled',
  'zlib=internal',
]
sdl3_ttf_required = get_option('sdl3-ttf')
if get_option('bootstrapper')
  sdl3_ttf_required = true
endif

freetype2 = dependency('freetype2',
  required:        sdl3_ttf_required,
  fallback:        ['freetype2', 'freetype_dep'],
  default_options: freetype_fallback_opt,
)

harfbuzz_fallback_opt = quiet_fallback_opt + [
  'cpp_std=c++14',
  'cpp_args=-w -D_CRT_SECURE_NO_WARNINGS',
  'glib=disabled',
  'gobject=disabled',
  'cairo=disabled',
  'chafa=disabled',
  'icu=disabled',
  'graphite2=disabled',
  'freetype=enabled',
  'fontations=disabled',
  'gdi=disabled',
  'directwrite=disabled',
  'coretext=disabled',
  'harfrust=disabled',
  'kbts=disabled',
  'wasm=disabled',
  'tests=disabled',
  'utilities=disabled',
  'benchmark=disabled',
  'docs=disabled',
  'introspection=disabled',
]
harfbuzz = dependency('harfbuzz',
  required:        sdl3_ttf_required,
  fallback:        ['harfbuzz', 'libharfbuzz_dep'],
  default_options: harfbuzz_fallback_opt,
)

sdl3_ttf = dependency('sdl3_ttf',
  required:        sdl3_ttf_required,
  fallback:        ['sdl3_ttf', 'sdl3_ttf_dep'],
  default_options: quiet_fallback_opt,
)
config.set10('USE_SDL3_TTF', sdl3_ttf.found())
config.set10('USE_HARFBUZZ', harfbuzz.found() and sdl3_ttf.found())

openal_fallback_opt = quiet_fallback_opt + ['utils=false']

khr_headers_dep = []
if win32
  khr_headers_dep = [subproject('khr-headers').get_variable('khr_headers_dep')]
endif

vulkan = dependency('vulkan', required: get_option('vulkan'))

common_deps = [zlib]
client_deps = [png, curl, sdl3]
if sdl3_ttf.found()
  client_deps += sdl3_ttf
endif
if freetype2.found()
  client_deps += freetype2
endif
if harfbuzz.found()
  client_deps += harfbuzz
endif

rmlui_runtime = false
rmlui_opt = get_option('rmlui')
if rmlui_opt.allowed()
  rmlui_dep = dependency('RmlUi',
    method:   'cmake',
    modules:  ['RmlUi::Core'],
    required: false,
    allow_fallback: false,
  )
  if not rmlui_dep.found()
    rmlui_dep = dependency('rmlui',
      required: false,
      allow_fallback: false,
    )
  endif
  if not rmlui_dep.found()
    rmlui_cmake = import('cmake')
    rmlui_cmake_options = rmlui_cmake.subproject_options()
    rmlui_cmake_options.add_cmake_defines({
      'RMLUI_FONT_ENGINE': 'none',
      'RMLUI_LUA_BINDINGS': false,
      'RMLUI_SAMPLES': false,
      'RMLUI_TESTS': false,
    })
    rmlui_subproject = rmlui_cmake.subproject(
      'rmlui',
      options:  rmlui_cmake_options,
      required: rmlui_opt,
    )
    if rmlui_subproject.found()
      rmlui_dep = rmlui_subproject.dependency('rmlui_core')
    endif
  endif
  if rmlui_dep.found()
    client_deps += rmlui_dep
    client_cpp_args += '-DUI_RML_HAS_RUNTIME=1'
    rmlui_runtime = true
  elif rmlui_opt.enabled()
    error('RmlUi dependency was requested, but neither CMake package RmlUi::Core nor pkg-config dependency rmlui was found')
  endif
endif
server_deps = []
game_deps = [fmt_dep, jsoncpp_dep]

vkpt_shader_target = []
if vulkan.found() and cpu.endswith('64')
  vkpt_shader_sources = files(
    'src/rend_rtx/vkpt/shader/animate_materials.comp',
    'src/rend_rtx/vkpt/shader/god_rays_filter.comp',
    'src/rend_rtx/vkpt/shader/god_rays.comp',
    'src/rend_rtx/vkpt/shader/bloom_composite.comp',
    'src/rend_rtx/vkpt/shader/bloom_blur.comp',
    'src/rend_rtx/vkpt/shader/bloom_downscale.comp',
    'src/rend_rtx/vkpt/shader/compositing.comp',
    'src/rend_rtx/vkpt/shader/checkerboard_interleave.comp',
    'src/rend_rtx/vkpt/shader/asvgf_atrous.comp',
    'src/rend_rtx/vkpt/shader/asvgf_gradient_atrous.comp',
    'src/rend_rtx/vkpt/shader/asvgf_gradient_img.comp',
    'src/rend_rtx/vkpt/shader/asvgf_gradient_reproject.comp',
    'src/rend_rtx/vkpt/shader/asvgf_lf.comp',
    'src/rend_rtx/vkpt/shader/asvgf_taau.comp',
    'src/rend_rtx/vkpt/shader/asvgf_temporal.comp',
    'src/rend_rtx/vkpt/shader/instance_geometry.comp',
    'src/rend_rtx/vkpt/shader/normalize_normal_map.comp',
    'src/rend_rtx/vkpt/shader/tone_mapping_histogram.comp',
    'src/rend_rtx/vkpt/shader/tone_mapping_curve.comp',
    'src/rend_rtx/vkpt/shader/tone_mapping_apply.comp',
    'src/rend_rtx/vkpt/shader/physical_sky.comp',
    'src/rend_rtx/vkpt/shader/physical_sky_space.comp',
    'src/rend_rtx/vkpt/shader/shadow_map.vert',
    'src/rend_rtx/vkpt/shader/sky_buffer_resolve.comp',
    'src/rend_rtx/vkpt/shader/rmlui.vert',
    'src/rend_rtx/vkpt/shader/stretch_pic.frag',
    'src/rend_rtx/vkpt/shader/stretch_pic.vert',
    'src/rend_rtx/vkpt/shader/final_blit.frag',
    'src/rend_rtx/vkpt/shader/final_blit.vert',
    'src/rend_rtx/vkpt/shader/fsr_easu_fp16.comp',
    'src/rend_rtx/vkpt/shader/fsr_easu_fp32.comp',
    'src/rend_rtx/vkpt/shader/fsr_rcas_fp16.comp',
    'src/rend_rtx/vkpt/shader/fsr_rcas_fp32.comp',
    'src/rend_rtx/vkpt/shader/debug_line.frag',
    'src/rend_rtx/vkpt/shader/debug_line.vert',
  )

  vkpt_shader_rt_sources = files(
    'src/rend_rtx/vkpt/shader/primary_rays.rgen',
    'src/rend_rtx/vkpt/shader/direct_lighting.rgen',
    'src/rend_rtx/vkpt/shader/indirect_lighting.rgen',
    'src/rend_rtx/vkpt/shader/path_tracer.rchit',
    'src/rend_rtx/vkpt/shader/path_tracer.rmiss',
    'src/rend_rtx/vkpt/shader/path_tracer_masked.rahit',
    'src/rend_rtx/vkpt/shader/path_tracer_particle.rahit',
    'src/rend_rtx/vkpt/shader/path_tracer_sprite.rahit',
    'src/rend_rtx/vkpt/shader/path_tracer_beam.rahit',
    'src/rend_rtx/vkpt/shader/path_tracer_beam.rint',
    'src/rend_rtx/vkpt/shader/path_tracer_explosion.rahit',
    'src/rend_rtx/vkpt/shader/reflect_refract.rgen',
  )

  vkpt_shader_includes = files(
    'src/rend_rtx/vkpt/shader/asvgf.glsl',
    'src/rend_rtx/vkpt/shader/brdf.glsl',
    'src/rend_rtx/vkpt/shader/fsr_easu.glsl',
    'src/rend_rtx/vkpt/shader/fsr_rcas.glsl',
    'src/rend_rtx/vkpt/shader/fsr_utils.glsl',
    'src/rend_rtx/vkpt/shader/path_tracer_transparency.glsl',
    'src/rend_rtx/vkpt/shader/precomputed_sky.glsl',
    'src/rend_rtx/vkpt/shader/projection.glsl',
    'src/rend_rtx/vkpt/shader/tone_mapping_utils.glsl',
    'src/rend_rtx/vkpt/shader/utils.glsl',
    'src/rend_rtx/vkpt/shader/water.glsl',
    'src/rend_rtx/vkpt/shader/constants.h',
    'src/rend_rtx/vkpt/shader/global_textures.h',
    'src/rend_rtx/vkpt/shader/global_ubo.h',
    'src/rend_rtx/vkpt/shader/god_rays_shared.h',
    'src/rend_rtx/vkpt/shader/light_lists.h',
    'src/rend_rtx/vkpt/shader/path_tracer_hit_shaders.h',
    'src/rend_rtx/vkpt/shader/path_tracer_rgen.h',
    'src/rend_rtx/vkpt/shader/path_tracer.h',
    'src/rend_rtx/vkpt/shader/precomputed_sky_params.h',
    'src/rend_rtx/vkpt/shader/shader_structs.h',
    'src/rend_rtx/vkpt/shader/sky.h',
    'src/rend_rtx/vkpt/shader/tiny_encryption_algorithm.h',
    'src/rend_rtx/vkpt/shader/vertex_buffer.h',
    'src/rend_rtx/vkpt/fsr/ffx_a.h',
    'src/rend_rtx/vkpt/fsr/ffx_fsr1.h',
  )

  vkpt_shader_out_dir = meson.current_build_dir() / get_option('base-game') / 'shader_vkpt'
  prog_python = find_program('python3')
  prog_compile_vkpt_shaders = meson.current_source_dir() / 'tools' / 'compile_vkpt_shaders.py'

  vkpt_shader_target = custom_target(
    'vkpt_shaders',
    input: vkpt_shader_sources + vkpt_shader_rt_sources + vkpt_shader_includes,
    output: 'vkpt_shaders.stamp',
    command: [
      prog_python,
      prog_compile_vkpt_shaders,
      '--out-dir', vkpt_shader_out_dir,
      '--stamp', '@OUTPUT@'
    ],
    build_by_default: true,
  )
endif

jpeg = dependency('libjpeg',
  required:        get_option('libjpeg'),
  default_options: fallback_opt + [ 'jpeg-turbo=disabled', 'tests=disabled' ]
)
if jpeg.found()
  if jpeg.type_name() == 'internal'
    has_rgba = true
  else
    has_rgba = cc.has_header_symbol('jpeglib.h', 'JCS_EXT_RGBA',
      prefix:       '#include <stdio.h>',
      dependencies: jpeg,
      required:     get_option('libjpeg'))
  endif
  if has_rgba
    client_deps += jpeg
    texture_formats += 'jpg'
    config.set10('USE_JPG', true)
  endif
endif

if get_option('tga')
  texture_formats += 'tga'
endif

texture_formats += 'dds'

openal = dependency('openal',
  version:         '>= 1.23.1',
  required:        get_option('openal'),
  fallback:        ['openal-soft', 'openal_dep'],
  default_options: openal_fallback_opt,
)
if openal.found()
  client_src += [ 'src/client/sound/al.cpp', 'src/client/sound/qal.cpp', 'inc/common/jsmn.h' ]
  client_deps += openal
  config.set10('USE_OPENAL', true)
  # Work around OpenAL protected symbol linker error on Fedora/glibc
  if not win32 and cpp.get_argument_syntax() == 'gcc'
    client_cpp_args += cpp.get_supported_arguments(['-mno-direct-extern-access'])
  endif
endif
client_link_args = exe_link_args

# require FFmpeg >= 5.1.3
ffmpeg_defaults = [
  'tests=disabled',
  'programs=disabled',
  'devices=disabled',
  'network=disabled',

  'avdevice=disabled',
  'avfilter=disabled',
  'postproc=disabled',

  # Start with a very small set of features, and bit by bit add what we need
  'protocols=disabled',
  'file_protocol=enabled',

  'hwaccels=disabled',
  'bsfs=disabled',

  'parsers=disabled',
  'vp3_parser=enabled',
  'vorbis_parser=enabled',

  'decoders=disabled',
  'idcin_decoder=enabled',
  'theora_decoder=enabled',
  'vorbis_decoder=enabled',
  'pcm_f16le_decoder=enabled',
  'pcm_f24le_decoder=enabled',
  'pcm_f32be_decoder=enabled',
  'pcm_f32le_decoder=enabled',
  'pcm_f64be_decoder=enabled',
  'pcm_f64le_decoder=enabled',
  'pcm_lxf_decoder=enabled',
  'pcm_s8_decoder=enabled',
  'pcm_s8_planar_decoder=enabled',
  'pcm_s16be_decoder=enabled',
  'pcm_s16be_planar_decoder=enabled',
  'pcm_s16le_decoder=enabled',
  'pcm_s16le_planar_decoder=enabled',
  'pcm_s24be_decoder=enabled',
  'pcm_s24daud_decoder=enabled',
  'pcm_s24le_decoder=enabled',
  'pcm_s24le_planar_decoder=enabled',
  'pcm_s32be_decoder=enabled',
  'pcm_s32le_decoder=enabled',
  'pcm_s32le_planar_decoder=enabled',
  'pcm_s64be_decoder=enabled',
  'pcm_s64le_decoder=enabled',
  'pcm_sga_decoder=enabled',
  'pcm_u8_decoder=enabled',
  'pcm_u16be_decoder=enabled',
  'pcm_u16le_decoder=enabled',
  'pcm_u24be_decoder=enabled',
  'pcm_u24le_decoder=enabled',
  'pcm_u32be_decoder=enabled',
  'pcm_u32le_decoder=enabled',
  'pcm_vidc_decoder=enabled',
  'vp3_decoder=enabled',

  'demuxers=disabled',
  'idcin_demuxer=enabled',
  'ogg_demuxer=enabled',
  'wav_demuxer=enabled',

  'encoders=disabled',
  'muxers=disabled',
]
avcodec_opt = get_option('avcodec')
avcodec = dependency('libavcodec', version: '>= 59.37.100', default_options: ffmpeg_defaults, required: avcodec_opt)
avformat = dependency('libavformat', version: '>= 59.27.100', default_options: ffmpeg_defaults, required: avcodec_opt)
avutil = dependency('libavutil', version: '>= 57.28.100', default_options: ffmpeg_defaults, required: avcodec_opt)
swresample = dependency('libswresample', version: '>= 4.7.100', default_options: ffmpeg_defaults, required: avcodec_opt)
swscale = dependency('libswscale', version: '>= 6.7.100', default_options: ffmpeg_defaults, required: avcodec_opt)
if avcodec.found() and avformat.found() and avutil.found() and swresample.found() and swscale.found()
  client_src += ['src/client/cin.cpp', 'src/client/sound/ogg.cpp']
  client_deps += [avcodec, avformat, avutil, swresample, swscale]
  config.set10('USE_AVCODEC', true)
endif

if win32
  subdir('src/windows')
else
  subdir('src/unix')
endif

if get_option('client-gtv')
  client_src += 'src/client/gtv.cpp'
  config.set10('USE_CLIENT_GTV', true)
endif

prog_embed = find_program('embed.py')

# embed localization files for builtin fallback
loc_files = [
  'assets/localization/loc_english.txt',
  'assets/localization/loc_french.txt',
  'assets/localization/loc_german.txt',
  'assets/localization/loc_italian.txt',
  'assets/localization/loc_spanish.txt',
  'assets/localization/loc_russian.txt',
  'assets/localization/loc_arabic.txt',
  'assets/localization/loc_bulgarian.txt',
  'assets/localization/loc_czech.txt',
  'assets/localization/loc_danish.txt',
  'assets/localization/loc_dutch.txt',
  'assets/localization/loc_finnish.txt',
  'assets/localization/loc_norwegian.txt',
  'assets/localization/loc_polish.txt',
  'assets/localization/loc_portuguese_br.txt',
  'assets/localization/loc_portuguese_pt.txt',
  'assets/localization/loc_swedish.txt',
  'assets/localization/loc_turkish.txt',
  'assets/localization/loc_chinese_traditional.txt',
  'assets/localization/loc_chinese_simplified.txt',
  'assets/localization/loc_japanese.txt',
  'assets/localization/loc_korean.txt',
]

foreach loc_file : loc_files
  loc_parts = loc_file.split('/')
  loc_name = loc_parts[loc_parts.length() - 1]
  loc_base = loc_name.replace('.txt', '')
  loc_output = '@0@.c'.format(loc_name)
  loc_target = custom_target(
      loc_base + '_embed',
      output : loc_output,
      input : loc_file,
      command : [prog_embed, '@INPUT@', '@OUTPUT@', 'res_' + loc_base],
  )
  client_src += loc_target
endforeach

if get_option('client-ui')

  # embed worr.json
  worr_json_c = custom_target(
      'worr.json.c',
      output : 'worr.json.c',
      input : 'src/game/cgame/ui/worr.json',
      command : [prog_embed, '@INPUT@', '@OUTPUT@', 'res_worr_json'],
  )
  client_src += worr_json_c

  # embed worr-multiplayer.json
  worr_multiplayer_json_c = custom_target(
      'worr-multiplayer.json.c',
      output : 'worr-multiplayer.json.c',
      input : 'src/game/cgame/ui/worr-multiplayer.json',
      command : [prog_embed, '@INPUT@', '@OUTPUT@', 'res_worr_multiplayer_json'],
  )
  client_src += worr_multiplayer_json_c

  config.set10('USE_UI', true)
endif

if get_option('software-sound').require(win32 or sdl3.found()).allowed()
  client_src += 'src/client/sound/dma.cpp'
  if sdl3.found()
    client_src += 'src/unix/sound/sdl.c'
  endif
  config.set10('USE_SNDDMA', true)
elif not openal.found()
  warning('Neither software sound nor OpenAL enabled, this is not supported')
endif

if get_option('anticheat-server')
  server_src += 'src/server/ac.c'
  config.set('USE_AC_SERVER', 'USE_SERVER')
endif

if get_option('mvd-server')
  common_src += [
    'src/server/mvd.c',
    'inc/server/mvd/client.h',
    'inc/server/mvd/protocol.h'
  ]
  config.set10('USE_MVD_SERVER', true)
endif

if get_option('mvd-client')
  common_src += [
    'src/server/mvd/client.c',
    'src/server/mvd/game.c',
    'src/server/mvd/parse.c'
  ]
  config.set10('USE_MVD_CLIENT', true)
endif

if get_option('save-games')
  common_src += 'src/server/save.c'
  config.set10('USE_SAVEGAMES', true)
endif

if get_option('system-console')
  if not win32
    common_src += 'src/unix/tty.c'
  endif
  config.set10('USE_SYSCON', true)
endif

if get_option('tests')
  common_src += 'src/common/tests.c'
  config.set10('USE_TESTS', true)
endif

renderer_engine_src = renderer_src
renderer_gl_lib = []
renderer_vk_lib = []
renderer_vk_rtx_lib = []

if get_option('external-renderers')
  renderer_engine_src = []

  libm = cc.find_library('m', required: false)
  renderer_deps = []
  if libm.found()
    renderer_deps += libm
  endif
  if zlib.found()
    renderer_deps += zlib
  endif
  if png.found()
    renderer_deps += png
  endif
  if jpeg.found()
    renderer_deps += jpeg
  endif
  renderer_deps += khr_headers_dep

  renderer_c_args = ['-DUSE_CLIENT=1', '-DRENDERER_DLL'] + engine_c_args
  renderer_cpp_args = [
    '-DHAVE_CONFIG_H',
    '-DQ2PROTO_CONFIG_H="common/q2proto_config.h"',
    '-DUSE_CLIENT=1',
    '-DRENDERER_DLL',
  ]
  if win32
    renderer_cpp_args += '-D_USE_MATH_DEFINES'
  else
    renderer_cpp_args += '-D_GNU_SOURCE'
  endif
  renderer_cpp_args += engine_args
  renderer_gl_c_args = renderer_c_args + ['-DUSE_REF=REF_GL']
  renderer_gl_cpp_args = renderer_cpp_args + ['-DUSE_REF=REF_GL']
  renderer_vk_rtx_c_args = renderer_c_args + ['-DUSE_REF=REF_VKPT', '-DRENDERER_VULKAN', '-DRENDERER_VULKAN_RTX=1']
  renderer_vk_rtx_cpp_args = renderer_cpp_args + ['-DUSE_REF=REF_VKPT', '-DRENDERER_VULKAN', '-DRENDERER_VULKAN_RTX=1']
  renderer_vk_c_args = renderer_c_args + ['-DUSE_REF=REF_VKPT', '-DRENDERER_VULKAN', '-DRENDERER_VULKAN_LEGACY=1']
  renderer_vk_cpp_args = renderer_cpp_args + ['-DUSE_REF=REF_VKPT', '-DRENDERER_VULKAN', '-DRENDERER_VULKAN_LEGACY=1']

  renderer_gl_lib_src = renderer_src + ['src/renderer/renderer_api.c']
  renderer_gl_lib_name = meson.project_name() + '_opengl_' + cpu
  renderer_gl_deps = renderer_deps
  if rmlui_runtime
    renderer_gl_deps += rmlui_dep
    renderer_gl_cpp_args += '-DUI_RML_HAS_RUNTIME=1'
  endif
  renderer_gl_lib = shared_library(renderer_gl_lib_name, renderer_gl_lib_src,
    dependencies:          renderer_gl_deps,
    include_directories:   ['inc', 'q2proto/inc'],
    gnu_symbol_visibility: 'hidden',
    link_args:             dll_link_args,
    c_args:                renderer_gl_c_args,
    cpp_args:              renderer_gl_cpp_args,
    name_prefix:           '',
    install:               true,
    install_dir:           libdir,
  )

  if vulkan.found()
    renderer_vk_deps = renderer_deps + [vulkan]
    renderer_vk_rtx_deps = renderer_deps + [vulkan]
    if sdl3.found()
      renderer_vk_deps += sdl3
    endif
    if rmlui_runtime
      renderer_vk_deps += rmlui_dep
      renderer_vk_cpp_args += '-DUI_RML_HAS_RUNTIME=1'
      renderer_vk_rtx_deps += rmlui_dep
      renderer_vk_rtx_cpp_args += '-DUI_RML_HAS_RUNTIME=1'
    endif

    renderer_vk_rtx_lib_src = renderer_vk_rtx_src + ['src/renderer/renderer_api.c']
    renderer_vk_rtx_lib_name = meson.project_name() + '_rtx_' + cpu
    renderer_vk_rtx_lib = shared_library(renderer_vk_rtx_lib_name, renderer_vk_rtx_lib_src,
      dependencies:          renderer_vk_rtx_deps,
      include_directories:   ['inc', 'q2proto/inc', 'src/rend_vk/refresh/stb', 'src/rend_rtx/third_party'],
      gnu_symbol_visibility: 'hidden',
      link_args:             dll_link_args,
      c_args:                renderer_vk_rtx_c_args,
      cpp_args:              renderer_vk_rtx_cpp_args,
      name_prefix:           '',
      install:               true,
      install_dir:           libdir,
    )

    renderer_vk_lib_src = renderer_vk_src + ['src/renderer/renderer_api.c']
    renderer_vk_lib_name = meson.project_name() + '_vulkan_' + cpu
    renderer_vk_lib = shared_library(renderer_vk_lib_name, renderer_vk_lib_src,
      dependencies:          renderer_vk_deps,
      include_directories:   ['inc', 'q2proto/inc', 'src/rend_vk/refresh/stb'],
      gnu_symbol_visibility: 'hidden',
      link_args:             dll_link_args,
      c_args:                renderer_vk_c_args,
      cpp_args:              renderer_vk_cpp_args,
      name_prefix:           '',
      install:               true,
      install_dir:           libdir,
    )
  endif
endif

if get_option('game-abi-hack').require(x86 and cc.get_id() == 'gcc' and cc.has_argument('-mstackrealign')).allowed()
  config.set10('USE_GAME_ABI_HACK', true)
  engine_args += '-mstackrealign'
  engine_c_args += '-mstackrealign'
endif

# Net command canonicalization is part of the wire contract.  Keep its C
# floating-point behavior strict in both engine copies and every proof target.
prediction_core_c_fp_args = []
if cc.get_argument_syntax() == 'gcc'
  prediction_core_c_fp_args += cc.get_supported_arguments([
    '-fno-fast-math',
    '-ffp-contract=off',
  ])
else
  prediction_core_c_fp_args += cc.get_supported_arguments(['/fp:strict'])
endif

prediction_core_fp_args = []
if cpp.get_argument_syntax() == 'gcc'
  prediction_core_fp_args += cpp.get_supported_arguments([
    '-fno-fast-math',
    '-ffp-contract=off',
  ])
else
  prediction_core_fp_args += cpp.get_supported_arguments(['/fp:strict'])
endif

q2proto_c_args = engine_c_args + []
if cc.get_argument_syntax() == 'gcc'
  q2proto_c_args += cc.get_supported_arguments(['-Wno-unused-function'])
endif

q2proto_lib = static_library('q2proto', q2proto_src,
  include_directories: ['inc', 'q2proto/inc'],
  c_args:              q2proto_c_args,
)

canonical_event_math_core_lib = static_library(
  'canonical_event_math_core',
  'src/common/math.c',
  include_directories: include_directories('inc'),
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

networking_math_deps = []
if host_machine.system() != 'windows'
  networking_math_deps += cc.find_library('m', required: true)
endif

canonical_snapshot_core_lib = static_library(
  'canonical_snapshot_core',
  [
    'src/common/net/demo_clock_sideband.c',
    'src/common/net/snapshot_abi.c',
    'src/common/net/snapshot_projection.c',
    'src/common/net/snapshot_recovery.c',
    'src/common/net/snapshot_store.c',
    'src/common/net/snapshot_timeline.c',
  ],
  include_directories: include_directories('inc'),
  c_args: prediction_core_c_fp_args,
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

snapshot_q2proto_core_lib = static_library(
  'snapshot_q2proto_core',
  'src/common/net/snapshot_q2proto.cpp',
  include_directories: include_directories('inc', 'q2proto/inc'),
  c_args: game_c_args + prediction_core_c_fp_args,
  cpp_args: game_cpp_args + prediction_core_fp_args,
  install: false,
  pic: true,
  override_options: ['c_std=c11', 'cpp_std=c++20'],
)

canonical_command_core_lib = static_library(
  'canonical_command_core',
  [
    'src/common/net/adaptive_input.c',
    'src/common/net/command_abi.c',
    'src/common/net/command_canonical.c',
    'src/common/net/command_stream.c',
    'src/common/net/consumed_cursor_sideband.c',
    'src/common/net/legacy_command_adapter.c',
    'src/common/net/prediction_input.c',
  ],
  include_directories: include_directories('inc'),
  c_args: prediction_core_c_fp_args,
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

canonical_local_action_core_lib = static_library(
  'canonical_local_action_core',
  'src/common/net/local_action.c',
  include_directories: include_directories('inc'),
  c_args: prediction_core_c_fp_args,
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

local_action_observation_core_lib = static_library(
  'local_action_observation_core',
  'src/common/net/local_action_observation.c',
  include_directories: include_directories('inc'),
  c_args: prediction_core_c_fp_args,
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

local_interaction_core_lib = static_library(
  'local_interaction_core',
  'src/common/net/local_interaction.c',
  include_directories: include_directories('inc'),
  link_with: [canonical_command_core_lib],
  c_args: prediction_core_c_fp_args,
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

canonical_event_core_lib = static_library(
  'canonical_event_core',
  [
    'src/common/net/event_abi.c',
    'src/common/net/event_journal.c',
    'src/common/net/event_stream.c',
    'src/common/net/legacy_entity_event_candidate.c',
    'src/common/net/legacy_game_event_candidate.c',
    'src/common/net/legacy_muzzle_event_candidate.c',
    'src/common/net/legacy_spatial_audio_event_candidate.c',
    'src/common/net/legacy_temp_event_candidate.c',
  ],
  include_directories: include_directories('inc', 'q2proto/inc'),
  link_with: [canonical_event_math_core_lib, local_interaction_core_lib],
  c_args: prediction_core_c_fp_args,
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

local_action_audit_core_lib = static_library(
  'local_action_audit_core',
  'src/common/net/local_action_audit.c',
  include_directories: include_directories('inc'),
  c_args: prediction_core_c_fp_args,
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

net_capability_core_lib = static_library(
  'net_capability_core',
  'src/common/net/capability.c',
  include_directories: include_directories('inc'),
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

native_readiness_core_lib = static_library(
  'native_readiness_core',
  'src/common/net/native_readiness.c',
  include_directories: include_directories('inc'),
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

native_readiness_sideband_core_lib = static_library(
  'native_readiness_sideband_core',
  'src/common/net/native_readiness_sideband.c',
  include_directories: include_directories('inc'),
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

native_command_shadow_core_lib = static_library(
  'native_command_shadow_core',
  'src/common/net/native_command_shadow.c',
  include_directories: include_directories('inc'),
  c_args: prediction_core_c_fp_args,
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

native_envelope_core_lib = static_library(
  'native_envelope_core',
  'src/common/net/native_envelope.c',
  include_directories: include_directories('inc'),
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

native_carrier_core_lib = static_library(
  'native_carrier_core',
  'src/common/net/native_carrier.c',
  include_directories: include_directories('inc'),
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

native_codec_core_lib = static_library(
  'native_codec_core',
  'src/common/net/native_codec.c',
  include_directories: include_directories('inc'),
  c_args: prediction_core_c_fp_args,
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

native_session_core_lib = static_library(
  'native_session_core',
  'src/common/net/native_session.c',
  include_directories: include_directories('inc'),
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

native_carrier_session_core_lib = static_library(
  'native_carrier_session_core',
  'src/common/net/native_carrier_session.c',
  include_directories: include_directories('inc'),
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

native_carrier_ack_core_lib = static_library(
  'native_carrier_ack_core',
  'src/common/net/native_carrier_ack.c',
  include_directories: include_directories('inc'),
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

native_carrier_mixed_core_lib = static_library(
  'native_carrier_mixed_core',
  'src/common/net/native_carrier_mixed.c',
  include_directories: include_directories('inc'),
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

native_event_sender_core_lib = static_library(
  'native_event_sender_core',
  'src/common/net/native_event_sender.c',
  include_directories: include_directories('inc'),
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

native_event_admission_core_lib = static_library(
  'native_event_admission_core',
  [
    'src/common/net/event_stream_owner.c',
    'src/common/net/native_event_admission.c',
  ],
  include_directories: include_directories('inc'),
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

native_snapshot_admission_core_lib = static_library(
  'native_snapshot_admission_core',
  'src/common/net/native_snapshot_admission.c',
  include_directories: include_directories('inc'),
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

native_snapshot_receiver_core_lib = static_library(
  'native_snapshot_receiver_core',
  'src/common/net/native_snapshot_receiver.c',
  include_directories: include_directories('inc'),
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

native_snapshot_sender_core_lib = static_library(
  'native_snapshot_sender_core',
  'src/common/net/native_snapshot_sender.c',
  include_directories: include_directories('inc'),
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

canonical_rewind_core_lib = static_library(
  'canonical_rewind_core',
  [
    'src/common/net/rewind.c',
    'src/common/net/rewind_observation.c',
  ],
  include_directories: include_directories('inc'),
  c_args: prediction_core_c_fp_args,
  install: false,
  pic: true,
  override_options: ['c_std=c11'],
)

bootstrapper_requested = get_option('bootstrapper')
bootstrapper_enabled = bootstrapper_requested
if bootstrapper_enabled and not curl.found()
  warning('bootstrapper disabled: libcurl is unavailable in this build environment')
  bootstrapper_enabled = false
endif
if bootstrapper_enabled and not sdl3.found()
  warning('bootstrapper disabled: SDL3 is unavailable in this build environment')
  bootstrapper_enabled = false
endif
if not bootstrapper_enabled
  error('bootstrapper is required because WORR client and dedicated engines now run as shared libraries behind the launcher executables')
endif

client_launcher_binary_name = 'worr_' + cpu
server_launcher_binary_name = 'worr_ded_' + cpu
client_engine_binary_name = 'worr_engine_' + cpu
server_engine_binary_name = 'worr_ded_engine_' + cpu
updater_binary_name = 'worr_updater_' + cpu
sgame_binary_name = 'sgame_' + cpu
cgame_binary_name = 'cgame_' + cpu

shared_library(client_engine_binary_name, common_src, client_src, renderer_engine_src,
  dependencies:          common_deps + client_deps,
  include_directories:   ['inc', 'q2proto/inc'],
  gnu_symbol_visibility: 'hidden',
  link_args:             dll_link_args,
  link_with:             [q2proto_lib, canonical_command_core_lib],
  link_whole:            [canonical_event_core_lib,
                          canonical_snapshot_core_lib,
                          snapshot_q2proto_core_lib,
                          net_capability_core_lib,
                          native_readiness_core_lib,
                          native_readiness_sideband_core_lib,
                          native_command_shadow_core_lib,
                          native_codec_core_lib,
                          native_envelope_core_lib,
                          native_session_core_lib,
                          native_carrier_core_lib,
                          native_carrier_session_core_lib,
                          native_carrier_ack_core_lib,
                          native_carrier_mixed_core_lib,
                          native_event_sender_core_lib,
                          native_event_admission_core_lib,
                          native_snapshot_admission_core_lib,
                          native_snapshot_receiver_core_lib,
                          native_snapshot_sender_core_lib,
                          canonical_rewind_core_lib],
  c_args:                ['-DUSE_CLIENT=1', '-DUSE_REF=REF_GL', engine_c_args, prediction_core_c_fp_args],
  cpp_args:              client_cpp_args,
  name_prefix:           '',
  override_options:      ['cpp_std=c++20'],
  install:               true,
  install_dir:           bindir,
)

shared_library(server_engine_binary_name, common_src, server_src,
  dependencies:          common_deps + server_deps + networking_math_deps,
  include_directories:   ['inc', 'q2proto/inc'],
  gnu_symbol_visibility: 'hidden',
  link_args:             dll_link_args,
  link_with:             [q2proto_lib, canonical_command_core_lib],
  link_whole:            [canonical_event_core_lib,
                          net_capability_core_lib,
                          native_readiness_core_lib,
                          native_readiness_sideband_core_lib,
                          native_command_shadow_core_lib,
                          native_envelope_core_lib,
                          native_carrier_core_lib,
                          native_codec_core_lib,
                          native_session_core_lib,
                          native_carrier_session_core_lib,
                          native_carrier_ack_core_lib,
                          native_carrier_mixed_core_lib,
                          native_event_sender_core_lib,
                          native_event_admission_core_lib,
                          native_snapshot_admission_core_lib,
                          native_snapshot_sender_core_lib,
                          canonical_rewind_core_lib,
                          canonical_snapshot_core_lib,
                          snapshot_q2proto_core_lib],
  c_args:                ['-DUSE_SERVER=1', engine_c_args, prediction_core_c_fp_args],
  name_prefix:           '',
  install:               true,
  install_dir:           bindir,
)

if bootstrapper_enabled
  bootstrap_logo_h = custom_target('bootstrap_logo_rgba',
    input:  'assets/art/logo.png',
    output: 'bootstrap_logo_rgba.h',
    command: [find_program('python3'), files('tools/generate_png_rgba_header.py'), '@INPUT@', '@OUTPUT@'],
  )

  updater_src += [
    bootstrap_logo_h,
    'src/updater/bootstrap.cpp',
    'src/updater/third_party/miniz.c',
    'src/updater/third_party/miniz_tdef.c',
    'src/updater/third_party/miniz_tinfl.c',
    'src/updater/third_party/miniz_zip.c',
  ]

  updater_c_args = [
    '-DHAVE_CONFIG_H',
    '-DMINIZ_NO_ARCHIVE_WRITING_APIS',
    '-DMINIZ_NO_ZLIB_COMPATIBLE_NAMES',
  ]
  updater_cpp_args = updater_c_args + []
  updater_link_args = exe_link_args
  if cc.get_argument_syntax() == 'gcc'
    updater_c_args += cc.get_supported_arguments([
      '-Wno-unused-function',
    ])
    updater_cpp_args += cpp.get_supported_arguments([
      '-Wno-unused-function',
    ])
    if win32
      updater_c_args += cc.get_supported_arguments(['-municode'])
      updater_link_args += ['-municode']
    endif
  endif

  updater_common_deps = updater_deps + [curl, jsoncpp_dep, sdl3, sdl3_ttf]
  updater_include_dirs = ['.', 'inc', 'src/updater/third_party']

  executable(client_launcher_binary_name, client_launcher_src + updater_src + ['src/updater/client_launcher_main.cpp'],
    dependencies:        updater_common_deps,
    include_directories: updater_include_dirs,
    win_subsystem:       win_subsystem_client,
    link_args:           updater_link_args,
    c_args:              updater_c_args,
    cpp_args:            updater_cpp_args,
    install:             true,
    install_dir:         bindir,
  )

  executable(server_launcher_binary_name, server_launcher_src + updater_src + ['src/updater/server_launcher_main.cpp'],
    dependencies:        updater_common_deps,
    include_directories: updater_include_dirs,
    win_subsystem:       win_subsystem_server,
    link_args:           updater_link_args,
    c_args:              updater_c_args,
    cpp_args:            updater_cpp_args,
    install:             true,
    install_dir:         bindir,
  )

  executable(updater_binary_name, updater_worker_src + updater_src + ['src/updater/worker_main.cpp'],
    dependencies:        updater_common_deps,
    include_directories: updater_include_dirs,
    win_subsystem:       win_subsystem_client,
    link_args:           updater_link_args,
    c_args:              updater_c_args,
    cpp_args:            updater_cpp_args,
    install:             true,
    install_dir:         bindir,
  )
endif

sgame_inc = include_directories('inc', 'src/game/sgame', 'src/game/bgame')
cgame_inc = include_directories('src/game/cgame', 'src/game/bgame')

cgame_defines = [
  '-DUSE_CLIENT=1',
  '-DUSE_REF=1',
  '-DUSE_UI=1',
  '-DRENDERER_DLL',
]

# MSVC: use c++latest (c++23 not in Meson's MSVC list). GCC/Clang: use c++23 (c++latest unsupported).
sgame_cpp_std = cpp.get_id() == 'msvc' ? 'c++latest' : 'c++23'

q3a_botlib_utility_c_args = game_c_args + [
  '-DQ3A_BOTLIB_WORR_BOUNDARY=1',
  '-DMEMORYMANEGER=1',
]
if cc.get_argument_syntax() == 'gcc'
  if host_machine.system() == 'windows'
    q3a_botlib_utility_c_args += ['-DWIN32']
  else
    q3a_botlib_utility_c_args += ['-DID_INLINE=inline']
  endif
  q3a_botlib_utility_c_args += cc.get_supported_arguments([
    '-Wno-absolute-value',
    '-Wno-missing-prototypes',
    '-Wno-return-type',
    '-Wno-sign-compare',
    '-Wno-strict-prototypes',
    '-Wno-unused-but-set-variable',
    '-Wno-unused-parameter',
  ])
else
  if host_machine.system() == 'windows'
    q3a_botlib_utility_c_args += ['/DWIN32']
  else
    q3a_botlib_utility_c_args += ['/DID_INLINE=inline']
  endif
  if cc.get_id() == 'msvc'
    q3a_botlib_utility_c_args += ['/wd4018', '/wd4100', '/wd4244']
  endif
endif

q3a_botlib_utility_lib = static_library('q3a_botlib_utility',
  q3a_botlib_utility_src,
  include_directories: include_directories('src/game/sgame/bots/q3a'),
  c_args:              q3a_botlib_utility_c_args,
  install:             false,
  pic:                 true,
)

prediction_core_lib = static_library('prediction_core',
  prediction_core_src,
  dependencies:        game_deps,
  include_directories: include_directories('inc', 'src/game/sgame', 'src/game/bgame'),
  c_args:               game_c_args + prediction_core_c_fp_args,
  cpp_args:             game_cpp_args + prediction_core_fp_args,
  install:              false,
  pic:                  true,
  override_options:     ['b_vscrt=from_buildtype', 'cpp_std=c++20'],
)

sgame_dll = shared_library(sgame_binary_name, sgame_src,
  name_prefix:           '',
  gnu_symbol_visibility: 'hidden',
  dependencies:          game_deps + networking_math_deps,
  include_directories:   sgame_inc,
  c_args:                game_c_args,
  cpp_args:              game_cpp_args,
  link_with:             [canonical_command_core_lib,
                          canonical_snapshot_core_lib,
                          canonical_event_core_lib,
                          local_action_observation_core_lib,
                          local_interaction_core_lib],
  link_whole:            [q3a_botlib_utility_lib, prediction_core_lib,
                          canonical_rewind_core_lib],
  link_args:             dll_link_args + game_link_args,
  install:              true,
  install_dir:          libdir / get_option('base-game'),
  install_tag:          'runtime',
  override_options:      ['b_vscrt=from_buildtype', 'cpp_std=' + sgame_cpp_std],
)

cgame_all_src = cgame_src + ui_src + [
  'src/common/field.c',
  'src/common/jsmn.c',
  'src/common/math.c',
  'src/common/mapdb.c',
  'src/common/mdfour.c',
  'src/common/net/cgame_event_shadow.c',
  'src/common/utf8.c',
  'src/shared/m_flash.c',
  'src/shared/shared.c',
]

if zlib.found()
  cgame_deps = game_deps + [zlib]
else
  cgame_deps = game_deps
endif

cgame_inc = [
  include_directories('inc', 'q2proto/inc'),
  cgame_inc,
]

cgame_dll = shared_library(cgame_binary_name, cgame_all_src,
  name_prefix:           '',
  gnu_symbol_visibility: 'hidden',
  dependencies:          cgame_deps + networking_math_deps,
  include_directories:   cgame_inc,
  c_args:                game_c_args + cgame_defines,
  cpp_args:              game_cpp_args + cgame_defines,
  link_whole:            [prediction_core_lib, canonical_event_core_lib,
                          canonical_snapshot_core_lib,
                          local_interaction_core_lib],
  link_args:             dll_link_args + game_link_args,
  install:              true,
  install_dir:          libdir / get_option('base-game'),
  install_tag:          'runtime',
  override_options:     ['b_vscrt=from_buildtype', 'cpp_std=c++20'],
)

prog_copy_game_dll = find_program('copy-game-dll.py')
custom_target(
    'copy_sgame_dll',
    build_by_default: true,
    input : sgame_dll,
    output: '@PLAINNAME@.stamp',
    command : [prog_copy_game_dll, '@OUTPUT@', '@INPUT@', '--copy-dir', meson.current_build_dir() / get_option('base-game'), '--stamp'],
)
custom_target(
    'copy_cgame_dll',
    build_by_default: true,
    input : cgame_dll,
    output: '@PLAINNAME@.stamp',
    command : [prog_copy_game_dll, '@OUTPUT@', '@INPUT@', '--copy-dir', meson.current_build_dir() / get_option('base-game'), '--stamp'],
)

version_prog = find_program('python3')

network_impair_model_test = executable(
  'network_impair_model_test',
  [
    'tools/networking/net_impair_model_test.c',
    'src/common/net/impair.c',
    'src/common/net/impair_queue.c',
    'src/common/net/sequence.c',
  ],
  include_directories: include_directories('inc'),
  install: false,
)

event_journal_test = executable(
  'event_journal_test',
  'tools/networking/event_journal_test.c',
  include_directories: include_directories('inc'),
  link_with: canonical_event_core_lib,
  install: false,
  override_options: ['c_std=c11'],
)

event_stream_test = executable(
  'event_stream_test',
  'tools/networking/event_stream_test.c',
  include_directories: include_directories('inc'),
  link_with: [native_codec_core_lib,
              native_envelope_core_lib,
              canonical_command_core_lib,
              canonical_event_core_lib,
              canonical_snapshot_core_lib],
  install: false,
  override_options: ['c_std=c11'],
)

event_stream_layout_c_test = executable(
  'event_stream_layout_c_test',
  'tools/networking/event_stream_layout_c.c',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['c_std=c11'],
)

event_stream_layout_cpp_test = executable(
  'event_stream_layout_cpp_test',
  'tools/networking/event_stream_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

event_stream_owner_test = executable(
  'event_stream_owner_test',
  'tools/networking/event_stream_owner_test.c',
  include_directories: include_directories('inc'),
  link_with: [native_event_admission_core_lib,
              canonical_event_core_lib],
  install: false,
  override_options: ['c_std=c11'],
)

native_event_admission_test = executable(
  'native_event_admission_test',
  'tools/networking/native_event_admission_test.c',
  dependencies: networking_math_deps,
  include_directories: include_directories('inc'),
  link_with: [native_event_admission_core_lib,
              native_carrier_ack_core_lib,
              native_carrier_session_core_lib,
              native_carrier_core_lib,
              native_session_core_lib,
              native_readiness_core_lib,
              net_capability_core_lib,
              native_codec_core_lib,
              native_envelope_core_lib,
              canonical_command_core_lib,
              canonical_event_core_lib,
              canonical_snapshot_core_lib],
  install: false,
  override_options: ['c_std=c11'],
)

native_snapshot_admission_test = executable(
  'native_snapshot_admission_test',
  'tools/networking/native_snapshot_admission_test.c',
  dependencies: networking_math_deps,
  include_directories: include_directories('inc'),
  link_with: [native_snapshot_admission_core_lib,
              native_carrier_ack_core_lib,
              native_carrier_session_core_lib,
              native_carrier_core_lib,
              native_session_core_lib,
              native_readiness_core_lib,
              net_capability_core_lib,
              native_codec_core_lib,
              native_envelope_core_lib,
              canonical_command_core_lib,
              canonical_event_core_lib,
              canonical_snapshot_core_lib],
  install: false,
  override_options: ['c_std=c11'],
)

native_snapshot_admission_layout_c_test = executable(
  'native_snapshot_admission_layout_c_test',
  'tools/networking/native_snapshot_admission_layout_c.c',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['c_std=c11'],
)

native_snapshot_admission_layout_cpp_test = executable(
  'native_snapshot_admission_layout_cpp_test',
  'tools/networking/native_snapshot_admission_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

native_snapshot_receiver_test = executable(
  'native_snapshot_receiver_test',
  'tools/networking/native_snapshot_receiver_test.c',
  dependencies: networking_math_deps,
  include_directories: include_directories('inc'),
  link_with: [native_snapshot_receiver_core_lib,
              native_snapshot_admission_core_lib,
              native_carrier_ack_core_lib,
              native_carrier_session_core_lib,
              native_carrier_core_lib,
              native_session_core_lib,
              native_readiness_core_lib,
              net_capability_core_lib,
              native_codec_core_lib,
              native_envelope_core_lib,
              canonical_command_core_lib,
              canonical_event_core_lib,
              canonical_snapshot_core_lib],
  install: false,
  override_options: ['c_std=c11'],
)

native_event_sender_test = executable(
  'native_event_sender_test',
  'tools/networking/native_event_sender_test.c',
  dependencies: networking_math_deps,
  include_directories: include_directories('inc'),
  link_with: [native_event_sender_core_lib,
              native_carrier_mixed_core_lib,
              native_carrier_ack_core_lib,
              native_carrier_session_core_lib,
              native_carrier_core_lib,
              native_session_core_lib,
              native_readiness_core_lib,
              net_capability_core_lib,
              native_codec_core_lib,
              native_envelope_core_lib,
              canonical_command_core_lib,
              canonical_event_core_lib,
              canonical_snapshot_core_lib],
  install: false,
  override_options: ['c_std=c11'],
)

native_snapshot_sender_test = executable(
  'native_snapshot_sender_test',
  'tools/networking/native_snapshot_sender_test.c',
  dependencies: networking_math_deps,
  include_directories: include_directories('inc'),
  link_with: [native_snapshot_sender_core_lib,
              native_carrier_mixed_core_lib,
              native_carrier_ack_core_lib,
              native_carrier_session_core_lib,
              native_carrier_core_lib,
              native_session_core_lib,
              native_readiness_core_lib,
              net_capability_core_lib,
              native_codec_core_lib,
              native_envelope_core_lib,
              canonical_command_core_lib,
              canonical_event_core_lib,
              canonical_snapshot_core_lib],
  install: false,
  override_options: ['c_std=c11'],
)

event_shadow_test = executable(
  'event_shadow_test',
  [
    'tools/networking/event_shadow_test.c',
    'src/common/net/event_shadow_map.c',
    'src/server/event_shadow.c',
  ],
  include_directories: include_directories('inc'),
  c_args: ['-DWORR_EVENT_SHADOW_TESTING=1'],
  link_with: canonical_event_core_lib,
  install: false,
  override_options: ['c_std=c11'],
)

local_interaction_authority_test = executable(
  'local_interaction_authority_test',
  [
    'tools/networking/local_interaction_authority_test.c',
    'src/server/local_interaction_authority.c',
  ],
  include_directories: include_directories('inc'),
  link_with: local_interaction_core_lib,
  install: false,
  override_options: ['c_std=c11'],
)

cgame_event_shadow_test = executable(
  'cgame_event_shadow_test',
  [
    'tools/networking/cgame_event_shadow_test.c',
    'src/common/net/cgame_event_shadow.c',
  ],
  include_directories: include_directories('inc'),
  link_with: canonical_event_core_lib,
  install: false,
  override_options: ['c_std=c11'],
)

legacy_temp_event_candidate_test = executable(
  'legacy_temp_event_candidate_test',
  'tools/networking/legacy_temp_event_candidate_test.c',
  include_directories: include_directories('inc', 'q2proto/inc'),
  link_with: canonical_event_core_lib,
  install: false,
  override_options: ['c_std=c11'],
)

legacy_muzzle_event_candidate_test = executable(
  'legacy_muzzle_event_candidate_test',
  'tools/networking/legacy_muzzle_event_candidate_test.c',
  include_directories: include_directories('inc', 'q2proto/inc'),
  link_with: canonical_event_core_lib,
  install: false,
  override_options: ['c_std=c11'],
)

legacy_game_event_candidate_test = executable(
  'legacy_game_event_candidate_test',
  'tools/networking/legacy_game_event_candidate_test.c',
  include_directories: include_directories('inc', 'q2proto/inc'),
  link_with: canonical_event_core_lib,
  install: false,
  override_options: ['c_std=c11'],
)

legacy_spatial_audio_event_candidate_test = executable(
  'legacy_spatial_audio_event_candidate_test',
  'tools/networking/legacy_spatial_audio_event_candidate_test.c',
  include_directories: include_directories('inc', 'q2proto/inc'),
  link_with: canonical_event_core_lib,
  install: false,
  override_options: ['c_std=c11'],
)

cgame_event_presentation_test = executable(
  'cgame_event_presentation_test',
  [
    'tools/networking/cgame_event_presentation_test.cpp',
    'src/game/cgame/cg_event_shadow.cpp',
    'src/game/cgame/cg_event_runtime.cpp',
    'src/game/cgame/cg_local_interaction.cpp',
    'src/common/net/cgame_event_shadow.c',
  ],
  include_directories: include_directories('inc', 'src/game/cgame'),
  link_with: [canonical_event_core_lib, canonical_snapshot_core_lib,
              local_interaction_core_lib, canonical_command_core_lib],
  install: false,
  override_options: ['c_std=c11', 'cpp_std=c++20'],
)

cgame_local_interaction_test = executable(
  'cgame_local_interaction_test',
  [
    'tools/networking/cgame_local_interaction_test.cpp',
    'src/game/cgame/cg_local_interaction.cpp',
  ],
  include_directories: include_directories('inc', 'src/game/cgame'),
  link_with: [local_interaction_core_lib, canonical_command_core_lib],
  install: false,
  override_options: ['c_std=c11', 'cpp_std=c++20'],
)

cgame_event_runtime_test = executable(
  'cgame_event_runtime_test',
  [
    'tools/networking/cgame_event_runtime_test.cpp',
    'src/game/cgame/cg_event_shadow.cpp',
    'src/game/cgame/cg_event_runtime.cpp',
    'src/game/cgame/cg_local_interaction.cpp',
    'src/common/net/cgame_event_shadow.c',
  ],
  include_directories: include_directories('inc', 'src/game/cgame'),
  link_with: [canonical_event_core_lib, canonical_snapshot_core_lib,
              local_interaction_core_lib, canonical_command_core_lib],
  install: false,
  override_options: ['c_std=c11', 'cpp_std=c++20'],
)

cgame_event_runtime_export_test = executable(
  'cgame_event_runtime_export_test',
  [
    'tools/networking/cgame_event_runtime_export_test.cpp',
    'src/game/cgame/cg_event_shadow.cpp',
    'src/game/cgame/cg_event_runtime.cpp',
    'src/game/cgame/cg_local_interaction.cpp',
    'src/common/net/cgame_event_shadow.c',
  ],
  include_directories: include_directories('inc', 'src/game/cgame'),
  link_with: [canonical_event_core_lib, canonical_snapshot_core_lib,
              local_interaction_core_lib, canonical_command_core_lib],
  install: false,
  override_options: ['c_std=c11', 'cpp_std=c++20'],
)

cgame_event_runtime_owner_test = executable(
  'cgame_event_runtime_owner_test',
  [
    'tools/networking/cgame_event_runtime_owner_test.cpp',
    'src/client/cgame_event_runtime.cpp',
  ],
  include_directories: include_directories('inc'),
  link_with: canonical_event_core_lib,
  install: false,
  override_options: ['cpp_std=c++20'],
)

cgame_event_runtime_layout_c_test = executable(
  'cgame_event_runtime_layout_c_test',
  'tools/networking/cgame_event_runtime_layout_c.c',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['c_std=c11'],
)

cgame_event_runtime_layout_cpp_test = executable(
  'cgame_event_runtime_layout_cpp_test',
  'tools/networking/cgame_event_runtime_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

event_schema_layout_c_test = executable(
  'event_schema_layout_c_test',
  'tools/networking/event_schema_layout_c.c',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['c_std=c11'],
)

event_schema_layout_cpp_test = executable(
  'event_schema_layout_cpp_test',
  'tools/networking/event_schema_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

snapshot_store_test = executable(
  'snapshot_store_test',
  'tools/networking/snapshot_store_test.c',
  include_directories: include_directories('inc'),
  link_with: [canonical_snapshot_core_lib, canonical_event_core_lib],
  install: false,
  override_options: ['c_std=c11'],
)

snapshot_schema_layout_c_test = executable(
  'snapshot_schema_layout_c_test',
  'tools/networking/snapshot_schema_layout_c.c',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['c_std=c11'],
)

snapshot_schema_layout_cpp_test = executable(
  'snapshot_schema_layout_cpp_test',
  'tools/networking/snapshot_schema_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

snapshot_recovery_test = executable(
  'snapshot_recovery_test',
  'tools/networking/snapshot_recovery_test.c',
  include_directories: include_directories('inc'),
  link_with: canonical_snapshot_core_lib,
  install: false,
  override_options: ['c_std=c11'],
)

snapshot_recovery_layout_c_test = executable(
  'snapshot_recovery_layout_c_test',
  'tools/networking/snapshot_recovery_layout_c.c',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['c_std=c11'],
)

snapshot_recovery_layout_cpp_test = executable(
  'snapshot_recovery_layout_cpp_test',
  'tools/networking/snapshot_recovery_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

snapshot_shadow_schema_layout_c_test = executable(
  'snapshot_shadow_schema_layout_c_test',
  'tools/networking/snapshot_shadow_schema_layout_c.c',
  include_directories: include_directories('inc', 'q2proto/inc'),
  c_args: game_c_args,
  install: false,
  override_options: ['c_std=c11'],
)

snapshot_shadow_schema_layout_cpp_test = executable(
  'snapshot_shadow_schema_layout_cpp_test',
  'tools/networking/snapshot_shadow_schema_layout_cpp.cpp',
  include_directories: include_directories('inc', 'q2proto/inc'),
  cpp_args: game_cpp_args,
  install: false,
  override_options: ['cpp_std=c++20'],
)

snapshot_timeline_test = executable(
  'snapshot_timeline_test',
  'tools/networking/snapshot_timeline_test.c',
  include_directories: include_directories('inc'),
  dependencies: networking_math_deps,
  c_args: prediction_core_c_fp_args,
  link_with: [canonical_snapshot_core_lib, canonical_event_core_lib],
  install: false,
  override_options: ['c_std=c11'],
)

snapshot_timeline_schema_layout_c_test = executable(
  'snapshot_timeline_schema_layout_c_test',
  'tools/networking/snapshot_timeline_schema_layout_c.c',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['c_std=c11'],
)

snapshot_timeline_schema_layout_cpp_test = executable(
  'snapshot_timeline_schema_layout_cpp_test',
  'tools/networking/snapshot_timeline_schema_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

rewind_core_test = executable(
  'rewind_core_test',
  'tools/networking/rewind_core_test.c',
  include_directories: include_directories('inc'),
  dependencies: networking_math_deps,
  c_args: prediction_core_c_fp_args,
  link_with: [
    canonical_rewind_core_lib,
    canonical_command_core_lib,
    canonical_snapshot_core_lib,
    canonical_event_core_lib,
  ],
  install: false,
  override_options: ['c_std=c11'],
)

rewind_observation_test = executable(
  'rewind_observation_test',
  'tools/networking/rewind_observation_test.c',
  include_directories: include_directories('inc'),
  dependencies: networking_math_deps,
  c_args: prediction_core_c_fp_args,
  link_with: [
    canonical_rewind_core_lib,
    canonical_command_core_lib,
    canonical_snapshot_core_lib,
    canonical_event_core_lib,
  ],
  install: false,
  override_options: ['c_std=c11'],
)

rewind_observation_layout_c_test = executable(
  'rewind_observation_layout_c_test',
  'tools/networking/rewind_observation_layout_c.c',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['c_std=c11'],
)

rewind_observation_layout_cpp_test = executable(
  'rewind_observation_layout_cpp_test',
  'tools/networking/rewind_observation_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

rewind_acceptance_probe = executable(
  'rewind_acceptance_probe',
  'tools/networking/rewind_acceptance_probe.c',
  include_directories: include_directories('inc'),
  dependencies: networking_math_deps,
  c_args: prediction_core_c_fp_args,
  link_with: [
    canonical_rewind_core_lib,
    canonical_command_core_lib,
    canonical_snapshot_core_lib,
    canonical_event_core_lib,
  ],
  install: false,
  override_options: ['c_std=c11'],
)

rewind_schema_layout_c_test = executable(
  'rewind_schema_layout_c_test',
  'tools/networking/rewind_schema_layout_c.c',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['c_std=c11'],
)

rewind_schema_layout_cpp_test = executable(
  'rewind_schema_layout_cpp_test',
  'tools/networking/rewind_schema_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

rewind_collision_provider_test = executable(
  'rewind_collision_provider_test',
  [
    'tools/networking/server_rewind_collision_test.c',
    'src/server/rewind_collision.c',
  ],
  include_directories: include_directories('inc', 'q2proto/inc'),
  dependencies: networking_math_deps + [zlib],
  c_args: ['-DUSE_SERVER=1', engine_c_args, prediction_core_c_fp_args],
  install: false,
  override_options: ['c_std=c11'],
)

rewind_collision_layout_c_test = executable(
  'rewind_collision_layout_c_test',
  'tools/networking/rewind_collision_layout_c.c',
  include_directories: include_directories('inc'),
  c_args: game_c_args,
  install: false,
  override_options: ['c_std=c11'],
)

rewind_collision_layout_cpp_test = executable(
  'rewind_collision_layout_cpp_test',
  'tools/networking/rewind_collision_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  cpp_args: game_cpp_args,
  install: false,
  override_options: ['cpp_std=c++20'],
)

rewind_collision_real_bsp_hunk_src = win32 ? 'src/windows/hunk.c' : 'src/unix/hunk.c'
rewind_collision_real_bsp_test = executable(
  'rewind_collision_real_bsp_test',
  [
    'tools/networking/rewind_collision_real_bsp_test.c',
    'src/server/rewind_collision.c',
    'src/server/world.c',
    'src/common/bsp.c',
    'src/common/cmodel.c',
    'src/common/crc.c',
    'src/common/error.c',
    'src/common/math.c',
    'src/common/mdfour.c',
    'src/common/natsort.c',
    'src/common/sizebuf.c',
    'src/shared/shared.c',
    rewind_collision_real_bsp_hunk_src,
  ],
  include_directories: include_directories('inc', 'q2proto/inc'),
  dependencies: networking_math_deps + [zlib],
  c_args: ['-DUSE_SERVER=1', engine_c_args, prediction_core_c_fp_args],
  link_with: q2proto_lib,
  install: false,
  override_options: ['c_std=c11'],
)

command_context_test = executable(
  'command_context_test',
  [
    'tools/networking/command_context_test.c',
    'src/server/command_context.c',
  ],
  include_directories: include_directories('inc'),
  dependencies: networking_math_deps,
  c_args: prediction_core_c_fp_args,
  link_with: [
    canonical_rewind_core_lib,
    canonical_command_core_lib,
    canonical_snapshot_core_lib,
    canonical_event_core_lib,
  ],
  install: false,
  override_options: ['c_std=c11'],
)

command_context_layout_c_test = executable(
  'command_context_layout_c_test',
  'tools/networking/command_context_layout_c.c',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['c_std=c11'],
)

command_context_layout_cpp_test = executable(
  'command_context_layout_cpp_test',
  'tools/networking/command_context_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

snapshot_q2proto_test = executable(
  'snapshot_q2proto_test',
  'tools/networking/snapshot_q2proto_test.cpp',
  include_directories: include_directories('inc', 'q2proto/inc'),
  cpp_args: game_cpp_args + prediction_core_fp_args,
  link_with: [
    snapshot_q2proto_core_lib,
    canonical_snapshot_core_lib,
    canonical_event_core_lib,
    q2proto_lib,
  ],
  install: false,
  override_options: ['cpp_std=c++20'],
)

server_snapshot_shadow_test = executable(
  'server_snapshot_shadow_test',
  [
    'tools/networking/server_snapshot_shadow_test.cpp',
    'src/server/snapshot_event_candidates.c',
    'src/server/snapshot_shadow.c',
  ],
  include_directories: include_directories('inc', 'q2proto/inc'),
  c_args: game_c_args + prediction_core_c_fp_args,
  cpp_args: game_cpp_args + prediction_core_fp_args,
  link_with: [
    snapshot_q2proto_core_lib,
    canonical_snapshot_core_lib,
    canonical_event_core_lib,
    q2proto_lib,
  ],
  install: false,
  override_options: ['c_std=c11', 'cpp_std=c++20'],
)

server_snapshot_event_candidates_test = executable(
  'server_snapshot_event_candidates_test',
  [
    'tools/networking/server_snapshot_event_candidates_test.cpp',
    'src/server/snapshot_event_candidates.c',
    'src/server/snapshot_shadow.c',
  ],
  include_directories: include_directories('inc', 'q2proto/inc'),
  c_args: game_c_args + prediction_core_c_fp_args,
  cpp_args: game_cpp_args + prediction_core_fp_args,
  link_with: [
    snapshot_q2proto_core_lib,
    canonical_snapshot_core_lib,
    canonical_event_core_lib,
    q2proto_lib,
  ],
  install: false,
  override_options: ['c_std=c11', 'cpp_std=c++20'],
)

snapshot_final_projection_parity_corpus = executable(
  'snapshot_final_projection_parity_corpus',
  [
    'tools/networking/snapshot_final_projection_parity_corpus.cpp',
    'src/server/snapshot_event_candidates.c',
    'src/server/snapshot_shadow.c',
  ],
  include_directories: include_directories('inc', 'q2proto/inc'),
  c_args: game_c_args + prediction_core_c_fp_args,
  cpp_args: game_cpp_args + prediction_core_fp_args,
  link_with: [
    snapshot_q2proto_core_lib,
    canonical_snapshot_core_lib,
    canonical_event_core_lib,
    q2proto_lib,
  ],
  install: false,
  override_options: ['c_std=c11', 'cpp_std=c++20'],
)

demo_clock_sideband_test = executable(
  'demo_clock_sideband_test',
  'tools/networking/demo_clock_sideband_test.c',
  include_directories: include_directories('inc'),
  link_with: canonical_snapshot_core_lib,
  install: false,
  override_options: ['c_std=c11'],
)

demo_clock_sideband_layout_c_test = executable(
  'demo_clock_sideband_layout_c_test',
  'tools/networking/demo_clock_sideband_layout_c.c',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['c_std=c11'],
)

demo_clock_sideband_layout_cpp_test = executable(
  'demo_clock_sideband_layout_cpp_test',
  'tools/networking/demo_clock_sideband_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

demo_clock_runtime_test = executable(
  'demo_clock_runtime_test',
  [
    'tools/networking/demo_clock_runtime_test.cpp',
    'src/client/demo_clock.cpp',
  ],
  include_directories: include_directories('inc'),
  cpp_args: client_cpp_args,
  link_with: canonical_snapshot_core_lib,
  install: false,
  override_options: ['cpp_std=c++20'],
)

snapshot_q2proto_wire_test = executable(
  'snapshot_q2proto_wire_test',
  'tools/networking/snapshot_q2proto_wire_test.cpp',
  include_directories: include_directories('inc', 'q2proto/inc'),
  cpp_args: game_cpp_args + prediction_core_fp_args,
  link_with: [
    snapshot_q2proto_core_lib,
    canonical_snapshot_core_lib,
    canonical_event_core_lib,
    q2proto_lib,
  ],
  install: false,
  override_options: ['cpp_std=c++20'],
)

snapshot_q2proto_schema_layout_c_test = executable(
  'snapshot_q2proto_schema_layout_c_test',
  'tools/networking/snapshot_q2proto_schema_layout_c.c',
  include_directories: include_directories('inc', 'q2proto/inc'),
  c_args: game_c_args,
  install: false,
  override_options: ['c_std=c11'],
)

snapshot_q2proto_schema_layout_cpp_test = executable(
  'snapshot_q2proto_schema_layout_cpp_test',
  'tools/networking/snapshot_q2proto_schema_layout_cpp.cpp',
  include_directories: include_directories('inc', 'q2proto/inc'),
  cpp_args: game_cpp_args,
  install: false,
  override_options: ['cpp_std=c++20'],
)

command_stream_test = executable(
  'command_stream_test',
  'tools/networking/command_stream_test.c',
  include_directories: include_directories('inc'),
  link_with: canonical_command_core_lib,
  install: false,
  override_options: ['c_std=c11'],
)

command_schema_layout_c_test = executable(
  'command_schema_layout_c_test',
  'tools/networking/command_schema_layout_c.c',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['c_std=c11'],
)

command_schema_layout_cpp_test = executable(
  'command_schema_layout_cpp_test',
  'tools/networking/command_schema_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

local_action_test = executable(
  'local_action_test',
  'tools/networking/local_action_test.c',
  include_directories: include_directories('inc'),
  link_with: [
    canonical_local_action_core_lib,
    canonical_command_core_lib,
    canonical_event_core_lib,
  ],
  install: false,
  override_options: ['c_std=c11'],
)

local_action_layout_c_test = executable(
  'local_action_layout_c_test',
  'tools/networking/local_action_layout_c.c',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['c_std=c11'],
)

local_action_layout_cpp_test = executable(
  'local_action_layout_cpp_test',
  'tools/networking/local_action_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

local_action_observation_test = executable(
  'local_action_observation_test',
  'tools/networking/local_action_observation_test.c',
  include_directories: include_directories('inc'),
  link_with: [
    local_action_observation_core_lib,
    canonical_command_core_lib,
  ],
  install: false,
  override_options: ['c_std=c11'],
)

local_interaction_test = executable(
  'local_interaction_test',
  'tools/networking/local_interaction_test.c',
  include_directories: include_directories('inc'),
  link_with: [
    local_interaction_core_lib,
    canonical_command_core_lib,
  ],
  install: false,
  override_options: ['c_std=c11'],
)

local_action_audit_test = executable(
  'local_action_audit_test',
  'tools/networking/local_action_audit_test.c',
  include_directories: include_directories('inc'),
  link_with: [
    local_action_audit_core_lib,
    canonical_local_action_core_lib,
    canonical_command_core_lib,
    canonical_event_core_lib,
  ],
  install: false,
  override_options: ['c_std=c11'],
)

local_action_audit_benchmark = executable(
  'local_action_audit_benchmark',
  'tools/networking/local_action_audit_benchmark.c',
  include_directories: include_directories('inc'),
  link_with: [
    local_action_audit_core_lib,
    canonical_local_action_core_lib,
    canonical_command_core_lib,
    canonical_event_core_lib,
  ],
  install: false,
  override_options: ['c_std=c11'],
)

local_action_audit_layout_c_test = executable(
  'local_action_audit_layout_c_test',
  'tools/networking/local_action_audit_layout_c.c',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['c_std=c11'],
)

local_action_audit_layout_cpp_test = executable(
  'local_action_audit_layout_cpp_test',
  'tools/networking/local_action_audit_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

legacy_command_adapter_test = executable(
  'legacy_command_adapter_test',
  'tools/networking/legacy_command_adapter_test.c',
  include_directories: include_directories('inc'),
  link_with: canonical_command_core_lib,
  install: false,
  override_options: ['c_std=c11'],
)

legacy_command_adapter_layout_c_test = executable(
  'legacy_command_adapter_layout_c_test',
  'tools/networking/legacy_command_adapter_layout_c.c',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['c_std=c11'],
)

legacy_command_adapter_layout_cpp_test = executable(
  'legacy_command_adapter_layout_cpp_test',
  'tools/networking/legacy_command_adapter_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

consumed_cursor_sideband_test = executable(
  'consumed_cursor_sideband_test',
  'tools/networking/consumed_cursor_sideband_test.c',
  include_directories: include_directories('inc'),
  link_with: canonical_command_core_lib,
  install: false,
  override_options: ['c_std=c11'],
)

consumed_cursor_sideband_layout_c_test = executable(
  'consumed_cursor_sideband_layout_c_test',
  'tools/networking/consumed_cursor_sideband_layout_c.c',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['c_std=c11'],
)

consumed_cursor_sideband_layout_cpp_test = executable(
  'consumed_cursor_sideband_layout_cpp_test',
  'tools/networking/consumed_cursor_sideband_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

prediction_input_range_test = executable(
  'prediction_input_range_test',
  'tools/networking/prediction_input_range_test.c',
  include_directories: include_directories('inc'),
  link_with: canonical_command_core_lib,
  install: false,
  override_options: ['c_std=c11'],
)

prediction_input_layout_c_test = executable(
  'prediction_input_layout_c_test',
  'tools/networking/prediction_input_layout_c.c',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['c_std=c11'],
)

prediction_input_layout_cpp_test = executable(
  'prediction_input_layout_cpp_test',
  'tools/networking/prediction_input_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

cgame_prediction_snapshot_authority_test = executable(
  'cgame_prediction_snapshot_authority_test',
  [
    'tools/networking/cgame_prediction_snapshot_authority_test.cpp',
    'src/game/cgame/cg_prediction_authority.cpp',
  ],
  include_directories: include_directories(
    'inc',
    'src/game/cgame',
  ),
  cpp_args: game_cpp_args + prediction_core_fp_args,
  link_with: [
    canonical_command_core_lib,
    canonical_event_core_lib,
    canonical_snapshot_core_lib,
  ],
  install: false,
  override_options: ['cpp_std=c++20'],
)

adaptive_input_test = executable(
  'adaptive_input_test',
  'tools/networking/adaptive_input_test.c',
  include_directories: include_directories('inc'),
  link_with: canonical_command_core_lib,
  install: false,
  override_options: ['c_std=c11'],
)

adaptive_input_layout_cpp_test = executable(
  'adaptive_input_layout_cpp_test',
  'tools/networking/adaptive_input_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

net_capability_test = executable(
  'net_capability_test',
  'tools/networking/capability_test.c',
  include_directories: include_directories('inc'),
  link_with: net_capability_core_lib,
  install: false,
  override_options: ['c_std=c11'],
)

net_capability_layout_c_test = executable(
  'net_capability_layout_c_test',
  'tools/networking/capability_layout_c.c',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['c_std=c11'],
)

net_capability_layout_cpp_test = executable(
  'net_capability_layout_cpp_test',
  'tools/networking/capability_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

native_envelope_test = executable(
  'native_envelope_test',
  'tools/networking/native_envelope_test.c',
  include_directories: include_directories('inc'),
  link_with: native_envelope_core_lib,
  install: false,
  override_options: ['c_std=c11'],
)

native_envelope_layout_c_test = executable(
  'native_envelope_layout_c_test',
  'tools/networking/native_envelope_layout_c.c',
  include_directories: include_directories('inc'),
  link_with: native_envelope_core_lib,
  install: false,
  override_options: ['c_std=c11'],
)

native_envelope_layout_cpp_test = executable(
  'native_envelope_layout_cpp_test',
  'tools/networking/native_envelope_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  link_with: native_envelope_core_lib,
  install: false,
  override_options: ['cpp_std=c++20'],
)

native_carrier_test = executable(
  'native_carrier_test',
  'tools/networking/native_carrier_test.c',
  include_directories: include_directories('inc'),
  link_with: [native_carrier_core_lib,
              native_envelope_core_lib],
  install: false,
  override_options: ['c_std=c11'],
)

native_carrier_layout_test = executable(
  'native_carrier_layout_test',
  'tools/networking/native_carrier_layout.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

native_codec_test = executable(
  'native_codec_test',
  'tools/networking/native_codec_test.c',
  dependencies: networking_math_deps,
  include_directories: include_directories('inc'),
  link_with: [native_codec_core_lib,
              native_envelope_core_lib,
              canonical_command_core_lib,
              canonical_event_core_lib,
              canonical_snapshot_core_lib],
  install: false,
  override_options: ['c_std=c11'],
)

native_codec_layout_c_test = executable(
  'native_codec_layout_c_test',
  'tools/networking/native_codec_layout_c.c',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['c_std=c11'],
)

native_codec_layout_cpp_test = executable(
  'native_codec_layout_cpp_test',
  'tools/networking/native_codec_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

native_session_test = executable(
  'native_session_test',
  'tools/networking/native_session_test.c',
  include_directories: include_directories('inc'),
  link_with: [native_session_core_lib,
              native_readiness_core_lib,
              native_envelope_core_lib,
              net_capability_core_lib],
  install: false,
  override_options: ['c_std=c11'],
)

native_session_layout_c_test = executable(
  'native_session_layout_c_test',
  'tools/networking/native_session_layout_c.c',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['c_std=c11'],
)

native_session_layout_cpp_test = executable(
  'native_session_layout_cpp_test',
  'tools/networking/native_session_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

native_carrier_session_test = executable(
  'native_carrier_session_test',
  'tools/networking/native_carrier_session_test.c',
  include_directories: include_directories('inc'),
  link_with: [native_carrier_session_core_lib,
              native_carrier_core_lib,
              native_session_core_lib,
              native_readiness_core_lib,
              native_envelope_core_lib,
              net_capability_core_lib],
  install: false,
  override_options: ['c_std=c11'],
)

native_carrier_session_layout_test = executable(
  'native_carrier_session_layout_test',
  'tools/networking/native_carrier_session_layout.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

native_carrier_ack_test = executable(
  'native_carrier_ack_test',
  'tools/networking/native_carrier_ack_test.c',
  include_directories: include_directories('inc'),
  link_with: [native_carrier_ack_core_lib,
              native_carrier_session_core_lib,
              native_carrier_core_lib,
              native_session_core_lib,
              native_readiness_core_lib,
              native_envelope_core_lib,
              net_capability_core_lib],
  install: false,
  override_options: ['c_std=c11'],
)

native_carrier_ack_layout_test = executable(
  'native_carrier_ack_layout_test',
  'tools/networking/native_carrier_ack_layout.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

native_carrier_mixed_test = executable(
  'native_carrier_mixed_test',
  'tools/networking/native_carrier_mixed_test.c',
  include_directories: include_directories('inc'),
  link_with: [native_carrier_mixed_core_lib,
              native_carrier_ack_core_lib,
              native_carrier_session_core_lib,
              native_carrier_core_lib,
              native_session_core_lib,
              native_readiness_core_lib,
              native_envelope_core_lib,
              net_capability_core_lib],
  install: false,
  override_options: ['c_std=c11'],
)

native_carrier_mixed_layout_test = executable(
  'native_carrier_mixed_layout_test',
  'tools/networking/native_carrier_mixed_layout.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

native_readiness_test = executable(
  'native_readiness_test',
  'tools/networking/native_readiness_test.c',
  include_directories: include_directories('inc'),
  link_with: native_readiness_core_lib,
  install: false,
  override_options: ['c_std=c11'],
)

native_readiness_layout_test = executable(
  'native_readiness_layout_test',
  'tools/networking/native_readiness_layout.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

native_readiness_sideband_test = executable(
  'native_readiness_sideband_test',
  'tools/networking/native_readiness_sideband_test.c',
  include_directories: include_directories('inc'),
  link_with: [native_readiness_sideband_core_lib,
              native_readiness_core_lib],
  install: false,
  override_options: ['c_std=c11'],
)

native_readiness_sideband_layout_test = executable(
  'native_readiness_sideband_layout_test',
  'tools/networking/native_readiness_sideband_layout.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

native_readiness_q2proto_wire_test = executable(
  'native_readiness_q2proto_wire_test',
  'tools/networking/native_readiness_q2proto_wire_test.cpp',
  include_directories: include_directories('inc', 'q2proto/inc'),
  cpp_args: game_cpp_args,
  link_with: [q2proto_lib,
              native_readiness_sideband_core_lib,
              native_readiness_core_lib,
              net_capability_core_lib],
  install: false,
  override_options: ['cpp_std=c++20'],
)

native_server_shadow_pilot_test = executable(
  'native_server_shadow_pilot_test',
  [
    'tools/networking/native_server_shadow_pilot_test.c',
    'src/server/native_shadow.c',
    'src/server/snapshot_event_candidates.c',
    'src/server/snapshot_shadow.c',
  ],
  include_directories: include_directories('inc', 'q2proto/inc'),
  dependencies: networking_math_deps,
  c_args: ['-DUSE_SERVER=1', engine_c_args],
  link_with: [native_carrier_ack_core_lib,
              native_carrier_mixed_core_lib,
              native_carrier_session_core_lib,
              native_carrier_core_lib,
              native_event_sender_core_lib,
              native_snapshot_sender_core_lib,
              native_session_core_lib,
              native_command_shadow_core_lib,
              native_codec_core_lib,
              native_envelope_core_lib,
              native_readiness_sideband_core_lib,
              native_readiness_core_lib,
              net_capability_core_lib,
              canonical_command_core_lib,
              canonical_event_core_lib,
              canonical_snapshot_core_lib,
              snapshot_q2proto_core_lib,
              q2proto_lib],
  install: false,
  override_options: ['c_std=c11'],
)

native_client_readiness_pilot_test = executable(
  'native_client_readiness_pilot_test',
  [
    'tools/networking/native_client_readiness_pilot_test.cpp',
    'src/client/cgame_event_runtime.cpp',
    'src/client/native_readiness_pilot.cpp',
  ],
  include_directories: include_directories('inc', 'q2proto/inc', 'src/client'),
  dependencies: networking_math_deps + [zlib],
  cpp_args: client_cpp_args + ['-DWORR_NATIVE_READINESS_PILOT_TESTING=1'],
  link_with: [native_event_admission_core_lib,
              native_snapshot_receiver_core_lib,
              native_snapshot_admission_core_lib,
              native_carrier_mixed_core_lib,
              native_carrier_ack_core_lib,
              native_carrier_session_core_lib,
              native_carrier_core_lib,
              native_session_core_lib,
              native_command_shadow_core_lib,
              native_codec_core_lib,
              native_envelope_core_lib,
              native_readiness_sideband_core_lib,
              native_readiness_core_lib,
              net_capability_core_lib,
              canonical_command_core_lib,
              canonical_event_core_lib,
              canonical_snapshot_core_lib],
  install: false,
  override_options: ['cpp_std=c++20'],
)

native_event_virtual_link_test = executable(
  'native_event_virtual_link_test',
  [
    'tools/networking/native_event_virtual_link_test.cpp',
    'src/client/cgame_event_runtime.cpp',
    'src/client/native_readiness_pilot.cpp',
    'src/game/cgame/cg_event_shadow.cpp',
    'src/game/cgame/cg_event_runtime.cpp',
    'src/game/cgame/cg_local_interaction.cpp',
    'src/common/net/cgame_event_shadow.c',
    'src/server/native_shadow.c',
    'src/server/snapshot_event_candidates.c',
    'src/server/snapshot_shadow.c',
  ],
  include_directories: include_directories('inc', 'q2proto/inc', 'src/client', 'src/game/cgame'),
  dependencies: networking_math_deps + [zlib],
  c_args: ['-DUSE_SERVER=1', engine_c_args],
  cpp_args: client_cpp_args + ['-DWORR_NATIVE_READINESS_PILOT_TESTING=1'],
  link_with: [native_event_admission_core_lib,
              native_snapshot_receiver_core_lib,
              native_snapshot_admission_core_lib,
              native_carrier_mixed_core_lib,
              native_carrier_ack_core_lib,
              native_carrier_session_core_lib,
              native_carrier_core_lib,
              native_event_sender_core_lib,
              native_snapshot_sender_core_lib,
              native_session_core_lib,
              native_command_shadow_core_lib,
              native_codec_core_lib,
              native_envelope_core_lib,
              native_readiness_sideband_core_lib,
              native_readiness_core_lib,
              net_capability_core_lib,
              canonical_command_core_lib,
              canonical_event_core_lib,
              canonical_snapshot_core_lib,
              local_interaction_core_lib,
              snapshot_q2proto_core_lib,
              q2proto_lib],
  install: false,
  override_options: ['c_std=c11', 'cpp_std=c++20'],
)

native_snapshot_production_virtual_link_test = executable(
  'native_snapshot_production_virtual_link_test',
  [
    'tools/networking/native_snapshot_production_virtual_link_test.cpp',
    'src/client/cgame_event_runtime.cpp',
    'src/client/native_readiness_pilot.cpp',
    'src/client/snapshot_shadow.cpp',
    'src/game/cgame/cg_canonical_snapshot_timeline.cpp',
    'src/game/cgame/cg_prediction_authority.cpp',
    'src/game/cgame/cg_event_runtime.cpp',
    'src/game/cgame/cg_local_interaction.cpp',
    'src/server/native_shadow.c',
    'src/server/snapshot_event_candidates.c',
    'src/server/snapshot_shadow.c',
  ],
  include_directories: include_directories(
    'inc',
    'q2proto/inc',
    'src/client',
    'src/game/cgame',
    'src/game/bgame',
  ),
  dependencies: networking_math_deps + [zlib],
  c_args: ['-DUSE_SERVER=1', engine_c_args],
  cpp_args: client_cpp_args + [
    '-DWORR_NATIVE_READINESS_PILOT_TESTING=1',
  ] + prediction_core_fp_args,
  link_with: [native_event_admission_core_lib,
              native_snapshot_receiver_core_lib,
              native_snapshot_admission_core_lib,
              native_carrier_mixed_core_lib,
              native_carrier_ack_core_lib,
              native_carrier_session_core_lib,
              native_carrier_core_lib,
              native_event_sender_core_lib,
              native_snapshot_sender_core_lib,
              native_session_core_lib,
              native_command_shadow_core_lib,
              native_codec_core_lib,
              native_envelope_core_lib,
              native_readiness_sideband_core_lib,
              native_readiness_core_lib,
              net_capability_core_lib,
              canonical_command_core_lib,
              canonical_event_core_lib,
              canonical_snapshot_core_lib,
              local_interaction_core_lib,
              snapshot_q2proto_core_lib,
              q2proto_lib],
  install: false,
  override_options: ['c_std=c11', 'cpp_std=c++20'],
)

native_snapshot_production_corpus_test = executable(
  'native_snapshot_production_corpus_test',
  [
    'tools/networking/native_snapshot_production_corpus_test.cpp',
    'src/client/cgame_event_runtime.cpp',
    'src/client/native_readiness_pilot.cpp',
    'src/client/snapshot_shadow.cpp',
    'src/common/net/impair.c',
    'src/game/cgame/cg_canonical_snapshot_timeline.cpp',
    'src/game/cgame/cg_prediction_authority.cpp',
    'src/game/cgame/cg_event_runtime.cpp',
    'src/game/cgame/cg_local_interaction.cpp',
    'src/server/native_shadow.c',
    'src/server/snapshot_event_candidates.c',
    'src/server/snapshot_shadow.c',
  ],
  include_directories: include_directories(
    'inc',
    'q2proto/inc',
    'src/client',
    'src/game/cgame',
    'src/game/bgame',
  ),
  dependencies: networking_math_deps + [zlib],
  c_args: ['-DUSE_SERVER=1', engine_c_args],
  cpp_args: client_cpp_args + [
    '-DWORR_NATIVE_READINESS_PILOT_TESTING=1',
  ] + prediction_core_fp_args,
  link_with: [native_event_admission_core_lib,
              native_snapshot_receiver_core_lib,
              native_snapshot_admission_core_lib,
              native_carrier_mixed_core_lib,
              native_carrier_ack_core_lib,
              native_carrier_session_core_lib,
              native_carrier_core_lib,
              native_event_sender_core_lib,
              native_snapshot_sender_core_lib,
              native_session_core_lib,
              native_command_shadow_core_lib,
              native_codec_core_lib,
              native_envelope_core_lib,
              native_readiness_sideband_core_lib,
              native_readiness_core_lib,
              net_capability_core_lib,
              canonical_command_core_lib,
              canonical_event_core_lib,
              canonical_snapshot_core_lib,
              local_interaction_core_lib,
              snapshot_q2proto_core_lib,
              q2proto_lib],
  install: false,
  override_options: ['c_std=c11', 'cpp_std=c++20'],
)

native_snapshot_virtual_link_test = executable(
  'native_snapshot_virtual_link_test',
  [
    'tools/networking/native_snapshot_virtual_link_test.cpp',
    'src/game/cgame/cg_canonical_snapshot_timeline.cpp',
    'src/game/cgame/cg_event_runtime.cpp',
    'src/game/cgame/cg_local_interaction.cpp',
    'src/server/snapshot_event_candidates.c',
    'src/server/snapshot_shadow.c',
  ],
  include_directories: cgame_inc,
  dependencies: networking_math_deps,
  c_args: game_c_args + prediction_core_c_fp_args,
  cpp_args: game_cpp_args + prediction_core_fp_args,
  link_with: [native_snapshot_admission_core_lib,
              native_carrier_ack_core_lib,
              native_carrier_session_core_lib,
              native_carrier_core_lib,
              native_session_core_lib,
              native_readiness_core_lib,
              net_capability_core_lib,
              native_codec_core_lib,
              native_envelope_core_lib,
              canonical_command_core_lib,
              canonical_event_core_lib,
              canonical_snapshot_core_lib,
              local_interaction_core_lib,
              snapshot_q2proto_core_lib,
              q2proto_lib],
  install: false,
  override_options: ['c_std=c11', 'cpp_std=c++20'],
)

native_command_shadow_test = executable(
  'native_command_shadow_test',
  'tools/networking/native_command_shadow_test.c',
  dependencies: networking_math_deps,
  include_directories: include_directories('inc'),
  link_with: [native_command_shadow_core_lib,
              native_codec_core_lib,
              native_envelope_core_lib,
              canonical_command_core_lib,
              canonical_event_core_lib,
              canonical_snapshot_core_lib],
  install: false,
  override_options: ['c_std=c11'],
)

native_command_shadow_layout_test = executable(
  'native_command_shadow_layout_test',
  'tools/networking/native_command_shadow_layout.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

netchan_application_tx_hook_test = executable(
  'netchan_application_tx_hook_test',
  [
    'tools/networking/netchan_application_tx_hook_test.c',
    'src/common/net/chan.c',
    'src/common/net/sequence.c',
    'src/common/sizebuf.c',
  ],
  include_directories: include_directories('inc'),
  c_args: ['-DUSE_CLIENT=1', engine_c_args],
  install: false,
  override_options: ['c_std=c11'],
)

netchan_application_tx_hook_layout_test = executable(
  'netchan_application_tx_hook_layout_test',
  'tools/networking/netchan_application_tx_hook_layout.cpp',
  include_directories: include_directories('inc'),
  cpp_args: client_cpp_args,
  install: false,
  override_options: ['cpp_std=c++20'],
)

netchan_application_rx_hook_test = executable(
  'netchan_application_rx_hook_test',
  [
    'tools/networking/netchan_application_rx_hook_test.c',
    'src/common/net/chan.c',
    'src/common/net/sequence.c',
    'src/common/sizebuf.c',
  ],
  include_directories: include_directories('inc'),
  c_args: ['-DUSE_CLIENT=1', engine_c_args],
  install: false,
  override_options: ['c_std=c11'],
)

netchan_application_rx_hook_server_test = executable(
  'netchan_application_rx_hook_server_test',
  [
    'tools/networking/netchan_application_rx_hook_test.c',
    'src/common/net/chan.c',
    'src/common/net/sequence.c',
    'src/common/sizebuf.c',
  ],
  include_directories: include_directories('inc'),
  c_args: ['-DUSE_CLIENT=0', '-DUSE_SERVER=1', engine_c_args],
  install: false,
  override_options: ['c_std=c11'],
)

netchan_application_rx_hook_layout_test = executable(
  'netchan_application_rx_hook_layout_test',
  'tools/networking/netchan_application_rx_hook_layout.cpp',
  include_directories: include_directories('inc'),
  cpp_args: client_cpp_args,
  install: false,
  override_options: ['cpp_std=c++20'],
)

prediction_replay_parity_test = executable(
  'prediction_replay_parity_test',
  'tools/networking/prediction_replay_parity_test.cpp',
  dependencies: game_deps,
  include_directories: include_directories('inc', 'src/game/bgame', 'src/game/sgame'),
  cpp_args: game_cpp_args + prediction_core_fp_args,
  link_whole: prediction_core_lib,
  install: false,
  override_options: ['b_vscrt=from_buildtype', 'cpp_std=c++20'],
)

usercmd_wire_parity_test = executable(
  'usercmd_wire_parity_test',
  [
    'tools/networking/usercmd_wire_parity_test.cpp',
    'src/common/net/usercmd_delta.c',
  ],
  dependencies: game_deps,
  include_directories: include_directories('inc', 'q2proto/inc'),
  c_args: game_c_args + prediction_core_c_fp_args,
  cpp_args: game_cpp_args + prediction_core_fp_args,
  link_with: q2proto_lib,
  link_whole: prediction_core_lib,
  install: false,
  override_options: ['b_vscrt=from_buildtype', 'cpp_std=c++20'],
)

prediction_schema_layout_c_test = executable(
  'prediction_schema_layout_c_test',
  'tools/networking/prediction_schema_layout_c.c',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['c_std=c11'],
)

prediction_schema_layout_cpp_test = executable(
  'prediction_schema_layout_cpp_test',
  'tools/networking/prediction_schema_layout_cpp.cpp',
  include_directories: include_directories('inc'),
  install: false,
  override_options: ['cpp_std=c++20'],
)

gameplay_light_test = executable(
  'gameplay_light_test',
  [
    'tools/renderer_parity/gameplay_light_test.c',
    'src/client/gameplay_light.c',
  ],
  include_directories: include_directories('inc'),
  c_args: ['-DUSE_CLIENT=1', '-DUSE_REF=REF_GL', engine_c_args],
  install: false,
  override_options: ['c_std=c11'],
)

test(
  'network-impair-model',
  network_impair_model_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'renderer-gameplay-light',
  gameplay_light_test,
  suite: 'renderer',
  timeout: 30,
)

test(
  'network-event-journal',
  event_journal_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-event-stream',
  event_stream_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-event-stream-layout-c',
  event_stream_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-event-stream-layout-cpp',
  event_stream_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-event-stream-owner',
  event_stream_owner_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-event-admission',
  native_event_admission_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-snapshot-admission',
  native_snapshot_admission_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-snapshot-admission-layout-c',
  native_snapshot_admission_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-snapshot-admission-layout-cpp',
  native_snapshot_admission_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-snapshot-receiver',
  native_snapshot_receiver_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-event-sender',
  native_event_sender_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-snapshot-sender',
  native_snapshot_sender_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-event-shadow',
  event_shadow_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-local-interaction-authority',
  local_interaction_authority_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-cgame-event-shadow',
  cgame_event_shadow_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-legacy-temp-event-candidate',
  legacy_temp_event_candidate_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-legacy-muzzle-event-candidate',
  legacy_muzzle_event_candidate_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-legacy-game-event-candidate',
  legacy_game_event_candidate_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-legacy-spatial-audio-event-candidate',
  legacy_spatial_audio_event_candidate_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-cgame-event-presentation',
  cgame_event_presentation_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-cgame-local-interaction',
  cgame_local_interaction_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-cgame-event-runtime',
  cgame_event_runtime_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-cgame-event-runtime-export',
  cgame_event_runtime_export_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-cgame-event-runtime-owner',
  cgame_event_runtime_owner_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-cgame-event-runtime-layout-c',
  cgame_event_runtime_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-cgame-event-runtime-layout-cpp',
  cgame_event_runtime_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-event-schema-layout-c',
  event_schema_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-event-schema-layout-cpp',
  event_schema_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-snapshot-store',
  snapshot_store_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-snapshot-schema-layout-c',
  snapshot_schema_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-snapshot-schema-layout-cpp',
  snapshot_schema_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-snapshot-recovery',
  snapshot_recovery_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-snapshot-recovery-layout-c',
  snapshot_recovery_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-snapshot-recovery-layout-cpp',
  snapshot_recovery_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-snapshot-shadow-schema-layout-c',
  snapshot_shadow_schema_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-snapshot-shadow-schema-layout-cpp',
  snapshot_shadow_schema_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-snapshot-timeline',
  snapshot_timeline_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-snapshot-timeline-schema-layout-c',
  snapshot_timeline_schema_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-snapshot-timeline-schema-layout-cpp',
  snapshot_timeline_schema_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-rewind-core',
  rewind_core_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-rewind-observation',
  rewind_observation_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-rewind-observation-layout-c',
  rewind_observation_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-rewind-observation-layout-cpp',
  rewind_observation_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-rewind-acceptance-probe',
  rewind_acceptance_probe,
  args: [
    '--scenario', 'normal',
    '--weapon-id', '1',
    '--weapon-name', 'machinegun',
    '--latency-ms', '100',
  ],
  suite: 'networking',
  timeout: 30,
)

test(
  'network-rewind-schema-layout-c',
  rewind_schema_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-rewind-schema-layout-cpp',
  rewind_schema_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-rewind-collision-provider',
  rewind_collision_provider_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-rewind-collision-layout-c',
  rewind_collision_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-rewind-collision-layout-cpp',
  rewind_collision_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-rewind-collision-real-bsp-parity',
  find_program('python3'),
  args: [
    files('tools/networking/run_rewind_collision_real_bsp_test.py'),
    '--probe', rewind_collision_real_bsp_test,
    '--repo-root', meson.current_source_dir(),
  ],
  suite: 'networking',
  timeout: 60,
)

test(
  'network-command-context',
  command_context_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-command-context-layout-c',
  command_context_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-command-context-layout-cpp',
  command_context_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-snapshot-q2proto-projection',
  snapshot_q2proto_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-server-snapshot-shadow',
  server_snapshot_shadow_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-server-snapshot-event-candidates',
  server_snapshot_event_candidates_test,
  suite: 'networking',
  timeout: 30,
)

snapshot_final_projection_parity_script = files(
  'tools/networking/run_snapshot_final_projection_parity.py',
)
snapshot_final_projection_parity_manifest = files(
  'tools/networking/scenarios/snapshot_final_projection_parity_corpus.json',
)
test(
  'network-snapshot-final-projection-parity-corpus',
  version_prog,
  args: [
    snapshot_final_projection_parity_script,
    '--probe-exe', snapshot_final_projection_parity_corpus,
    '--manifest', snapshot_final_projection_parity_manifest,
    '--output', meson.current_build_dir() / 'meson-logs' / 'networking-snapshot-final-projection-parity.json',
    '--platform-id', host_machine.system() + '-' + host_machine.cpu_family(),
    '--build-type', get_option('buildtype'),
    '--compiler-id', cc.get_id(),
  ],
  suite: 'networking',
  timeout: 300,
)

test(
  'network-demo-clock-sideband',
  demo_clock_sideband_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-demo-clock-sideband-layout-c',
  demo_clock_sideband_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-demo-clock-sideband-layout-cpp',
  demo_clock_sideband_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-demo-clock-runtime',
  demo_clock_runtime_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-snapshot-q2proto-wire',
  snapshot_q2proto_wire_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-snapshot-q2proto-schema-layout-c',
  snapshot_q2proto_schema_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-snapshot-q2proto-schema-layout-cpp',
  snapshot_q2proto_schema_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-command-stream',
  command_stream_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-command-schema-layout-c',
  command_schema_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-command-schema-layout-cpp',
  command_schema_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-local-action-v2',
  local_action_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-local-action-layout-c',
  local_action_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-local-action-layout-cpp',
  local_action_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-local-action-observation',
  local_action_observation_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-local-interaction',
  local_interaction_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-local-action-audit',
  local_action_audit_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-local-action-audit-performance',
  local_action_audit_benchmark,
  suite: 'networking',
  timeout: 60,
)

test(
  'network-local-action-audit-layout-c',
  local_action_audit_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-local-action-audit-layout-cpp',
  local_action_audit_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-legacy-command-adapter',
  legacy_command_adapter_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-legacy-command-adapter-layout-c',
  legacy_command_adapter_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-legacy-command-adapter-layout-cpp',
  legacy_command_adapter_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-consumed-cursor-sideband',
  consumed_cursor_sideband_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-consumed-cursor-sideband-layout-c',
  consumed_cursor_sideband_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-consumed-cursor-sideband-layout-cpp',
  consumed_cursor_sideband_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-prediction-input-range',
  prediction_input_range_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-prediction-input-layout-c',
  prediction_input_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-prediction-input-layout-cpp',
  prediction_input_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-cgame-prediction-snapshot-authority',
  cgame_prediction_snapshot_authority_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-adaptive-input',
  adaptive_input_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-adaptive-input-layout-cpp',
  adaptive_input_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-capability-negotiation',
  net_capability_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-capability-layout-c',
  net_capability_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-capability-layout-cpp',
  net_capability_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-envelope',
  native_envelope_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-envelope-layout-c',
  native_envelope_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-envelope-layout-cpp',
  native_envelope_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-carrier',
  native_carrier_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-carrier-layout',
  native_carrier_layout_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-codec',
  native_codec_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-codec-layout-c',
  native_codec_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-codec-layout-cpp',
  native_codec_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-session',
  native_session_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-session-layout-c',
  native_session_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-session-layout-cpp',
  native_session_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-carrier-session',
  native_carrier_session_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-carrier-session-layout',
  native_carrier_session_layout_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-carrier-ack',
  native_carrier_ack_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-carrier-ack-layout',
  native_carrier_ack_layout_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-carrier-mixed',
  native_carrier_mixed_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-carrier-mixed-layout',
  native_carrier_mixed_layout_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-readiness',
  native_readiness_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-readiness-layout',
  native_readiness_layout_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-readiness-sideband',
  native_readiness_sideband_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-readiness-sideband-layout',
  native_readiness_sideband_layout_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-readiness-q2proto-wire',
  native_readiness_q2proto_wire_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-server-shadow-pilot',
  native_server_shadow_pilot_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-client-readiness-pilot',
  native_client_readiness_pilot_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-event-virtual-link',
  native_event_virtual_link_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-snapshot-production-virtual-link',
  native_snapshot_production_virtual_link_test,
  suite: 'networking',
  timeout: 30,
)

native_snapshot_production_corpus_script = files(
  'tools/networking/run_native_snapshot_production_corpus.py',
)
native_snapshot_production_corpus_manifest = files(
  'tools/networking/scenarios/native_snapshot_production_corpus.json',
)

native_snapshot_production_corpus_runner_test = files(
  'tools/networking/test_run_native_snapshot_production_corpus.py',
)
test(
  'network-native-snapshot-production-corpus-runner',
  version_prog,
  args: native_snapshot_production_corpus_runner_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-snapshot-production-corpus',
  version_prog,
  args: [
    native_snapshot_production_corpus_script,
    '--corpus-exe', native_snapshot_production_corpus_test,
    '--manifest', native_snapshot_production_corpus_manifest,
    '--evidence', meson.current_source_dir() / '.tmp' / 'networking' / 'native_snapshot_production_corpus' / 'evidence.json',
  ],
  suite: 'networking',
  timeout: 1900,
)

test(
  'network-native-snapshot-virtual-link',
  native_snapshot_virtual_link_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-command-shadow',
  native_command_shadow_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-native-command-shadow-layout',
  native_command_shadow_layout_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-netchan-application-tx-hook',
  netchan_application_tx_hook_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-netchan-application-tx-hook-layout',
  netchan_application_tx_hook_layout_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-netchan-application-rx-hook',
  netchan_application_rx_hook_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-netchan-application-rx-hook-layout',
  netchan_application_rx_hook_layout_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-netchan-application-rx-hook-dedicated',
  netchan_application_rx_hook_server_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-prediction-replay-parity',
  prediction_replay_parity_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-usercmd-live-wire-parity',
  usercmd_wire_parity_test,
  args: '--json',
  suite: 'networking',
  timeout: 30,
)

test(
  'network-prediction-schema-layout-c',
  prediction_schema_layout_c_test,
  suite: 'networking',
  timeout: 30,
)

test(
  'network-prediction-schema-layout-cpp',
  prediction_schema_layout_cpp_test,
  suite: 'networking',
  timeout: 30,
)

network_baseline_script = files('tools/networking/run_networking_baseline.py')
test(
  'network-impair-baseline-repeatability',
  version_prog,
  args: [
    network_baseline_script,
    '--model-exe', network_impair_model_test,
    '--output', meson.current_build_dir() / 'meson-logs' / 'networking-impairment-baseline.json',
    '--repeat', '3',
  ],
  suite: 'networking',
  timeout: 60,
)

prediction_baseline_script = files(
  'tools/networking/run_prediction_parity_baseline.py',
)
test(
  'network-prediction-baseline-repeatability',
  version_prog,
  args: [
    prediction_baseline_script,
    '--harness-exe', prediction_replay_parity_test,
    '--wire-harness-exe', usercmd_wire_parity_test,
    '--output', meson.current_build_dir() / 'meson-logs' / 'networking-prediction-parity.json',
    '--repeat', '3',
  ],
  suite: 'networking',
  timeout: 60,
)

rewind_acceptance_script = files(
  'tools/networking/run_rewind_acceptance.py',
)
rewind_acceptance_matrix = files(
  'tools/networking/scenarios/rewind_player_acceptance_matrix.json',
)
test(
  'network-rewind-acceptance-evidence',
  version_prog,
  args: [
    rewind_acceptance_script,
    '--probe-exe', rewind_acceptance_probe,
    '--matrix', rewind_acceptance_matrix,
    '--output', meson.current_build_dir() / 'meson-logs' / 'networking-rewind-acceptance.json',
    '--repeat', '3',
    '--platform-id', host_machine.system() + '-' + host_machine.cpu_family(),
    '--build-type', get_option('buildtype'),
    '--compiler-id', cc.get_id(),
    '--sgame-module', sgame_dll,
  ],
  suite: 'networking',
  timeout: 90,
)

run_target(
  'networking-baseline',
  command: [
    version_prog,
    network_baseline_script,
    '--model-exe', network_impair_model_test,
    '--output', meson.current_source_dir() / '.tmp' / 'networking' / 'impairment-baseline.json',
    '--repeat', '3',
  ],
)

run_target(
  'networking-prediction-baseline',
  command: [
    version_prog,
    prediction_baseline_script,
    '--harness-exe', prediction_replay_parity_test,
    '--wire-harness-exe', usercmd_wire_parity_test,
    '--output', meson.current_source_dir() / '.tmp' / 'networking' / 'prediction-parity.json',
    '--repeat', '3',
  ],
)

run_target(
  'networking-rewind-acceptance',
  command: [
    version_prog,
    rewind_acceptance_script,
    '--probe-exe', rewind_acceptance_probe,
    '--matrix', rewind_acceptance_matrix,
    '--output', meson.current_source_dir() / '.tmp' / 'networking' / 'rewind-acceptance' / 'evidence.json',
    '--repeat', '3',
    '--platform-id', host_machine.system() + '-' + host_machine.cpu_family(),
    '--build-type', get_option('buildtype'),
    '--compiler-id', cc.get_id(),
    '--sgame-module', sgame_dll,
  ],
)

run_target(
  'networking-snapshot-final-projection-parity',
  command: [
    version_prog,
    snapshot_final_projection_parity_script,
    '--probe-exe', snapshot_final_projection_parity_corpus,
    '--manifest', snapshot_final_projection_parity_manifest,
    '--output', meson.current_source_dir() / '.tmp' / 'networking' / 'snapshot-parity' / 'evidence.json',
    '--platform-id', host_machine.system() + '-' + host_machine.cpu_family(),
    '--build-type', get_option('buildtype'),
    '--compiler-id', cc.get_id(),
  ],
)

console_integration_check_script = files(
  'tools/console/validate_console_integration.py',
)
test(
  'console-integration-check',
  version_prog,
  args: [
    console_integration_check_script,
    '--root', meson.current_source_dir(),
  ],
  suite: 'client',
  timeout: 30,
)
run_target(
  'console-integration-check',
  command: [
    version_prog,
    console_integration_check_script,
    '--root', meson.current_source_dir(),
  ],
)

network_runtime_smoke_parser_test = files(
  'tools/networking/test_run_staged_impairment_smoke.py',
)
test(
  'network-runtime-smoke-status-parser',
  version_prog,
  args: network_runtime_smoke_parser_test,
  suite: 'networking',
  timeout: 30,
)

network_live_snapshot_acceptance_parser_test = files(
  'tools/networking/test_run_live_snapshot_acceptance_gate.py',
)
test(
  'network-live-snapshot-acceptance-gate-parser',
  version_prog,
  args: network_live_snapshot_acceptance_parser_test,
  suite: 'networking',
  timeout: 30,
)

network_command_gap_acceptance_parser_test = files(
  'tools/networking/test_run_command_gap_acceptance_gate.py',
)
test(
  'network-command-gap-acceptance-gate-parser',
  version_prog,
  args: network_command_gap_acceptance_parser_test,
  suite: 'networking',
  timeout: 30,
)

network_cgame_prediction_fail_closed_contract_test = files(
  'tools/networking/test_cgame_prediction_fail_closed_contract.py',
)
test(
  'network-cgame-prediction-fail-closed-contract',
  version_prog,
  args: network_cgame_prediction_fail_closed_contract_test,
  suite: 'networking',
  timeout: 30,
)

network_lag_compensation_mover_capture_contract_test = files(
  'tools/networking/test_lag_compensation_mover_capture_contract.py',
)
test(
  'network-lag-compensation-mover-capture-contract',
  version_prog,
  args: network_lag_compensation_mover_capture_contract_test,
  suite: 'networking',
  timeout: 30,
)

network_rewind_mover_runtime_gate_parser_test = files(
  'tools/networking/test_run_rewind_mover_runtime_gate.py',
)
test(
  'network-rewind-mover-runtime-gate-parser',
  version_prog,
  args: network_rewind_mover_runtime_gate_parser_test,
  suite: 'networking',
  timeout: 30,
)

network_rewind_rail_damage_runtime_gate_parser_test = files(
  'tools/networking/test_run_rewind_rail_damage_runtime_gate.py',
)
test(
  'network-rewind-rail-damage-runtime-gate-parser',
  version_prog,
  args: network_rewind_rail_damage_runtime_gate_parser_test,
  suite: 'networking',
  timeout: 30,
)

network_canonical_rail_damage_runtime_gate_parser_test = files(
  'tools/networking/test_run_canonical_rail_damage_runtime_gate.py',
)
test(
  'network-canonical-rail-damage-runtime-gate-parser',
  version_prog,
  args: network_canonical_rail_damage_runtime_gate_parser_test,
  suite: 'networking',
  timeout: 30,
)

network_lag_compensation_canonical_rail_contract_test = files(
  'tools/networking/test_lag_compensation_canonical_rail_contract.py',
)
test(
  'network-lag-compensation-canonical-rail-contract',
  version_prog,
  args: network_lag_compensation_canonical_rail_contract_test,
  suite: 'networking',
  timeout: 30,
)

network_headless_input_contract_test = files(
  'tools/networking/test_headless_input_contract.py',
)
test(
  'network-headless-input-contract',
  version_prog,
  args: network_headless_input_contract_test,
  suite: 'networking',
  timeout: 30,
)

network_headless_process_test = files(
  'tools/networking/test_headless_process.py',
)
test(
  'network-headless-process-policy',
  version_prog,
  args: network_headless_process_test,
  suite: 'networking',
  timeout: 30,
)

release_bootstrap_headless_contract_test = files(
  'tools/release/test_bootstrap_headless_contract.py',
)
test(
  'release-bootstrap-headless-contract',
  version_prog,
  args: release_bootstrap_headless_contract_test,
  suite: 'release',
  timeout: 30,
)

network_lag_compensation_rail_damage_contract_test = files(
  'tools/networking/test_lag_compensation_rail_damage_contract.py',
)
test(
  'network-lag-compensation-rail-damage-contract',
  version_prog,
  args: network_lag_compensation_rail_damage_contract_test,
  suite: 'networking',
  timeout: 30,
)

native_shadow_runtime_parser_test = files(
  'tools/networking/test_run_native_shadow_runtime_smoke.py',
)
test(
  'network-native-shadow-runtime-parser',
  version_prog,
  args: native_shadow_runtime_parser_test,
  suite: 'networking',
  timeout: 30,
)

native_shadow_repeated_runtime_parser_test = files(
  'tools/networking/test_run_native_shadow_repeated_runtime_smoke.py',
)
test(
  'network-native-shadow-repeated-runtime-parser',
  version_prog,
  args: native_shadow_repeated_runtime_parser_test,
  suite: 'networking',
  timeout: 30,
)

if win32
  network_runtime_smoke_script = files(
    'tools/networking/run_staged_impairment_smoke.py',
  )
  run_target(
    'networking-runtime-smoke',
    command: [
      version_prog,
      network_runtime_smoke_script,
      '--client-exe', meson.current_source_dir() / '.install' / 'worr_x86_64.exe',
      '--dedicated-exe', meson.current_source_dir() / '.install' / 'worr_ded_x86_64.exe',
      '--working-dir', meson.current_source_dir() / '.install',
      '--output', meson.current_source_dir() / '.tmp' / 'networking' / 'impairment-runtime.json',
      '--timeout', '90',
    ],
  )

  native_shadow_runtime_script = files(
    'tools/networking/run_native_shadow_runtime_smoke.py',
  )
  run_target(
    'networking-native-shadow-runtime',
    command: [
      version_prog,
      native_shadow_runtime_script,
      '--client-exe', meson.current_source_dir() / '.install' / 'worr_x86_64.exe',
      '--dedicated-exe', meson.current_source_dir() / '.install' / 'worr_ded_x86_64.exe',
      '--working-dir', meson.current_source_dir() / '.install',
      '--output', meson.current_source_dir() / '.tmp' / 'networking' / 'native-shadow-runtime.json',
      '--timeout', '120',
    ],
  )

  native_shadow_repeated_runtime_script = files(
    'tools/networking/run_native_shadow_repeated_runtime_smoke.py',
  )
  run_target(
    'networking-native-shadow-repeated-runtime',
    command: [
      version_prog,
      native_shadow_repeated_runtime_script,
      '--client-exe', meson.current_source_dir() / '.install' / 'worr_x86_64.exe',
      '--dedicated-exe', meson.current_source_dir() / '.install' / 'worr_ded_x86_64.exe',
      '--working-dir', meson.current_source_dir() / '.install',
      '--output', meson.current_source_dir() / '.tmp' / 'networking' / 'native-shadow-repeated-runtime.json',
      '--timeout', '45',
    ],
  )

  network_live_snapshot_acceptance_script = files(
    'tools/networking/run_live_snapshot_acceptance_gate.py',
  )
  run_target(
    'networking-live-snapshot-acceptance',
    command: [
      version_prog,
      network_live_snapshot_acceptance_script,
      '--client-exe', meson.current_source_dir() / '.install' / 'worr_x86_64.exe',
      '--working-dir', meson.current_source_dir() / '.install',
      '--output', meson.current_source_dir() / '.tmp' / 'networking' / 'live-snapshot-acceptance.json',
      '--target', '100000',
      '--profile', 'impaired',
      '--timeout', '1800',
    ],
  )

  network_command_gap_acceptance_script = files(
    'tools/networking/run_command_gap_acceptance_gate.py',
  )
  run_target(
    'networking-command-gap-acceptance',
    command: [
      version_prog,
      network_command_gap_acceptance_script,
      '--dedicated-exe', meson.current_source_dir() / '.install' / 'worr_ded_x86_64.exe',
      '--working-dir', meson.current_source_dir() / '.install',
      '--output', meson.current_source_dir() / '.tmp' / 'networking' / 'command-gap-acceptance.json',
      '--timeout', '30',
    ],
  )

  network_canonical_rail_damage_runtime_script = files(
    'tools/networking/run_canonical_rail_damage_runtime_gate.py',
  )
  run_target(
    'networking-canonical-rail-damage-acceptance',
    command: [
      version_prog,
      network_canonical_rail_damage_runtime_script,
      '--client-exe', meson.current_source_dir() / '.install' / 'worr_x86_64.exe',
      '--dedicated-exe', meson.current_source_dir() / '.install' / 'worr_ded_x86_64.exe',
      '--working-dir', meson.current_source_dir() / '.install',
      '--output', meson.current_source_dir() / '.tmp' / 'networking' / 'canonical-rail-damage-runtime.json',
      '--timeout', '35',
    ],
  )
endif

version_revision = run_command(version_prog, 'version.py', '--revision', check: true).stdout().strip()
version_semver = run_command(version_prog, 'version.py', '--semver', check: true).stdout().strip()
version_major = run_command(version_prog, 'version.py', '--major', check: true).stdout().strip()
version_minor = run_command(version_prog, 'version.py', '--minor', check: true).stdout().strip()
version_patch = run_command(version_prog, 'version.py', '--patch', check: true).stdout().strip()
version_prerelease = run_command(version_prog, 'version.py', '--prerelease', check: true).stdout().strip()

config.set('REVISION',            version_revision.to_int())
config.set('VERSION_MAJOR',       version_major.to_int())
config.set('VERSION_MINOR',       version_minor.to_int())
config.set('VERSION_PATCH',       version_patch.to_int())
config.set_quoted('VERSION',      meson.project_version())
config.set_quoted('VERSION_SEMVER', version_semver)
config.set_quoted('VERSION_PRERELEASE', version_prerelease)
config.set_quoted('CPUSTRING',    cpu)
config.set_quoted('BUILDSTRING',  sys)
config.set_quoted('BASEGAME',     get_option('base-game'))
config.set_quoted('DEFGAME',      get_option('default-game'))
if not win32
  config.set_quoted('DATADIR',    datadir)
  config.set_quoted('LIBDIR',     libdir)
  config.set_quoted('HOMEDIR',    homedir)
  if default_prefix != ''
    config.set_quoted('DEFAULT_PREFIX', default_prefix)
  endif
endif
config.set_quoted('R_TEXTURE_FORMATS', ' '.join(texture_formats))
config.set_quoted('VID_GEOMETRY', get_option('vid-geometry'))
config.set_quoted('VID_MODELIST', get_option('vid-modelist'))

config.set10('USE_AUTOREPLY',     get_option('auto-reply'))
config.set10('USE_DEBUG',         get_option('debug'))
config.set10('USE_FPS',           get_option('variable-fps'))
config.set10('USE_GLES',          get_option('opengl-es1'))
config.set10('USE_VULKAN',        vulkan.found())
config.set10('USE_ICMP',          get_option('icmp-errors').require(win32 or cc.has_header('linux/errqueue.h')).allowed())
config.set10('USE_EXTERNAL_RENDERERS', get_option('external-renderers'))
config.set10('USE_MD3',           get_option('md3'))
config.set10('USE_MD5',           get_option('md5'))
config.set10('USE_MEMORY_TRACES', get_option('memory-traces'))
config.set10('USE_PACKETDUP',     get_option('packetdup-hack'))
config.set10('USE_TGA',           get_option('tga'))
config.set10('USE_' + host_machine.endian().to_upper() + '_ENDIAN', true)

if host_machine.system() == 'darwin'
  config.set_quoted('DEFGLPROFILE', 'gl3.2')
elif get_option('opengl-es1')
  config.set_quoted('DEFGLPROFILE', 'es1')
else
  config.set_quoted('DEFGLPROFILE', '')
endif

# protocol extensions are always on
config.set10('USE_PROTOCOL_EXTENSIONS', true)

if not win32
  have_backtrace = cc.has_header('execinfo.h') and cc.has_function('backtrace')
  config.set10('HAVE_BACKTRACE',  have_backtrace)
endif
config.set10('HAVE_MALLOC_H', cc.has_header('malloc.h'))
# new game API flag is *always on* for engine,
# and can be enabled here for game as well
if get_option('game-new-api')
  config.set10('USE_NEW_GAME_API', true)
endif

configure_file(output: 'config.h', configuration: config)

if system_wide
  summary({'datadir': datadir, 'libdir': libdir, 'homedir': homedir}, section: 'Directories')
endif

summary({
  'anticheat-server'   : config.get('USE_AC_SERVER', '') != '',
  'auto-reply'         : config.get('USE_AUTOREPLY', 0) != 0,
  'avcodec'            : config.get('USE_AVCODEC', 0) != 0,
  'client-gtv'         : config.get('USE_CLIENT_GTV', 0) != 0,
  'client-ui'          : config.get('USE_UI', 0) != 0,
  'debug'              : config.get('USE_DEBUG', 0) != 0,
  'game-abi-hack'      : config.get('USE_GAME_ABI_HACK', 0) != 0,
  'game-new-api'       : config.get('USE_NEW_GAME_API', 0) != 0,
  'icmp-errors'        : config.get('USE_ICMP', 0) != 0,
  'libcurl'            : config.get('USE_CURL', 0) != 0,
  'libjpeg'            : config.get('USE_JPG', 0) != 0,
  'libpng'             : config.get('USE_PNG', 0) != 0,
  'md3'                : config.get('USE_MD3', 0) != 0,
  'md5'                : config.get('USE_MD5', 0) != 0,
  'memory-traces'      : config.get('USE_MEMORY_TRACES', 0) != 0,
  'mvd-client'         : config.get('USE_MVD_CLIENT', 0) != 0,
  'mvd-server'         : config.get('USE_MVD_SERVER', 0) != 0,
  'openal'             : config.get('USE_OPENAL', 0) != 0,
  'packetdup-hack'     : config.get('USE_PACKETDUP', 0) != 0,
  'rmlui'              : rmlui_runtime,
  'save-games'         : config.get('USE_SAVEGAMES', 0) != 0,
  'sdl3'               : config.get('USE_SDL', '') != '',
  'software-sound'     : config.get('USE_SNDDMA', 0) != 0,
  'system-console'     : config.get('USE_SYSCON', 0) != 0,
  'tests'              : config.get('USE_TESTS', 0) != 0,
  'tga'                : config.get('USE_TGA', 0) != 0,
  'variable-fps'       : config.get('USE_FPS', 0) != 0,
  'wayland'            : config.get('USE_WAYLAND', 0) != 0,
  'windows-crash-dumps': config.get('USE_DBGHELP', 0) != 0,
  'windows-egl'        : config.get('USE_WIN32EGL', 0) != 0,
  'windows-service'    : config.get('USE_WINSVC', '') != '',
  'x11'                : config.get('USE_X11', 0) != 0,
  'zlib'               : config.get('USE_ZLIB', 0) != 0,
  }, section: 'Features', bool_yn: true)
