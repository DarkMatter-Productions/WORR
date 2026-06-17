// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once

#include "botlib_adapter.hpp"

struct gentity_t;

struct BotNavRouteStatus {
	int requests = 0;
	int queries = 0;
	int refreshes = 0;
	int reuses = 0;
	int failures = 0;
	int invalidSlots = 0;
	int cadenceRefreshes = 0;
	int targetRefreshes = 0;
	int driftRefreshes = 0;
	int preferredGoalRefreshes = 0;
	int debugOverlayFrames = 0;
	int debugOverlayRoutes = 0;
	int debugOverlayGoals = 0;
	int debugOverlayMissingFrames = 0;
	int debugOverlayLines = 0;
	int debugOverlayCrosses = 0;
	int debugOverlayArrows = 0;
	int debugOverlayLabels = 0;
	int debugOverlayPolylinePoints = 0;
	int debugOverlayPolylineSegments = 0;
	int debugOverlayFilteredSlots = 0;
	int debugOverlayFilterMissFrames = 0;
	int lastClient = -1;
	int lastDebugClient = -1;
	int lastDebugFilterClient = -1;
	int lastCurrentArea = 0;
	int lastStartArea = 0;
	int lastGoalArea = 0;
	int lastRouteEndArea = 0;
	int lastRoutePointCount = 0;
	int lastTravelTime = 0;
	int lastReachability = 0;
	int lastReachabilityTravelType = 0;
	int lastReachabilityTravelFlags = 0;
	int lastReachabilityEndArea = 0;
	int lastStopEvent = 0;
};

void BotNav_ResetAll();
void BotNav_ResetClient(int clientIndex);
bool BotNav_GetRouteSteer(const gentity_t *bot, BotLibAdapterRouteSteer *route);
bool BotNav_DrawDebugOverlay(bool drawRoute, bool drawGoal, int debugClientIndex);
const BotNavRouteStatus &BotNav_GetRouteStatus();
