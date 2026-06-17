// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "../g_local.hpp"
#include "bot_nav.hpp"

#include <algorithm>
#include <array>

namespace {

constexpr uint32_t BOT_NAV_ROUTE_REFRESH_FRAMES = 4;
constexpr float BOT_NAV_TARGET_REACHED_DIST_SQUARED = 16.0f * 16.0f;
constexpr float BOT_NAV_ROUTE_DRIFT_DIST_SQUARED = 96.0f * 96.0f;
constexpr float BOT_NAV_DEBUG_ROUTE_LIFETIME = 0.10f;
constexpr float BOT_NAV_DEBUG_CROSS_SIZE = 10.0f;
constexpr float BOT_NAV_DEBUG_LABEL_SIZE = 6.0f;

enum class BotNavRefreshReason {
	None,
	Invalid,
	Cadence,
	TargetReached,
	OriginDrift,
	PreferredGoal,
};

struct BotNavRouteSlot {
	bool valid = false;
	uint32_t nextRefreshFrame = 0;
	int preferredGoalArea = 0;
	Vector3 origin = vec3_origin;
	BotLibAdapterRouteSteer route{};
};

std::array<BotNavRouteSlot, MAX_CLIENTS> botNavRouteSlots{};
BotNavRouteStatus botNavRouteStatus;

int BotNavClientIndex(const gentity_t *bot) {
	if (bot == nullptr || bot->client == nullptr || bot->s.number <= 0) {
		return -1;
	}

	const int clientIndex = static_cast<int>(bot->s.number) - 1;
	if (clientIndex < 0 ||
		clientIndex >= static_cast<int>(botNavRouteSlots.size()) ||
		clientIndex >= static_cast<int>(game.maxClients)) {
		return -1;
	}

	return clientIndex;
}

Vector3 BotNavRouteTarget(const BotLibAdapterRouteSteer &route) {
	return { route.moveTarget[0], route.moveTarget[1], route.moveTarget[2] };
}

Vector3 BotNavRouteGoal(const BotLibAdapterRouteSteer &route) {
	return { route.goalOrigin[0], route.goalOrigin[1], route.goalOrigin[2] };
}

Vector3 BotNavRoutePoint(const BotLibAdapterRouteSteer &route, int pointIndex) {
	return {
		route.routePoints[pointIndex][0],
		route.routePoints[pointIndex][1],
		route.routePoints[pointIndex][2]
	};
}

int BotNavRoutePointCount(const BotLibAdapterRouteSteer &route) {
	return std::clamp(route.routePointCount, 0, BOTLIB_ADAPTER_MAX_ROUTE_POINTS);
}

Vector3 BotNavDebugPoint(const Vector3 &point) {
	return point + Vector3{ 0.0f, 0.0f, 8.0f };
}

float BotNavHorizontalDistanceSquared(const Vector3 &a, const Vector3 &b) {
	const float dx = a.x - b.x;
	const float dy = a.y - b.y;
	return dx * dx + dy * dy;
}

BotNavRefreshReason BotNavRefreshReasonFor(
	const BotNavRouteSlot &slot,
	const gentity_t *bot,
	int preferredGoalArea,
	uint32_t frame) {
	if (!slot.valid) {
		return BotNavRefreshReason::Invalid;
	}
	if (slot.preferredGoalArea != preferredGoalArea) {
		return BotNavRefreshReason::PreferredGoal;
	}
	if (frame >= slot.nextRefreshFrame) {
		return BotNavRefreshReason::Cadence;
	}
	if (BotNavHorizontalDistanceSquared(bot->s.origin, BotNavRouteTarget(slot.route)) <=
		BOT_NAV_TARGET_REACHED_DIST_SQUARED) {
		return BotNavRefreshReason::TargetReached;
	}
	if (BotNavHorizontalDistanceSquared(bot->s.origin, slot.origin) >= BOT_NAV_ROUTE_DRIFT_DIST_SQUARED) {
		return BotNavRefreshReason::OriginDrift;
	}

	return BotNavRefreshReason::None;
}

void BotNavRecordRefresh(BotNavRefreshReason reason) {
	switch (reason) {
	case BotNavRefreshReason::Cadence:
		botNavRouteStatus.cadenceRefreshes++;
		break;
	case BotNavRefreshReason::TargetReached:
		botNavRouteStatus.targetRefreshes++;
		break;
	case BotNavRefreshReason::OriginDrift:
		botNavRouteStatus.driftRefreshes++;
		break;
	case BotNavRefreshReason::PreferredGoal:
		botNavRouteStatus.preferredGoalRefreshes++;
		break;
	default:
		break;
	}
}

void BotNavRecordRoute(int clientIndex, const BotLibAdapterRouteSteer &route) {
	botNavRouteStatus.lastClient = clientIndex;
	botNavRouteStatus.lastCurrentArea = route.startArea;
	botNavRouteStatus.lastStartArea = route.startArea;
	botNavRouteStatus.lastGoalArea = route.goalArea;
	botNavRouteStatus.lastRouteEndArea = route.routeEndArea;
	botNavRouteStatus.lastRoutePointCount = BotNavRoutePointCount(route);
	botNavRouteStatus.lastTravelTime = route.travelTime;
	botNavRouteStatus.lastReachability = route.reachability;
	botNavRouteStatus.lastReachabilityTravelType = route.reachabilityTravelType;
	botNavRouteStatus.lastReachabilityTravelFlags = route.reachabilityTravelFlags;
	botNavRouteStatus.lastReachabilityEndArea = route.reachabilityEndArea;
	botNavRouteStatus.lastStopEvent = route.stopEvent;
}

const char *BotNavTravelTypeName(int travelType) {
	switch (travelType) {
	case 2:
		return "walk";
	case 3:
		return "crouch";
	case 4:
		return "barrier_jump";
	case 5:
		return "jump";
	case 6:
		return "ladder";
	case 7:
		return "walkoffledge";
	case 8:
		return "swim";
	case 9:
		return "waterjump";
	case 10:
		return "teleport";
	case 11:
		return "elevator";
	case 12:
		return "rocketjump";
	case 13:
		return "bfgjump";
	case 14:
		return "grapplehook";
	case 15:
		return "doublejump";
	case 16:
		return "rampjump";
	case 17:
		return "strafejump";
	case 18:
		return "jumppad";
	case 19:
		return "funcbob";
	default:
		return "unknown";
	}
}

gentity_t *BotNavClientEntity(int clientIndex) {
	if (g_entities == nullptr || clientIndex < 0 || clientIndex >= static_cast<int>(game.maxClients)) {
		return nullptr;
	}

	const int entnum = clientIndex + 1;
	if (entnum <= 0 || entnum >= static_cast<int>(game.maxEntities)) {
		return nullptr;
	}

	gentity_t *ent = &g_entities[entnum];
	if (!ent->inUse || ent->client == nullptr) {
		return nullptr;
	}
	if ((ent->svFlags & SVF_BOT) == 0 && !ent->client->sess.is_a_bot) {
		return nullptr;
	}

	return ent;
}

void BotNavDrawCross(const Vector3 &origin, float size, const rgba_t &color, float lifeTime) {
	const float halfSize = std::max(size, 1.0f);
	gi.Draw_Line(
		origin + Vector3{ -halfSize, 0.0f, 0.0f },
		origin + Vector3{ halfSize, 0.0f, 0.0f },
		color,
		lifeTime,
		false);
	gi.Draw_Line(
		origin + Vector3{ 0.0f, -halfSize, 0.0f },
		origin + Vector3{ 0.0f, halfSize, 0.0f },
		color,
		lifeTime,
		false);
	gi.Draw_Line(
		origin + Vector3{ 0.0f, 0.0f, -halfSize },
		origin + Vector3{ 0.0f, 0.0f, halfSize },
		color,
		lifeTime,
		false);
	botNavRouteStatus.debugOverlayCrosses++;
}

void BotNavDrawCachedRoute(int clientIndex, const gentity_t *bot, const BotLibAdapterRouteSteer &route, bool drawRoute, bool drawGoal) {
	const float lifeTime = std::max(gi.frameTimeSec * 2.0f, BOT_NAV_DEBUG_ROUTE_LIFETIME);
	const Vector3 botOrigin = BotNavDebugPoint(bot->s.origin);
	const Vector3 moveTarget = BotNavDebugPoint(BotNavRouteTarget(route));
	const Vector3 goalOrigin = BotNavDebugPoint(BotNavRouteGoal(route));
	const int routePointCount = BotNavRoutePointCount(route);

	if (drawRoute) {
		Vector3 previousPoint = botOrigin;
		if (routePointCount > 0) {
			for (int pointIndex = 0; pointIndex < routePointCount; ++pointIndex) {
				const Vector3 routePoint = BotNavDebugPoint(BotNavRoutePoint(route, pointIndex));
				if (pointIndex == 0) {
					gi.Draw_Arrow(previousPoint, routePoint, 8.0f, rgba_cyan, rgba_yellow, lifeTime, false);
					botNavRouteStatus.debugOverlayArrows++;
				} else {
					gi.Draw_Line(previousPoint, routePoint, rgba_cyan, lifeTime, false);
					botNavRouteStatus.debugOverlayLines++;
				}
				botNavRouteStatus.debugOverlayPolylineSegments++;
				botNavRouteStatus.debugOverlayPolylinePoints++;
				previousPoint = routePoint;
			}

			if ((goalOrigin - previousPoint).lengthSquared() > 1.0f) {
				gi.Draw_Line(previousPoint, goalOrigin, rgba_green, lifeTime, false);
				botNavRouteStatus.debugOverlayLines++;
				botNavRouteStatus.debugOverlayPolylineSegments++;
			}
		} else {
			gi.Draw_Arrow(botOrigin, moveTarget, 8.0f, rgba_cyan, rgba_yellow, lifeTime, false);
			gi.Draw_Line(moveTarget, goalOrigin, rgba_green, lifeTime, false);
			botNavRouteStatus.debugOverlayArrows++;
			botNavRouteStatus.debugOverlayLines++;
			botNavRouteStatus.debugOverlayPolylineSegments += 2;
		}

		BotNavDrawCross(moveTarget, BOT_NAV_DEBUG_CROSS_SIZE, rgba_yellow, lifeTime);
		botNavRouteStatus.debugOverlayRoutes++;

		const auto label = G_Fmt(
			"area {} reach {} {} -> {} pts {}",
			route.startArea,
			route.reachability,
			BotNavTravelTypeName(route.reachabilityTravelType),
			route.reachabilityEndArea,
			routePointCount);
		gi.Draw_OrientedWorldText(
			botOrigin + Vector3{ 0.0f, 0.0f, 32.0f },
			label.data(),
			rgba_cyan,
			BOT_NAV_DEBUG_LABEL_SIZE,
			lifeTime,
			false);
		botNavRouteStatus.debugOverlayLabels++;
	}

	if (drawGoal) {
		BotNavDrawCross(goalOrigin, BOT_NAV_DEBUG_CROSS_SIZE + 2.0f, rgba_green, lifeTime);
		botNavRouteStatus.debugOverlayGoals++;
	}

	botNavRouteStatus.lastDebugClient = clientIndex;
}

bool BotNavRefreshRoute(
	const gentity_t *bot,
	int clientIndex,
	int preferredGoalArea,
	BotNavRefreshReason reason,
	BotLibAdapterRouteSteer *route) {
	BotLibAdapterRouteSteer refreshedRoute{};
	const float origin[3] = {
		bot->s.origin.x,
		bot->s.origin.y,
		bot->s.origin.z
	};

	botNavRouteStatus.queries++;
	BotNavRecordRefresh(reason);

	if (!BotLibAdapter_BuildRouteSteer(origin, preferredGoalArea, &refreshedRoute) || !refreshedRoute.success) {
		botNavRouteSlots[clientIndex].valid = false;
		botNavRouteStatus.failures++;
		BotNavRecordRoute(clientIndex, refreshedRoute);
		return false;
	}

	BotNavRouteSlot &slot = botNavRouteSlots[clientIndex];
	slot.valid = true;
	slot.nextRefreshFrame = gi.ServerFrame() + BOT_NAV_ROUTE_REFRESH_FRAMES;
	slot.preferredGoalArea = preferredGoalArea;
	slot.origin = bot->s.origin;
	slot.route = refreshedRoute;

	botNavRouteStatus.refreshes++;
	BotNavRecordRoute(clientIndex, refreshedRoute);
	if (route != nullptr) {
		*route = refreshedRoute;
	}
	return true;
}

} // namespace

void BotNav_ResetAll() {
	botNavRouteSlots = {};
	botNavRouteStatus = {};
}

void BotNav_ResetClient(int clientIndex) {
	if (clientIndex < 0 || clientIndex >= static_cast<int>(botNavRouteSlots.size())) {
		return;
	}

	botNavRouteSlots[clientIndex] = {};
}

bool BotNav_GetRouteSteer(const gentity_t *bot, BotLibAdapterRouteSteer *route) {
	const int clientIndex = BotNavClientIndex(bot);
	const int preferredGoalArea = 0;
	const uint32_t frame = gi.ServerFrame();

	botNavRouteStatus.requests++;
	if (clientIndex < 0) {
		botNavRouteStatus.invalidSlots++;
		return false;
	}

	BotNavRouteSlot &slot = botNavRouteSlots[clientIndex];
	const BotNavRefreshReason reason = BotNavRefreshReasonFor(slot, bot, preferredGoalArea, frame);
	if (reason == BotNavRefreshReason::None) {
		botNavRouteStatus.reuses++;
		BotNavRecordRoute(clientIndex, slot.route);
		if (route != nullptr) {
			*route = slot.route;
		}
		return true;
	}

	return BotNavRefreshRoute(bot, clientIndex, preferredGoalArea, reason, route);
}

bool BotNav_DrawDebugOverlay(bool drawRoute, bool drawGoal, int debugClientIndex) {
	if (!drawRoute && !drawGoal) {
		return false;
	}

	botNavRouteStatus.debugOverlayFrames++;
	botNavRouteStatus.lastDebugFilterClient = debugClientIndex;

	bool drewAny = false;
	const int clientCount = std::min(
		static_cast<int>(botNavRouteSlots.size()),
		static_cast<int>(game.maxClients));
	for (int clientIndex = 0; clientIndex < clientCount; ++clientIndex) {
		const BotNavRouteSlot &slot = botNavRouteSlots[clientIndex];
		if (!slot.valid) {
			continue;
		}
		if (debugClientIndex >= 0 && clientIndex != debugClientIndex) {
			botNavRouteStatus.debugOverlayFilteredSlots++;
			continue;
		}

		gentity_t *bot = BotNavClientEntity(clientIndex);
		if (bot == nullptr) {
			continue;
		}

		BotNavDrawCachedRoute(clientIndex, bot, slot.route, drawRoute, drawGoal);
		drewAny = true;
	}

	if (!drewAny) {
		botNavRouteStatus.debugOverlayMissingFrames++;
		if (debugClientIndex >= 0) {
			botNavRouteStatus.debugOverlayFilterMissFrames++;
		}
	}

	return drewAny;
}

const BotNavRouteStatus &BotNav_GetRouteStatus() {
	return botNavRouteStatus;
}
