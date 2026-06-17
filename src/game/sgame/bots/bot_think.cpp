/*Copyright (c) 2024 ZeniMax Media Inc.
Licensed under the GNU General Public License 2.0.

bot_think.cpp implementation.*/

#include "../g_local.hpp"
#include "bot_nav.hpp"
#include "botlib_adapter.hpp"
#include "bot_runtime.hpp"
#include "bot_think.hpp"

namespace {

struct BotFrameCommandStatus {
	int frames = 0;
	int commands = 0;
	int skippedInvalid = 0;
	int skippedRuntime = 0;
	int skippedInactive = 0;
	int skippedNotBot = 0;
	int routeCommands = 0;
};

BotFrameCommandStatus botFrameCommandStatus;

byte Bot_CommandMsec() {
	const int frameTimeMs = gi.frameTimeMs > 0 ? static_cast<int>(gi.frameTimeMs) : 1;
	return static_cast<byte>(std::clamp(frameTimeMs, 1, 255));
}

Vector3 Bot_CommandAnglesToTarget( const gentity_t * bot, const BotLibAdapterRouteSteer & route ) {
	Vector3 target = {
		route.moveTarget[0],
		route.moveTarget[1],
		route.moveTarget[2]
	};
	Vector3 direction = target - bot->s.origin;
	direction.z = 0.0f;

	if (direction.lengthSquared() < 1.0f) {
		target = {
			route.goalOrigin[0],
			route.goalOrigin[1],
			route.goalOrigin[2]
		};
		direction = target - bot->s.origin;
		direction.z = 0.0f;
	}

	if (direction.lengthSquared() < 1.0f) {
		Vector3 angles = bot->client->vAngle;
		if (angles == vec3_origin) {
			angles = bot->s.angles;
		}
		angles[PITCH] = 0.0f;
		angles[ROLL] = 0.0f;
		return angles;
	}

	Vector3 angles = VectorToAngles(direction);
	angles[PITCH] = 0.0f;
	angles[ROLL] = 0.0f;
	angles[YAW] = anglemod(angles[YAW]);
	return angles;
}

} // namespace

/*
================
Bot_BeginFrame
================
*/
void Bot_BeginFrame( gentity_t * bot ) {
	(void)bot;
	if (!Bot_RuntimeEnabled() || !Bot_RuntimeAasLoaded()) {
		return;
	}

}

/*
================
Bot_EndFrame
================
*/
void Bot_EndFrame( gentity_t * bot ) {
	(void)bot;
	if (!Bot_RuntimeEnabled() || !Bot_RuntimeAasLoaded()) {
		return;
	}

}

/*
================
Bot_BuildFrameCommand
================
*/
bool Bot_BuildFrameCommand( gentity_t * bot, usercmd_t * cmd ) {
	botFrameCommandStatus.frames++;

	if (!bot || !cmd || !bot->client) {
		botFrameCommandStatus.skippedInvalid++;
		return false;
	}

	if ((bot->svFlags & SVF_BOT) == 0 && !bot->client->sess.is_a_bot) {
		botFrameCommandStatus.skippedNotBot++;
		return false;
	}

	if (!Bot_RuntimeEnabled() || !Bot_RuntimeAasLoaded()) {
		botFrameCommandStatus.skippedRuntime++;
		return false;
	}

	if (!ClientIsPlaying(bot->client) || bot->client->eliminated || bot->deadFlag) {
		botFrameCommandStatus.skippedInactive++;
		return false;
	}

	BotLibAdapterRouteSteer route{};
	if (!BotNav_GetRouteSteer(bot, &route)) {
		return false;
	}

	*cmd = {};
	cmd->msec = Bot_CommandMsec();
	cmd->angles = Bot_CommandAnglesToTarget(bot, route);
	cmd->forwardMove = 180.0f;
	cmd->serverFrame = gi.ServerFrame();

	botFrameCommandStatus.commands++;
	botFrameCommandStatus.routeCommands++;
	return true;
}

/*
================
Bot_FrameCommandPrintStatus
================
*/
void Bot_FrameCommandPrintStatus( int expectedMinFrames, int expectedMinCommands ) {
	const BotNavRouteStatus &routeStatus = BotNav_GetRouteStatus();
	const int pass = (botFrameCommandStatus.frames >= expectedMinFrames &&
		botFrameCommandStatus.commands >= expectedMinCommands &&
		botFrameCommandStatus.routeCommands >= expectedMinCommands &&
		(expectedMinCommands <= 0 || routeStatus.failures == 0)) ? 1 : 0;

	base_import.Com_Print(
		G_Fmt("q3a_bot_frame_command_status frames={} commands={} "
			  "route_requests={} route_queries={} route_refreshes={} "
			  "route_reuses={} route_commands={} route_failures={} "
			  "route_invalid_slots={} route_cadence_refreshes={} "
			  "route_target_refreshes={} route_drift_refreshes={} "
			  "route_preferred_goal_refreshes={} route_debug_frames={} "
			  "route_debug_routes={} route_debug_goals={} "
			  "route_debug_missing_frames={} route_debug_lines={} "
			  "route_debug_crosses={} route_debug_arrows={} "
			  "route_debug_labels={} route_debug_polyline_points={} "
			  "route_debug_polyline_segments={} "
			  "route_debug_filtered_slots={} route_debug_filter_miss_frames={} "
			  "last_route_client={} last_route_debug_client={} "
			  "last_debug_filter_client={} "
			  "last_current_area={} last_start_area={} last_goal_area={} "
			  "last_route_end_area={} last_route_point_count={} last_travel_time={} "
			  "last_reachability={} last_reachability_type={} "
			  "last_reachability_flags={} last_reachability_end_area={} "
			  "last_stop_event={} "
			  "skipped_invalid={} skipped_not_bot={} skipped_runtime={} "
			  "skipped_inactive={} expected_min_frames={} "
			  "expected_min_commands={} pass={}\n",
			  botFrameCommandStatus.frames,
			  botFrameCommandStatus.commands,
			  routeStatus.requests,
			  routeStatus.queries,
			  routeStatus.refreshes,
			  routeStatus.reuses,
			  botFrameCommandStatus.routeCommands,
			  routeStatus.failures,
			  routeStatus.invalidSlots,
			  routeStatus.cadenceRefreshes,
			  routeStatus.targetRefreshes,
			  routeStatus.driftRefreshes,
			  routeStatus.preferredGoalRefreshes,
			  routeStatus.debugOverlayFrames,
			  routeStatus.debugOverlayRoutes,
			  routeStatus.debugOverlayGoals,
			  routeStatus.debugOverlayMissingFrames,
			  routeStatus.debugOverlayLines,
			  routeStatus.debugOverlayCrosses,
			  routeStatus.debugOverlayArrows,
			  routeStatus.debugOverlayLabels,
			  routeStatus.debugOverlayPolylinePoints,
			  routeStatus.debugOverlayPolylineSegments,
			  routeStatus.debugOverlayFilteredSlots,
			  routeStatus.debugOverlayFilterMissFrames,
			  routeStatus.lastClient,
			  routeStatus.lastDebugClient,
			  routeStatus.lastDebugFilterClient,
			  routeStatus.lastCurrentArea,
			  routeStatus.lastStartArea,
			  routeStatus.lastGoalArea,
			  routeStatus.lastRouteEndArea,
			  routeStatus.lastRoutePointCount,
			  routeStatus.lastTravelTime,
			  routeStatus.lastReachability,
			  routeStatus.lastReachabilityTravelType,
			  routeStatus.lastReachabilityTravelFlags,
			  routeStatus.lastReachabilityEndArea,
			  routeStatus.lastStopEvent,
			  botFrameCommandStatus.skippedInvalid,
			  botFrameCommandStatus.skippedNotBot,
			  botFrameCommandStatus.skippedRuntime,
			  botFrameCommandStatus.skippedInactive,
			  expectedMinFrames,
			  expectedMinCommands,
			  pass)
			.data());
}
