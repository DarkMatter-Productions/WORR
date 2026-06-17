// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once

#include <cstddef>

struct Q3ABotLibPlannedFile {
	const char *upstreamPath;
	const char *role;
	bool initialRuntimeCandidate;
};

struct Q3ABotLibBoundaryInfo {
	const char *sourceName;
	const char *sourceUrl;
	const char *sourceCommit;
	const char *localImportRoot;
	const char *buildStrategy;
	const char *provenanceRule;
	bool runtimeImported;
};

const Q3ABotLibBoundaryInfo &Q3A_BotLibBoundaryInfo();
const Q3ABotLibPlannedFile *Q3A_BotLibPlannedFiles();
size_t Q3A_BotLibPlannedFileCount();
