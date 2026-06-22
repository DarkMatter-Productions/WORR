// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "../g_local.hpp"
#include "../../bgame/logger.hpp"
#include "bot_brain.hpp"
#include "bot_nav.hpp"
#include "botlib_adapter.hpp"
#include "bot_runtime.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace {
constexpr int32_t AAS_ID = ('S' << 24) + ('A' << 16) + ('A' << 8) + 'E';
constexpr int32_t AAS_VERSION = 5;
constexpr size_t AAS_LUMP_COUNT = 14;
constexpr size_t AAS_HEADER_SIZE = 12 + AAS_LUMP_COUNT * 8;
constexpr int32_t Q2_BSP_ID = ('P' << 24) + ('S' << 16) + ('B' << 8) + 'I';
constexpr int32_t Q2_BSP_VERSION = 38;
constexpr size_t Q2_BSP_LUMP_COUNT = 19;
constexpr size_t Q2_BSP_HEADER_SIZE = 8 + Q2_BSP_LUMP_COUNT * 8;

enum AasLumpIndex : size_t {
	AAS_LUMP_AREAS = 7,
	AAS_LUMP_AREA_SETTINGS = 8,
	AAS_LUMP_REACHABILITY = 9,
	AAS_LUMP_CLUSTERS = 13,
};

enum BspLumpIndex : size_t {
	Q2_BSP_LUMP_ENTITIES = 0,
	Q2_BSP_LUMP_MODELS = 13,
};

constexpr int32_t AAS_AREA_SIZE = 48;
constexpr int32_t AAS_AREA_SETTINGS_SIZE = 28;
constexpr int32_t AAS_REACHABILITY_SIZE = 44;
constexpr int32_t AAS_CLUSTER_SIZE = 16;
constexpr int32_t Q2_BSP_MODEL_SIZE = 48;

struct AasLump {
	int32_t offset = 0;
	int32_t length = 0;
};

struct BspLump {
	int32_t offset = 0;
	int32_t length = 0;
};

BotAasRuntimeStatus botRuntimeStatus;
GameTime lastDebugPrintTime = 0_ms;
GameTime lastDebugDrawSmokeTime = 0_ms;
int lifecycleSmokePhase = 0;

struct BotFilesystemApiV1 {
	int64_t (*OpenFile)(const char *path, fs_handle_t *file, unsigned mode);
	int (*CloseFile)(fs_handle_t file);
	int (*LoadFile)(const char *path, void **buffer, unsigned flags, unsigned tag);
};

constexpr char BOT_FILESYSTEM_API_V1[] = "FILESYSTEM_API_V1";

const BotFilesystemApiV1 *Bot_GetFilesystem() {
	return static_cast<const BotFilesystemApiV1 *>(gi.GetExtension(BOT_FILESYSTEM_API_V1));
}

int32_t ReadLittleInt32(const unsigned char *data) {
	const uint32_t value = static_cast<uint32_t>(data[0]) |
		(static_cast<uint32_t>(data[1]) << 8) |
		(static_cast<uint32_t>(data[2]) << 16) |
		(static_cast<uint32_t>(data[3]) << 24);
	return static_cast<int32_t>(value);
}

void DecodeAasV5Data(unsigned char *data, size_t size) {
	for (size_t i = 0; i < size; ++i) {
		data[i] ^= static_cast<unsigned char>(i * 119);
	}
}

int32_t CountFixedSizeLump(const AasLump &lump, int32_t elementSize) {
	if (elementSize <= 0 || lump.length <= 0 || (lump.length % elementSize) != 0) {
		return 0;
	}
	return lump.length / elementSize;
}

bool ValidateAasHeader(
	const unsigned char *data,
	int64_t fileSize,
	std::array<AasLump, AAS_LUMP_COUNT> &lumps,
	std::string &message) {
	if (fileSize < static_cast<int64_t>(AAS_HEADER_SIZE)) {
		message = "AAS file is smaller than the header";
		return false;
	}

	std::array<unsigned char, AAS_HEADER_SIZE> header{};
	std::copy_n(data, AAS_HEADER_SIZE, header.begin());

	const int32_t ident = ReadLittleInt32(header.data());
	if (ident != AAS_ID) {
		message = "AAS header ident is not EAAS";
		return false;
	}

	const int32_t version = ReadLittleInt32(header.data() + 4);
	if (version != AAS_VERSION) {
		message = G_Fmt("unsupported AAS version {}; expected {}", version, AAS_VERSION);
		return false;
	}

	DecodeAasV5Data(header.data() + 8, AAS_HEADER_SIZE - 8);

	for (size_t i = 0; i < AAS_LUMP_COUNT; ++i) {
		const size_t offset = 12 + i * 8;
		lumps[i].offset = ReadLittleInt32(header.data() + offset);
		lumps[i].length = ReadLittleInt32(header.data() + offset + 4);

		if (lumps[i].offset < 0 || lumps[i].length < 0) {
			message = G_Fmt("AAS lump {} has a negative offset or length", i);
			return false;
		}

		const int64_t lumpEnd = static_cast<int64_t>(lumps[i].offset) + lumps[i].length;
		if (lumpEnd < lumps[i].offset || lumpEnd > fileSize) {
			message = G_Fmt("AAS lump {} extends outside the file", i);
			return false;
		}
	}

	message = "AAS header valid";
	return true;
}

void SetRuntimeState(BotAasRuntimeState state, std::string message) {
	botRuntimeStatus.state = state;
	botRuntimeStatus.message = std::move(message);
}

void ResetRuntimeStatusForMap() {
	botRuntimeStatus = {};
	botRuntimeStatus.enabled = Bot_RuntimeEnabled();
	botRuntimeStatus.mapName = level.mapName.data();
	if (!botRuntimeStatus.mapName.empty()) {
		botRuntimeStatus.aasPath = G_Fmt("maps/{}.aas", botRuntimeStatus.mapName.c_str());
		botRuntimeStatus.bspPath = G_Fmt("maps/{}.bsp", botRuntimeStatus.mapName.c_str());
	}
	botRuntimeStatus.state = botRuntimeStatus.enabled ? BotAasRuntimeState::NotLoaded : BotAasRuntimeState::Disabled;
}

int CurrentBotRuntimeMilliseconds() {
	return static_cast<int>(std::clamp<int64_t>(
		level.time.milliseconds(),
		0,
		static_cast<int64_t>(std::numeric_limits<int>::max())));
}

constexpr int Q3A_ENTITYNUM_NONE = 1023;
constexpr int Q3A_LINECOLOR_RED = 1;
constexpr int Q3A_LINECOLOR_GREEN = 2;
constexpr int Q3A_LINECOLOR_BLUE = 3;
constexpr int Q3A_LINECOLOR_YELLOW = 4;
constexpr int Q3A_LINECOLOR_ORANGE = 5;
constexpr int Q3A_PRT_MESSAGE = 1;
constexpr int Q3A_PRT_WARNING = 2;
constexpr int Q3A_PRT_ERROR = 3;
constexpr int Q3A_PRT_FATAL = 4;
constexpr uint32_t Q3A_CONTENTS_SOLID = 0x00000001u;
constexpr uint32_t Q3A_CONTENTS_PLAYERCLIP = 0x00010000u;
constexpr uint32_t Q3A_CONTENTS_MONSTERCLIP = 0x00020000u;
constexpr uint32_t Q3A_CONTENTS_BODY = 0x02000000u;
constexpr uint32_t Q3A_CONTENTS_CORPSE = 0x04000000u;

void CopyVectorToBotLib(float dest[3], const gvec3_t &source) {
	dest[0] = source[0];
	dest[1] = source[1];
	dest[2] = source[2];
}

Vector3 CopyVectorFromBotLib(const float source[3]) {
	if (source == nullptr) {
		return vec3_origin;
	}
	return { source[0], source[1], source[2] };
}

bool BotRuntimeDebugDrawEnabled() {
	return (sg_bot_debug_aas != nullptr && sg_bot_debug_aas->integer >= 3) ||
		(sg_bot_debug_route != nullptr && sg_bot_debug_route->integer != 0) ||
		(sg_bot_debug_goal != nullptr && sg_bot_debug_goal->integer != 0);
}

bool BotRuntimeQ3APrintMessageEnabled() {
	return sg_bot_debug_aas != nullptr && sg_bot_debug_aas->integer >= 3;
}

const char *BotRuntimeQ3APrintTypeName(int type) {
	switch (type) {
	case Q3A_PRT_WARNING:
		return "warning";
	case Q3A_PRT_ERROR:
		return "error";
	case Q3A_PRT_FATAL:
		return "fatal";
	default:
		return "message";
	}
}

void BotRuntimeQ3APrint(int type, const char *message) {
	if (message == nullptr || message[0] == '\0') {
		return;
	}

	if (type == Q3A_PRT_MESSAGE && !BotRuntimeQ3APrintMessageEnabled()) {
		return;
	}

	std::string text = message;
	while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
		text.pop_back();
	}

	if (text.empty()) {
		return;
	}

	gi.Com_PrintFmt("Q3A BotLib {}: {}\n", BotRuntimeQ3APrintTypeName(type), text.c_str());
}

bool BotRuntimeBotClientCommand(int client, const char *command) {
	if (command == nullptr || command[0] == '\0' || g_entities == nullptr) {
		return false;
	}
	if (client < 0 || client >= static_cast<int>(game.maxClients)) {
		return false;
	}

	const int entnum = client + 1;
	if (entnum <= 0 || entnum >= static_cast<int>(game.maxEntities)) {
		return false;
	}

	gentity_t *ent = &g_entities[entnum];
	if (!ent->inUse || ent->client == nullptr) {
		return false;
	}
	if ((ent->svFlags & SVF_BOT) == 0 && !ent->client->sess.is_a_bot) {
		return false;
	}

	if (sg_bot_debug_aas != nullptr && sg_bot_debug_aas->integer >= 3) {
		gi.Com_PrintFmt(
			"Q3A BotClientCommand rejected until bot command dispatch is implemented: client={} command=\"{}\"\n",
			client,
			command);
	}
	return false;
}

int BotRuntimeQ3AFilesystemLoad(const char *path, const unsigned char **data) {
	if (data != nullptr) {
		*data = nullptr;
	}
	if (path == nullptr || path[0] == '\0' || data == nullptr) {
		return -1;
	}

	const BotFilesystemApiV1 *fs = Bot_GetFilesystem();
	if (fs == nullptr || fs->LoadFile == nullptr) {
		return -1;
	}

	void *buffer = nullptr;
	const int length = fs->LoadFile(path, &buffer, 0, TAG_LEVEL);
	if (length <= 0 || buffer == nullptr) {
		if (buffer != nullptr) {
			gi.TagFree(buffer);
		}
		return -1;
	}

	*data = static_cast<const unsigned char *>(buffer);
	return length;
}

void BotRuntimeQ3AFilesystemFree(const unsigned char *data) {
	if (data == nullptr) {
		return;
	}

	gi.TagFree(const_cast<unsigned char *>(data));
}

rgba_t BotRuntimeQ3ADebugColor(int color) {
	switch (color) {
	case Q3A_LINECOLOR_RED:
		return rgba_red;
	case Q3A_LINECOLOR_GREEN:
		return rgba_green;
	case Q3A_LINECOLOR_BLUE:
		return rgba_blue;
	case Q3A_LINECOLOR_YELLOW:
		return rgba_yellow;
	case Q3A_LINECOLOR_ORANGE:
		return rgba_orange;
	default:
		return rgba_white;
	}
}

void BotRuntimeDrawCross(const Vector3 &origin, float size, const rgba_t &color, float lifeTime) {
	const float halfSize = std::max(size, 1.0f);
	gi.Draw_Line(origin + Vector3{ -halfSize, 0.0f, 0.0f }, origin + Vector3{ halfSize, 0.0f, 0.0f }, color, lifeTime, false);
	gi.Draw_Line(origin + Vector3{ 0.0f, -halfSize, 0.0f }, origin + Vector3{ 0.0f, halfSize, 0.0f }, color, lifeTime, false);
	gi.Draw_Line(origin + Vector3{ 0.0f, 0.0f, -halfSize }, origin + Vector3{ 0.0f, 0.0f, halfSize }, color, lifeTime, false);
}

bool BotRuntimeDebugDraw(
	int primitive,
	const float start[3],
	const float end[3],
	float size,
	int color,
	int secondaryColor) {
	if (!BotRuntimeDebugDrawEnabled()) {
		return false;
	}

	const Vector3 startVec = CopyVectorFromBotLib(start);
	const Vector3 endVec = CopyVectorFromBotLib(end);
	const rgba_t primaryColor = BotRuntimeQ3ADebugColor(color);
	const rgba_t secondary = BotRuntimeQ3ADebugColor(secondaryColor);
	const float frameLifeTime = std::max(gi.frameTimeSec * 2.0f, 0.10f);

	switch (primitive) {
	case BotLibAdapterDebugDrawClear:
		return true;
	case BotLibAdapterDebugDrawLine:
		gi.Draw_Line(startVec, endVec, primaryColor, frameLifeTime, false);
		return true;
	case BotLibAdapterDebugDrawPermanentLine:
		gi.Draw_Line(startVec, endVec, primaryColor, 5.0f, false);
		return true;
	case BotLibAdapterDebugDrawCross:
		BotRuntimeDrawCross(startVec, size, primaryColor, frameLifeTime);
		return true;
	case BotLibAdapterDebugDrawArrow:
		gi.Draw_Arrow(startVec, endVec, std::max(size, 8.0f), primaryColor, secondary, frameLifeTime, false);
		return true;
	default:
		return false;
	}
}

bool BotRuntimeDebugPolygon(int color, int pointCount, const float *points) {
	if (!BotRuntimeDebugDrawEnabled() || points == nullptr || pointCount < 3) {
		return false;
	}

	const rgba_t polygonColor = BotRuntimeQ3ADebugColor(color);
	const float frameLifeTime = std::max(gi.frameTimeSec * 2.0f, 0.10f);
	auto pointAt = [points](int index) {
		const float *point = points + index * 3;
		return Vector3{ point[0], point[1], point[2] };
	};

	for (int i = 0; i < pointCount; ++i) {
		gi.Draw_Line(pointAt(i), pointAt((i + 1) % pointCount), polygonColor, frameLifeTime, false);
	}

	for (int i = 2; i + 1 < pointCount; ++i) {
		gi.Draw_Line(pointAt(0), pointAt(i), polygonColor, frameLifeTime, false);
	}

	return true;
}

contents_t BotRuntimeMapQ3AEntityTraceMask(int contentmask) {
	const uint32_t q3aMask = static_cast<uint32_t>(contentmask);
	contents_t mask = CONTENTS_NONE;

	if ((q3aMask & Q3A_CONTENTS_SOLID) != 0) {
		mask |= CONTENTS_SOLID | CONTENTS_WINDOW;
	}
	if ((q3aMask & Q3A_CONTENTS_PLAYERCLIP) != 0) {
		mask |= CONTENTS_PLAYERCLIP | CONTENTS_PLAYER | CONTENTS_MONSTER;
	}
	if ((q3aMask & Q3A_CONTENTS_MONSTERCLIP) != 0) {
		mask |= CONTENTS_MONSTERCLIP | CONTENTS_MONSTER | CONTENTS_PLAYER;
	}
	if ((q3aMask & Q3A_CONTENTS_BODY) != 0) {
		mask |= CONTENTS_MONSTER | CONTENTS_PLAYER;
	}
	if ((q3aMask & Q3A_CONTENTS_CORPSE) != 0) {
		mask |= CONTENTS_DEADMONSTER;
	}

	return mask != CONTENTS_NONE ? mask : (MASK_NAV_SOLID | CONTENTS_PLAYER | CONTENTS_MONSTER);
}

int BotRuntimeEntityNumber(const gentity_t *ent) {
	if (ent == nullptr || g_entities == nullptr) {
		return Q3A_ENTITYNUM_NONE;
	}

	const ptrdiff_t index = ent - g_entities;
	if (index < 0 || index >= static_cast<ptrdiff_t>(game.maxEntities)) {
		return Q3A_ENTITYNUM_NONE;
	}

	return static_cast<int>(index);
}

bool BotRuntimeIsObjectiveEntity(const gentity_t *ent) {
	if (ent == nullptr) {
		return false;
	}
	if ((ent->sv.entFlags & SVFL_OBJECTIVE_DROPPED) != 0) {
		return true;
	}
	if (ent->item == nullptr) {
		return false;
	}

	switch (ent->item->id) {
	case IT_FLAG_RED:
	case IT_FLAG_BLUE:
	case IT_FLAG_NEUTRAL:
		return true;
	default:
		return false;
	}
}

bool BotRuntimeIsDroppedItemEntity(const gentity_t *ent) {
	return ent != nullptr &&
		ent->item != nullptr &&
		(ent->spawnFlags.has(SPAWNFLAG_ITEM_DROPPED) ||
		 ent->spawnFlags.has(SPAWNFLAG_ITEM_DROPPED_PLAYER));
}

bool BotRuntimeIsHazardEntity(const gentity_t *ent) {
	return ent != nullptr &&
		((ent->sv.entFlags & SVFL_TRAP_DANGER) != 0 ||
		 (ent->svFlags & SVF_PROJECTILE) != 0 ||
		 ent->moveType == MoveType::FlyMissile ||
		 ent->moveType == MoveType::Bounce);
}

int BotRuntimeEntityType(const gentity_t *ent) {
	if (ent->client != nullptr) {
		if (ent->client->sess.team == Team::Spectator ||
			(ent->sv.entFlags & SVFL_IS_SPECTATOR) != 0) {
			return BotLibAdapterEntitySpectator;
		}
		if ((ent->svFlags & SVF_BOT) != 0 || ent->client->sess.is_a_bot) {
			return BotLibAdapterEntityBot;
		}
		return BotLibAdapterEntityPlayer;
	}
	if ((ent->svFlags & SVF_MONSTER) != 0) {
		return BotLibAdapterEntityMonster;
	}
	if (BotRuntimeIsObjectiveEntity(ent)) {
		return BotLibAdapterEntityObjective;
	}
	if (BotRuntimeIsDroppedItemEntity(ent)) {
		return BotLibAdapterEntityDroppedItem;
	}
	if ((ent->sv.entFlags & SVFL_IS_ITEM) != 0) {
		return BotLibAdapterEntityItem;
	}
	if (BotRuntimeIsHazardEntity(ent)) {
		return BotLibAdapterEntityHazard;
	}
	if (ent->solid == SOLID_BSP || ent->moveType == MoveType::Push) {
		return BotLibAdapterEntityMover;
	}
	return BotLibAdapterEntityGeneral;
}

int BotRuntimeQ3ABspModelIndex(const gentity_t *ent) {
	if (ent == nullptr || ent->solid != SOLID_BSP || ent->s.modelIndex <= 0) {
		return ent != nullptr ? ent->s.modelIndex : 0;
	}

	int modelIndex = ent->s.modelIndex - 1;
	if (modelIndex >= MODELINDEX_PLAYER) {
		--modelIndex;
	}
	return modelIndex;
}

bool BotRuntimeBuildEntitySnapshot(gentity_t *ent, BotLibAdapterEntitySnapshot &snapshot) {
	if (ent == nullptr || !ent->inUse || !ent->linked || !ent->sv.init || (ent->svFlags & SVF_NOCLIENT) != 0) {
		return false;
	}

	snapshot.type = BotRuntimeEntityType(ent);
	snapshot.flags = static_cast<int>(static_cast<uint64_t>(ent->sv.entFlags) & 0x7fffffff);
	CopyVectorToBotLib(snapshot.origin, ent->s.origin);
	CopyVectorToBotLib(snapshot.angles, ent->client != nullptr ? ent->sv.viewAngles : ent->s.angles);
	CopyVectorToBotLib(snapshot.oldOrigin, ent->s.oldOrigin);
	CopyVectorToBotLib(snapshot.mins, ent->mins);
	CopyVectorToBotLib(snapshot.maxs, ent->maxs);
	snapshot.groundent = BotRuntimeEntityNumber(ent->sv.groundEntity);
	snapshot.solid = static_cast<int>(ent->solid);
	snapshot.modelindex = BotRuntimeQ3ABspModelIndex(ent);
	snapshot.modelindex2 = ent->s.modelIndex2;
	snapshot.frame = ent->s.frame;
	snapshot.event = static_cast<int>(ent->s.event);
	snapshot.eventParm = 0;
	snapshot.powerups = 0;
	snapshot.weapon = ent->sv.weapon;
	snapshot.legsAnim = 0;
	snapshot.torsoAnim = 0;
	return true;
}

void CountRuntimeEntitySnapshot(int type) {
	switch (type) {
	case BotLibAdapterEntityPlayer:
		++botRuntimeStatus.entitySnapshotPlayers;
		break;
	case BotLibAdapterEntityBot:
		++botRuntimeStatus.entitySnapshotBots;
		break;
	case BotLibAdapterEntitySpectator:
		++botRuntimeStatus.entitySnapshotSpectators;
		break;
	case BotLibAdapterEntityMonster:
		++botRuntimeStatus.entitySnapshotMonsters;
		break;
	case BotLibAdapterEntityItem:
		++botRuntimeStatus.entitySnapshotItems;
		break;
	case BotLibAdapterEntityDroppedItem:
		++botRuntimeStatus.entitySnapshotDroppedItems;
		break;
	case BotLibAdapterEntityMissile:
	case BotLibAdapterEntityHazard:
		++botRuntimeStatus.entitySnapshotHazards;
		break;
	case BotLibAdapterEntityMover:
		++botRuntimeStatus.entitySnapshotMovers;
		break;
	case BotLibAdapterEntityObjective:
		++botRuntimeStatus.entitySnapshotObjectives;
		break;
	default:
		break;
	}
}

bool BotRuntimeEntityTrace(
	int entnum,
	const float start[3],
	const float mins[3],
	const float maxs[3],
	const float end[3],
	int contentmask,
	BotLibAdapterTraceResult *trace) {
	if (trace == nullptr) {
		return false;
	}

	trace->hit = false;
	trace->allSolid = false;
	trace->startSolid = false;
	trace->fraction = 1.0f;
	CopyVectorToBotLib(trace->endPos, CopyVectorFromBotLib(end));
	trace->planeDist = 0.0f;
	trace->contents = 0;
	trace->entnum = entnum;

	if (g_entities == nullptr || entnum < 0 || entnum >= static_cast<int>(game.maxEntities)) {
		return true;
	}

	gentity_t *target = &g_entities[entnum];
	if (!target->inUse || !target->linked || (target->solid != SOLID_BBOX && target->solid != SOLID_BSP)) {
		return true;
	}

	const Vector3 startVec = CopyVectorFromBotLib(start);
	const Vector3 minsVec = CopyVectorFromBotLib(mins);
	const Vector3 maxsVec = CopyVectorFromBotLib(maxs);
	const Vector3 endVec = CopyVectorFromBotLib(end);
	const contents_t mappedMask = BotRuntimeMapQ3AEntityTraceMask(contentmask);
	const trace_t result = gi.clip(target, startVec, minsVec, maxsVec, endVec, mappedMask);

	trace->hit = result.fraction < 1.0f || result.startSolid || result.allSolid;
	trace->allSolid = result.allSolid;
	trace->startSolid = result.startSolid;
	trace->fraction = result.fraction;
	CopyVectorToBotLib(trace->endPos, result.endPos);
	CopyVectorToBotLib(trace->planeNormal, result.plane.normal);
	trace->planeDist = result.plane.dist;
	trace->contents = static_cast<int>(static_cast<uint32_t>(result.contents));
	const int hitEntnum = BotRuntimeEntityNumber(result.ent);
	trace->entnum = hitEntnum != Q3A_ENTITYNUM_NONE ? hitEntnum : entnum;
	return true;
}

void SyncBotLibEntities() {
	if (botRuntimeStatus.state != BotAasRuntimeState::Loaded || g_entities == nullptr) {
		return;
	}

	const int gameEntityCount = static_cast<int>(std::min<uint32_t>(
		game.maxEntities,
		static_cast<uint32_t>(std::numeric_limits<int>::max())));
	if (!BotLibAdapter_BeginEntitySync(gameEntityCount)) {
		return;
	}

	const BotLibAdapterStatus &adapter = BotLibAdapter_GetStatus();
	const int botLibEntityCount = adapter.q3aEntitySyncMaxEntities > 0
		? std::min(gameEntityCount, adapter.q3aEntitySyncMaxEntities)
		: gameEntityCount;

	botRuntimeStatus.entitySnapshotPlayers = 0;
	botRuntimeStatus.entitySnapshotBots = 0;
	botRuntimeStatus.entitySnapshotSpectators = 0;
	botRuntimeStatus.entitySnapshotMonsters = 0;
	botRuntimeStatus.entitySnapshotItems = 0;
	botRuntimeStatus.entitySnapshotDroppedItems = 0;
	botRuntimeStatus.entitySnapshotHazards = 0;
	botRuntimeStatus.entitySnapshotMovers = 0;
	botRuntimeStatus.entitySnapshotObjectives = 0;

	for (int entnum = 0; entnum < botLibEntityCount; ++entnum) {
		gentity_t *ent = &g_entities[entnum];
		BotLibAdapterEntitySnapshot snapshot{};
		if (BotRuntimeBuildEntitySnapshot(ent, snapshot)) {
			CountRuntimeEntitySnapshot(snapshot.type);
			BotLibAdapter_UpdateEntity(entnum, &snapshot);
		} else {
			BotLibAdapter_UpdateEntity(entnum, nullptr);
		}
	}

	BotLibAdapter_FinishEntitySync();
}

void RunBotLibDebugDrawIfRequested() {
	if (botRuntimeStatus.state != BotAasRuntimeState::Loaded) {
		return;
	}

	const bool routeDebug = sg_bot_debug_route != nullptr && sg_bot_debug_route->integer != 0;
	const bool goalDebug = sg_bot_debug_goal != nullptr && sg_bot_debug_goal->integer != 0;
	const int debugClient = sg_bot_debug_client != nullptr ? sg_bot_debug_client->integer : -1;
	if (routeDebug || goalDebug) {
		if (!BotNav_DrawDebugOverlay(routeDebug, goalDebug, debugClient)) {
			BotLibAdapter_RunRouteOverlaySmoke();
		}
		return;
	}

	if (sg_bot_debug_aas == nullptr || sg_bot_debug_aas->integer < 3) {
		return;
	}
	if (level.time < lastDebugDrawSmokeTime) {
		return;
	}
	lastDebugDrawSmokeTime = level.time + 1_sec;

	BotLibAdapter_RunBotClientCommandSmoke();
	BotLibAdapter_RunDebugDrawSmoke();
	BotLibAdapter_RunDebugPolygonSmoke();
	BotLibAdapter_RunDebugAreaSmoke();
}

bool ValidateQ2BspLumps(
	const unsigned char *data,
	int64_t fileSize,
	std::array<BspLump, Q2_BSP_LUMP_COUNT> &lumps,
	std::string &message) {
	if (fileSize < static_cast<int64_t>(Q2_BSP_HEADER_SIZE)) {
		message = "Q2 BSP file is smaller than the header";
		return false;
	}

	const int32_t ident = ReadLittleInt32(data);
	if (ident != Q2_BSP_ID) {
		message = "Q2 BSP header ident is not IBSP";
		return false;
	}

	const int32_t version = ReadLittleInt32(data + 4);
	if (version != Q2_BSP_VERSION) {
		message = G_Fmt("unsupported Q2 BSP version {}; expected {}", version, Q2_BSP_VERSION);
		return false;
	}

	for (size_t i = 0; i < Q2_BSP_LUMP_COUNT; ++i) {
		const size_t lumpOffset = 8 + i * 8;
		lumps[i].offset = ReadLittleInt32(data + lumpOffset);
		lumps[i].length = ReadLittleInt32(data + lumpOffset + 4);
		if (lumps[i].offset < 0 || lumps[i].length < 0) {
			message = G_Fmt("Q2 BSP lump {} has a negative offset or length", i);
			return false;
		}

		const int64_t lumpEnd = static_cast<int64_t>(lumps[i].offset) + lumps[i].length;
		if (lumpEnd < lumps[i].offset || lumpEnd > fileSize) {
			message = G_Fmt("Q2 BSP lump {} extends outside the file", i);
			return false;
		}
	}

	if (lumps[Q2_BSP_LUMP_ENTITIES].length <= 0) {
		message = "Q2 BSP entity lump is empty";
		return false;
	}

	if (lumps[Q2_BSP_LUMP_MODELS].length <= 0 ||
		(lumps[Q2_BSP_LUMP_MODELS].length % Q2_BSP_MODEL_SIZE) != 0) {
		message = "Q2 BSP model lump is empty or has an invalid record size";
		return false;
	}

	message = "Q2 BSP bridge lumps valid";
	return true;
}

bool LoadLevelBspBridgeData(const BotFilesystemApiV1 *fs) {
	botRuntimeStatus.attemptedBspEntityLoad = true;
	botRuntimeStatus.attemptedBspModelLoad = true;
	botRuntimeStatus.attemptedBspCollisionLoad = true;
	botRuntimeStatus.attemptedBspVisibilityLoad = true;

	if (botRuntimeStatus.bspPath.empty()) {
		botRuntimeStatus.bspEntityMessage = "level BSP path is empty";
		botRuntimeStatus.bspModelMessage = "level BSP path is empty";
		botRuntimeStatus.bspCollisionMessage = "level BSP path is empty";
		botRuntimeStatus.bspVisibilityMessage = "level BSP path is empty";
		BotLibAdapter_LoadBspEntityData(botRuntimeStatus.mapName.c_str(), "", nullptr, 0);
		BotLibAdapter_LoadBspModelData(botRuntimeStatus.mapName.c_str(), "", nullptr, 0);
		BotLibAdapter_LoadBspCollisionData(botRuntimeStatus.mapName.c_str(), "", nullptr, 0);
		BotLibAdapter_LoadBspVisibilityData(botRuntimeStatus.mapName.c_str(), "", nullptr, 0);
		return false;
	}

	void *rawBuffer = nullptr;
	const int length = fs->LoadFile(botRuntimeStatus.bspPath.c_str(), &rawBuffer, 0, TAG_LEVEL);
	if (length <= 0 || rawBuffer == nullptr) {
		if (rawBuffer != nullptr) {
			gi.TagFree(rawBuffer);
		}
		botRuntimeStatus.bspEntityMessage = G_Fmt("could not load {}", botRuntimeStatus.bspPath.c_str());
		botRuntimeStatus.bspModelMessage = botRuntimeStatus.bspEntityMessage;
		botRuntimeStatus.bspCollisionMessage = botRuntimeStatus.bspEntityMessage;
		botRuntimeStatus.bspVisibilityMessage = botRuntimeStatus.bspEntityMessage;
		BotLibAdapter_LoadBspEntityData(
			botRuntimeStatus.mapName.c_str(),
			botRuntimeStatus.bspPath.c_str(),
			nullptr,
			0);
		BotLibAdapter_LoadBspModelData(
			botRuntimeStatus.mapName.c_str(),
			botRuntimeStatus.bspPath.c_str(),
			nullptr,
			0);
		BotLibAdapter_LoadBspCollisionData(
			botRuntimeStatus.mapName.c_str(),
			botRuntimeStatus.bspPath.c_str(),
			nullptr,
			0);
		BotLibAdapter_LoadBspVisibilityData(
			botRuntimeStatus.mapName.c_str(),
			botRuntimeStatus.bspPath.c_str(),
			nullptr,
			0);
		return false;
	}

	botRuntimeStatus.bspFileSize = length;

	std::array<BspLump, Q2_BSP_LUMP_COUNT> bspLumps{};
	std::string message;
	const auto *data = static_cast<const unsigned char *>(rawBuffer);
	if (!ValidateQ2BspLumps(data, length, bspLumps, message)) {
		gi.TagFree(rawBuffer);
		botRuntimeStatus.bspEntityMessage = std::move(message);
		botRuntimeStatus.bspModelMessage = botRuntimeStatus.bspEntityMessage;
		botRuntimeStatus.bspCollisionMessage = botRuntimeStatus.bspEntityMessage;
		botRuntimeStatus.bspVisibilityMessage = botRuntimeStatus.bspEntityMessage;
		BotLibAdapter_LoadBspEntityData(
			botRuntimeStatus.mapName.c_str(),
			botRuntimeStatus.bspPath.c_str(),
			nullptr,
			0);
		BotLibAdapter_LoadBspModelData(
			botRuntimeStatus.mapName.c_str(),
			botRuntimeStatus.bspPath.c_str(),
			nullptr,
			0);
		BotLibAdapter_LoadBspCollisionData(
			botRuntimeStatus.mapName.c_str(),
			botRuntimeStatus.bspPath.c_str(),
			nullptr,
			0);
		BotLibAdapter_LoadBspVisibilityData(
			botRuntimeStatus.mapName.c_str(),
			botRuntimeStatus.bspPath.c_str(),
			nullptr,
			0);
		return false;
	}

	const BspLump &entityLump = bspLumps[Q2_BSP_LUMP_ENTITIES];
	botRuntimeStatus.bspEntityBytes = entityLump.length;
	const bool entityLoaded = BotLibAdapter_LoadBspEntityData(
		botRuntimeStatus.mapName.c_str(),
		botRuntimeStatus.bspPath.c_str(),
		data + entityLump.offset,
		entityLump.length);
	const BspLump &modelLump = bspLumps[Q2_BSP_LUMP_MODELS];
	botRuntimeStatus.bspModelBytes = modelLump.length;
	botRuntimeStatus.bspModelCount = modelLump.length / Q2_BSP_MODEL_SIZE;
	const bool modelLoaded = BotLibAdapter_LoadBspModelData(
		botRuntimeStatus.mapName.c_str(),
		botRuntimeStatus.bspPath.c_str(),
		data + modelLump.offset,
		modelLump.length);
	const bool collisionLoaded = BotLibAdapter_LoadBspCollisionData(
		botRuntimeStatus.mapName.c_str(),
		botRuntimeStatus.bspPath.c_str(),
		data,
		length);
	const bool visibilityLoaded = BotLibAdapter_LoadBspVisibilityData(
		botRuntimeStatus.mapName.c_str(),
		botRuntimeStatus.bspPath.c_str(),
		data,
		length);
	const BotLibAdapterStatus &adapter = BotLibAdapter_GetStatus();
	botRuntimeStatus.bspEntityMessage =
		adapter.bspEntityMessage.empty() ? "Q2 BSP entity lump handed to Q3A bridge" : adapter.bspEntityMessage;
	botRuntimeStatus.bspModelMessage =
		adapter.bspModelMessage.empty() ? "Q2 BSP model lump handed to Q3A bridge" : adapter.bspModelMessage;
	botRuntimeStatus.bspCollisionMessage =
		adapter.bspCollisionMessage.empty() ? "Q2 BSP collision lumps handed to Q3A bridge" : adapter.bspCollisionMessage;
	botRuntimeStatus.bspCollisionPlaneCount = adapter.q3aBspCollisionPlanes;
	botRuntimeStatus.bspCollisionNodeCount = adapter.q3aBspCollisionNodes;
	botRuntimeStatus.bspCollisionLeafCount = adapter.q3aBspCollisionLeafs;
	botRuntimeStatus.bspCollisionBrushCount = adapter.q3aBspCollisionBrushes;
	botRuntimeStatus.bspVisibilityMessage =
		adapter.bspVisibilityMessage.empty() ? "Q2 BSP visibility lump handed to Q3A bridge" : adapter.bspVisibilityMessage;
	botRuntimeStatus.bspVisibilityClusterCount = adapter.q3aBspVisibilityClusters;
	gi.TagFree(rawBuffer);
	return entityLoaded && modelLoaded && collisionLoaded && visibilityLoaded;
}

void LoadLevelAas() {
	botRuntimeStatus.attemptedLoad = true;

	if (botRuntimeStatus.mapName.empty()) {
		SetRuntimeState(BotAasRuntimeState::Failed, "level map name is empty");
		return;
	}

	const BotFilesystemApiV1 *fs = Bot_GetFilesystem();
	if (fs == nullptr || fs->LoadFile == nullptr) {
		SetRuntimeState(BotAasRuntimeState::Failed, "filesystem extension is unavailable");
		return;
	}

	LoadLevelBspBridgeData(fs);

	void *rawBuffer = nullptr;
	const int length = fs->LoadFile(botRuntimeStatus.aasPath.c_str(), &rawBuffer, 0, TAG_LEVEL);
	if (length <= 0 || rawBuffer == nullptr) {
		SetRuntimeState(
			BotAasRuntimeState::Failed,
			std::string(G_Fmt("could not load {}", botRuntimeStatus.aasPath.c_str())));
		return;
	}

	botRuntimeStatus.fileSize = length;

	std::array<AasLump, AAS_LUMP_COUNT> lumps{};
	std::string message;
	const auto *data = static_cast<const unsigned char *>(rawBuffer);
	const bool valid = ValidateAasHeader(data, length, lumps, message);
	if (!valid) {
		gi.TagFree(rawBuffer);
		SetRuntimeState(BotAasRuntimeState::Failed, std::move(message));
		return;
	}

	botRuntimeStatus.version = ReadLittleInt32(data + 4);
	std::array<unsigned char, AAS_HEADER_SIZE> decodedHeader{};
	std::copy_n(data, AAS_HEADER_SIZE, decodedHeader.begin());
	DecodeAasV5Data(decodedHeader.data() + 8, AAS_HEADER_SIZE - 8);
	botRuntimeStatus.bspChecksum = ReadLittleInt32(decodedHeader.data() + 8);
	botRuntimeStatus.areaCount = CountFixedSizeLump(lumps[AAS_LUMP_AREAS], AAS_AREA_SIZE);
	botRuntimeStatus.areaSettingsCount =
		CountFixedSizeLump(lumps[AAS_LUMP_AREA_SETTINGS], AAS_AREA_SETTINGS_SIZE);
	botRuntimeStatus.reachabilityCount =
		CountFixedSizeLump(lumps[AAS_LUMP_REACHABILITY], AAS_REACHABILITY_SIZE);
	botRuntimeStatus.clusterCount = CountFixedSizeLump(lumps[AAS_LUMP_CLUSTERS], AAS_CLUSTER_SIZE);

	const bool q3aAasLoaded = BotLibAdapter_LoadAasBuffer(
		botRuntimeStatus.mapName.c_str(),
		botRuntimeStatus.aasPath.c_str(),
		data,
		length,
		botRuntimeStatus.bspChecksum);
	if (!q3aAasLoaded) {
		const BotLibAdapterStatus &adapter = BotLibAdapter_GetStatus();
		const std::string q3aFailure =
			adapter.q3aAasSampleAttempted && !adapter.q3aAasSamplePassed && !adapter.aasSampleMessage.empty()
				? adapter.aasSampleMessage
				: (adapter.q3aAasClusterAttempted && !adapter.q3aAasClusterPassed &&
							!adapter.aasClusterMessage.empty()
						? adapter.aasClusterMessage
						: (adapter.q3aAasRouteAttempted && !adapter.q3aAasRoutePassed && !adapter.aasRouteMessage.empty()
								? adapter.aasRouteMessage
								: (adapter.q3aAasAltRouteAttempted && !adapter.q3aAasAltRoutePassed &&
											!adapter.aasAltRouteMessage.empty()
										? adapter.aasAltRouteMessage
										: (adapter.q3aAasMovementAttempted && !adapter.q3aAasMovementPassed &&
											!adapter.aasMovementMessage.empty()
										? adapter.aasMovementMessage
										: (adapter.aasMessage.empty() ? "unknown error" : adapter.aasMessage)))));
		gi.TagFree(rawBuffer);
		SetRuntimeState(
			BotAasRuntimeState::Failed,
			std::string(G_Fmt(
				"Q3A AAS load failed: {} (BLERR {})",
				q3aFailure.c_str(),
				adapter.q3aAasLoadResult)));
		return;
	}

	gi.TagFree(rawBuffer);
	SetRuntimeState(BotAasRuntimeState::Loaded, std::move(message));
}

void PrintBotLibAdapterStatusIfRequested() {
	if (sg_bot_debug_aas == nullptr || sg_bot_debug_aas->integer <= 1) {
		return;
	}

	const BotLibAdapterStatus &adapter = BotLibAdapter_GetStatus();
	gi.Com_PrintFmt(
		"BotLib adapter: {} (utility={}, q3a_print_callback={}, q3a_print_messages={}, q3a_print_warnings={}, q3a_print_errors={}, q3a_print_fatals={}, q3a_print_last_type={}, q3a_aas={}, q3a_sample={}, q3a_sample_area={}, q3a_sample_point_area={}, q3a_sample_cluster={}, q3a_sample_reachability={}, q3a_cluster={}, q3a_cluster_area={}, q3a_cluster_cluster={}, q3a_cluster_count={}, q3a_cluster_areas={}, q3a_cluster_reachability_areas={}, q3a_cluster_failures={}, q3a_route={}, q3a_route_start={}, q3a_route_goal={}, q3a_route_time={}, q3a_route_reachability={}, q3a_route_end={}, q3a_route_stop={}, q3a_alt_route={}, q3a_alt_route_start={}, q3a_alt_route_goal={}, q3a_alt_route_goals={}, q3a_alt_route_first_area={}, q3a_alt_route_first_start_time={}, q3a_alt_route_first_goal_time={}, q3a_alt_route_first_extra_time={}, q3a_alt_route_failures={}, q3a_movement={}, q3a_movement_end={}, q3a_movement_stop={}, q3a_movement_frames={}, q3a_movement_drop={}, q3a_movement_jump={}, q3a_start_frame={}, q3a_start_result={}, q3a_start_frames={}, q3a_start_time_ms={}, q3a_entity_sync={}, q3a_entity_updated={}, q3a_entity_unlinked={}, q3a_entity_skipped={}, q3a_entity_failures={}, q3a_entity_max={}, q3a_entity_trace={}, q3a_entity_trace_attempts={}, q3a_entity_trace_hits={}, q3a_entity_trace_misses={}, q3a_entity_trace_failures={}, q3a_entity_trace_callback={}, q3a_debug_draw={}, q3a_debug_draw_callback={}, q3a_debug_draw_lines={}, q3a_debug_draw_crosses={}, q3a_debug_draw_arrows={}, q3a_debug_draw_clears={}, q3a_debug_draw_failures={}, q3a_debug_polygon={}, q3a_debug_polygon_callback={}, q3a_debug_polygon_creates={}, q3a_debug_polygon_deletes={}, q3a_debug_polygon_points={}, q3a_debug_polygon_last_id={}, q3a_debug_polygon_failures={}, q3a_debug_area={}, q3a_debug_area_area={}, q3a_debug_area_lines={}, q3a_debug_area_polygon_creates={}, q3a_debug_area_polygon_deletes={}, q3a_debug_area_failures={}, q3a_route_overlay={}, q3a_route_overlay_start={}, q3a_route_overlay_goal={}, q3a_route_overlay_end={}, q3a_route_overlay_time={}, q3a_route_overlay_reachability={}, q3a_route_overlay_lines={}, q3a_route_overlay_crosses={}, q3a_route_overlay_arrows={}, q3a_route_overlay_clears={}, q3a_route_overlay_failures={}, q3a_bsp_entity={}, q3a_bsp_entities={}, q3a_bsp_epairs={}, q3a_bsp_entity_smoke={}, q3a_bsp_model={}, q3a_bsp_models={}, q3a_bsp_model_smoke={}, q3a_bsp_collision={}, q3a_bsp_planes={}, q3a_bsp_nodes={}, q3a_bsp_leafs={}, q3a_bsp_brushes={}, q3a_bsp_point_contents_smoke={}, q3a_bsp_trace_smoke={}, q3a_bsp_leaf_link={}, q3a_bsp_leaf_links={}, q3a_bsp_leaf_link_failures={}, q3a_bsp_box_entities_smoke={}, q3a_bsp_box_entities={}, q3a_bsp_visibility={}, q3a_bsp_vis_clusters={}, q3a_bsp_pvs_smoke={}, q3a_bsp_phs_smoke={}, q3a_angle_vectors={}, q3a_time_ms={}, q3a_areas={}, q3a_reachability={}, q3a_clusters={}, imported={}, planned_files={}, commit={})\n",
		adapter.message.empty() ? "not initialized" : adapter.message.c_str(),
		adapter.utilityMessage.empty() ? "not run" : adapter.utilityMessage.c_str(),
		adapter.q3aPrintCallbackSet ? "yes" : "no",
		adapter.q3aPrintMessages,
		adapter.q3aPrintWarnings,
		adapter.q3aPrintErrors,
		adapter.q3aPrintFatals,
		adapter.q3aPrintLastType,
		adapter.aasMessage.empty() ? "not run" : adapter.aasMessage.c_str(),
		adapter.aasSampleMessage.empty() ? "not run" : adapter.aasSampleMessage.c_str(),
		adapter.q3aAasSampleArea,
		adapter.q3aAasSamplePointArea,
		adapter.q3aAasSampleCluster,
		adapter.q3aAasSampleReachability,
		adapter.aasClusterMessage.empty() ? "not run" : adapter.aasClusterMessage.c_str(),
		adapter.q3aAasClusterArea,
		adapter.q3aAasClusterCluster,
		adapter.q3aAasClusterNumClusters,
		adapter.q3aAasClusterAreas,
		adapter.q3aAasClusterReachabilityAreas,
		adapter.q3aAasClusterFailures,
		adapter.aasRouteMessage.empty() ? "not run" : adapter.aasRouteMessage.c_str(),
		adapter.q3aAasRouteStartArea,
		adapter.q3aAasRouteGoalArea,
		adapter.q3aAasRouteTravelTime,
		adapter.q3aAasRouteReachability,
		adapter.q3aAasRouteEndArea,
		adapter.q3aAasRouteStopEvent,
		adapter.aasAltRouteMessage.empty() ? "not run" : adapter.aasAltRouteMessage.c_str(),
		adapter.q3aAasAltRouteStartArea,
		adapter.q3aAasAltRouteGoalArea,
		adapter.q3aAasAltRouteGoals,
		adapter.q3aAasAltRouteFirstArea,
		adapter.q3aAasAltRouteFirstStartTravelTime,
		adapter.q3aAasAltRouteFirstGoalTravelTime,
		adapter.q3aAasAltRouteFirstExtraTravelTime,
		adapter.q3aAasAltRouteFailures,
		adapter.aasMovementMessage.empty() ? "not run" : adapter.aasMovementMessage.c_str(),
		adapter.q3aAasMovementEndArea,
		adapter.q3aAasMovementStopEvent,
		adapter.q3aAasMovementFrames,
		adapter.q3aAasMovementDropToFloorPassed ? "yes" : "no",
		adapter.q3aAasMovementJumpVelocityPassed ? "yes" : "no",
		adapter.aasStartFrameMessage.empty() ? "not run" : adapter.aasStartFrameMessage.c_str(),
		adapter.q3aAasStartFrameResult,
		adapter.q3aAasStartFrameCount,
		adapter.q3aAasStartFrameTimeMilliseconds,
		adapter.entitySyncMessage.empty() ? "not run" : adapter.entitySyncMessage.c_str(),
		adapter.q3aEntitySyncUpdated,
		adapter.q3aEntitySyncUnlinked,
		adapter.q3aEntitySyncSkipped,
		adapter.q3aEntitySyncFailures,
		adapter.q3aEntitySyncMaxEntities,
		adapter.entityTraceMessage.empty() ? "not run" : adapter.entityTraceMessage.c_str(),
		adapter.q3aEntityTraceAttempted,
		adapter.q3aEntityTraceHits,
		adapter.q3aEntityTraceMisses,
		adapter.q3aEntityTraceFailures,
		adapter.q3aEntityTraceCallbackSet ? "yes" : "no",
		adapter.debugDrawMessage.empty() ? "not run" : adapter.debugDrawMessage.c_str(),
		adapter.q3aDebugDrawCallbackSet ? "yes" : "no",
		adapter.q3aDebugDrawLines,
		adapter.q3aDebugDrawCrosses,
		adapter.q3aDebugDrawArrows,
		adapter.q3aDebugDrawClears,
		adapter.q3aDebugDrawFailures,
		adapter.debugPolygonMessage.empty() ? "not run" : adapter.debugPolygonMessage.c_str(),
		adapter.q3aDebugPolygonCallbackSet ? "yes" : "no",
		adapter.q3aDebugPolygonCreates,
		adapter.q3aDebugPolygonDeletes,
		adapter.q3aDebugPolygonPoints,
		adapter.q3aDebugPolygonLastId,
		adapter.q3aDebugPolygonFailures,
		adapter.debugAreaMessage.empty() ? "not run" : adapter.debugAreaMessage.c_str(),
		adapter.q3aDebugAreaArea,
		adapter.q3aDebugAreaLines,
		adapter.q3aDebugAreaPolygonCreates,
		adapter.q3aDebugAreaPolygonDeletes,
		adapter.q3aDebugAreaFailures,
		adapter.routeOverlayMessage.empty() ? "not run" : adapter.routeOverlayMessage.c_str(),
		adapter.q3aRouteOverlayStartArea,
		adapter.q3aRouteOverlayGoalArea,
		adapter.q3aRouteOverlayEndArea,
		adapter.q3aRouteOverlayTravelTime,
		adapter.q3aRouteOverlayReachability,
		adapter.q3aRouteOverlayLines,
		adapter.q3aRouteOverlayCrosses,
		adapter.q3aRouteOverlayArrows,
		adapter.q3aRouteOverlayClears,
		adapter.q3aRouteOverlayFailures,
		adapter.bspEntityMessage.empty() ? "not run" : adapter.bspEntityMessage.c_str(),
		adapter.q3aBspEntityCount,
		adapter.q3aBspEntityPairs,
		adapter.q3aBspEntityValueSmokePassed ? "yes" : "no",
		adapter.bspModelMessage.empty() ? "not run" : adapter.bspModelMessage.c_str(),
		adapter.q3aBspModelCount,
		adapter.q3aBspModelBoundsSmokePassed ? "yes" : "no",
		adapter.bspCollisionMessage.empty() ? "not run" : adapter.bspCollisionMessage.c_str(),
		adapter.q3aBspCollisionPlanes,
		adapter.q3aBspCollisionNodes,
		adapter.q3aBspCollisionLeafs,
		adapter.q3aBspCollisionBrushes,
		adapter.q3aBspCollisionPointContentsSmokePassed ? "yes" : "no",
		adapter.q3aBspCollisionTraceSmokePassed ? "yes" : "no",
		adapter.bspLeafLinkMessage.empty() ? "not run" : adapter.bspLeafLinkMessage.c_str(),
		adapter.q3aBspLeafLinks,
		adapter.q3aBspLeafLinkFailures,
		adapter.q3aBspBoxEntitiesSmokePassed ? "yes" : "no",
		adapter.q3aBspBoxEntitiesCount,
		adapter.bspVisibilityMessage.empty() ? "not run" : adapter.bspVisibilityMessage.c_str(),
		adapter.q3aBspVisibilityClusters,
		adapter.q3aBspVisibilityPvsSmokePassed ? "yes" : "no",
		adapter.q3aBspVisibilityPhsSmokePassed ? "yes" : "no",
		adapter.angleVectorsMessage.empty() ? "not run" : adapter.angleVectorsMessage.c_str(),
		adapter.q3aRuntimeMilliseconds,
		adapter.q3aAasAreas,
		adapter.q3aAasReachability,
		adapter.q3aAasClusters,
		adapter.q3aRuntimeImported ? "yes" : "no",
		adapter.plannedImportFileCount,
		adapter.sourceCommit != nullptr ? adapter.sourceCommit : "<unset>");
	gi.Com_PrintFmt(
		"BotLib adapter lifecycle: q3a_lifecycle={}, q3a_lifecycle_inits={}, q3a_lifecycle_shutdowns={}, q3a_lifecycle_load_attempts={}, q3a_lifecycle_load_successes={}, q3a_lifecycle_active_unloads={}, q3a_lifecycle_clean_unloads={}, q3a_lifecycle_unload_failures={}, q3a_lifecycle_last_unload_zone_active={}, q3a_lifecycle_last_unload_hunk_active={}, q3a_lifecycle_last_unload_open_files={}, q3a_lifecycle_persistent_zone={}\n",
		adapter.lifecycleMessage.empty() ? "not run" : adapter.lifecycleMessage.c_str(),
		adapter.q3aLifecycleInitCount,
		adapter.q3aLifecycleShutdownCount,
		adapter.q3aLifecycleLoadAttempts,
		adapter.q3aLifecycleLoadSuccesses,
		adapter.q3aLifecycleActiveUnloads,
		adapter.q3aLifecycleCleanUnloads,
		adapter.q3aLifecycleUnloadFailures,
		adapter.q3aLifecycleLastUnloadZoneActiveBytes,
		adapter.q3aLifecycleLastUnloadHunkActiveBytes,
		adapter.q3aLifecycleLastUnloadOpenFiles,
		adapter.q3aLifecyclePersistentZoneBytes);
	gi.Com_PrintFmt(
		"BotLib adapter memory: q3a_memory={}, q3a_memory_zone_active={}, q3a_memory_zone_peak={}, q3a_memory_zone_allocs={}, q3a_memory_zone_frees={}, q3a_memory_hunk_active={}, q3a_memory_hunk_peak={}, q3a_memory_hunk_allocs={}, q3a_memory_hunk_groups={}, q3a_memory_failures={}, q3a_memory_available={}\n",
		adapter.memoryMessage.empty() ? "not run" : adapter.memoryMessage.c_str(),
		adapter.q3aMemoryZoneActiveBytes,
		adapter.q3aMemoryZonePeakBytes,
		adapter.q3aMemoryZoneAllocations,
		adapter.q3aMemoryZoneFrees,
		adapter.q3aMemoryHunkActiveBytes,
		adapter.q3aMemoryHunkPeakBytes,
		adapter.q3aMemoryHunkAllocations,
		adapter.q3aMemoryHunkGroupFrees,
		adapter.q3aMemoryFailures,
		adapter.q3aAvailableMemory);
	gi.Com_PrintFmt(
		"BotLib adapter filesystem: q3a_fs={}, q3a_fs_callback={}, q3a_fs_attempted={}, q3a_fs_passed={}, q3a_fs_open_attempts={}, q3a_fs_files={}, q3a_fs_memory_files={}, q3a_fs_open_failures={}, q3a_fs_route_cache_misses={}, q3a_fs_read_bytes={}, q3a_fs_seeks={}, q3a_fs_closes={}, q3a_fs_writes_rejected={}\n",
		adapter.filesystemMessage.empty() ? "not run" : adapter.filesystemMessage.c_str(),
		adapter.q3aFilesystemCallbackSet ? "yes" : "no",
		adapter.q3aFilesystemAttempted ? "yes" : "no",
		adapter.q3aFilesystemPassed ? "yes" : "no",
		adapter.q3aFilesystemOpenAttempts,
		adapter.q3aFilesystemOpenFiles,
		adapter.q3aFilesystemOpenMemoryFiles,
		adapter.q3aFilesystemOpenFailures,
		adapter.q3aFilesystemRouteCacheMisses,
		adapter.q3aFilesystemReadBytes,
		adapter.q3aFilesystemSeekCount,
		adapter.q3aFilesystemCloseCount,
		adapter.q3aFilesystemWriteRejected);
	gi.Com_PrintFmt(
		"BotLib adapter BotClientCommand: q3a_bot_client_command={}, q3a_bot_client_command_callback={}, q3a_bot_client_command_client={}, q3a_bot_client_command_accepted={}, q3a_bot_client_command_rejected={}, q3a_bot_client_command_failures={}\n",
		adapter.botClientCommandMessage.empty() ? "not run" : adapter.botClientCommandMessage.c_str(),
		adapter.q3aBotClientCommandCallbackSet ? "yes" : "no",
		adapter.q3aBotClientCommandClient,
		adapter.q3aBotClientCommandAccepted,
		adapter.q3aBotClientCommandRejected,
		adapter.q3aBotClientCommandFailures);
}

void PrintAasStatusIfRequested() {
	if (sg_bot_debug_aas == nullptr || !sg_bot_debug_aas->integer) {
		return;
	}

	if (level.time < lastDebugPrintTime) {
		return;
	}

	lastDebugPrintTime = level.time + 5_sec;

	if (botRuntimeStatus.state == BotAasRuntimeState::Loaded) {
		gi.Com_PrintFmt(
			"Bot AAS: {} loaded (areas={}, reachability={}, clusters={}, bytes={})\n",
			botRuntimeStatus.aasPath.c_str(),
			botRuntimeStatus.areaCount,
			botRuntimeStatus.reachabilityCount,
			botRuntimeStatus.clusterCount,
			botRuntimeStatus.fileSize);
		gi.Com_PrintFmt(
			"Bot AAS entity snapshots: players={}, bots={}, spectators={}, monsters={}, items={}, dropped_items={}, hazards={}, movers={}, objectives={}\n",
			botRuntimeStatus.entitySnapshotPlayers,
			botRuntimeStatus.entitySnapshotBots,
			botRuntimeStatus.entitySnapshotSpectators,
			botRuntimeStatus.entitySnapshotMonsters,
			botRuntimeStatus.entitySnapshotItems,
			botRuntimeStatus.entitySnapshotDroppedItems,
			botRuntimeStatus.entitySnapshotHazards,
			botRuntimeStatus.entitySnapshotMovers,
			botRuntimeStatus.entitySnapshotObjectives);
		PrintBotLibAdapterStatusIfRequested();
		return;
	}

	if (botRuntimeStatus.enabled || sg_bot_debug_aas->integer > 1) {
		gi.Com_PrintFmt(
			"Bot AAS: {} ({})\n",
			botRuntimeStatus.aasPath.empty() ? "<none>" : botRuntimeStatus.aasPath.c_str(),
			botRuntimeStatus.message.empty() ? "not loaded" : botRuntimeStatus.message.c_str());
	}

	PrintBotLibAdapterStatusIfRequested();
}

void RunLifecycleSmokeAfterBeginLevel() {
	if (sg_bot_lifecycle_smoke == nullptr || sg_bot_lifecycle_smoke->integer <= 0) {
		lifecycleSmokePhase = 0;
		return;
	}

	const int mode = sg_bot_lifecycle_smoke->integer;
	worr::SetLogLevel(worr::LogLevel::Info);

	if (mode == 4) {
		Bot_RuntimePrintLifecycleStatus();
		gi.Com_Print("BotLib lifecycle smoke: quitting after reload\n");
		gi.AddCommandString("quit\n");
		lifecycleSmokePhase = 2;
		return;
	}

	if (lifecycleSmokePhase > 1) {
		return;
	}

	Bot_RuntimePrintLifecycleStatus();

	if (mode >= 2 && lifecycleSmokePhase == 0 && botRuntimeStatus.attemptedLoad &&
		!botRuntimeStatus.mapName.empty()) {
		lifecycleSmokePhase = 1;
		gi.cvarSet("sg_bot_lifecycle_smoke", mode >= 3 ? "4" : "1");
		gi.Com_PrintFmt("BotLib lifecycle smoke: reloading {}\n", botRuntimeStatus.mapName.c_str());
		gi.AddCommandString(G_Fmt("map {}\n", botRuntimeStatus.mapName).data());
		return;
	}

	if (mode >= 3 && lifecycleSmokePhase == 1) {
		lifecycleSmokePhase = 2;
		gi.Com_Print("BotLib lifecycle smoke: quitting after reload\n");
		gi.AddCommandString("quit\n");
		return;
	}

	lifecycleSmokePhase = 2;
}
} // namespace

void Bot_RuntimeRegisterCvars() {
	sg_bot_enable = gi.cvar("sg_bot_enable", "0", CVAR_NOFLAGS);
	sg_bot_debug = gi.cvar("sg_bot_debug", "0", CVAR_NOFLAGS);
	sg_bot_debug_aas = gi.cvar("sg_bot_debug_aas", "0", CVAR_NOFLAGS);
	sg_bot_debug_route = gi.cvar("sg_bot_debug_route", "0", CVAR_NOFLAGS);
	sg_bot_debug_goal = gi.cvar("sg_bot_debug_goal", "0", CVAR_NOFLAGS);
	sg_bot_debug_client = gi.cvar("sg_bot_debug_client", "-1", CVAR_NOFLAGS);
	sg_bot_cpu_budget_ms = gi.cvar("sg_bot_cpu_budget_ms", "2", CVAR_NOFLAGS);
	sg_bot_allow_chat = gi.cvar("sg_bot_allow_chat", "0", CVAR_NOFLAGS);
	sg_bot_chat_team_only = gi.cvar("sg_bot_chat_team_only", "0", CVAR_NOFLAGS);
	sg_bot_chat_min_interval_ms = gi.cvar("sg_bot_chat_min_interval_ms", "0", CVAR_NOFLAGS);
	sg_bot_chat_reply_policy_smoke = gi.cvar("sg_bot_chat_reply_policy_smoke", "0", CVAR_NOFLAGS);
	sg_bot_chat_event_policy_smoke = gi.cvar("sg_bot_chat_event_policy_smoke", "0", CVAR_NOFLAGS);
	sg_bot_lifecycle_smoke = gi.cvar("sg_bot_lifecycle_smoke", "0", CVAR_NOFLAGS);

	BotLibAdapter_SetPrintCallback(BotRuntimeQ3APrint);
	BotLibAdapter_SetBotClientCommandCallback(BotRuntimeBotClientCommand);
	BotLibAdapter_SetFilesystemCallbacks(BotRuntimeQ3AFilesystemLoad, BotRuntimeQ3AFilesystemFree);
	BotLibAdapter_SetEntityTraceCallback(BotRuntimeEntityTrace);
	BotLibAdapter_SetDebugDrawCallback(BotRuntimeDebugDraw);
	BotLibAdapter_SetDebugPolygonCallback(BotRuntimeDebugPolygon);
	BotLibAdapter_Init();
}

void Bot_RuntimeBeginLevel() {
	BotNav_ResetAll();
	BotBrain_ResetChatPolicyState();
	BotChatPolicy_ResetDispatchStatus();
	ResetRuntimeStatusForMap();
	lastDebugPrintTime = 0_ms;

	if (!botRuntimeStatus.enabled) {
		SetRuntimeState(BotAasRuntimeState::Disabled, "sg_bot_enable is 0");
		PrintAasStatusIfRequested();
		RunLifecycleSmokeAfterBeginLevel();
		return;
	}

	LoadLevelAas();
	if (botRuntimeStatus.state == BotAasRuntimeState::Loaded) {
		BotLibAdapter_BeginLevel(botRuntimeStatus.mapName.c_str(), botRuntimeStatus.aasPath.c_str());
		gi.Com_PrintFmt(
			"Bot AAS: loaded {} (areas={}, reachability={}, clusters={})\n",
			botRuntimeStatus.aasPath.c_str(),
			botRuntimeStatus.areaCount,
			botRuntimeStatus.reachabilityCount,
			botRuntimeStatus.clusterCount);
	} else {
		BotLibAdapter_EndLevel();
		gi.Com_PrintFmt(
			"Bot AAS: failed to load {}: {}\n",
			botRuntimeStatus.aasPath.empty() ? "<none>" : botRuntimeStatus.aasPath.c_str(),
			botRuntimeStatus.message.c_str());
	}
	RunLifecycleSmokeAfterBeginLevel();
}

void Bot_RuntimeEndLevel() {
	BotNav_ResetAll();
	BotBrain_ResetChatPolicyState();
	BotChatPolicy_ResetDispatchStatus();
	BotLibAdapter_EndLevel();
	botRuntimeStatus = {};
	botRuntimeStatus.state = BotAasRuntimeState::Disabled;
	lastDebugPrintTime = 0_ms;
}

void Bot_RuntimeShutdown() {
	BotLibAdapter_Shutdown();
}

void Bot_RuntimeRunFrame() {
	if (Bot_RuntimeEnabled() != botRuntimeStatus.enabled) {
		Bot_RuntimeBeginLevel();
	}
	BotLibAdapter_RunFrame(CurrentBotRuntimeMilliseconds());
	SyncBotLibEntities();
	RunBotLibDebugDrawIfRequested();
	PrintAasStatusIfRequested();
}

void Bot_RuntimePrintLifecycleStatus() {
	const BotLibAdapterStatus &adapter = BotLibAdapter_GetStatus();
	gi.Com_PrintFmt(
		"BotLib lifecycle status: sg_bot_enable={}, runtime_state={}, map={}, aas={}, q3a_lifecycle={}, q3a_lifecycle_inits={}, q3a_lifecycle_shutdowns={}, q3a_lifecycle_load_attempts={}, q3a_lifecycle_load_successes={}, q3a_lifecycle_active_unloads={}, q3a_lifecycle_clean_unloads={}, q3a_lifecycle_unload_failures={}, q3a_lifecycle_last_unload_zone_active={}, q3a_lifecycle_last_unload_hunk_active={}, q3a_lifecycle_last_unload_open_files={}, q3a_lifecycle_persistent_zone={}\n",
		Bot_RuntimeEnabled() ? "1" : "0",
		static_cast<int>(botRuntimeStatus.state),
		botRuntimeStatus.mapName.empty() ? "<none>" : botRuntimeStatus.mapName.c_str(),
		botRuntimeStatus.aasPath.empty() ? "<none>" : botRuntimeStatus.aasPath.c_str(),
		adapter.lifecycleMessage.empty() ? "not run" : adapter.lifecycleMessage.c_str(),
		adapter.q3aLifecycleInitCount,
		adapter.q3aLifecycleShutdownCount,
		adapter.q3aLifecycleLoadAttempts,
		adapter.q3aLifecycleLoadSuccesses,
		adapter.q3aLifecycleActiveUnloads,
		adapter.q3aLifecycleCleanUnloads,
		adapter.q3aLifecycleUnloadFailures,
		adapter.q3aLifecycleLastUnloadZoneActiveBytes,
		adapter.q3aLifecycleLastUnloadHunkActiveBytes,
		adapter.q3aLifecycleLastUnloadOpenFiles,
		adapter.q3aLifecyclePersistentZoneBytes);
}

bool Bot_RuntimeEnabled() {
	return sg_bot_enable != nullptr && sg_bot_enable->integer != 0;
}

bool Bot_RuntimeAasLoaded() {
	return botRuntimeStatus.state == BotAasRuntimeState::Loaded;
}

const BotAasRuntimeStatus &Bot_RuntimeStatus() {
	return botRuntimeStatus;
}
