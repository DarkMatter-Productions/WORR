// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "q3a_botlib_boundary.hpp"

namespace {
constexpr Q3ABotLibBoundaryInfo kBoundaryInfo = {
	.sourceName = "id Software Quake III Arena",
	.sourceUrl = "https://github.com/id-Software/Quake-III-Arena",
	.sourceCommit = "dbe4ddb10315479fc00086f08e25d968b4b43c49",
	.localImportRoot = "src/game/sgame/bots/q3a",
	.buildStrategy = "Compile imported Q3A BotLib C files as an internal sgame object group behind botlib_adapter.*",
	.provenanceRule = "Do not copy Q3A files here until the exact upstream commit/path is recorded in docs-dev/q3a-botlib-aas-credits.md",
	.runtimeImported = false,
};

constexpr Q3ABotLibPlannedFile kPlannedFiles[] = {
	{ "code/botlib/aasfile.h", "AAS file format declarations", true },
	{ "code/game/be_aas.h", "public AAS query declarations", true },
	{ "code/game/botlib.h", "BotLib import/export ABI shared with the game", true },
	{ "code/game/q_shared.h", "shared Q3 scalar/vector compatibility surface", true },
	{ "code/game/surfaceflags.h", "shared Q3 surface and contents flags", true },
	{ "code/qcommon/qfiles.h", "Q3 file format constants referenced by BotLib", false },
	{ "code/botlib/be_aas_bsp.h", "BSP callback declarations", true },
	{ "code/botlib/be_aas_bspq3.c", "Q3 BSP adapter to replace behind WORR callbacks", false },
	{ "code/botlib/be_aas_cluster.c", "AAS cluster processing", true },
	{ "code/botlib/be_aas_cluster.h", "AAS cluster declarations", true },
	{ "code/botlib/be_aas_debug.c", "AAS debug draw helpers", false },
	{ "code/botlib/be_aas_debug.h", "AAS debug draw declarations", false },
	{ "code/botlib/be_aas_def.h", "AAS runtime world definitions", true },
	{ "code/botlib/be_aas_entity.c", "BotLib entity state cache", true },
	{ "code/botlib/be_aas_entity.h", "BotLib entity declarations", true },
	{ "code/botlib/be_aas_file.c", "AAS load/unload implementation", true },
	{ "code/botlib/be_aas_file.h", "AAS file load declarations", true },
	{ "code/botlib/be_aas_funcs.h", "AAS exported function declarations", true },
	{ "code/botlib/be_aas_main.c", "AAS runtime initialization and frame state", true },
	{ "code/botlib/be_aas_main.h", "AAS runtime initialization declarations", true },
	{ "code/botlib/be_aas_move.c", "AAS movement prediction", true },
	{ "code/botlib/be_aas_move.h", "AAS movement declarations", true },
	{ "code/botlib/be_aas_optimize.c", "AAS optimization helpers", true },
	{ "code/botlib/be_aas_optimize.h", "AAS optimization declarations", true },
	{ "code/botlib/be_aas_reach.h", "AAS reachability declarations", true },
	{ "code/botlib/be_aas_route.c", "AAS routing implementation", true },
	{ "code/botlib/be_aas_route.h", "AAS routing declarations", true },
	{ "code/botlib/be_aas_routealt.c", "alternate route goals", true },
	{ "code/botlib/be_aas_routealt.h", "alternate route declarations", true },
	{ "code/botlib/be_aas_sample.c", "area lookup and sampling", true },
	{ "code/botlib/be_aas_sample.h", "area lookup declarations", true },
	{ "code/botlib/be_interface.c", "BotLib import/export entry point", true },
	{ "code/botlib/be_interface.h", "BotLib interface declarations", true },
	{ "code/botlib/l_crc.c", "script and checksum utility", true },
	{ "code/botlib/l_crc.h", "script and checksum utility declarations", true },
	{ "code/botlib/l_libvar.c", "BotLib libvar storage", true },
	{ "code/botlib/l_libvar.h", "BotLib libvar declarations", true },
	{ "code/botlib/l_log.c", "BotLib logging utility", true },
	{ "code/botlib/l_log.h", "BotLib logging declarations", true },
	{ "code/botlib/l_memory.c", "BotLib memory utility", true },
	{ "code/botlib/l_memory.h", "BotLib memory declarations", true },
	{ "code/botlib/l_precomp.c", "BotLib precompiler/parser utility", true },
	{ "code/botlib/l_precomp.h", "BotLib precompiler/parser declarations", true },
	{ "code/botlib/l_script.c", "BotLib script parser utility", true },
	{ "code/botlib/l_script.h", "BotLib script parser declarations", true },
	{ "code/botlib/l_struct.c", "BotLib structure parser utility", true },
	{ "code/botlib/l_struct.h", "BotLib structure parser declarations", true },
	{ "code/botlib/l_utils.h", "BotLib shared utility declarations", true },
};
} // namespace

const Q3ABotLibBoundaryInfo &Q3A_BotLibBoundaryInfo() {
	return kBoundaryInfo;
}

const Q3ABotLibPlannedFile *Q3A_BotLibPlannedFiles() {
	return kPlannedFiles;
}

size_t Q3A_BotLibPlannedFileCount() {
	return sizeof(kPlannedFiles) / sizeof(kPlannedFiles[0]);
}
