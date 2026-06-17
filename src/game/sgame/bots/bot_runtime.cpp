// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "../g_local.hpp"
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
				: (adapter.aasMessage.empty() ? "unknown error" : adapter.aasMessage);
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
		"BotLib adapter: {} (utility={}, q3a_aas={}, q3a_sample={}, q3a_sample_area={}, q3a_sample_point_area={}, q3a_sample_cluster={}, q3a_sample_reachability={}, q3a_bsp_entity={}, q3a_bsp_entities={}, q3a_bsp_epairs={}, q3a_bsp_entity_smoke={}, q3a_bsp_model={}, q3a_bsp_models={}, q3a_bsp_model_smoke={}, q3a_bsp_collision={}, q3a_bsp_planes={}, q3a_bsp_nodes={}, q3a_bsp_leafs={}, q3a_bsp_brushes={}, q3a_bsp_point_contents_smoke={}, q3a_bsp_trace_smoke={}, q3a_bsp_visibility={}, q3a_bsp_vis_clusters={}, q3a_bsp_pvs_smoke={}, q3a_bsp_phs_smoke={}, q3a_angle_vectors={}, q3a_time_ms={}, q3a_areas={}, q3a_reachability={}, q3a_clusters={}, imported={}, planned_files={}, commit={})\n",
		adapter.message.empty() ? "not initialized" : adapter.message.c_str(),
		adapter.utilityMessage.empty() ? "not run" : adapter.utilityMessage.c_str(),
		adapter.aasMessage.empty() ? "not run" : adapter.aasMessage.c_str(),
		adapter.aasSampleMessage.empty() ? "not run" : adapter.aasSampleMessage.c_str(),
		adapter.q3aAasSampleArea,
		adapter.q3aAasSamplePointArea,
		adapter.q3aAasSampleCluster,
		adapter.q3aAasSampleReachability,
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
} // namespace

void Bot_RuntimeRegisterCvars() {
	sg_bot_enable = gi.cvar("sg_bot_enable", "0", CVAR_NOFLAGS);
	sg_bot_debug = gi.cvar("sg_bot_debug", "0", CVAR_NOFLAGS);
	sg_bot_debug_aas = gi.cvar("sg_bot_debug_aas", "0", CVAR_NOFLAGS);
	sg_bot_debug_route = gi.cvar("sg_bot_debug_route", "0", CVAR_NOFLAGS);
	sg_bot_debug_goal = gi.cvar("sg_bot_debug_goal", "0", CVAR_NOFLAGS);
	sg_bot_cpu_budget_ms = gi.cvar("sg_bot_cpu_budget_ms", "2", CVAR_NOFLAGS);

	BotLibAdapter_Init();
}

void Bot_RuntimeBeginLevel() {
	ResetRuntimeStatusForMap();
	lastDebugPrintTime = 0_ms;

	if (!botRuntimeStatus.enabled) {
		SetRuntimeState(BotAasRuntimeState::Disabled, "sg_bot_enable is 0");
		PrintAasStatusIfRequested();
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
}

void Bot_RuntimeEndLevel() {
	BotLibAdapter_EndLevel();
	botRuntimeStatus = {};
	botRuntimeStatus.state = BotAasRuntimeState::Disabled;
	lastDebugPrintTime = 0_ms;
}

void Bot_RuntimeRunFrame() {
	if (Bot_RuntimeEnabled() != botRuntimeStatus.enabled) {
		Bot_RuntimeBeginLevel();
	}
	BotLibAdapter_RunFrame(CurrentBotRuntimeMilliseconds());
	PrintAasStatusIfRequested();
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
