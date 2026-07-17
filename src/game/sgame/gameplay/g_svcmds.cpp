/*Copyright (c) 2024 ZeniMax Media Inc.
Licensed under the GNU General Public License 2.0.

g_svcmds.cpp (Game Server Commands) - modernized C++ Responsibilities: - ServerCommand():
dispatch "sv" console/RCON commands - IP filtering: addip/removeip/listip/writeip -
G_FilterPacket(): packet gate using configured filters*/

#include "../../bgame/char_array_utils.hpp"
#include "../g_local.hpp"
#include "../bots/bot_runtime.hpp"
#include "../commands/command_registration.hpp"
#include "../commands/command_system.hpp"
#include "../commands/command_voting.hpp"
#include "../network/lag_compensation.hpp"

#include <array>
#include <vector>
#include <string>
#include <string_view>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <memory>
#include <system_error>

// External cvars/engine globals expected from g_local.hpp
// extern cvar_t* filterBan; // 1 = matching IPs are banned, 0 = only matching IPs are allowed

namespace
{

	/*
	===============
	IPFilter
	===============
	*/
	struct IPFilter
	{
		std::array<uint8_t, 4> compare{}; // value to compare
		std::array<uint8_t, 4> mask{};    // mask; 255 means must match, 0 means wildcard
	};

	constexpr size_t MAX_IPFILTERS = 1024;

	static std::vector<IPFilter> g_filters; // active filters

	/*
	===============
	IsDigit
	===============
	*/
	inline bool IsDigit(char c) noexcept
	{
		return c >= '0' && c <= '9';
	}

	/*
	===============
	IsWhitespace

	Returns true if the character is a standard ASCII whitespace.
	===============
	*/
	inline bool IsWhitespace(char c) noexcept
	{
		return c == ' ' || c == '\t' || c == '\r' || c == '\n';
	}

	/*
	===============
	ParseOctet

	Parses a decimal octet [0..255] from the head of s.
	Advances sv to the first non-digit.
	Returns std::optional style via bool success + out value.
	===============
	*/
	static bool ParseOctet(std::string_view& sv, uint8_t& out) noexcept
	{
		if (sv.empty() || !IsDigit(sv.front()))
			return false;

		unsigned value = 0;
		size_t i = 0;
		while (i < sv.size() && IsDigit(sv[i]) && value <= 255u) {
			value = (value * 10u) + static_cast<unsigned>(sv[i] - '0');
			++i;
		}

		if (value > 255u)
			return false;

		out = static_cast<uint8_t>(value);
		sv.remove_prefix(i);
		return true;
	}

	/*
	===============
	StringToFilter

	Parses an IP mask string into an IPFilter.
	Behavior matches classic Quake II semantics:
	- Dotted quad with optional trailing segments.
	- Any segment set to 0 acts as a wildcard (mask 0).
	Examples:
	  "192.168.1.15"  => exact host
	  "192.168.0.0"   => wildcard last two (class C style)
	  "10"            => wildcard last three
	===============
	*/
	static bool StringToFilter(std::string_view s, IPFilter& out)
	{
		std::array<uint8_t, 4> b{ 0, 0, 0, 0 };
		std::array<uint8_t, 4> m{ 0, 0, 0, 0 };

		for (int seg = 0; seg < 4; ++seg) {
			// empty segment list: stop early (remaining stay as 0/wildcards)
			if (s.empty())
				break;

			// malformed: non-digit at start (except we allow immediate '.' to mean empty which we reject)
			if (!IsDigit(s.front())) {
				gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_sgame_auto_762a1dd7ee0a", std::string(s).c_str());
				return false;
			}

			uint8_t oct = 0;
			if (!ParseOctet(s, oct)) {
				gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_sgame_auto_762a1dd7ee0a", std::string(s).c_str());
				return false;
			}

			b[seg] = oct;
			m[seg] = (oct != 0) ? 255 : 0;

			// expect '.' between segments, otherwise we stop
			if (!s.empty()) {
				if (s.front() == '.') {
					s.remove_prefix(1);
					continue;
				}
				// allow stopping if not '.' (e.g. end or port ':' etc.)
				break;
			}
		}

		out.compare = b;
		out.mask = m;
		return true;
	}

	/*
	===============
	ParseFromAddress

	Extract dotted IPv4 left side of "a.b.c.d[:port]" into 4 octets.
	Silently ignores trailing ":port". Non-digit leading chars are skipped.
	Returns true on success.
	===============
	*/
	static bool ParseFromAddress(std::string_view s, std::array<uint8_t, 4>& out)
	{
		// Skip up to the first digit
		while (!s.empty() && !IsDigit(s.front()))
			s.remove_prefix(1);

		std::array<uint8_t, 4> b{ 0, 0, 0, 0 };
		int seg = 0;
		while (!s.empty() && seg < 4) {
			if (!IsDigit(s.front()))
				break;

			uint8_t oct = 0;
			if (!ParseOctet(s, oct))
				return false;

			b[seg++] = oct;

			if (!s.empty() && s.front() == '.') {
				s.remove_prefix(1);
				continue;
			}
			break;
		}

		if (seg == 0)
			return false;

		out = b;
		return true;
	}

	/*
	===============
	Matches
	===============
	*/
	static bool Matches(const IPFilter& f, const std::array<uint8_t, 4>& in) noexcept
	{
		for (int i = 0; i < 4; ++i) {
			if ((in[i] & f.mask[i]) != f.compare[i])
				return false;
		}
		return true;
	}

	/*
	===============
	FormatIP

	Formats an IPv4 address into dotted-quad notation.
	===============
	*/
	static std::string FormatIP(const std::array<uint8_t, 4>& b)
	{
		char buffer[sizeof("255.255.255.255")];
		G_FmtTo(buffer, "{}.{}.{}.{}",
			static_cast<unsigned>(b[0]),
			static_cast<unsigned>(b[1]),
			static_cast<unsigned>(b[2]),
			static_cast<unsigned>(b[3]));
		return std::string(buffer);
	}

	/*
	===============
	PrintIP
	===============
	*/
	static void PrintIP(const std::array<uint8_t, 4>& b)
	{
		const std::string formatted = FormatIP(b);
		gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_sgame_auto_5f36b2ea2906", formatted.c_str());
	}

	/*
	===============
	ResolveIPFilterPath

	Determines where the persistent IP filter configuration should be stored.
	===============
	*/
	static std::filesystem::path ResolveIPFilterPath()
	{
		cvar_t* gameCvar = gi.cvar("game", "", CVAR_NOFLAGS);
		const char* gameDir = (gameCvar && gameCvar->string) ? gameCvar->string : "";

		if (gameDir && *gameDir)
			return std::filesystem::path(gameDir) / "listip.cfg";

		return std::filesystem::path(GAMEVERSION) / "listip.cfg";
	}

	/*
	===============
	Svcmd_Test_f
	===============
	*/
	static void Svcmd_Test_f()
	{
		gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_sgame_auto_e248f6b18c28");
	}

	/*
	===============
	SVCmd_AddIP_f
	===============
	*/
	static void SVCmd_AddIP_f()
	{
		if (gi.argc() < 3) {
			gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_sgame_auto_49b37599328b", gi.argv(1));
			return;
		}

		if (g_filters.size() >= MAX_IPFILTERS) {
			gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_sgame_auto_02ac71f31677");
			return;
		}

		IPFilter f{};
		if (!StringToFilter(gi.argv(2), f))
			return;

		// If identical exists, do not duplicate.
		auto it = std::find_if(g_filters.begin(), g_filters.end(),
			[&](const IPFilter& x)
			{
				return x.compare == f.compare && x.mask == f.mask;
			});
		if (it == g_filters.end()) {
			g_filters.emplace_back(f);
		}
	}

	/*
	===============
	SVCmd_RemoveIP_f
	===============
	*/
	static void SVCmd_RemoveIP_f()
	{
		if (gi.argc() < 3) {
			gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_sgame_auto_49b37599328b", gi.argv(1));
			return;
		}

		IPFilter f{};
		if (!StringToFilter(gi.argv(2), f))
			return;

		const auto oldSize = g_filters.size();
		g_filters.erase(std::remove_if(g_filters.begin(), g_filters.end(),
			[&](const IPFilter& x)
			{
				return x.mask == f.mask && x.compare == f.compare;
			}),
			g_filters.end());

		if (g_filters.size() != oldSize) {
			gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_sgame_auto_fe7c444adc1a");
		}
		else {
			gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_sgame_auto_c79065397faf", gi.argv(2));
		}
	}

	/*
	===============
	SVCmd_ListIP_f
	===============
	*/
	static void SVCmd_ListIP_f()
	{
		gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_sgame_auto_d1d00cfb9080");
		for (const auto& f : g_filters) {
			PrintIP(f.compare);
		}
	}

	/*
	===============
	SVCmd_WriteIP_f

	Writes the active IP filters to disk in Quake II's listip.cfg format.
	===============
	*/
	static void SVCmd_WriteIP_f()
	{
		const std::filesystem::path path = ResolveIPFilterPath();
		const std::string pathStr = path.generic_string();

		gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_sgame_auto_b2f31e2d2c6e", pathStr.c_str());

		if (!path.parent_path().empty()) {
			std::error_code dirError;
			std::filesystem::create_directories(path.parent_path(), dirError);
			if (dirError) {
				gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_sgame_auto_bb7da3428607",
					path.parent_path().generic_string().c_str(), dirError.message().c_str());
				return;
			}
		}

		ScopedStdioFile file(std::fopen(pathStr.c_str(), "wb"));
		if (!file) {
			gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_sgame_auto_7aa2109d84ab", pathStr.c_str());
			return;
		}

		const int filterValue = (filterBan != nullptr) ? filterBan->integer : 1;
		if (std::fprintf(file.get(), "set filterban %d\n", filterValue) < 0) {
			gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_sgame_auto_fcde7b73d2ab", pathStr.c_str());
			return;
		}

		for (const auto& f : g_filters) {
			const std::string ip = FormatIP(f.compare);
			if (std::fprintf(file.get(), "sv addip %s\n", ip.c_str()) < 0) {
				gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_sgame_auto_ddbe0ec85279", ip.c_str());
				return;
			}
		}

		if (std::fclose(file.release()) != 0) {
			gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_sgame_auto_75533e2f5be8", pathStr.c_str());
		}
	}

	/*
	==============
	SVCmd_NextMap_f
	==============
	*/
	static void SVCmd_NextMap_f()
	{
		gi.LocBroadcast_Print(PRINT_HIGH, "$g_map_ended_by_server");
		Match_End();
	}

	struct BotTeamPolicyStatus
	{
		int bots = 0;
		int playing = 0;
		int spectators = 0;
		int queued = 0;
		int none = 0;
		int free = 0;
		int red = 0;
		int blue = 0;
	};

	struct BotWarmupStatus
	{
		int bots = 0;
		int humans = 0;
		int playing = 0;
		int botPlaying = 0;
		int humanPlaying = 0;
		int spectators = 0;
		int readyHumans = 0;
		int readyBots = 0;
		int minplayersValue = 0;
		int minplayersMet = 0;
		int warmupEnabled = 0;
		int readyUp = 0;
		int startNoHumans = 0;
		int botOnlyStart = 0;
		int noPlayersReady = 0;
		int readyPercentage = 0;
		int requiredReadyPercentage = 0;
		int canStart = 0;
		int matchState = 0;
		int warmupState = 0;
		const char* matchStateName = "unknown";
		const char* warmupStateName = "unknown";
	};

	struct BotVoteLaunchStatus
	{
		int attempted = 0;
		int botFound = 0;
		int client = -1;
		int success = 0;
		int blocked = 0;
		const char* reason = "none";
	};

	struct BotAdminAuditAttemptStatus
	{
		int attempted = 0;
		int botFound = 0;
		int client = -1;
		int forcedAdmin = 0;
		int adminSession = 0;
		int commandFound = 0;
		int adminOnly = 0;
		int allowed = 0;
		int executed = 0;
		int blocked = 0;
		int redLockedBefore = 0;
		int redLockedAfter = 0;
		std::string command = "none";
		std::string reason = "none";
	};

	struct BotAdminAuditStatus
	{
		int bots = 0;
		int humans = 0;
		int playing = 0;
		int botPlaying = 0;
		int humanPlaying = 0;
		int spectators = 0;
		int adminBots = 0;
		int adminHumans = 0;
		int redLocked = 0;
		int blueLocked = 0;
		int allowAdmin = 0;
		BotAdminAuditAttemptStatus lastAttempt{};
	};

	struct BotTournamentVetoAttemptStatus
	{
		int attempted = 0;
		int botFound = 0;
		int client = -1;
		int activeBefore = 0;
		int vetoStartedBefore = 0;
		int vetoCompleteBefore = 0;
		int picksBefore = 0;
		int bansBefore = 0;
		int allowed = 0;
		int blocked = 0;
		int picksAfter = 0;
		int bansAfter = 0;
		int vetoCompleteAfter = 0;
		std::string map = "none";
		std::string message = "none";
	};

	struct BotTournamentReplaySetupStatus
	{
		int attempted = 0;
		int configured = 0;
		int active = 0;
		int order = 0;
		int history = 0;
		int gamesPlayed = 0;
		int player0Wins = 0;
		int player1Wins = 0;
		int seriesComplete = 0;
		int bestOf = 0;
		int winTarget = 0;
		std::string replayMap = "none";
	};

	struct BotTournamentReplayAttemptStatus
	{
		int attempted = 0;
		int gameNumber = 0;
		int activeBefore = 0;
		int success = 0;
		int rejected = 0;
		int preserved = 0;
		int resetApplied = 0;
		int gamesBefore = 0;
		int gamesAfter = 0;
		int winnersBefore = 0;
		int winnersAfter = 0;
		int idsBefore = 0;
		int idsAfter = 0;
		int mapsBefore = 0;
		int mapsAfter = 0;
		int player0WinsBefore = 0;
		int player0WinsAfter = 0;
		int player1WinsBefore = 0;
		int player1WinsAfter = 0;
		int seriesCompleteBefore = 0;
		int seriesCompleteAfter = 0;
		int changeMapBefore = 0;
		int changeMapAfter = 0;
		std::string reason = "none";
		std::string targetMap = "none";
	};

	struct BotTournamentSetupStatus
	{
		int attempted = 0;
		int botFound = 0;
		int client = -1;
		int configured = 0;
		int active = 0;
		int vetoStarted = 0;
		int botIsHome = 0;
		int pool = 0;
		int bestOf = 0;
		int picksNeeded = 0;
		std::string botSocialID = "none";
		std::string map0 = "none";
	};

	struct BotTournamentStatus
	{
		int bots = 0;
		int humans = 0;
		int playing = 0;
		int botPlaying = 0;
		int humanPlaying = 0;
		int spectators = 0;
		int active = 0;
		int vetoStarted = 0;
		int vetoComplete = 0;
		int homeTurn = 0;
		int teamBased = 0;
		int pool = 0;
		int picks = 0;
		int bans = 0;
		int order = 0;
		int picksNeeded = 0;
		int gamesPlayed = 0;
		int seriesComplete = 0;
		int matchWinners = 0;
		int matchIds = 0;
		int matchMaps = 0;
		int player0Wins = 0;
		int player1Wins = 0;
		int changeMapSet = 0;
		std::string homeId = "none";
		std::string awayId = "none";
		std::string firstMap = "none";
		std::string changeMap = "none";
		BotTournamentSetupStatus lastSetup{};
		BotTournamentVetoAttemptStatus lastVeto{};
		BotTournamentReplaySetupStatus lastReplaySetup{};
		BotTournamentReplayAttemptStatus lastReplay{};
	};

	struct BotVoteStatus
	{
		int bots = 0;
		int humans = 0;
		int playing = 0;
		int botPlaying = 0;
		int humanPlaying = 0;
		int spectators = 0;
		int votingClients = 0;
		int activeVote = 0;
		int voteOpen = 0;
		int executePending = 0;
		int callerBot = 0;
		int callerHuman = 0;
		int voteYes = 0;
		int voteNo = 0;
		int botYes = 0;
		int botNo = 0;
		int humanYes = 0;
		int humanNo = 0;
		int allowVoting = 0;
		int allowSpecVote = 0;
		int voteFlags = 0;
		BotVoteLaunchStatus lastLaunch{};
	};

	struct BotMapVoteBotCastStatus
	{
		int attempted = 0;
		int botFound = 0;
		int client = -1;
		int requestedIndex = -1;
		int active = 0;
		int blocked = 0;
		int counted = 0;
		int storedVote = -1;
		int count0 = 0;
		int count1 = 0;
		int count2 = 0;
		const char* reason = "none";
	};

	struct BotMapVoteFinalizeStatus
	{
		int attempted = 0;
		int success = 0;
		int selectedIndex = -1;
		int selectedVotes = 0;
		int candidates = 0;
		int exitRequested = 0;
		int changeMapSet = 0;
		const char* reason = "none";
		std::string targetMap = {};
		std::string currentMap = {};
	};

	struct BotMapVoteStatus
	{
		int bots = 0;
		int humans = 0;
		int playing = 0;
		int botPlaying = 0;
		int humanPlaying = 0;
		int spectators = 0;
		int connectedClients = 0;
		int active = 0;
		int forceExit = 0;
		int postIntermission = 0;
		int exitRequested = 0;
		int candidates = 0;
		int totalCountedVotes = 0;
		int botVotes = 0;
		int humanVotes = 0;
		int count0 = 0;
		int count1 = 0;
		int count2 = 0;
		int changeMapSet = 0;
		std::string currentMap = {};
		std::string changeMap = {};
		std::string candidate0 = {};
		std::string candidate1 = {};
		std::string candidate2 = {};
		BotMapVoteBotCastStatus lastBotVote{};
		BotMapVoteFinalizeStatus lastFinalize{};
	};

	struct BotMyMapQueueStatus
	{
		int attempted = 0;
		int botFound = 0;
		int client = -1;
		int socialAssigned = 0;
		int mapSeeded = 0;
		int success = 0;
		int rejected = 0;
		const char* reason = "none";
		std::string mapName = {};
		std::string socialID = {};
	};

	struct BotMyMapConsumeStatus
	{
		int attempted = 0;
		int success = 0;
		const char* reason = "none";
		std::string mapName = {};
		std::string socialID = {};
	};

	struct BotMyMapStatus
	{
		int bots = 0;
		int humans = 0;
		int playing = 0;
		int botPlaying = 0;
		int humanPlaying = 0;
		int spectators = 0;
		int playQueue = 0;
		int myMapQueue = 0;
		int allowMyMap = 0;
		int mapsMyMap = 0;
		int queueLimit = 0;
		std::string frontMap = {};
		std::string frontSocial = {};
		uint16_t frontEnableFlags = 0;
		uint16_t frontDisableFlags = 0;
		std::string myMapFrontMap = {};
		std::string myMapFrontSocial = {};
		BotMyMapQueueStatus lastQueue{};
		BotMyMapConsumeStatus lastConsume{};
	};

	struct BotIntermissionBeginStatus
	{
		int attempted = 0;
		int success = 0;
		int botCount = 0;
		const char* reason = "none";
		std::string mapName = {};
	};

	struct BotIntermissionStatus
	{
		int bots = 0;
		int humans = 0;
		int playing = 0;
		int botPlaying = 0;
		int humanPlaying = 0;
		int spectators = 0;
		int connectedClients = 0;
		int sortedClients = 0;
		int sortedBots = 0;
		int sortedHumans = 0;
		int intermission = 0;
		int intermissionQueued = 0;
		int postIntermission = 0;
		int readyToExit = 0;
		int changeMapSet = 0;
		int changeMapCurrent = 0;
		std::string changeMap = {};
		std::string currentMap = {};
		int intermissionBots = 0;
		int pmFreezeBots = 0;
		int freecamBots = 0;
		int solidNotBots = 0;
		BotIntermissionBeginStatus lastBegin{};
	};

	struct BotNextMapTransitionStatus
	{
		int attempted = 0;
		int success = 0;
		int consumed = 0;
		int playQueueBefore = 0;
		int myMapQueueBefore = 0;
		int playQueueAfter = 0;
		int myMapQueueAfter = 0;
		int overrideEnableFlags = 0;
		int overrideDisableFlags = 0;
		const char* reason = "none";
		std::string targetMap = {};
		std::string currentMap = {};
	};

	struct BotNextMapStatus
	{
		int bots = 0;
		int humans = 0;
		int playing = 0;
		int botPlaying = 0;
		int humanPlaying = 0;
		int spectators = 0;
		int connectedClients = 0;
		int playQueue = 0;
		int myMapQueue = 0;
		int changeMapSet = 0;
		std::string changeMap = {};
		std::string currentMap = {};
		std::string frontMap = {};
		std::string frontSocial = {};
		BotNextMapTransitionStatus lastTransition{};
	};

	struct BotScoreboardApplyStatus
	{
		int attempted = 0;
		int botCount = 0;
		int applied = 0;
		int leaderClient = -1;
		int runnerClient = -1;
		int leaderScore = 0;
		int runnerScore = 0;
		const char* reason = "none";
	};

	struct BotScoreboardRowStatus
	{
		int client = -1;
		int isBot = 0;
		int isHuman = 0;
		int isPlaying = 0;
		int isSpectator = 0;
		int score = 0;
		int rank = -1;
		int rankTied = 0;
	};

	struct BotScoreboardStatus
	{
		int bots = 0;
		int humans = 0;
		int playing = 0;
		int botPlaying = 0;
		int humanPlaying = 0;
		int spectators = 0;
		int votingClients = 0;
		int connectedClients = 0;
		int sortedClients = 0;
		int sortedBots = 0;
		int sortedHumans = 0;
		int sortedSpectators = 0;
		int leaderBot = 0;
		int runnerBot = 0;
		int scoreOrdered = 0;
		int rankOrdered = 0;
		BotScoreboardRowStatus top{};
		BotScoreboardRowStatus second{};
		BotScoreboardApplyStatus lastApply{};
	};

	struct BotChatPolicyStatus
	{
		int bots = 0;
		int humans = 0;
		int playing = 0;
		int botPlaying = 0;
		int profileChat = 0;
		int allowChat = 0;
		int teamOnly = 0;
		int consumerReady = 0;
		int dispatchEnabled = 0;
		int dispatchAttempts = 0;
		int dispatchSubmitted = 0;
		int dispatchFailures = 0;
		int dispatchRateLimited = 0;
		int rateLimitMs = 0;
		int lastDispatchTimeMs = -1;
		int lastDispatchClient = -1;
		int lastDispatchTeam = 0;
		int initialSelections = 0;
		int initialKnownPersonalities = 0;
		int initialUnknownPersonalities = 0;
		int initialQuiet = 0;
		int initialDirect = 0;
		int initialTaunting = 0;
		int initialHelpful = 0;
		int initialSteady = 0;
		int initialPhraseVariants = 0;
		int initialUniquePhraseVariants = 0;
		int lastInitialClient = -1;
		int lastInitialPersonality = 0;
		int lastInitialPhrase = 0;
		int lastInitialPhraseVariant = -1;
		int replyEnabled = 0;
		int replyEvents = 0;
		int replySelections = 0;
		int replyKnownPersonalities = 0;
		int replyUnknownPersonalities = 0;
		int replyTeamReady = 0;
		int replyRouteReady = 0;
		int replyItemTaken = 0;
		int replyItemDenied = 0;
		int replyObjectiveChanged = 0;
		int replyFlagState = 0;
		int replyEnemySighted = 0;
		int replyLowHealth = 0;
		int replyBlocked = 0;
		int replyMatchResult = 0;
		int replyMatchResultUnknown = 0;
		int replyMatchResultWin = 0;
		int replyMatchResultLoss = 0;
		int replyMatchResultTie = 0;
		int replyMatchResultAbort = 0;
		int replySubmitted = 0;
		int replyRateLimited = 0;
		int replyDuplicateSuppressed = 0;
		int replyFailures = 0;
		int replyPhraseVariants = 0;
		int replyUniquePhraseVariants = 0;
		int lastReplyClient = -1;
		int lastReplyPersonality = 0;
		int lastReplyPhrase = 0;
		int lastReplyPhraseVariant = -1;
		int lastReplyEvent = 0;
		int lastMatchResultOutcome = 0;
		const char *lastMatchResultOutcomeName = "unknown";
		int liveEnabled = 0;
		int liveEvents = 0;
		int liveSpawn = 0;
		int liveRouteReady = 0;
		int liveItemTaken = 0;
		int liveItemDenied = 0;
		int liveObjectiveChanged = 0;
		int liveFlagState = 0;
		int liveEnemySighted = 0;
		int liveLowHealth = 0;
		int liveBlocked = 0;
		int liveMatchResult = 0;
		int liveSubmitted = 0;
		int liveRateLimited = 0;
		int liveDuplicateSuppressed = 0;
		int liveFailures = 0;
		int liveEventTaxonomy = 0;
		int duplicateWindowMs = 0;
		int lastDuplicateClient = -1;
		int lastDuplicateEvent = 0;
		const char* lastDuplicateEventName = "none";
		int lastDuplicatePhrase = 0;
		int lastDuplicateElapsedMs = -1;
		int lastLiveEvent = 0;
		const char* lastLiveEventName = "none";
		int blockedUntilConsumer = 0;
	};

	static BotVoteLaunchStatus botVoteLastLaunch{};
	static BotAdminAuditAttemptStatus botAdminAuditLastAttempt{};
	static BotTournamentSetupStatus botTournamentLastSetup{};
	static BotTournamentVetoAttemptStatus botTournamentLastVeto{};
	static BotTournamentReplaySetupStatus botTournamentLastReplaySetup{};
	static BotTournamentReplayAttemptStatus botTournamentLastReplay{};
	static BotMapVoteBotCastStatus botMapVoteLastBotCast{};
	static BotMapVoteFinalizeStatus botMapVoteLastFinalize{};
	static BotMyMapQueueStatus botMyMapLastQueue{};
	static BotMyMapConsumeStatus botMyMapLastConsume{};
	static BotIntermissionBeginStatus botIntermissionLastBegin{};
	static BotNextMapTransitionStatus botNextMapLastTransition{};
	static BotScoreboardApplyStatus botScoreboardLastApply{};

	static bool IsBotClient(const gentity_t* ent)
	{
		return ent && ent->client && ((ent->svFlags & SVF_BOT) || ent->client->sess.is_a_bot);
	}

	static const char* MatchStateName(MatchState state)
	{
		switch (state) {
		case MatchState::None:
			return "none";
		case MatchState::Initial_Delay:
			return "initial_delay";
		case MatchState::Warmup_Default:
			return "warmup_default";
		case MatchState::Warmup_ReadyUp:
			return "warmup_ready_up";
		case MatchState::Countdown:
			return "countdown";
		case MatchState::In_Progress:
			return "in_progress";
		case MatchState::Ended:
			return "ended";
		default:
			return "unknown";
		}
	}

	static const char* WarmupStateName(WarmupState state)
	{
		switch (state) {
		case WarmupState::Default:
			return "default";
		case WarmupState::Too_Few_Players:
			return "too_few_players";
		case WarmupState::Teams_Imbalanced:
			return "teams_imbalanced";
		case WarmupState::Not_Ready:
			return "not_ready";
		default:
			return "unknown";
		}
	}

	static const char* BotVoteLaunchReason(const std::string& message)
	{
		if (message == "Bots cannot call votes.")
			return "bot_blocked";
		if (message.empty())
			return "rejected_no_message";
		return "rejected";
	}

	static gentity_t* FindFirstPlayingBotClient()
	{
		CalculateRanks();

		for (auto ent : active_clients()) {
			if (!IsBotClient(ent))
				continue;
			if (!ClientIsPlaying(ent->client))
				continue;
			return ent;
		}

		return nullptr;
	}

	static int FindFirstTwoPlayingBots(gentity_t** first, gentity_t** second)
	{
		int found = 0;

		if (first)
			*first = nullptr;
		if (second)
			*second = nullptr;

		CalculateRanks();

		for (auto ent : active_clients()) {
			if (!IsBotClient(ent))
				continue;
			if (!ClientIsPlaying(ent->client))
				continue;

			if (found == 0 && first)
				*first = ent;
			else if (found == 1 && second)
				*second = ent;

			found++;
			if (found >= 2)
				break;
		}

		return found;
	}

	static BotTeamPolicyStatus CountBotTeamPolicyStatus()
	{
		BotTeamPolicyStatus status{};

		CalculateRanks();

		for (auto ent : active_clients()) {
			if (!IsBotClient(ent))
				continue;

			gclient_t* client = ent->client;
			status.bots++;

			if (ClientIsPlaying(client))
				status.playing++;

			if (client->sess.matchQueued)
				status.queued++;

			switch (client->sess.team) {
			case Team::None:
				status.none++;
				break;
			case Team::Spectator:
				status.spectators++;
				break;
			case Team::Free:
				status.free++;
				break;
			case Team::Red:
				status.red++;
				break;
			case Team::Blue:
				status.blue++;
				break;
			default:
				break;
			}
		}

		return status;
	}

	static void FillBotScoreboardRow(BotScoreboardRowStatus& row,
		int clientIndex)
	{
		if (clientIndex < 0 || clientIndex >= static_cast<int>(game.maxClients))
			return;

		gclient_t* client = &game.clients[clientIndex];
		if (!client->pers.connected)
			return;

		gentity_t* ent = &g_entities[clientIndex + 1];
		const bool isBot = IsBotClient(ent);
		const bool isPlaying = ClientIsPlaying(client);
		const int rank = client->pers.currentRank;

		row.client = clientIndex;
		row.isBot = isBot ? 1 : 0;
		row.isHuman = isBot ? 0 : 1;
		row.isPlaying = isPlaying ? 1 : 0;
		row.isSpectator = isPlaying ? 0 : 1;
		row.score = ClientScoreForStandings(client);
		row.rank = rank >= 0 ? (rank & ~RANK_TIED_FLAG) : -1;
		row.rankTied = (rank >= 0 && (rank & RANK_TIED_FLAG)) ? 1 : 0;
	}

	static BotScoreboardStatus CountBotScoreboardStatus()
	{
		BotScoreboardStatus status{};
		int playingRows = 0;

		CalculateRanks();

		status.votingClients = level.pop.num_voting_clients;
		status.connectedClients = level.pop.num_connected_clients;
		status.lastApply = botScoreboardLastApply;

		for (auto ent : active_clients()) {
			gclient_t* client = ent->client;
			const bool isBot = IsBotClient(ent);
			const bool isPlaying = ClientIsPlaying(client);

			if (isBot)
				status.bots++;
			else
				status.humans++;

			if (isPlaying) {
				status.playing++;
				if (isBot)
					status.botPlaying++;
				else
					status.humanPlaying++;
			}
			else {
				status.spectators++;
			}
		}

		for (size_t i = 0; i < level.sortedClients.size(); ++i) {
			const int clientIndex = level.sortedClients[i];
			if (clientIndex < 0)
				continue;
			if (clientIndex >= static_cast<int>(game.maxClients))
				continue;

			gclient_t* client = &game.clients[clientIndex];
			if (!client->pers.connected)
				continue;

			gentity_t* ent = &g_entities[clientIndex + 1];
			const bool isBot = IsBotClient(ent);
			const bool isPlaying = ClientIsPlaying(client);

			status.sortedClients++;
			if (isBot)
				status.sortedBots++;
			else
				status.sortedHumans++;

			if (!isPlaying) {
				status.sortedSpectators++;
				continue;
			}

			if (playingRows == 0)
				FillBotScoreboardRow(status.top, clientIndex);
			else if (playingRows == 1)
				FillBotScoreboardRow(status.second, clientIndex);

			playingRows++;
		}

		status.leaderBot = status.top.isBot;
		status.runnerBot = status.second.isBot;
		status.scoreOrdered = (status.top.client >= 0 &&
			(status.second.client < 0 || status.top.score >= status.second.score)) ? 1 : 0;
		status.rankOrdered = (status.top.client >= 0 && status.top.rank == 0 &&
			(status.second.client < 0 || status.second.rank == 1)) ? 1 : 0;

		return status;
	}

	static BotChatPolicyStatus CountBotChatPolicyStatus()
	{
		BotChatPolicyStatus status{};

		status.allowChat = bot_allow_chat && bot_allow_chat->integer > 0 ? 1 : 0;
		status.teamOnly = bot_chat_team_only && bot_chat_team_only->integer > 0 ? 1 : 0;
		status.consumerReady = BotChatPolicy_ConsumerReady();
		status.dispatchEnabled = status.allowChat && status.consumerReady ? 1 : 0;
		status.dispatchAttempts = BotChatPolicy_DispatchAttempts();
		status.dispatchSubmitted = BotChatPolicy_DispatchSubmitted();
		status.dispatchFailures = BotChatPolicy_DispatchFailures();
		status.dispatchRateLimited = BotChatPolicy_DispatchRateLimited();
		status.rateLimitMs = BotChatPolicy_RateLimitMilliseconds();
		status.lastDispatchTimeMs = BotChatPolicy_LastDispatchTimeMilliseconds();
		status.lastDispatchClient = BotChatPolicy_LastDispatchClient();
		status.lastDispatchTeam = BotChatPolicy_LastDispatchTeam();
		status.initialSelections = BotChatPolicy_InitialSelections();
		status.initialKnownPersonalities = BotChatPolicy_InitialKnownPersonalities();
		status.initialUnknownPersonalities = BotChatPolicy_InitialUnknownPersonalities();
		status.initialQuiet = BotChatPolicy_InitialQuiet();
		status.initialDirect = BotChatPolicy_InitialDirect();
		status.initialTaunting = BotChatPolicy_InitialTaunting();
		status.initialHelpful = BotChatPolicy_InitialHelpful();
		status.initialSteady = BotChatPolicy_InitialSteady();
		status.initialPhraseVariants = BotChatPolicy_InitialPhraseVariants();
		status.initialUniquePhraseVariants = BotChatPolicy_InitialUniquePhraseVariants();
		status.lastInitialClient = BotChatPolicy_LastInitialClient();
		status.lastInitialPersonality = BotChatPolicy_LastInitialPersonality();
		status.lastInitialPhrase = BotChatPolicy_LastInitialPhrase();
		status.lastInitialPhraseVariant = BotChatPolicy_LastInitialPhraseVariant();
		status.replyEnabled = BotChatPolicy_ReplyEnabled();
		status.replyEvents = BotChatPolicy_ReplyEvents();
		status.replySelections = BotChatPolicy_ReplySelections();
		status.replyKnownPersonalities = BotChatPolicy_ReplyKnownPersonalities();
		status.replyUnknownPersonalities = BotChatPolicy_ReplyUnknownPersonalities();
		status.replyTeamReady = BotChatPolicy_ReplyTeamReady();
		status.replyRouteReady = BotChatPolicy_ReplyRouteReady();
		status.replyItemTaken = BotChatPolicy_ReplyItemTaken();
		status.replyItemDenied = BotChatPolicy_ReplyItemDenied();
		status.replyObjectiveChanged = BotChatPolicy_ReplyObjectiveChanged();
		status.replyFlagState = BotChatPolicy_ReplyFlagState();
		status.replyEnemySighted = BotChatPolicy_ReplyEnemySighted();
		status.replyLowHealth = BotChatPolicy_ReplyLowHealth();
		status.replyBlocked = BotChatPolicy_ReplyBlocked();
		status.replyMatchResult = BotChatPolicy_ReplyMatchResult();
		status.replyMatchResultUnknown = BotChatPolicy_ReplyMatchResultUnknown();
		status.replyMatchResultWin = BotChatPolicy_ReplyMatchResultWin();
		status.replyMatchResultLoss = BotChatPolicy_ReplyMatchResultLoss();
		status.replyMatchResultTie = BotChatPolicy_ReplyMatchResultTie();
		status.replyMatchResultAbort = BotChatPolicy_ReplyMatchResultAbort();
		status.replySubmitted = BotChatPolicy_ReplySubmitted();
		status.replyRateLimited = BotChatPolicy_ReplyRateLimited();
		status.replyDuplicateSuppressed = BotChatPolicy_ReplyDuplicateSuppressed();
		status.replyFailures = BotChatPolicy_ReplyFailures();
		status.replyPhraseVariants = BotChatPolicy_ReplyPhraseVariants();
		status.replyUniquePhraseVariants = BotChatPolicy_ReplyUniquePhraseVariants();
		status.lastReplyClient = BotChatPolicy_LastReplyClient();
		status.lastReplyPersonality = BotChatPolicy_LastReplyPersonality();
		status.lastReplyPhrase = BotChatPolicy_LastReplyPhrase();
		status.lastReplyPhraseVariant = BotChatPolicy_LastReplyPhraseVariant();
		status.lastReplyEvent = BotChatPolicy_LastReplyEvent();
		status.lastMatchResultOutcome = BotChatPolicy_LastMatchResultOutcome();
		status.lastMatchResultOutcomeName = BotChatPolicy_LastMatchResultOutcomeName();
		status.liveEnabled = BotChatPolicy_LiveEnabled();
		status.liveEvents = BotChatPolicy_LiveEvents();
		status.liveSpawn = BotChatPolicy_LiveSpawn();
		status.liveRouteReady = BotChatPolicy_LiveRouteReady();
		status.liveItemTaken = BotChatPolicy_LiveItemTaken();
		status.liveItemDenied = BotChatPolicy_LiveItemDenied();
		status.liveObjectiveChanged = BotChatPolicy_LiveObjectiveChanged();
		status.liveFlagState = BotChatPolicy_LiveFlagState();
		status.liveEnemySighted = BotChatPolicy_LiveEnemySighted();
		status.liveLowHealth = BotChatPolicy_LiveLowHealth();
		status.liveBlocked = BotChatPolicy_LiveBlocked();
		status.liveMatchResult = BotChatPolicy_LiveMatchResult();
		status.liveSubmitted = BotChatPolicy_LiveSubmitted();
		status.liveRateLimited = BotChatPolicy_LiveRateLimited();
		status.liveDuplicateSuppressed = BotChatPolicy_LiveDuplicateSuppressed();
		status.liveFailures = BotChatPolicy_LiveFailures();
		status.liveEventTaxonomy = BotChatPolicy_LiveEventTaxonomy();
		status.duplicateWindowMs = BotChatPolicy_DuplicateWindowMilliseconds();
		status.lastDuplicateClient = BotChatPolicy_LastDuplicateClient();
		status.lastDuplicateEvent = BotChatPolicy_LastDuplicateEvent();
		status.lastDuplicateEventName = BotChatPolicy_LastDuplicateEventName();
		status.lastDuplicatePhrase = BotChatPolicy_LastDuplicatePhrase();
		status.lastDuplicateElapsedMs =
			BotChatPolicy_LastDuplicateElapsedMilliseconds();
		status.lastLiveEvent = BotChatPolicy_LastLiveEvent();
		status.lastLiveEventName = BotChatPolicy_LastLiveEventName();
		status.blockedUntilConsumer = status.allowChat && !status.consumerReady ? 1 : 0;

		CalculateRanks();

		for (auto ent : active_clients()) {
			if (!ent || !ent->client)
				continue;

			const bool isBot = IsBotClient(ent);
			gclient_t* client = ent->client;

			if (isBot) {
				status.bots++;
				char chatPersonality[MAX_INFO_VALUE] = {};
				if (gi.Info_ValueForKey(client->pers.userInfo,
						"bot_chat_personality", chatPersonality,
						sizeof(chatPersonality)) &&
					chatPersonality[0])
					status.profileChat++;
			}
			else {
				status.humans++;
			}

			if (ClientIsPlaying(client)) {
				status.playing++;
				if (isBot)
					status.botPlaying++;
			}
		}

		return status;
	}

	static BotIntermissionStatus CountBotIntermissionStatus()
	{
		BotIntermissionStatus status{};

		CalculateRanks();

		status.connectedClients = level.pop.num_connected_clients;
		status.intermission = level.intermission.time ? 1 : 0;
		status.intermissionQueued = level.intermission.queued ? 1 : 0;
		status.postIntermission = level.intermission.postIntermission ? 1 : 0;
		status.readyToExit = level.readyToExit ? 1 : 0;
		status.changeMap = level.changeMap;
		status.currentMap = std::string(CharArrayToStringView(level.mapName));
		status.changeMapSet = status.changeMap.empty() ? 0 : 1;
		status.changeMapCurrent =
			(!status.changeMap.empty() && status.changeMap == status.currentMap) ? 1 : 0;
		status.lastBegin = botIntermissionLastBegin;

		for (auto ent : active_clients()) {
			gclient_t* client = ent->client;
			const bool isBot = IsBotClient(ent);
			const bool isPlaying = ClientIsPlaying(client);

			if (isBot)
				status.bots++;
			else
				status.humans++;

			if (isPlaying) {
				status.playing++;
				if (isBot)
					status.botPlaying++;
				else
					status.humanPlaying++;
			}
			else {
				status.spectators++;
			}

			if (!isBot)
				continue;

			const bool pmFreeze = client->ps.pmove.pmType == PM_FREEZE;
			const bool freecam = ent->moveType == MoveType::FreeCam;
			const bool solidNot = ent->solid == SOLID_NOT;

			if (pmFreeze)
				status.pmFreezeBots++;
			if (freecam)
				status.freecamBots++;
			if (solidNot)
				status.solidNotBots++;
			if (status.intermission && pmFreeze && freecam && solidNot)
				status.intermissionBots++;
		}

		for (size_t i = 0; i < level.sortedClients.size(); ++i) {
			const int clientIndex = level.sortedClients[i];
			if (clientIndex < 0)
				continue;
			if (clientIndex >= static_cast<int>(game.maxClients))
				continue;

			gclient_t* client = &game.clients[clientIndex];
			if (!client->pers.connected)
				continue;

			gentity_t* ent = &g_entities[clientIndex + 1];
			if (IsBotClient(ent))
				status.sortedBots++;
			else
				status.sortedHumans++;

			status.sortedClients++;
		}

		return status;
	}

	static BotNextMapStatus CountBotNextMapStatus()
	{
		BotNextMapStatus status{};

		CalculateRanks();

		status.connectedClients = level.pop.num_connected_clients;
		status.playQueue = static_cast<int>(game.mapSystem.playQueue.size());
		status.myMapQueue = static_cast<int>(game.mapSystem.myMapQueue.size());
		status.changeMap = level.changeMap;
		status.currentMap = std::string(CharArrayToStringView(level.mapName));
		status.changeMapSet = status.changeMap.empty() ? 0 : 1;
		status.lastTransition = botNextMapLastTransition;

		if (!game.mapSystem.playQueue.empty()) {
			const QueuedMap& queued = game.mapSystem.playQueue.front();
			status.frontMap = queued.filename;
			status.frontSocial = queued.socialID;
		}

		for (auto ent : active_clients()) {
			gclient_t* client = ent->client;
			const bool isBot = IsBotClient(ent);
			const bool isPlaying = ClientIsPlaying(client);

			if (isBot)
				status.bots++;
			else
				status.humans++;

			if (isPlaying) {
				status.playing++;
				if (isBot)
					status.botPlaying++;
				else
					status.humanPlaying++;
			}
			else {
				status.spectators++;
			}
		}

		return status;
	}

	static BotWarmupStatus CountBotWarmupStatus()
	{
		BotWarmupStatus status{};

		CalculateRanks();

		for (auto ent : active_clients()) {
			gclient_t* client = ent->client;
			const bool isBot = IsBotClient(ent);
			const bool isPlaying = ClientIsPlaying(client);

			if (isBot)
				status.bots++;
			else
				status.humans++;

			if (!isPlaying) {
				status.spectators++;
				continue;
			}

			status.playing++;
			if (isBot) {
				status.botPlaying++;
				if (client->pers.readyStatus)
					status.readyBots++;
			}
			else {
				status.humanPlaying++;
				if (client->pers.readyStatus)
					status.readyHumans++;
			}
		}

		status.minplayersValue = minplayers ? minplayers->integer : 0;
		status.warmupEnabled = warmup_enabled ? warmup_enabled->integer : 0;
		status.readyUp = warmup_doReadyUp ? warmup_doReadyUp->integer : 0;
		status.startNoHumans = match_startNoHumans ? match_startNoHumans->integer : 0;
		status.requiredReadyPercentage = g_warmup_ready_percentage ?
			static_cast<int>(g_warmup_ready_percentage->value * 100.0f) : 0;
		status.readyPercentage = status.humanPlaying > 0 ?
			static_cast<int>((static_cast<float>(status.readyHumans) /
				static_cast<float>(status.humanPlaying)) * 100.0f) : 0;
		status.minplayersMet = (status.minplayersValue <= 0 ||
			status.playing >= status.minplayersValue ||
			level.devWarmupReadyBypass) ? 1 : 0;
		status.botOnlyStart = (!status.humanPlaying && status.botPlaying &&
			status.startNoHumans) ? 1 : 0;
		status.noPlayersReady = (!status.humanPlaying && !status.botPlaying) ? 1 : 0;

		if (!status.readyUp) {
			status.canStart = 1;
		}
		else if (status.noPlayersReady) {
			status.canStart = 1;
		}
		else if (!status.minplayersMet) {
			status.canStart = 0;
		}
		else if (status.botOnlyStart) {
			status.canStart = 1;
		}
		else if (!status.readyHumans) {
			status.canStart = 0;
		}
		else {
			status.canStart = (status.readyPercentage >=
				status.requiredReadyPercentage) ? 1 : 0;
		}

		status.matchState = static_cast<int>(level.matchState);
		status.warmupState = static_cast<int>(level.warmupState);
		status.matchStateName = MatchStateName(level.matchState);
		status.warmupStateName = WarmupStateName(level.warmupState);
		return status;
	}

	static BotVoteStatus CountBotVoteStatus()
	{
		BotVoteStatus status{};

		CalculateRanks();

		status.votingClients = level.pop.num_voting_clients;
		status.activeVote = (level.vote.time || level.vote.executeTime) ? 1 : 0;
		status.voteOpen = level.vote.time ? 1 : 0;
		status.executePending = level.vote.executeTime ? 1 : 0;
		status.voteYes = level.vote.countYes;
		status.voteNo = level.vote.countNo;
		status.allowVoting = g_allowVoting ? g_allowVoting->integer : 0;
		status.allowSpecVote = g_allowSpecVote ? g_allowSpecVote->integer : 0;
		status.voteFlags = g_vote_flags ? g_vote_flags->integer : 0;
		status.lastLaunch = botVoteLastLaunch;

		for (auto ent : active_clients()) {
			gclient_t* client = ent->client;
			const bool isBot = IsBotClient(ent);
			const bool isPlaying = ClientIsPlaying(client);

			if (isBot)
				status.bots++;
			else
				status.humans++;

			if (isPlaying) {
				status.playing++;
				if (isBot)
					status.botPlaying++;
				else
					status.humanPlaying++;
			}
			else {
				status.spectators++;
			}

			if (client->pers.voted > 0) {
				if (isBot)
					status.botYes++;
				else
					status.humanYes++;
			}
			else if (client->pers.voted < 0) {
				if (isBot)
					status.botNo++;
				else
					status.humanNo++;
			}

			if (level.vote.client == client) {
				if (isBot)
					status.callerBot = 1;
				else
					status.callerHuman = 1;
			}
		}

		return status;
	}

	static BotAdminAuditStatus CountBotAdminAuditStatus()
	{
		BotAdminAuditStatus status{};

		CalculateRanks();

		status.redLocked =
			level.locked[static_cast<size_t>(Team::Red)] ? 1 : 0;
		status.blueLocked =
			level.locked[static_cast<size_t>(Team::Blue)] ? 1 : 0;
		status.allowAdmin = g_allowAdmin ? g_allowAdmin->integer : 0;
		status.lastAttempt = botAdminAuditLastAttempt;

		for (auto ent : active_clients()) {
			gclient_t* client = ent->client;
			const bool isBot = IsBotClient(ent);
			const bool isPlaying = ClientIsPlaying(client);

			if (isBot)
				status.bots++;
			else
				status.humans++;

			if (isPlaying) {
				status.playing++;
				if (isBot)
					status.botPlaying++;
				else
					status.humanPlaying++;
			}
			else {
				status.spectators++;
			}

			if (client->sess.admin) {
				if (isBot)
					status.adminBots++;
				else
					status.adminHumans++;
			}
		}

		return status;
	}

	static BotTournamentStatus CountBotTournamentStatus()
	{
		BotTournamentStatus status{};

		CalculateRanks();

		status.active = Tournament_IsActive() ? 1 : 0;
		status.vetoStarted = game.tournament.vetoStarted ? 1 : 0;
		status.vetoComplete = game.tournament.vetoComplete ? 1 : 0;
		status.homeTurn = game.tournament.vetoHomeTurn ? 1 : 0;
		status.teamBased = game.tournament.teamBased ? 1 : 0;
		status.pool = static_cast<int>(game.tournament.mapPool.size());
		status.picks = static_cast<int>(game.tournament.mapPicks.size());
		status.bans = static_cast<int>(game.tournament.mapBans.size());
		status.order = static_cast<int>(game.tournament.mapOrder.size());
		status.picksNeeded = std::max(0, game.tournament.bestOf - 1);
		status.gamesPlayed = game.tournament.gamesPlayed;
		status.seriesComplete = game.tournament.seriesComplete ? 1 : 0;
		status.matchWinners =
			static_cast<int>(game.tournament.matchWinners.size());
		status.matchIds =
			static_cast<int>(game.tournament.matchIds.size());
		status.matchMaps =
			static_cast<int>(game.tournament.matchMaps.size());
		status.player0Wins = game.tournament.playerWins[0];
		status.player1Wins = game.tournament.playerWins[1];
		status.changeMapSet = level.changeMap.empty() ? 0 : 1;
		status.homeId = game.tournament.homeId.empty()
			? "none" : game.tournament.homeId;
		status.awayId = game.tournament.awayId.empty()
			? "none" : game.tournament.awayId;
		status.firstMap = game.tournament.mapPool.empty()
			? "none" : game.tournament.mapPool.front();
		status.changeMap =
			level.changeMap.empty() ? "none" : level.changeMap;
		status.lastSetup = botTournamentLastSetup;
		status.lastVeto = botTournamentLastVeto;
		status.lastReplaySetup = botTournamentLastReplaySetup;
		status.lastReplay = botTournamentLastReplay;

		for (auto ent : active_clients()) {
			gclient_t* client = ent->client;
			const bool isBot = IsBotClient(ent);
			const bool isPlaying = ClientIsPlaying(client);

			if (isBot)
				status.bots++;
			else
				status.humans++;

			if (isPlaying) {
				status.playing++;
				if (isBot)
					status.botPlaying++;
				else
					status.humanPlaying++;
			}
			else {
				status.spectators++;
			}
		}

		return status;
	}

	static BotMapVoteStatus CountBotMapVoteStatus()
	{
		BotMapVoteStatus status{};
		auto& ms = level.mapSelector;

		CalculateRanks();

		status.active = ms.voteStartTime != 0_sec ? 1 : 0;
		status.forceExit = ms.forceExit ? 1 : 0;
		status.postIntermission =
			level.intermission.postIntermissionTime != 0_sec ? 1 : 0;
		status.exitRequested = level.intermission.exit ? 1 : 0;
		status.changeMapSet = level.changeMap.empty() ? 0 : 1;
		status.currentMap = std::string(CharArrayToStringView(level.mapName));
		status.changeMap = level.changeMap;
		status.candidate0 = ms.candidates[0];
		status.candidate1 = ms.candidates[1];
		status.candidate2 = ms.candidates[2];
		status.count0 = ms.voteCounts[0];
		status.count1 = ms.voteCounts[1];
		status.count2 = ms.voteCounts[2];
		status.totalCountedVotes = status.count0 + status.count1 + status.count2;
		status.lastBotVote = botMapVoteLastBotCast;
		status.lastFinalize = botMapVoteLastFinalize;

		for (const auto& candidate : ms.candidates) {
			if (!candidate.empty())
				status.candidates++;
		}

		for (auto ent : active_clients()) {
			gclient_t* client = ent->client;
			const bool isBot = IsBotClient(ent);
			const bool isPlaying = ClientIsPlaying(client);
			const int clientNum = static_cast<int>(ent - g_entities) - 1;

			status.connectedClients++;

			if (isBot)
				status.bots++;
			else
				status.humans++;

			if (isPlaying) {
				status.playing++;
				if (isBot)
					status.botPlaying++;
				else
					status.humanPlaying++;
			}
			else {
				status.spectators++;
			}

			if (clientNum < 0 || clientNum >= MAX_CLIENTS)
				continue;

			const int vote = ms.votes[clientNum];
			if (vote < 0 || vote >= static_cast<int>(ms.candidates.size()) ||
				ms.candidates[vote].empty()) {
				continue;
			}

			if (isBot)
				status.botVotes++;
			else
				status.humanVotes++;
		}

		return status;
	}

	static const char* MarkerString(const std::string& value)
	{
		return value.empty() ? "-" : value.c_str();
	}

	static void BotMapVote_ClearSelectorState()
	{
		auto& ms = level.mapSelector;

		ms.votes.fill(-1);
		ms.voteCounts.fill(0);
		ms.candidates.fill(std::string{});
		ms.voteStartTime = 0_sec;
		ms.forceExit = false;
		level.intermission.exit = false;
		level.intermission.postIntermissionTime = 0_sec;
		level.changeMap.clear();
	}

	static int BotMapVote_EnsureCurrentMapEntry(const std::string& mapName)
	{
		if (mapName.empty() || game.mapSystem.GetMapEntry(mapName))
			return 0;

		MapEntry smokeMap{};
		smokeMap.filename = mapName;
		smokeMap.longName = mapName;
		smokeMap.mapTypeFlags |= MAP_DM;
		smokeMap.isCycleable = true;
		game.mapSystem.mapPool.push_back(std::move(smokeMap));
		return 1;
	}

	static BotMyMapStatus CountBotMyMapStatus()
	{
		BotMyMapStatus status{};

		CalculateRanks();

		status.playQueue = static_cast<int>(game.mapSystem.playQueue.size());
		status.myMapQueue = static_cast<int>(game.mapSystem.myMapQueue.size());
		status.allowMyMap = g_allowMymap ? g_allowMymap->integer : 1;
		status.mapsMyMap = g_maps_mymap ? g_maps_mymap->integer : 0;
		status.queueLimit = g_maps_mymap_queue_limit ?
			g_maps_mymap_queue_limit->integer :
			MapSystem::DEFAULT_MYMAP_QUEUE_LIMIT;
		status.lastQueue = botMyMapLastQueue;
		status.lastConsume = botMyMapLastConsume;

		if (!game.mapSystem.playQueue.empty()) {
			const QueuedMap& queued = game.mapSystem.playQueue.front();
			status.frontMap = queued.filename;
			status.frontSocial = queued.socialID;
			status.frontEnableFlags = queued.enableFlags;
			status.frontDisableFlags = queued.disableFlags;
		}

		if (!game.mapSystem.myMapQueue.empty()) {
			const MyMapRequest& request = game.mapSystem.myMapQueue.front();
			status.myMapFrontMap = request.mapName;
			status.myMapFrontSocial = request.socialID;
		}

		for (auto ent : active_clients()) {
			gclient_t* client = ent->client;
			const bool isBot = IsBotClient(ent);
			const bool isPlaying = ClientIsPlaying(client);

			if (isBot)
				status.bots++;
			else
				status.humans++;

			if (isPlaying) {
				status.playing++;
				if (isBot)
					status.botPlaying++;
				else
					status.humanPlaying++;
			}
			else {
				status.spectators++;
			}
		}

		return status;
	}

	static bool ParseExpectedInt(int arg, int& value)
	{
		if (gi.argc() <= arg)
			return false;

		const char* text = gi.argv(arg);
		if (!text || !*text)
			return false;

		int parsed = 0;
		const char* end = text + std::strlen(text);
		const auto result = std::from_chars(text, end, parsed);
		if (result.ec != std::errc{} || result.ptr != end)
			return false;

		value = parsed;
		return true;
	}

	static void SVCmd_BotTeamPolicyStatus_f()
	{
		int expectedPlaying = -1;
		int expectedSpectators = -1;
		int expectedBots = -1;
		int expectedQueued = -1;

		ParseExpectedInt(2, expectedPlaying);
		ParseExpectedInt(3, expectedSpectators);
		ParseExpectedInt(4, expectedBots);
		ParseExpectedInt(5, expectedQueued);
		BotTeamPolicy_PrintStatus(expectedPlaying, expectedSpectators,
			expectedBots, expectedQueued);
	}

	static void SVCmd_BotWarmupStatus_f()
	{
		int expectedBots = -1;
		int expectedHumans = -1;
		int expectedPlaying = -1;
		int expectedCanStart = -1;

		ParseExpectedInt(2, expectedBots);
		ParseExpectedInt(3, expectedHumans);
		ParseExpectedInt(4, expectedPlaying);
		ParseExpectedInt(5, expectedCanStart);
		BotWarmup_PrintStatus(expectedBots, expectedHumans, expectedPlaying,
			expectedCanStart);
	}

	static void SVCmd_BotVoteStatus_f()
	{
		int expectedBots = -1;
		int expectedHumans = -1;
		int expectedPlaying = -1;
		int expectedVotingClients = -1;
		int expectedActiveVote = -1;
		int expectedLastLaunchBlocked = -1;

		ParseExpectedInt(2, expectedBots);
		ParseExpectedInt(3, expectedHumans);
		ParseExpectedInt(4, expectedPlaying);
		ParseExpectedInt(5, expectedVotingClients);
		ParseExpectedInt(6, expectedActiveVote);
		ParseExpectedInt(7, expectedLastLaunchBlocked);
		BotVote_PrintStatus(expectedBots, expectedHumans, expectedPlaying,
			expectedVotingClients, expectedActiveVote, expectedLastLaunchBlocked);
	}

	static void SVCmd_BotAdminAuditStatus_f()
	{
		int expectedBots = -1;
		int expectedAdminBots = -1;
		int expectedLastBlocked = -1;
		int expectedRedLocked = -1;

		ParseExpectedInt(2, expectedBots);
		ParseExpectedInt(3, expectedAdminBots);
		ParseExpectedInt(4, expectedLastBlocked);
		ParseExpectedInt(5, expectedRedLocked);
		BotAdminAudit_PrintStatus(expectedBots, expectedAdminBots,
			expectedLastBlocked, expectedRedLocked);
	}

	static void SVCmd_BotTournamentStatus_f()
	{
		int expectedBots = -1;
		int expectedActive = -1;
		int expectedVetoStarted = -1;
		int expectedPicks = -1;
		int expectedBans = -1;
		int expectedLastVetoBlocked = -1;

		ParseExpectedInt(2, expectedBots);
		ParseExpectedInt(3, expectedActive);
		ParseExpectedInt(4, expectedVetoStarted);
		ParseExpectedInt(5, expectedPicks);
		ParseExpectedInt(6, expectedBans);
		ParseExpectedInt(7, expectedLastVetoBlocked);
		BotTournament_PrintStatus(expectedBots, expectedActive,
			expectedVetoStarted, expectedPicks, expectedBans,
			expectedLastVetoBlocked);
	}

	static void SVCmd_BotMapVoteStatus_f()
	{
		int expectedBots = -1;
		int expectedActive = -1;
		int expectedCandidates = -1;
		int expectedLastBotVoteBlocked = -1;
		int expectedLastFinalizeSuccess = -1;
		int expectedChangeMapSet = -1;

		ParseExpectedInt(2, expectedBots);
		ParseExpectedInt(3, expectedActive);
		ParseExpectedInt(4, expectedCandidates);
		ParseExpectedInt(5, expectedLastBotVoteBlocked);
		ParseExpectedInt(6, expectedLastFinalizeSuccess);
		ParseExpectedInt(7, expectedChangeMapSet);
		BotMapVote_PrintStatus(expectedBots, expectedActive,
			expectedCandidates, expectedLastBotVoteBlocked,
			expectedLastFinalizeSuccess, expectedChangeMapSet);
	}

	static void SVCmd_BotMyMapStatus_f()
	{
		int expectedBots = -1;
		int expectedPlayQueue = -1;
		int expectedMyMapQueue = -1;
		int expectedLastQueueSuccess = -1;
		int expectedLastConsumeSuccess = -1;

		ParseExpectedInt(2, expectedBots);
		ParseExpectedInt(3, expectedPlayQueue);
		ParseExpectedInt(4, expectedMyMapQueue);
		ParseExpectedInt(5, expectedLastQueueSuccess);
		ParseExpectedInt(6, expectedLastConsumeSuccess);
		BotMyMap_PrintStatus(expectedBots, expectedPlayQueue,
			expectedMyMapQueue, expectedLastQueueSuccess,
			expectedLastConsumeSuccess);
	}

	static void SVCmd_BotIntermissionStatus_f()
	{
		int expectedBots = -1;
		int expectedHumans = -1;
		int expectedPlaying = -1;
		int expectedIntermission = -1;
		int expectedPmFreezeBots = -1;
		int expectedPostIntermission = -1;
		int expectedSortedBots = -1;

		ParseExpectedInt(2, expectedBots);
		ParseExpectedInt(3, expectedHumans);
		ParseExpectedInt(4, expectedPlaying);
		ParseExpectedInt(5, expectedIntermission);
		ParseExpectedInt(6, expectedPmFreezeBots);
		ParseExpectedInt(7, expectedPostIntermission);
		ParseExpectedInt(8, expectedSortedBots);
		BotIntermission_PrintStatus(expectedBots, expectedHumans,
			expectedPlaying, expectedIntermission, expectedPmFreezeBots,
			expectedPostIntermission, expectedSortedBots);
	}

	static void SVCmd_BotNextMapStatus_f()
	{
		int expectedBots = -1;
		int expectedPlayQueue = -1;
		int expectedMyMapQueue = -1;
		int expectedLastTransitionSuccess = -1;
		int expectedLastTransitionConsumed = -1;
		int expectedChangeMapSet = -1;

		ParseExpectedInt(2, expectedBots);
		ParseExpectedInt(3, expectedPlayQueue);
		ParseExpectedInt(4, expectedMyMapQueue);
		ParseExpectedInt(5, expectedLastTransitionSuccess);
		ParseExpectedInt(6, expectedLastTransitionConsumed);
		ParseExpectedInt(7, expectedChangeMapSet);
		BotNextMap_PrintStatus(expectedBots, expectedPlayQueue,
			expectedMyMapQueue, expectedLastTransitionSuccess,
			expectedLastTransitionConsumed, expectedChangeMapSet);
	}

	static void SVCmd_BotScoreboardStatus_f()
	{
		int expectedBots = -1;
		int expectedHumans = -1;
		int expectedPlaying = -1;
		int expectedSortedBots = -1;
		int expectedLeaderBot = -1;
		int expectedTopScore = -1;
		int expectedSecondScore = -1;

		ParseExpectedInt(2, expectedBots);
		ParseExpectedInt(3, expectedHumans);
		ParseExpectedInt(4, expectedPlaying);
		ParseExpectedInt(5, expectedSortedBots);
		ParseExpectedInt(6, expectedLeaderBot);
		ParseExpectedInt(7, expectedTopScore);
		ParseExpectedInt(8, expectedSecondScore);
		BotScoreboard_PrintStatus(expectedBots, expectedHumans,
			expectedPlaying, expectedSortedBots, expectedLeaderBot,
			expectedTopScore, expectedSecondScore);
	}

	static void SVCmd_BotChatPolicyStatus_f()
	{
		int expectedBots = -1;
		int expectedProfileChat = -1;
		int expectedAllowChat = -1;
		int expectedDispatchEnabled = -1;

		ParseExpectedInt(2, expectedBots);
		ParseExpectedInt(3, expectedProfileChat);
		ParseExpectedInt(4, expectedAllowChat);
		ParseExpectedInt(5, expectedDispatchEnabled);
		BotChatPolicy_PrintStatus(expectedBots, expectedProfileChat,
			expectedAllowChat, expectedDispatchEnabled);
	}
} // anonymous namespace

void BotTeamPolicy_PrintStatus(int expectedPlaying, int expectedSpectators,
	int expectedBots, int expectedQueued)
{
	const BotTeamPolicyStatus status = CountBotTeamPolicyStatus();
	bool pass = true;

	if (expectedPlaying >= 0 && status.playing != expectedPlaying)
		pass = false;
	if (expectedSpectators >= 0 && status.spectators != expectedSpectators)
		pass = false;
	if (expectedBots >= 0 && status.bots != expectedBots)
		pass = false;
	if (expectedQueued >= 0 && status.queued != expectedQueued)
		pass = false;

	base_import.Com_Print(G_Fmt(
		"q3a_bot_team_policy_status bots={} playing={} spectators={} queued={} "
		"none={} free={} red={} blue={} expected_playing={} "
		"expected_spectators={} expected_bots={} expected_queued={} pass={}\n",
		status.bots, status.playing, status.spectators, status.queued,
		status.none, status.free, status.red, status.blue, expectedPlaying,
		expectedSpectators, expectedBots, expectedQueued, pass ? 1 : 0).data());
}

void BotWarmup_PrintStatus(int expectedBots, int expectedHumans,
	int expectedPlaying, int expectedCanStart)
{
	const BotWarmupStatus status = CountBotWarmupStatus();
	bool pass = true;

	if (expectedBots >= 0 && status.bots != expectedBots)
		pass = false;
	if (expectedHumans >= 0 && status.humans != expectedHumans)
		pass = false;
	if (expectedPlaying >= 0 && status.playing != expectedPlaying)
		pass = false;
	if (expectedCanStart >= 0 && status.canStart != expectedCanStart)
		pass = false;

	base_import.Com_Print(G_Fmt(
		"q3a_bot_warmup_status bots={} humans={} playing={} bot_playing={} "
		"human_playing={} spectators={} ready_humans={} ready_bots={} "
		"minplayers={} minplayers_met={} warmup_enabled={} ready_up={} "
		"start_no_humans={} bot_only_start={} no_players_ready={} "
		"ready_percentage={} required_ready_percentage={} can_start={} "
		"match_state={} match_state_name={} warmup_state={} "
		"warmup_state_name={} expected_bots={} expected_humans={} "
		"expected_playing={} expected_can_start={} pass={}\n",
		status.bots, status.humans, status.playing, status.botPlaying,
		status.humanPlaying, status.spectators, status.readyHumans,
		status.readyBots, status.minplayersValue, status.minplayersMet,
		status.warmupEnabled, status.readyUp, status.startNoHumans,
		status.botOnlyStart, status.noPlayersReady, status.readyPercentage,
		status.requiredReadyPercentage, status.canStart, status.matchState,
		status.matchStateName, status.warmupState, status.warmupStateName,
		expectedBots, expectedHumans, expectedPlaying, expectedCanStart,
		pass ? 1 : 0).data());
}

void BotChatPolicy_PrintStatus(int expectedBots, int expectedProfileChat,
	int expectedAllowChat, int expectedDispatchEnabled)
{
	const BotChatPolicyStatus status = CountBotChatPolicyStatus();
	bool pass = true;

	if (expectedBots >= 0 && status.bots != expectedBots)
		pass = false;
	if (expectedProfileChat >= 0 && status.profileChat != expectedProfileChat)
		pass = false;
	if (expectedAllowChat >= 0 && status.allowChat != expectedAllowChat)
		pass = false;
	if (expectedDispatchEnabled >= 0 &&
		status.dispatchEnabled != expectedDispatchEnabled)
		pass = false;

	base_import.Com_Print(G_Fmt(
		"q3a_bot_chat_policy_status bots={} humans={} playing={} "
		"bot_playing={} profile_chat_metadata={} allow_chat={} "
		"team_only={} consumer_ready={} dispatch_enabled={} dispatch_attempts={} "
		"dispatch_submitted={} dispatch_failures={} dispatch_rate_limited={} "
		"rate_limit_ms={} last_dispatch_time_ms={} "
		"last_dispatch_client={} last_dispatch_team={} "
		"initial_chat_selections={} initial_chat_known_personalities={} "
		"initial_chat_unknown_personalities={} initial_chat_quiet={} "
		"initial_chat_direct={} initial_chat_taunting={} "
		"initial_chat_helpful={} initial_chat_steady={} "
		"initial_chat_phrase_variants={} "
		"initial_chat_unique_variants={} "
		"last_initial_chat_client={} last_initial_chat_personality={} "
		"last_initial_chat_phrase={} last_initial_chat_variant={} "
		"reply_chat_enabled={} reply_chat_events={} "
		"reply_chat_selections={} reply_chat_known_personalities={} "
		"reply_chat_unknown_personalities={} reply_chat_team_ready={} "
		"reply_chat_route_ready={} "
		"reply_chat_item_taken={} "
		"reply_chat_item_denied={} "
		"reply_chat_objective_changed={} "
		"reply_chat_flag_state={} "
		"reply_chat_enemy_sighted={} "
		"reply_chat_low_health={} "
		"reply_chat_blocked={} "
		"reply_chat_match_result={} "
		"reply_chat_match_result_unknown={} "
		"reply_chat_match_result_win={} "
		"reply_chat_match_result_loss={} "
		"reply_chat_match_result_tie={} "
		"reply_chat_match_result_abort={} "
		"reply_chat_submitted={} reply_chat_rate_limited={} "
		"reply_chat_duplicate_suppressed={} reply_chat_failures={} "
		"reply_chat_phrase_variants={} reply_chat_unique_variants={} "
		"last_reply_chat_client={} "
		"last_reply_chat_personality={} last_reply_chat_phrase={} "
		"last_reply_chat_variant={} last_reply_chat_event={} "
		"last_match_result_outcome={} last_match_result_outcome_name={} "
		"live_chat_enabled={} live_chat_events={} "
		"live_chat_spawn={} live_chat_route_ready={} "
		"live_chat_item_taken={} "
		"live_chat_item_denied={} "
		"live_chat_objective_changed={} "
		"live_chat_flag_state={} "
		"live_chat_enemy_sighted={} "
		"live_chat_low_health={} "
		"live_chat_blocked={} "
		"live_chat_match_result={} "
		"live_chat_submitted={} "
		"live_chat_rate_limited={} live_chat_duplicate_suppressed={} "
		"live_chat_failures={} "
		"live_chat_event_taxonomy={} chat_duplicate_window_ms={} "
		"last_duplicate_chat_client={} last_duplicate_chat_event={} "
		"last_duplicate_chat_event_name={} last_duplicate_chat_phrase={} "
		"last_duplicate_chat_elapsed_ms={} last_live_chat_event={} "
		"last_live_chat_event_name={} "
		"blocked_until_consumer={} expected_bots={} "
		"expected_profile_chat={} expected_allow_chat={} "
		"expected_dispatch_enabled={} pass={}\n",
		status.bots, status.humans, status.playing, status.botPlaying,
		status.profileChat, status.allowChat, status.teamOnly,
		status.consumerReady, status.dispatchEnabled, status.dispatchAttempts,
		status.dispatchSubmitted, status.dispatchFailures,
		status.dispatchRateLimited, status.rateLimitMs,
		status.lastDispatchTimeMs,
		status.lastDispatchClient, status.lastDispatchTeam,
		status.initialSelections, status.initialKnownPersonalities,
		status.initialUnknownPersonalities, status.initialQuiet,
		status.initialDirect, status.initialTaunting,
		status.initialHelpful, status.initialSteady,
		status.initialPhraseVariants, status.initialUniquePhraseVariants,
		status.lastInitialClient, status.lastInitialPersonality,
		status.lastInitialPhrase, status.lastInitialPhraseVariant,
		status.replyEnabled, status.replyEvents,
		status.replySelections, status.replyKnownPersonalities,
		status.replyUnknownPersonalities, status.replyTeamReady,
		status.replyRouteReady, status.replyItemTaken,
		status.replyItemDenied,
		status.replyObjectiveChanged, status.replyFlagState,
		status.replyEnemySighted,
		status.replyLowHealth,
		status.replyBlocked,
		status.replyMatchResult,
		status.replyMatchResultUnknown, status.replyMatchResultWin,
		status.replyMatchResultLoss, status.replyMatchResultTie,
		status.replyMatchResultAbort,
		status.replySubmitted, status.replyRateLimited,
		status.replyDuplicateSuppressed, status.replyFailures,
		status.replyPhraseVariants,
		status.replyUniquePhraseVariants, status.lastReplyClient,
		status.lastReplyPersonality, status.lastReplyPhrase,
		status.lastReplyPhraseVariant, status.lastReplyEvent,
		status.lastMatchResultOutcome, status.lastMatchResultOutcomeName,
		status.liveEnabled, status.liveEvents, status.liveSpawn,
		status.liveRouteReady, status.liveItemTaken,
		status.liveItemDenied,
		status.liveObjectiveChanged, status.liveFlagState,
		status.liveEnemySighted,
		status.liveLowHealth,
		status.liveBlocked,
		status.liveMatchResult,
		status.liveSubmitted,
		status.liveRateLimited, status.liveDuplicateSuppressed,
		status.liveFailures,
		status.liveEventTaxonomy, status.duplicateWindowMs,
		status.lastDuplicateClient, status.lastDuplicateEvent,
		status.lastDuplicateEventName, status.lastDuplicatePhrase,
		status.lastDuplicateElapsedMs, status.lastLiveEvent,
		status.lastLiveEventName,
		status.blockedUntilConsumer, expectedBots, expectedProfileChat,
		expectedAllowChat, expectedDispatchEnabled, pass ? 1 : 0).data());
}

void BotVote_ResetStatus()
{
	botVoteLastLaunch = {};
}

void BotAdminAudit_ResetStatus()
{
	botAdminAuditLastAttempt = {};
	level.locked[static_cast<size_t>(Team::Red)] = false;
	level.locked[static_cast<size_t>(Team::Blue)] = false;
}

int BotAdminAudit_TryFirstBotAdminCommand()
{
	botAdminAuditLastAttempt = {};
	botAdminAuditLastAttempt.attempted = 1;
	botAdminAuditLastAttempt.command = "lock_team";
	botAdminAuditLastAttempt.redLockedBefore =
		level.locked[static_cast<size_t>(Team::Red)] ? 1 : 0;

	gentity_t* bot = FindFirstPlayingBotClient();
	if (!bot || !bot->client) {
		botAdminAuditLastAttempt.reason = "no_playing_bot";
		botAdminAuditLastAttempt.redLockedAfter =
			level.locked[static_cast<size_t>(Team::Red)] ? 1 : 0;
		const BotAdminAuditStatus status = CountBotAdminAuditStatus();
		base_import.Com_Print(G_Fmt(
			"q3a_bot_admin_audit_attempt attempted={} bot_found={} "
			"client={} forced_admin={} admin_session={} command={} "
			"command_found={} admin_only={} allowed={} executed={} "
			"blocked={} reason={} red_locked_before={} red_locked_after={} "
			"admin_bots={}\n",
			botAdminAuditLastAttempt.attempted,
			botAdminAuditLastAttempt.botFound,
			botAdminAuditLastAttempt.client,
			botAdminAuditLastAttempt.forcedAdmin,
			botAdminAuditLastAttempt.adminSession,
			MarkerString(botAdminAuditLastAttempt.command),
			botAdminAuditLastAttempt.commandFound,
			botAdminAuditLastAttempt.adminOnly,
			botAdminAuditLastAttempt.allowed,
			botAdminAuditLastAttempt.executed,
			botAdminAuditLastAttempt.blocked,
			MarkerString(botAdminAuditLastAttempt.reason),
			botAdminAuditLastAttempt.redLockedBefore,
			botAdminAuditLastAttempt.redLockedAfter, status.adminBots).data());
		return 0;
	}

	botAdminAuditLastAttempt.botFound = 1;
	botAdminAuditLastAttempt.client = static_cast<int>(bot - g_entities) - 1;

	const bool wasAdmin = bot->client->sess.admin;
	bot->client->sess.admin = true;
	botAdminAuditLastAttempt.forcedAdmin = 1;

	CommandArgs args{ "lock_team", "red" };
	const CommandAuditResult result =
		Commands::AuditRegisteredCommand(bot, args, true);

	botAdminAuditLastAttempt.adminSession = result.adminSession ? 1 : 0;
	botAdminAuditLastAttempt.commandFound = result.commandFound ? 1 : 0;
	botAdminAuditLastAttempt.adminOnly = result.adminOnly ? 1 : 0;
	botAdminAuditLastAttempt.allowed = result.allowed ? 1 : 0;
	botAdminAuditLastAttempt.executed = result.executed ? 1 : 0;
	botAdminAuditLastAttempt.reason = result.reason.empty() ?
		"unknown" : result.reason;
	botAdminAuditLastAttempt.blocked =
		(result.commandFound && result.adminOnly && !result.allowed &&
		 result.reason == "bot_admin_blocked") ? 1 : 0;
	bot->client->sess.admin = wasAdmin;
	botAdminAuditLastAttempt.redLockedAfter =
		level.locked[static_cast<size_t>(Team::Red)] ? 1 : 0;

	const BotAdminAuditStatus status = CountBotAdminAuditStatus();
	base_import.Com_Print(G_Fmt(
		"q3a_bot_admin_audit_attempt attempted={} bot_found={} client={} "
		"forced_admin={} admin_session={} command={} command_found={} "
		"admin_only={} allowed={} executed={} blocked={} reason={} "
		"red_locked_before={} red_locked_after={} admin_bots={}\n",
		botAdminAuditLastAttempt.attempted,
		botAdminAuditLastAttempt.botFound,
		botAdminAuditLastAttempt.client,
		botAdminAuditLastAttempt.forcedAdmin,
		botAdminAuditLastAttempt.adminSession,
		MarkerString(botAdminAuditLastAttempt.command),
		botAdminAuditLastAttempt.commandFound,
		botAdminAuditLastAttempt.adminOnly, botAdminAuditLastAttempt.allowed,
		botAdminAuditLastAttempt.executed, botAdminAuditLastAttempt.blocked,
		MarkerString(botAdminAuditLastAttempt.reason),
		botAdminAuditLastAttempt.redLockedBefore,
		botAdminAuditLastAttempt.redLockedAfter, status.adminBots).data());
	return botAdminAuditLastAttempt.blocked;
}

void BotAdminAudit_PrintStatus(int expectedBots, int expectedAdminBots,
	int expectedLastBlocked, int expectedRedLocked)
{
	const BotAdminAuditStatus status = CountBotAdminAuditStatus();
	bool pass = true;

	if (expectedBots >= 0 && status.bots != expectedBots)
		pass = false;
	if (expectedAdminBots >= 0 && status.adminBots != expectedAdminBots)
		pass = false;
	if (expectedLastBlocked >= 0 &&
		status.lastAttempt.blocked != expectedLastBlocked)
		pass = false;
	if (expectedRedLocked >= 0 && status.redLocked != expectedRedLocked)
		pass = false;

	base_import.Com_Print(G_Fmt(
		"q3a_bot_admin_audit_status bots={} humans={} playing={} "
		"bot_playing={} human_playing={} spectators={} admin_bots={} "
		"admin_humans={} red_locked={} blue_locked={} allow_admin={} "
		"last_attempted={} last_bot_found={} last_client={} "
		"last_forced_admin={} last_admin_session={} last_command={} "
		"last_command_found={} last_admin_only={} last_allowed={} "
		"last_executed={} last_blocked={} last_reason={} "
		"last_red_locked_before={} last_red_locked_after={} "
		"expected_bots={} expected_admin_bots={} "
		"expected_last_blocked={} expected_red_locked={} pass={}\n",
		status.bots, status.humans, status.playing, status.botPlaying,
		status.humanPlaying, status.spectators, status.adminBots,
		status.adminHumans, status.redLocked, status.blueLocked,
		status.allowAdmin, status.lastAttempt.attempted,
		status.lastAttempt.botFound, status.lastAttempt.client,
		status.lastAttempt.forcedAdmin, status.lastAttempt.adminSession,
		MarkerString(status.lastAttempt.command),
		status.lastAttempt.commandFound, status.lastAttempt.adminOnly,
		status.lastAttempt.allowed, status.lastAttempt.executed,
		status.lastAttempt.blocked, MarkerString(status.lastAttempt.reason),
		status.lastAttempt.redLockedBefore,
		status.lastAttempt.redLockedAfter, expectedBots, expectedAdminBots,
		expectedLastBlocked, expectedRedLocked, pass ? 1 : 0).data());
}

static const char* BotTournamentVetoReason(const std::string& message,
	bool allowed)
{
	if (allowed)
		return "allowed";
	if (message == "Only the active side may perform this veto.")
		return "bot_blocked";
	if (message.empty())
		return "rejected_no_message";
	return "rejected";
}

static const char* BotTournamentReplayReason(const std::string& message,
	bool success)
{
	if (success)
		return "queued_replay";
	if (message == "Tournament mode is not active.")
		return "inactive";
	if (message == "Replay game number must be at least 1.")
		return "min_game";
	if (message == "Tournament map order is not locked yet.")
		return "missing_order";
	if (message.rfind("Replay game must be between", 0) == 0)
		return "range_error";
	if (message == "Tournament state is not active.")
		return "state_inactive";
	if (message == "Replay map is missing.")
		return "missing_map";
	if (message.empty())
		return "rejected_no_message";
	return "rejected";
}

void BotTournament_ResetStatus()
{
	botTournamentLastSetup = {};
	botTournamentLastVeto = {};
	botTournamentLastReplaySetup = {};
	botTournamentLastReplay = {};
	game.tournament = {};
	game.tournament.bestOf = 1;
	game.tournament.winTarget = 1;
	level.changeMap.clear();
	level.intermission.exit = false;
	if (match_setup_type)
		gi.cvarForceSet("match_setup_type", "standard");
}

int BotTournament_SetupReplayState()
{
	const std::string currentMap =
		std::string(CharArrayToStringView(level.mapName));
	const std::string map0 = currentMap.empty() ? "mm-rage" : currentMap;
	const std::string map1 = map0;
	const std::string map2 = map0 + "_decider";
	const char* homeId = "bot-tournament-home";
	const char* awayId = "human-tournament-away";

	botTournamentLastSetup = {};
	botTournamentLastVeto = {};
	botTournamentLastReplaySetup = {};
	botTournamentLastReplay = {};
	botTournamentLastReplaySetup.attempted = 1;
	botTournamentLastReplaySetup.replayMap = map1;

	if (match_setup_type)
		gi.cvarForceSet("match_setup_type", "tournament");
	if (match_setup_bestof)
		gi.cvarForceSet("match_setup_bestof", "bo3");

	game.tournament = {};
	game.tournament.configLoaded = true;
	game.tournament.configValid = true;
	game.tournament.active = true;
	game.tournament.seriesComplete = true;
	game.tournament.bestOf = 3;
	game.tournament.winTarget = 2;
	game.tournament.teamBased = false;
	game.tournament.gametype = GameType::FreeForAll;
	game.tournament.name = "Bot tournament replay smoke";
	game.tournament.seriesId = "bot_tournament_replay_smoke";
	game.tournament.homeId = homeId;
	game.tournament.awayId = awayId;
	game.tournament.mapPool = { map0, map1, map2 };
	game.tournament.mapPicks = { map0, map1 };
	game.tournament.mapOrder = { map0, map1, map2 };
	game.tournament.matchIds = {
		"bot-replay-game-1", "bot-replay-game-2", "bot-replay-game-3" };
	game.tournament.matchMaps = game.tournament.mapOrder;
	game.tournament.matchWinners = { homeId, awayId, homeId };
	game.tournament.playerIds[0] = homeId;
	game.tournament.playerIds[1] = awayId;
	game.tournament.playerNames[0] = "Bot Tournament Home";
	game.tournament.playerNames[1] = "Human Tournament Away";
	game.tournament.playerWins[0] = 2;
	game.tournament.playerWins[1] = 1;
	game.tournament.gamesPlayed = 3;
	game.tournament.vetoStarted = true;
	game.tournament.vetoComplete = true;
	game.tournament.vetoIndex = 2;
	game.tournament.vetoHomeTurn = true;
	level.matchState = MatchState::Warmup_ReadyUp;
	level.changeMap.clear();
	level.intermission.exit = false;

	botTournamentLastReplaySetup.configured =
		(Tournament_IsActive() && game.tournament.active) ? 1 : 0;
	botTournamentLastReplaySetup.active =
		game.tournament.active ? 1 : 0;
	botTournamentLastReplaySetup.order =
		static_cast<int>(game.tournament.mapOrder.size());
	botTournamentLastReplaySetup.history =
		static_cast<int>(game.tournament.matchWinners.size());
	botTournamentLastReplaySetup.gamesPlayed =
		game.tournament.gamesPlayed;
	botTournamentLastReplaySetup.player0Wins =
		game.tournament.playerWins[0];
	botTournamentLastReplaySetup.player1Wins =
		game.tournament.playerWins[1];
	botTournamentLastReplaySetup.seriesComplete =
		game.tournament.seriesComplete ? 1 : 0;
	botTournamentLastReplaySetup.bestOf = game.tournament.bestOf;
	botTournamentLastReplaySetup.winTarget = game.tournament.winTarget;

	base_import.Com_Print(G_Fmt(
		"q3a_bot_tournament_replay_setup attempted={} configured={} "
		"active={} order={} history={} games_played={} "
		"player0_wins={} player1_wins={} series_complete={} "
		"replay_map={} best_of={} win_target={}\n",
		botTournamentLastReplaySetup.attempted,
		botTournamentLastReplaySetup.configured,
		botTournamentLastReplaySetup.active,
		botTournamentLastReplaySetup.order,
		botTournamentLastReplaySetup.history,
		botTournamentLastReplaySetup.gamesPlayed,
		botTournamentLastReplaySetup.player0Wins,
		botTournamentLastReplaySetup.player1Wins,
		botTournamentLastReplaySetup.seriesComplete,
		MarkerString(botTournamentLastReplaySetup.replayMap),
		botTournamentLastReplaySetup.bestOf,
		botTournamentLastReplaySetup.winTarget).data());

	return botTournamentLastReplaySetup.configured &&
		botTournamentLastReplaySetup.order == 3 &&
		botTournamentLastReplaySetup.history == 3 &&
		botTournamentLastReplaySetup.seriesComplete == 1;
}

int BotTournament_TryReplayGame(int gameNumber)
{
	botTournamentLastReplay = {};
	botTournamentLastReplay.attempted = 1;
	botTournamentLastReplay.gameNumber = gameNumber;
	botTournamentLastReplay.activeBefore = Tournament_IsActive() ? 1 : 0;
	botTournamentLastReplay.gamesBefore = game.tournament.gamesPlayed;
	botTournamentLastReplay.winnersBefore =
		static_cast<int>(game.tournament.matchWinners.size());
	botTournamentLastReplay.idsBefore =
		static_cast<int>(game.tournament.matchIds.size());
	botTournamentLastReplay.mapsBefore =
		static_cast<int>(game.tournament.matchMaps.size());
	botTournamentLastReplay.player0WinsBefore =
		game.tournament.playerWins[0];
	botTournamentLastReplay.player1WinsBefore =
		game.tournament.playerWins[1];
	botTournamentLastReplay.seriesCompleteBefore =
		game.tournament.seriesComplete ? 1 : 0;
	botTournamentLastReplay.changeMapBefore =
		level.changeMap.empty() ? 0 : 1;

	if (gameNumber > 0 &&
		static_cast<size_t>(gameNumber - 1) <
			game.tournament.mapOrder.size()) {
		botTournamentLastReplay.targetMap =
			game.tournament.mapOrder[static_cast<size_t>(gameNumber - 1)];
	}

	std::string message;
	const bool success = Tournament_ReplayGame(gameNumber, message);
	botTournamentLastReplay.success = success ? 1 : 0;
	botTournamentLastReplay.rejected = success ? 0 : 1;
	botTournamentLastReplay.reason =
		BotTournamentReplayReason(message, success);
	botTournamentLastReplay.gamesAfter = game.tournament.gamesPlayed;
	botTournamentLastReplay.winnersAfter =
		static_cast<int>(game.tournament.matchWinners.size());
	botTournamentLastReplay.idsAfter =
		static_cast<int>(game.tournament.matchIds.size());
	botTournamentLastReplay.mapsAfter =
		static_cast<int>(game.tournament.matchMaps.size());
	botTournamentLastReplay.player0WinsAfter =
		game.tournament.playerWins[0];
	botTournamentLastReplay.player1WinsAfter =
		game.tournament.playerWins[1];
	botTournamentLastReplay.seriesCompleteAfter =
		game.tournament.seriesComplete ? 1 : 0;
	botTournamentLastReplay.changeMapAfter =
		level.changeMap.empty() ? 0 : 1;

	botTournamentLastReplay.preserved =
		(!success &&
		 botTournamentLastReplay.gamesAfter ==
			botTournamentLastReplay.gamesBefore &&
		 botTournamentLastReplay.winnersAfter ==
			botTournamentLastReplay.winnersBefore &&
		 botTournamentLastReplay.idsAfter ==
			botTournamentLastReplay.idsBefore &&
		 botTournamentLastReplay.mapsAfter ==
			botTournamentLastReplay.mapsBefore &&
		 botTournamentLastReplay.player0WinsAfter ==
			botTournamentLastReplay.player0WinsBefore &&
		 botTournamentLastReplay.player1WinsAfter ==
			botTournamentLastReplay.player1WinsBefore &&
		 botTournamentLastReplay.seriesCompleteAfter ==
			botTournamentLastReplay.seriesCompleteBefore &&
		 botTournamentLastReplay.changeMapAfter ==
			botTournamentLastReplay.changeMapBefore) ? 1 : 0;

	const int expectedGamesAfter =
		gameNumber > 0 ? std::max(0, gameNumber - 1) : 0;
	botTournamentLastReplay.resetApplied =
		(success &&
		 botTournamentLastReplay.gamesAfter == expectedGamesAfter &&
		 botTournamentLastReplay.winnersAfter == expectedGamesAfter &&
		 botTournamentLastReplay.idsAfter == expectedGamesAfter &&
		 botTournamentLastReplay.mapsAfter == expectedGamesAfter &&
		 botTournamentLastReplay.seriesCompleteAfter == 0 &&
		 botTournamentLastReplay.player0WinsAfter == 1 &&
		 botTournamentLastReplay.player1WinsAfter == 0 &&
		 !botTournamentLastReplay.targetMap.empty() &&
		 botTournamentLastReplay.targetMap != "none") ? 1 : 0;

	base_import.Com_Print(G_Fmt(
		"q3a_bot_tournament_replay attempted={} game={} "
		"active_before={} success={} rejected={} reason={} "
		"target_map={} games_before={} games_after={} "
		"winners_before={} winners_after={} ids_before={} ids_after={} "
		"maps_before={} maps_after={} player0_wins_before={} "
		"player0_wins_after={} player1_wins_before={} "
		"player1_wins_after={} series_complete_before={} "
		"series_complete_after={} change_map_before={} "
		"change_map_after={} preserved={} reset_applied={}\n",
		botTournamentLastReplay.attempted,
		botTournamentLastReplay.gameNumber,
		botTournamentLastReplay.activeBefore,
		botTournamentLastReplay.success,
		botTournamentLastReplay.rejected,
		MarkerString(botTournamentLastReplay.reason),
		MarkerString(botTournamentLastReplay.targetMap),
		botTournamentLastReplay.gamesBefore,
		botTournamentLastReplay.gamesAfter,
		botTournamentLastReplay.winnersBefore,
		botTournamentLastReplay.winnersAfter,
		botTournamentLastReplay.idsBefore,
		botTournamentLastReplay.idsAfter,
		botTournamentLastReplay.mapsBefore,
		botTournamentLastReplay.mapsAfter,
		botTournamentLastReplay.player0WinsBefore,
		botTournamentLastReplay.player0WinsAfter,
		botTournamentLastReplay.player1WinsBefore,
		botTournamentLastReplay.player1WinsAfter,
		botTournamentLastReplay.seriesCompleteBefore,
		botTournamentLastReplay.seriesCompleteAfter,
		botTournamentLastReplay.changeMapBefore,
		botTournamentLastReplay.changeMapAfter,
		botTournamentLastReplay.preserved,
		botTournamentLastReplay.resetApplied).data());

	return success ? botTournamentLastReplay.resetApplied
		: botTournamentLastReplay.preserved;
}

int BotTournament_SetupBotVetoState()
{
	gentity_t* bot = FindFirstPlayingBotClient();
	const std::string currentMap =
		std::string(CharArrayToStringView(level.mapName));
	const std::string map0 = currentMap.empty() ? "mm-rage" : currentMap;
	const std::string map1 = map0 + "_alt1";
	const std::string map2 = map0 + "_alt2";
	const char* botSocialID = "bot-tournament-home";

	botTournamentLastSetup = {};
	botTournamentLastVeto = {};
	botTournamentLastReplaySetup = {};
	botTournamentLastReplay = {};
	botTournamentLastSetup.attempted = 1;
	botTournamentLastSetup.map0 = map0;

	if (!bot || !bot->client) {
		botTournamentLastSetup.configured = 0;
		base_import.Com_Print(G_Fmt(
			"q3a_bot_tournament_setup attempted={} bot_found={} client={} "
			"configured={} active={} veto_started={} bot_is_home={} "
			"bot_social={} map0={} pool={} best_of={} picks_needed={}\n",
			botTournamentLastSetup.attempted,
			botTournamentLastSetup.botFound, botTournamentLastSetup.client,
			botTournamentLastSetup.configured, botTournamentLastSetup.active,
			botTournamentLastSetup.vetoStarted,
			botTournamentLastSetup.botIsHome,
			MarkerString(botTournamentLastSetup.botSocialID),
			MarkerString(botTournamentLastSetup.map0),
			botTournamentLastSetup.pool, botTournamentLastSetup.bestOf,
			botTournamentLastSetup.picksNeeded).data());
		return 0;
	}

	Q_strlcpy(bot->client->sess.socialID, botSocialID,
		sizeof(bot->client->sess.socialID));

	if (match_setup_type)
		gi.cvarForceSet("match_setup_type", "tournament");

	game.tournament = {};
	game.tournament.configLoaded = true;
	game.tournament.configValid = true;
	game.tournament.active = true;
	game.tournament.seriesComplete = false;
	game.tournament.bestOf = 3;
	game.tournament.winTarget = 2;
	game.tournament.teamBased = false;
	game.tournament.gametype = GameType::FreeForAll;
	game.tournament.name = "Bot tournament veto smoke";
	game.tournament.seriesId = "bot_tournament_veto_smoke";
	game.tournament.homeId = botSocialID;
	game.tournament.awayId = "human-tournament-away";
	game.tournament.mapPool = { map0, map1, map2 };
	game.tournament.participants.push_back(TournamentParticipant{
		botSocialID, "Bot Tournament Home", Team::Free, Team::Free, true });
	game.tournament.participants.push_back(TournamentParticipant{
		"human-tournament-away", "Human Tournament Away", Team::Free,
		Team::Free, true });
	game.tournament.vetoStarted = true;
	game.tournament.vetoComplete = false;
	game.tournament.vetoIndex = 0;
	game.tournament.vetoHomeTurn = true;
	level.matchState = MatchState::Warmup_ReadyUp;
	level.changeMap.clear();

	botTournamentLastSetup.botFound = 1;
	botTournamentLastSetup.client = static_cast<int>(bot - g_entities) - 1;
	botTournamentLastSetup.configured = Tournament_IsActive() ? 1 : 0;
	botTournamentLastSetup.active = botTournamentLastSetup.configured;
	botTournamentLastSetup.vetoStarted =
		game.tournament.vetoStarted ? 1 : 0;
	botTournamentLastSetup.botIsHome =
		game.tournament.homeId == bot->client->sess.socialID ? 1 : 0;
	botTournamentLastSetup.pool =
		static_cast<int>(game.tournament.mapPool.size());
	botTournamentLastSetup.bestOf = game.tournament.bestOf;
	botTournamentLastSetup.picksNeeded =
		std::max(0, game.tournament.bestOf - 1);
	botTournamentLastSetup.botSocialID = bot->client->sess.socialID;

	base_import.Com_Print(G_Fmt(
		"q3a_bot_tournament_setup attempted={} bot_found={} client={} "
		"configured={} active={} veto_started={} bot_is_home={} "
		"bot_social={} map0={} pool={} best_of={} picks_needed={}\n",
		botTournamentLastSetup.attempted, botTournamentLastSetup.botFound,
		botTournamentLastSetup.client, botTournamentLastSetup.configured,
		botTournamentLastSetup.active, botTournamentLastSetup.vetoStarted,
		botTournamentLastSetup.botIsHome,
		MarkerString(botTournamentLastSetup.botSocialID),
		MarkerString(botTournamentLastSetup.map0),
		botTournamentLastSetup.pool, botTournamentLastSetup.bestOf,
		botTournamentLastSetup.picksNeeded).data());

	return botTournamentLastSetup.configured &&
		botTournamentLastSetup.botIsHome;
}

int BotTournament_TryFirstBotVetoPick(const char* mapName)
{
	gentity_t* bot = FindFirstPlayingBotClient();
	const std::string effectiveMap =
		(mapName && *mapName)
			? std::string(mapName)
			: (game.tournament.mapPool.empty()
				? std::string()
				: game.tournament.mapPool.front());

	botTournamentLastVeto = {};
	botTournamentLastVeto.attempted = 1;
	botTournamentLastVeto.map = effectiveMap.empty() ? "none" : effectiveMap;
	botTournamentLastVeto.activeBefore = Tournament_IsActive() ? 1 : 0;
	botTournamentLastVeto.vetoStartedBefore =
		game.tournament.vetoStarted ? 1 : 0;
	botTournamentLastVeto.vetoCompleteBefore =
		game.tournament.vetoComplete ? 1 : 0;
	botTournamentLastVeto.picksBefore =
		static_cast<int>(game.tournament.mapPicks.size());
	botTournamentLastVeto.bansBefore =
		static_cast<int>(game.tournament.mapBans.size());

	if (!bot || !bot->client) {
		botTournamentLastVeto.message = "no_playing_bot";
		base_import.Com_Print(G_Fmt(
			"q3a_bot_tournament_veto attempted={} bot_found={} client={} "
			"map={} active_before={} veto_started_before={} "
			"veto_complete_before={} picks_before={} bans_before={} "
			"allowed={} blocked={} reason={} picks_after={} bans_after={} "
			"veto_complete_after={}\n",
			botTournamentLastVeto.attempted,
			botTournamentLastVeto.botFound, botTournamentLastVeto.client,
			MarkerString(botTournamentLastVeto.map),
			botTournamentLastVeto.activeBefore,
			botTournamentLastVeto.vetoStartedBefore,
			botTournamentLastVeto.vetoCompleteBefore,
			botTournamentLastVeto.picksBefore,
			botTournamentLastVeto.bansBefore,
			botTournamentLastVeto.allowed,
			botTournamentLastVeto.blocked,
			MarkerString(botTournamentLastVeto.message),
			botTournamentLastVeto.picksAfter,
			botTournamentLastVeto.bansAfter,
			botTournamentLastVeto.vetoCompleteAfter).data());
		return 0;
	}

	botTournamentLastVeto.botFound = 1;
	botTournamentLastVeto.client = static_cast<int>(bot - g_entities) - 1;

	std::string message;
	const bool allowed = Tournament_HandleVetoAction(
		bot, TournamentVetoAction::Pick, effectiveMap, message);
	botTournamentLastVeto.allowed = allowed ? 1 : 0;
	botTournamentLastVeto.picksAfter =
		static_cast<int>(game.tournament.mapPicks.size());
	botTournamentLastVeto.bansAfter =
		static_cast<int>(game.tournament.mapBans.size());
	botTournamentLastVeto.vetoCompleteAfter =
		game.tournament.vetoComplete ? 1 : 0;
	botTournamentLastVeto.message =
		BotTournamentVetoReason(message, allowed);
	botTournamentLastVeto.blocked =
		(!allowed && botTournamentLastVeto.message == "bot_blocked" &&
		 botTournamentLastVeto.picksAfter ==
			botTournamentLastVeto.picksBefore &&
		 botTournamentLastVeto.bansAfter ==
			botTournamentLastVeto.bansBefore) ? 1 : 0;

	base_import.Com_Print(G_Fmt(
		"q3a_bot_tournament_veto attempted={} bot_found={} client={} "
		"map={} active_before={} veto_started_before={} "
		"veto_complete_before={} picks_before={} bans_before={} "
		"allowed={} blocked={} reason={} picks_after={} bans_after={} "
		"veto_complete_after={}\n",
		botTournamentLastVeto.attempted, botTournamentLastVeto.botFound,
		botTournamentLastVeto.client,
		MarkerString(botTournamentLastVeto.map),
		botTournamentLastVeto.activeBefore,
		botTournamentLastVeto.vetoStartedBefore,
		botTournamentLastVeto.vetoCompleteBefore,
		botTournamentLastVeto.picksBefore,
		botTournamentLastVeto.bansBefore, botTournamentLastVeto.allowed,
		botTournamentLastVeto.blocked,
		MarkerString(botTournamentLastVeto.message),
		botTournamentLastVeto.picksAfter,
		botTournamentLastVeto.bansAfter,
		botTournamentLastVeto.vetoCompleteAfter).data());

	return botTournamentLastVeto.blocked;
}

void BotTournament_PrintStatus(int expectedBots, int expectedActive,
	int expectedVetoStarted, int expectedPicks, int expectedBans,
	int expectedLastVetoBlocked)
{
	const BotTournamentStatus status = CountBotTournamentStatus();
	bool pass = true;

	if (expectedBots >= 0 && status.bots != expectedBots)
		pass = false;
	if (expectedActive >= 0 && status.active != expectedActive)
		pass = false;
	if (expectedVetoStarted >= 0 &&
		status.vetoStarted != expectedVetoStarted)
		pass = false;
	if (expectedPicks >= 0 && status.picks != expectedPicks)
		pass = false;
	if (expectedBans >= 0 && status.bans != expectedBans)
		pass = false;
	if (expectedLastVetoBlocked >= 0 &&
		status.lastVeto.blocked != expectedLastVetoBlocked)
		pass = false;

	base_import.Com_Print(G_Fmt(
		"q3a_bot_tournament_status bots={} humans={} playing={} "
		"bot_playing={} human_playing={} spectators={} active={} "
		"veto_started={} veto_complete={} home_turn={} team_based={} "
		"pool={} picks={} bans={} order={} picks_needed={} home_id={} "
		"away_id={} first_map={} games_played={} series_complete={} "
		"match_winners={} match_ids={} match_maps={} player0_wins={} "
		"player1_wins={} change_map_set={} change_map={} "
		"last_setup_attempted={} "
		"last_setup_configured={} last_setup_bot_is_home={} "
		"last_veto_attempted={} last_veto_bot_found={} "
		"last_veto_client={} last_veto_map={} last_veto_allowed={} "
		"last_veto_blocked={} last_veto_reason={} "
		"last_veto_picks_before={} last_veto_picks_after={} "
		"last_veto_bans_before={} last_veto_bans_after={} "
		"last_replay_setup_attempted={} "
		"last_replay_setup_configured={} last_replay_setup_order={} "
		"last_replay_setup_history={} "
		"last_replay_attempted={} last_replay_game={} "
		"last_replay_success={} last_replay_rejected={} "
		"last_replay_reason={} last_replay_target_map={} "
		"last_replay_games_before={} last_replay_games_after={} "
		"last_replay_winners_before={} last_replay_winners_after={} "
		"last_replay_series_complete_before={} "
		"last_replay_series_complete_after={} last_replay_preserved={} "
		"last_replay_reset_applied={} "
		"expected_bots={} expected_active={} expected_veto_started={} "
		"expected_picks={} expected_bans={} "
		"expected_last_veto_blocked={} pass={}\n",
		status.bots, status.humans, status.playing, status.botPlaying,
		status.humanPlaying, status.spectators, status.active,
		status.vetoStarted, status.vetoComplete, status.homeTurn,
		status.teamBased, status.pool, status.picks, status.bans,
		status.order, status.picksNeeded, MarkerString(status.homeId),
		MarkerString(status.awayId), MarkerString(status.firstMap),
		status.gamesPlayed, status.seriesComplete, status.matchWinners,
		status.matchIds, status.matchMaps, status.player0Wins,
		status.player1Wins, status.changeMapSet,
		MarkerString(status.changeMap),
		status.lastSetup.attempted, status.lastSetup.configured,
		status.lastSetup.botIsHome, status.lastVeto.attempted,
		status.lastVeto.botFound, status.lastVeto.client,
		MarkerString(status.lastVeto.map), status.lastVeto.allowed,
		status.lastVeto.blocked, MarkerString(status.lastVeto.message),
		status.lastVeto.picksBefore, status.lastVeto.picksAfter,
		status.lastVeto.bansBefore, status.lastVeto.bansAfter,
		status.lastReplaySetup.attempted,
		status.lastReplaySetup.configured, status.lastReplaySetup.order,
		status.lastReplaySetup.history, status.lastReplay.attempted,
		status.lastReplay.gameNumber, status.lastReplay.success,
		status.lastReplay.rejected, MarkerString(status.lastReplay.reason),
		MarkerString(status.lastReplay.targetMap),
		status.lastReplay.gamesBefore, status.lastReplay.gamesAfter,
		status.lastReplay.winnersBefore, status.lastReplay.winnersAfter,
		status.lastReplay.seriesCompleteBefore,
		status.lastReplay.seriesCompleteAfter,
		status.lastReplay.preserved, status.lastReplay.resetApplied,
		expectedBots, expectedActive, expectedVetoStarted, expectedPicks,
		expectedBans, expectedLastVetoBlocked, pass ? 1 : 0).data());
}

int BotVote_TryLaunchFirstBotVote(const char* voteName, const char* voteArg)
{
	gentity_t* bot = FindFirstPlayingBotClient();
	const char* effectiveVoteName = voteName ? voteName : "";
	const char* effectiveVoteArg = voteArg ? voteArg : "";

	botVoteLastLaunch = {};
	botVoteLastLaunch.attempted = 1;

	if (!bot || !bot->client) {
		botVoteLastLaunch.reason = "no_playing_bot";
		const BotVoteStatus status = CountBotVoteStatus();
		base_import.Com_Print(G_Fmt(
			"q3a_bot_vote_launch attempted={} bot_found={} client={} "
			"vote={} arg={} success={} blocked={} reason={} active_vote={} "
			"voting_clients={}\n",
			botVoteLastLaunch.attempted, botVoteLastLaunch.botFound,
			botVoteLastLaunch.client, effectiveVoteName, effectiveVoteArg,
			botVoteLastLaunch.success, botVoteLastLaunch.blocked,
			botVoteLastLaunch.reason, status.activeVote,
			status.votingClients).data());
		return 0;
	}

	botVoteLastLaunch.botFound = 1;
	botVoteLastLaunch.client = static_cast<int>(bot - g_entities) - 1;

	Commands::VoteLaunchResult result =
		Commands::TryLaunchVote(bot, effectiveVoteName, effectiveVoteArg);
	botVoteLastLaunch.success = result.success ? 1 : 0;
	botVoteLastLaunch.blocked =
		(!result.success && result.message == "Bots cannot call votes.") ? 1 : 0;
	botVoteLastLaunch.reason =
		result.success ? "launched" : BotVoteLaunchReason(result.message);

	const BotVoteStatus status = CountBotVoteStatus();
	base_import.Com_Print(G_Fmt(
		"q3a_bot_vote_launch attempted={} bot_found={} client={} "
		"vote={} arg={} success={} blocked={} reason={} active_vote={} "
		"voting_clients={}\n",
		botVoteLastLaunch.attempted, botVoteLastLaunch.botFound,
		botVoteLastLaunch.client, effectiveVoteName, effectiveVoteArg,
		botVoteLastLaunch.success, botVoteLastLaunch.blocked,
		botVoteLastLaunch.reason, status.activeVote,
		status.votingClients).data());
	return botVoteLastLaunch.success;
}

void BotVote_PrintStatus(int expectedBots, int expectedHumans,
	int expectedPlaying, int expectedVotingClients, int expectedActiveVote,
	int expectedLastLaunchBlocked)
{
	const BotVoteStatus status = CountBotVoteStatus();
	bool pass = true;

	if (expectedBots >= 0 && status.bots != expectedBots)
		pass = false;
	if (expectedHumans >= 0 && status.humans != expectedHumans)
		pass = false;
	if (expectedPlaying >= 0 && status.playing != expectedPlaying)
		pass = false;
	if (expectedVotingClients >= 0 &&
		status.votingClients != expectedVotingClients)
		pass = false;
	if (expectedActiveVote >= 0 && status.activeVote != expectedActiveVote)
		pass = false;
	if (expectedLastLaunchBlocked >= 0 &&
		status.lastLaunch.blocked != expectedLastLaunchBlocked)
		pass = false;

	base_import.Com_Print(G_Fmt(
		"q3a_bot_vote_status bots={} humans={} playing={} bot_playing={} "
		"human_playing={} spectators={} voting_clients={} active_vote={} "
		"vote_open={} execute_pending={} caller_bot={} caller_human={} "
		"vote_yes={} vote_no={} bot_yes={} bot_no={} human_yes={} "
		"human_no={} allow_voting={} allow_spec_vote={} vote_flags={} "
		"last_launch_attempted={} last_launch_bot_found={} "
		"last_launch_client={} last_launch_success={} "
		"last_launch_blocked={} last_launch_reason={} expected_bots={} "
		"expected_humans={} expected_playing={} "
		"expected_voting_clients={} expected_active_vote={} "
		"expected_last_launch_blocked={} pass={}\n",
		status.bots, status.humans, status.playing, status.botPlaying,
		status.humanPlaying, status.spectators, status.votingClients,
		status.activeVote, status.voteOpen, status.executePending,
		status.callerBot, status.callerHuman, status.voteYes, status.voteNo,
		status.botYes, status.botNo, status.humanYes, status.humanNo,
		status.allowVoting, status.allowSpecVote, status.voteFlags,
		status.lastLaunch.attempted, status.lastLaunch.botFound,
		status.lastLaunch.client, status.lastLaunch.success,
		status.lastLaunch.blocked, status.lastLaunch.reason, expectedBots,
		expectedHumans, expectedPlaying, expectedVotingClients,
		expectedActiveVote, expectedLastLaunchBlocked, pass ? 1 : 0).data());
}

void BotMapVote_ResetStatus()
{
	botMapVoteLastBotCast = {};
	botMapVoteLastFinalize = {};
	BotMapVote_ClearSelectorState();
	game.mapSystem.playQueue.clear();
	game.mapSystem.myMapQueue.clear();
}

int BotMapVote_BeginCurrentMapVote()
{
	const std::string currentMap = std::string(CharArrayToStringView(level.mapName));
	int mapSeeded = 0;
	int success = 0;
	const char* reason = "blank_map";

	BotMapVote_ClearSelectorState();
	botMapVoteLastBotCast = {};
	botMapVoteLastFinalize = {};

	if (!currentMap.empty()) {
		auto& ms = level.mapSelector;

		mapSeeded = BotMapVote_EnsureCurrentMapEntry(currentMap);
		ms.candidates[0] = currentMap;
		ms.voteStartTime = level.time != 0_sec ? level.time : 1_ms;
		success = 1;
		reason = mapSeeded ? "seeded_current" : "current";
	}

	const BotMapVoteStatus status = CountBotMapVoteStatus();
	base_import.Com_Print(G_Fmt(
		"q3a_bot_mapvote_begin attempted=1 success={} reason={} "
		"map={} map_seeded={} active={} candidates={} candidate0={} "
		"vote_count0={} vote_count1={} vote_count2={}\n",
		success, reason, MarkerString(currentMap), mapSeeded, status.active,
		status.candidates, MarkerString(status.candidate0), status.count0,
		status.count1, status.count2).data());
	return success;
}

int BotMapVote_TryCastFirstBotVote(int voteIndex)
{
	auto& ms = level.mapSelector;
	gentity_t* bot = FindFirstPlayingBotClient();

	botMapVoteLastBotCast = {};
	botMapVoteLastBotCast.attempted = 1;
	botMapVoteLastBotCast.requestedIndex = voteIndex;
	botMapVoteLastBotCast.active = ms.voteStartTime != 0_sec ? 1 : 0;

	if (!bot || !bot->client) {
		botMapVoteLastBotCast.reason = "no_playing_bot";
		const BotMapVoteStatus status = CountBotMapVoteStatus();
		base_import.Com_Print(G_Fmt(
			"q3a_bot_mapvote_bot_vote attempted={} bot_found={} client={} "
			"requested_index={} active={} blocked={} counted={} "
			"stored_vote={} reason={} vote_count0={} vote_count1={} "
			"vote_count2={} bot_votes={} human_votes={}\n",
			botMapVoteLastBotCast.attempted, botMapVoteLastBotCast.botFound,
			botMapVoteLastBotCast.client, botMapVoteLastBotCast.requestedIndex,
			botMapVoteLastBotCast.active, botMapVoteLastBotCast.blocked,
			botMapVoteLastBotCast.counted, botMapVoteLastBotCast.storedVote,
			botMapVoteLastBotCast.reason, status.count0, status.count1,
			status.count2, status.botVotes, status.humanVotes).data());
		return 0;
	}

	botMapVoteLastBotCast.botFound = 1;
	botMapVoteLastBotCast.client = static_cast<int>(bot - g_entities) - 1;

	if (!botMapVoteLastBotCast.active) {
		botMapVoteLastBotCast.reason = "inactive";
		const BotMapVoteStatus status = CountBotMapVoteStatus();
		base_import.Com_Print(G_Fmt(
			"q3a_bot_mapvote_bot_vote attempted={} bot_found={} client={} "
			"requested_index={} active={} blocked={} counted={} "
			"stored_vote={} reason={} vote_count0={} vote_count1={} "
			"vote_count2={} bot_votes={} human_votes={}\n",
			botMapVoteLastBotCast.attempted, botMapVoteLastBotCast.botFound,
			botMapVoteLastBotCast.client, botMapVoteLastBotCast.requestedIndex,
			botMapVoteLastBotCast.active, botMapVoteLastBotCast.blocked,
			botMapVoteLastBotCast.counted, botMapVoteLastBotCast.storedVote,
			botMapVoteLastBotCast.reason, status.count0, status.count1,
			status.count2, status.botVotes, status.humanVotes).data());
		return 0;
	}

	if (voteIndex < 0 || voteIndex >= static_cast<int>(ms.candidates.size()) ||
		ms.candidates[voteIndex].empty()) {
		botMapVoteLastBotCast.reason = "invalid_candidate";
		const BotMapVoteStatus status = CountBotMapVoteStatus();
		base_import.Com_Print(G_Fmt(
			"q3a_bot_mapvote_bot_vote attempted={} bot_found={} client={} "
			"requested_index={} active={} blocked={} counted={} "
			"stored_vote={} reason={} vote_count0={} vote_count1={} "
			"vote_count2={} bot_votes={} human_votes={}\n",
			botMapVoteLastBotCast.attempted, botMapVoteLastBotCast.botFound,
			botMapVoteLastBotCast.client, botMapVoteLastBotCast.requestedIndex,
			botMapVoteLastBotCast.active, botMapVoteLastBotCast.blocked,
			botMapVoteLastBotCast.counted, botMapVoteLastBotCast.storedVote,
			botMapVoteLastBotCast.reason, status.count0, status.count1,
			status.count2, status.botVotes, status.humanVotes).data());
		return 0;
	}

	MapSelector_CastVote(bot, voteIndex);
	MapSelector_SyncVotes(level);

	botMapVoteLastBotCast.count0 = ms.voteCounts[0];
	botMapVoteLastBotCast.count1 = ms.voteCounts[1];
	botMapVoteLastBotCast.count2 = ms.voteCounts[2];
	if (botMapVoteLastBotCast.client >= 0 &&
		botMapVoteLastBotCast.client < MAX_CLIENTS) {
		botMapVoteLastBotCast.storedVote =
			ms.votes[botMapVoteLastBotCast.client];
	}
	botMapVoteLastBotCast.counted =
		(botMapVoteLastBotCast.storedVote == voteIndex &&
		 ms.voteCounts[voteIndex] > 0) ? 1 : 0;
	botMapVoteLastBotCast.blocked =
		(!botMapVoteLastBotCast.counted &&
		 botMapVoteLastBotCast.storedVote == -1) ? 1 : 0;
	botMapVoteLastBotCast.reason =
		botMapVoteLastBotCast.blocked ? "bot_blocked" : "not_blocked";

	const BotMapVoteStatus status = CountBotMapVoteStatus();
	base_import.Com_Print(G_Fmt(
		"q3a_bot_mapvote_bot_vote attempted={} bot_found={} client={} "
		"requested_index={} active={} blocked={} counted={} stored_vote={} "
		"reason={} vote_count0={} vote_count1={} vote_count2={} "
		"bot_votes={} human_votes={}\n",
		botMapVoteLastBotCast.attempted, botMapVoteLastBotCast.botFound,
		botMapVoteLastBotCast.client, botMapVoteLastBotCast.requestedIndex,
		botMapVoteLastBotCast.active, botMapVoteLastBotCast.blocked,
		botMapVoteLastBotCast.counted, botMapVoteLastBotCast.storedVote,
		botMapVoteLastBotCast.reason, status.count0, status.count1,
		status.count2, status.botVotes, status.humanVotes).data());
	return botMapVoteLastBotCast.blocked;
}

int BotMapVote_FinalizeAndExit()
{
	auto& ms = level.mapSelector;
	std::array<std::string, 3> candidates = ms.candidates;
	std::array<int, 3> voteCounts = ms.voteCounts;

	botMapVoteLastFinalize = {};
	botMapVoteLastFinalize.attempted = 1;
	botMapVoteLastFinalize.currentMap =
		std::string(CharArrayToStringView(level.mapName));
	for (const auto& candidate : candidates) {
		if (!candidate.empty())
			botMapVoteLastFinalize.candidates++;
	}

	if (ms.voteStartTime == 0_sec) {
		botMapVoteLastFinalize.reason = "inactive";
		const BotMapVoteStatus status = CountBotMapVoteStatus();
		base_import.Com_Print(G_Fmt(
			"q3a_bot_mapvote_finalize attempted={} success={} reason={} "
			"target_map={} current_map={} selected_index={} "
			"selected_votes={} candidates={} exit_requested={} "
			"change_map_set={} active={} vote_count0={} vote_count1={} "
			"vote_count2={}\n",
			botMapVoteLastFinalize.attempted, botMapVoteLastFinalize.success,
			botMapVoteLastFinalize.reason,
			MarkerString(botMapVoteLastFinalize.targetMap),
			MarkerString(botMapVoteLastFinalize.currentMap),
			botMapVoteLastFinalize.selectedIndex,
			botMapVoteLastFinalize.selectedVotes,
			botMapVoteLastFinalize.candidates,
			botMapVoteLastFinalize.exitRequested, status.changeMapSet,
			status.active, status.count0, status.count1, status.count2).data());
		return 0;
	}

	MapSelectorFinalize();

	botMapVoteLastFinalize.targetMap = level.changeMap;
	botMapVoteLastFinalize.changeMapSet = level.changeMap.empty() ? 0 : 1;
	botMapVoteLastFinalize.exitRequested = level.intermission.exit ? 1 : 0;

	for (size_t i = 0; i < candidates.size(); ++i) {
		if (!candidates[i].empty() && candidates[i] == botMapVoteLastFinalize.targetMap) {
			botMapVoteLastFinalize.selectedIndex = static_cast<int>(i);
			botMapVoteLastFinalize.selectedVotes = voteCounts[i];
			break;
		}
	}

	botMapVoteLastFinalize.success =
		(botMapVoteLastFinalize.changeMapSet &&
		 botMapVoteLastFinalize.exitRequested) ? 1 : 0;
	botMapVoteLastFinalize.reason =
		botMapVoteLastFinalize.success ? "selected_exit" : "finalize_failed";

	if (botMapVoteLastFinalize.success) {
		ExitLevel(true);
	}

	const BotMapVoteStatus status = CountBotMapVoteStatus();
	base_import.Com_Print(G_Fmt(
		"q3a_bot_mapvote_finalize attempted={} success={} reason={} "
		"target_map={} current_map={} selected_index={} selected_votes={} "
		"candidates={} exit_requested={} change_map_set={} active={} "
		"vote_count0={} vote_count1={} vote_count2={}\n",
		botMapVoteLastFinalize.attempted, botMapVoteLastFinalize.success,
		botMapVoteLastFinalize.reason,
		MarkerString(botMapVoteLastFinalize.targetMap),
		MarkerString(botMapVoteLastFinalize.currentMap),
		botMapVoteLastFinalize.selectedIndex,
		botMapVoteLastFinalize.selectedVotes,
		botMapVoteLastFinalize.candidates,
		botMapVoteLastFinalize.exitRequested, status.changeMapSet,
		status.active, status.count0, status.count1, status.count2).data());
	return botMapVoteLastFinalize.success;
}

void BotMapVote_PrintStatus(int expectedBots, int expectedActive,
	int expectedCandidates, int expectedLastBotVoteBlocked,
	int expectedLastFinalizeSuccess, int expectedChangeMapSet)
{
	const BotMapVoteStatus status = CountBotMapVoteStatus();
	bool pass = true;

	if (expectedBots >= 0 && status.bots != expectedBots)
		pass = false;
	if (expectedActive >= 0 && status.active != expectedActive)
		pass = false;
	if (expectedCandidates >= 0 && status.candidates != expectedCandidates)
		pass = false;
	if (expectedLastBotVoteBlocked >= 0 &&
		status.lastBotVote.blocked != expectedLastBotVoteBlocked)
		pass = false;
	if (expectedLastFinalizeSuccess >= 0 &&
		status.lastFinalize.success != expectedLastFinalizeSuccess)
		pass = false;
	if (expectedChangeMapSet >= 0 &&
		status.changeMapSet != expectedChangeMapSet)
		pass = false;

	base_import.Com_Print(G_Fmt(
		"q3a_bot_mapvote_status bots={} humans={} playing={} "
		"bot_playing={} human_playing={} spectators={} connected_clients={} "
		"active={} force_exit={} post_intermission={} exit_requested={} "
		"candidates={} candidate0={} candidate1={} candidate2={} "
		"vote_count0={} vote_count1={} vote_count2={} "
		"total_counted_votes={} bot_votes={} human_votes={} "
		"change_map_set={} change_map={} current_map={} "
		"last_bot_vote_attempted={} last_bot_vote_found={} "
		"last_bot_vote_client={} last_bot_vote_index={} "
		"last_bot_vote_active={} last_bot_vote_blocked={} "
		"last_bot_vote_counted={} last_bot_vote_stored={} "
		"last_bot_vote_reason={} last_finalize_attempted={} "
		"last_finalize_success={} last_finalize_reason={} "
		"last_finalize_target_map={} last_finalize_current_map={} "
		"last_finalize_selected_index={} last_finalize_selected_votes={} "
		"last_finalize_candidates={} last_finalize_exit_requested={} "
		"last_finalize_change_map_set={} expected_bots={} "
		"expected_active={} expected_candidates={} "
		"expected_last_bot_vote_blocked={} "
		"expected_last_finalize_success={} expected_change_map_set={} "
		"pass={}\n",
		status.bots, status.humans, status.playing, status.botPlaying,
		status.humanPlaying, status.spectators, status.connectedClients,
		status.active, status.forceExit, status.postIntermission,
		status.exitRequested, status.candidates,
		MarkerString(status.candidate0), MarkerString(status.candidate1),
		MarkerString(status.candidate2), status.count0, status.count1,
		status.count2, status.totalCountedVotes, status.botVotes,
		status.humanVotes, status.changeMapSet, MarkerString(status.changeMap),
		MarkerString(status.currentMap), status.lastBotVote.attempted,
		status.lastBotVote.botFound, status.lastBotVote.client,
		status.lastBotVote.requestedIndex, status.lastBotVote.active,
		status.lastBotVote.blocked, status.lastBotVote.counted,
		status.lastBotVote.storedVote, status.lastBotVote.reason,
		status.lastFinalize.attempted, status.lastFinalize.success,
		status.lastFinalize.reason, MarkerString(status.lastFinalize.targetMap),
		MarkerString(status.lastFinalize.currentMap),
		status.lastFinalize.selectedIndex,
		status.lastFinalize.selectedVotes, status.lastFinalize.candidates,
		status.lastFinalize.exitRequested,
		status.lastFinalize.changeMapSet, expectedBots, expectedActive,
		expectedCandidates, expectedLastBotVoteBlocked,
		expectedLastFinalizeSuccess, expectedChangeMapSet,
		pass ? 1 : 0).data());
}

void BotMyMap_ResetStatus()
{
	botMyMapLastQueue = {};
	botMyMapLastConsume = {};
}

void BotMyMap_ClearQueues()
{
	game.mapSystem.playQueue.clear();
	game.mapSystem.myMapQueue.clear();
}

int BotMyMap_TryQueueFirstBotMyMap(const char* mapName)
{
	const char* effectiveMapName = mapName ? mapName : "";
	gentity_t* bot = FindFirstPlayingBotClient();

	botMyMapLastQueue = {};
	botMyMapLastQueue.attempted = 1;
	botMyMapLastQueue.mapName = effectiveMapName;

	if (!effectiveMapName[0]) {
		botMyMapLastQueue.rejected = 1;
		botMyMapLastQueue.reason = "empty_map";
		const BotMyMapStatus status = CountBotMyMapStatus();
		base_import.Com_Print(G_Fmt(
			"q3a_bot_mymap_queue attempted={} bot_found={} client={} "
			"map={} social={} social_assigned={} map_seeded={} "
			"success={} rejected={} reason={} play_queue={} "
			"mymap_queue={}\n",
			botMyMapLastQueue.attempted, botMyMapLastQueue.botFound,
			botMyMapLastQueue.client, MarkerString(botMyMapLastQueue.mapName),
			MarkerString(botMyMapLastQueue.socialID),
			botMyMapLastQueue.socialAssigned, botMyMapLastQueue.mapSeeded,
			botMyMapLastQueue.success, botMyMapLastQueue.rejected,
			botMyMapLastQueue.reason, status.playQueue, status.myMapQueue).data());
		return 0;
	}

	if (!bot || !bot->client) {
		botMyMapLastQueue.rejected = 1;
		botMyMapLastQueue.reason = "no_playing_bot";
		const BotMyMapStatus status = CountBotMyMapStatus();
		base_import.Com_Print(G_Fmt(
			"q3a_bot_mymap_queue attempted={} bot_found={} client={} "
			"map={} social={} social_assigned={} map_seeded={} "
			"success={} rejected={} reason={} play_queue={} "
			"mymap_queue={}\n",
			botMyMapLastQueue.attempted, botMyMapLastQueue.botFound,
			botMyMapLastQueue.client, MarkerString(botMyMapLastQueue.mapName),
			MarkerString(botMyMapLastQueue.socialID),
			botMyMapLastQueue.socialAssigned, botMyMapLastQueue.mapSeeded,
			botMyMapLastQueue.success, botMyMapLastQueue.rejected,
			botMyMapLastQueue.reason, status.playQueue, status.myMapQueue).data());
		return 0;
	}

	botMyMapLastQueue.botFound = 1;
	botMyMapLastQueue.client = static_cast<int>(bot - g_entities) - 1;

	if (!bot->client->sess.socialID[0]) {
		char syntheticSocialID[MAX_INFO_VALUE] = {};
		std::snprintf(syntheticSocialID, sizeof(syntheticSocialID),
			"bot_mymap_%d", botMyMapLastQueue.client);
		Q_strlcpy(bot->client->sess.socialID, syntheticSocialID,
			sizeof(bot->client->sess.socialID));
		botMyMapLastQueue.socialAssigned = 1;
	}
	botMyMapLastQueue.socialID = bot->client->sess.socialID;

	if (!Commands::CheckMyMapAllowed(bot)) {
		botMyMapLastQueue.rejected = 1;
		botMyMapLastQueue.reason = "not_allowed";
		const BotMyMapStatus status = CountBotMyMapStatus();
		base_import.Com_Print(G_Fmt(
			"q3a_bot_mymap_queue attempted={} bot_found={} client={} "
			"map={} social={} social_assigned={} map_seeded={} "
			"success={} rejected={} reason={} play_queue={} "
			"mymap_queue={}\n",
			botMyMapLastQueue.attempted, botMyMapLastQueue.botFound,
			botMyMapLastQueue.client, MarkerString(botMyMapLastQueue.mapName),
			MarkerString(botMyMapLastQueue.socialID),
			botMyMapLastQueue.socialAssigned, botMyMapLastQueue.mapSeeded,
			botMyMapLastQueue.success, botMyMapLastQueue.rejected,
			botMyMapLastQueue.reason, status.playQueue, status.myMapQueue).data());
		return 0;
	}

	const MapEntry* map = game.mapSystem.GetMapEntry(effectiveMapName);
	if (!map && game.mapSystem.mapPool.empty()) {
		MapEntry smokeMap{};
		smokeMap.filename = effectiveMapName;
		smokeMap.longName = effectiveMapName;
		smokeMap.mapTypeFlags |= MAP_DM;
		game.mapSystem.mapPool.push_back(std::move(smokeMap));
		map = &game.mapSystem.mapPool.back();
		botMyMapLastQueue.mapSeeded = 1;
	}
	if (!map) {
		botMyMapLastQueue.rejected = 1;
		botMyMapLastQueue.reason = "map_missing";
		const BotMyMapStatus status = CountBotMyMapStatus();
		base_import.Com_Print(G_Fmt(
			"q3a_bot_mymap_queue attempted={} bot_found={} client={} "
			"map={} social={} social_assigned={} map_seeded={} "
			"success={} rejected={} reason={} play_queue={} "
			"mymap_queue={}\n",
			botMyMapLastQueue.attempted, botMyMapLastQueue.botFound,
			botMyMapLastQueue.client, MarkerString(botMyMapLastQueue.mapName),
			MarkerString(botMyMapLastQueue.socialID),
			botMyMapLastQueue.socialAssigned, botMyMapLastQueue.mapSeeded,
			botMyMapLastQueue.success, botMyMapLastQueue.rejected,
			botMyMapLastQueue.reason, status.playQueue, status.myMapQueue).data());
		return 0;
	}
	if (map->filename.empty()) {
		botMyMapLastQueue.rejected = 1;
		botMyMapLastQueue.reason = "empty_filename";
		const BotMyMapStatus status = CountBotMyMapStatus();
		base_import.Com_Print(G_Fmt(
			"q3a_bot_mymap_queue attempted={} bot_found={} client={} "
			"map={} social={} social_assigned={} map_seeded={} "
			"success={} rejected={} reason={} play_queue={} "
			"mymap_queue={}\n",
			botMyMapLastQueue.attempted, botMyMapLastQueue.botFound,
			botMyMapLastQueue.client, MarkerString(botMyMapLastQueue.mapName),
			MarkerString(botMyMapLastQueue.socialID),
			botMyMapLastQueue.socialAssigned, botMyMapLastQueue.mapSeeded,
			botMyMapLastQueue.success, botMyMapLastQueue.rejected,
			botMyMapLastQueue.reason, status.playQueue, status.myMapQueue).data());
		return 0;
	}
	if (game.mapSystem.IsMapInQueue(map->filename)) {
		botMyMapLastQueue.rejected = 1;
		botMyMapLastQueue.reason = "map_duplicate";
		const BotMyMapStatus status = CountBotMyMapStatus();
		base_import.Com_Print(G_Fmt(
			"q3a_bot_mymap_queue attempted={} bot_found={} client={} "
			"map={} social={} social_assigned={} map_seeded={} "
			"success={} rejected={} reason={} play_queue={} "
			"mymap_queue={}\n",
			botMyMapLastQueue.attempted, botMyMapLastQueue.botFound,
			botMyMapLastQueue.client, MarkerString(botMyMapLastQueue.mapName),
			MarkerString(botMyMapLastQueue.socialID),
			botMyMapLastQueue.socialAssigned, botMyMapLastQueue.mapSeeded,
			botMyMapLastQueue.success, botMyMapLastQueue.rejected,
			botMyMapLastQueue.reason, status.playQueue, status.myMapQueue).data());
		return 0;
	}
	if (game.mapSystem.IsClientInQueue(bot->client->sess.socialID)) {
		botMyMapLastQueue.rejected = 1;
		botMyMapLastQueue.reason = "client_duplicate";
		const BotMyMapStatus status = CountBotMyMapStatus();
		base_import.Com_Print(G_Fmt(
			"q3a_bot_mymap_queue attempted={} bot_found={} client={} "
			"map={} social={} social_assigned={} map_seeded={} "
			"success={} rejected={} reason={} play_queue={} "
			"mymap_queue={}\n",
			botMyMapLastQueue.attempted, botMyMapLastQueue.botFound,
			botMyMapLastQueue.client, MarkerString(botMyMapLastQueue.mapName),
			MarkerString(botMyMapLastQueue.socialID),
			botMyMapLastQueue.socialAssigned, botMyMapLastQueue.mapSeeded,
			botMyMapLastQueue.success, botMyMapLastQueue.rejected,
			botMyMapLastQueue.reason, status.playQueue, status.myMapQueue).data());
		return 0;
	}

	const auto enqueueResult = game.mapSystem.EnqueueMyMapRequest(
		*map, bot->client->sess.socialID, 0, 0, level.time);
	if (!enqueueResult.accepted) {
		botMyMapLastQueue.rejected = 1;
		botMyMapLastQueue.reason = "queue_rejected";
		const BotMyMapStatus status = CountBotMyMapStatus();
		base_import.Com_Print(G_Fmt(
			"q3a_bot_mymap_queue attempted={} bot_found={} client={} "
			"map={} social={} social_assigned={} map_seeded={} "
			"success={} rejected={} reason={} play_queue={} "
			"mymap_queue={}\n",
			botMyMapLastQueue.attempted, botMyMapLastQueue.botFound,
			botMyMapLastQueue.client, MarkerString(botMyMapLastQueue.mapName),
			MarkerString(botMyMapLastQueue.socialID),
			botMyMapLastQueue.socialAssigned, botMyMapLastQueue.mapSeeded,
			botMyMapLastQueue.success, botMyMapLastQueue.rejected,
			botMyMapLastQueue.reason, status.playQueue, status.myMapQueue).data());
		return 0;
	}

	botMyMapLastQueue.mapName = map->filename;
	botMyMapLastQueue.success = 1;
	botMyMapLastQueue.reason = "queued";
	const BotMyMapStatus status = CountBotMyMapStatus();
	base_import.Com_Print(G_Fmt(
		"q3a_bot_mymap_queue attempted={} bot_found={} client={} "
		"map={} social={} social_assigned={} map_seeded={} "
		"success={} rejected={} reason={} play_queue={} mymap_queue={}\n",
		botMyMapLastQueue.attempted, botMyMapLastQueue.botFound,
		botMyMapLastQueue.client, MarkerString(botMyMapLastQueue.mapName),
		MarkerString(botMyMapLastQueue.socialID),
		botMyMapLastQueue.socialAssigned, botMyMapLastQueue.mapSeeded,
		botMyMapLastQueue.success, botMyMapLastQueue.rejected,
		botMyMapLastQueue.reason, status.playQueue, status.myMapQueue).data());
	return 1;
}

int BotMyMap_ConsumeQueuedMap()
{
	botMyMapLastConsume = {};
	botMyMapLastConsume.attempted = 1;

	if (game.mapSystem.playQueue.empty()) {
		botMyMapLastConsume.reason = "empty_queue";
		const BotMyMapStatus status = CountBotMyMapStatus();
		base_import.Com_Print(G_Fmt(
			"q3a_bot_mymap_consume attempted={} success={} reason={} "
			"map={} social={} play_queue={} mymap_queue={}\n",
			botMyMapLastConsume.attempted, botMyMapLastConsume.success,
			botMyMapLastConsume.reason, MarkerString(botMyMapLastConsume.mapName),
			MarkerString(botMyMapLastConsume.socialID), status.playQueue,
			status.myMapQueue).data());
		return 0;
	}

	const QueuedMap queued = game.mapSystem.playQueue.front();
	botMyMapLastConsume.mapName = queued.filename;
	botMyMapLastConsume.socialID = queued.socialID;
	game.mapSystem.ConsumeQueuedMap();
	botMyMapLastConsume.success = 1;
	botMyMapLastConsume.reason = "consumed";

	const BotMyMapStatus status = CountBotMyMapStatus();
	base_import.Com_Print(G_Fmt(
		"q3a_bot_mymap_consume attempted={} success={} reason={} "
		"map={} social={} play_queue={} mymap_queue={}\n",
		botMyMapLastConsume.attempted, botMyMapLastConsume.success,
		botMyMapLastConsume.reason, MarkerString(botMyMapLastConsume.mapName),
		MarkerString(botMyMapLastConsume.socialID), status.playQueue,
		status.myMapQueue).data());
	return 1;
}

void BotMyMap_PrintStatus(int expectedBots, int expectedPlayQueue,
	int expectedMyMapQueue, int expectedLastQueueSuccess,
	int expectedLastConsumeSuccess)
{
	const BotMyMapStatus status = CountBotMyMapStatus();
	bool pass = true;

	if (expectedBots >= 0 && status.bots != expectedBots)
		pass = false;
	if (expectedPlayQueue >= 0 && status.playQueue != expectedPlayQueue)
		pass = false;
	if (expectedMyMapQueue >= 0 && status.myMapQueue != expectedMyMapQueue)
		pass = false;
	if (expectedLastQueueSuccess >= 0 &&
		status.lastQueue.success != expectedLastQueueSuccess)
		pass = false;
	if (expectedLastConsumeSuccess >= 0 &&
		status.lastConsume.success != expectedLastConsumeSuccess)
		pass = false;

	base_import.Com_Print(G_Fmt(
		"q3a_bot_mymap_status bots={} humans={} playing={} bot_playing={} "
		"human_playing={} spectators={} play_queue={} mymap_queue={} "
		"allow_mymap={} maps_mymap={} queue_limit={} front_map={} "
		"front_social={} front_enable_flags={} front_disable_flags={} "
		"mymap_front_map={} mymap_front_social={} "
		"last_queue_attempted={} last_queue_bot_found={} "
		"last_queue_client={} last_queue_social_assigned={} "
		"last_queue_map_seeded={} last_queue_success={} "
		"last_queue_rejected={} "
		"last_queue_reason={} last_queue_map={} last_queue_social={} "
		"last_consume_attempted={} last_consume_success={} "
		"last_consume_reason={} last_consume_map={} last_consume_social={} "
		"expected_bots={} expected_play_queue={} expected_mymap_queue={} "
		"expected_last_queue_success={} expected_last_consume_success={} "
		"pass={}\n",
		status.bots, status.humans, status.playing, status.botPlaying,
		status.humanPlaying, status.spectators, status.playQueue,
		status.myMapQueue, status.allowMyMap, status.mapsMyMap,
		status.queueLimit, MarkerString(status.frontMap),
		MarkerString(status.frontSocial), status.frontEnableFlags,
		status.frontDisableFlags, MarkerString(status.myMapFrontMap),
		MarkerString(status.myMapFrontSocial),
		status.lastQueue.attempted, status.lastQueue.botFound,
		status.lastQueue.client, status.lastQueue.socialAssigned,
		status.lastQueue.mapSeeded, status.lastQueue.success,
		status.lastQueue.rejected,
		status.lastQueue.reason, MarkerString(status.lastQueue.mapName),
		MarkerString(status.lastQueue.socialID),
		status.lastConsume.attempted, status.lastConsume.success,
		status.lastConsume.reason, MarkerString(status.lastConsume.mapName),
		MarkerString(status.lastConsume.socialID), expectedBots,
		expectedPlayQueue, expectedMyMapQueue, expectedLastQueueSuccess,
		expectedLastConsumeSuccess, pass ? 1 : 0).data());
}

void BotIntermission_ResetStatus()
{
	botIntermissionLastBegin = {};
}

int BotIntermission_BeginIntermission()
{
	botIntermissionLastBegin = {};
	botIntermissionLastBegin.attempted = 1;

	const BotIntermissionStatus before = CountBotIntermissionStatus();
	botIntermissionLastBegin.botCount = before.bots;
	botIntermissionLastBegin.mapName = before.currentMap;

	if (before.intermission) {
		botIntermissionLastBegin.success = 1;
		botIntermissionLastBegin.reason = "already_active";
		base_import.Com_Print(G_Fmt(
			"q3a_bot_intermission_begin attempted={} bot_count={} "
			"success={} reason={} map={} intermission={} "
			"change_map_current={} intermission_bots={} "
			"pm_freeze_bots={} sorted_bots={}\n",
			botIntermissionLastBegin.attempted,
			botIntermissionLastBegin.botCount,
			botIntermissionLastBegin.success,
			botIntermissionLastBegin.reason,
			MarkerString(botIntermissionLastBegin.mapName), before.intermission,
			before.changeMapCurrent, before.intermissionBots,
			before.pmFreezeBots, before.sortedBots).data());
		return 1;
	}

	if (before.currentMap.empty()) {
		botIntermissionLastBegin.reason = "blank_map";
		base_import.Com_Print(G_Fmt(
			"q3a_bot_intermission_begin attempted={} bot_count={} "
			"success={} reason={} map={} intermission={} "
			"change_map_current={} intermission_bots={} "
			"pm_freeze_bots={} sorted_bots={}\n",
			botIntermissionLastBegin.attempted,
			botIntermissionLastBegin.botCount,
			botIntermissionLastBegin.success,
			botIntermissionLastBegin.reason,
			MarkerString(botIntermissionLastBegin.mapName), before.intermission,
			before.changeMapCurrent, before.intermissionBots,
			before.pmFreezeBots, before.sortedBots).data());
		return 0;
	}

	gentity_t* target = CreateTargetChangeLevel(before.currentMap);
	if (!target) {
		botIntermissionLastBegin.reason = "target_failed";
		base_import.Com_Print(G_Fmt(
			"q3a_bot_intermission_begin attempted={} bot_count={} "
			"success={} reason={} map={} intermission={} "
			"change_map_current={} intermission_bots={} "
			"pm_freeze_bots={} sorted_bots={}\n",
			botIntermissionLastBegin.attempted,
			botIntermissionLastBegin.botCount,
			botIntermissionLastBegin.success,
			botIntermissionLastBegin.reason,
			MarkerString(botIntermissionLastBegin.mapName), before.intermission,
			before.changeMapCurrent, before.intermissionBots,
			before.pmFreezeBots, before.sortedBots).data());
		return 0;
	}

	BeginIntermission(target);
	CalculateRanks();

	const BotIntermissionStatus after = CountBotIntermissionStatus();
	botIntermissionLastBegin.success =
		(after.intermission && after.changeMapCurrent) ? 1 : 0;
	botIntermissionLastBegin.reason =
		botIntermissionLastBegin.success ? "begun" : "begin_failed";

	base_import.Com_Print(G_Fmt(
		"q3a_bot_intermission_begin attempted={} bot_count={} success={} "
		"reason={} map={} intermission={} change_map_current={} "
		"intermission_bots={} pm_freeze_bots={} sorted_bots={}\n",
		botIntermissionLastBegin.attempted,
		botIntermissionLastBegin.botCount,
		botIntermissionLastBegin.success,
		botIntermissionLastBegin.reason,
		MarkerString(botIntermissionLastBegin.mapName), after.intermission,
		after.changeMapCurrent, after.intermissionBots,
		after.pmFreezeBots, after.sortedBots).data());
	return botIntermissionLastBegin.success;
}

void BotIntermission_PrintStatus(int expectedBots, int expectedHumans,
	int expectedPlaying, int expectedIntermission, int expectedPmFreezeBots,
	int expectedPostIntermission, int expectedSortedBots)
{
	const BotIntermissionStatus status = CountBotIntermissionStatus();
	bool pass = true;

	if (expectedBots >= 0 && status.bots != expectedBots)
		pass = false;
	if (expectedHumans >= 0 && status.humans != expectedHumans)
		pass = false;
	if (expectedPlaying >= 0 && status.playing != expectedPlaying)
		pass = false;
	if (expectedIntermission >= 0 &&
		status.intermission != expectedIntermission)
		pass = false;
	if (expectedPmFreezeBots >= 0 &&
		status.pmFreezeBots != expectedPmFreezeBots)
		pass = false;
	if (expectedPostIntermission >= 0 &&
		status.postIntermission != expectedPostIntermission)
		pass = false;
	if (expectedSortedBots >= 0 && status.sortedBots != expectedSortedBots)
		pass = false;

	base_import.Com_Print(G_Fmt(
		"q3a_bot_intermission_status bots={} humans={} playing={} "
		"bot_playing={} human_playing={} spectators={} "
		"connected_clients={} sorted_clients={} sorted_bots={} "
		"sorted_humans={} intermission={} intermission_queued={} "
		"post_intermission={} ready_to_exit={} change_map_set={} "
		"change_map_current={} change_map={} current_map={} "
		"intermission_bots={} pm_freeze_bots={} freecam_bots={} "
		"solid_not_bots={} last_begin_attempted={} "
		"last_begin_success={} last_begin_bot_count={} "
		"last_begin_reason={} last_begin_map={} expected_bots={} "
		"expected_humans={} expected_playing={} expected_intermission={} "
		"expected_pm_freeze_bots={} expected_post_intermission={} "
		"expected_sorted_bots={} pass={}\n",
		status.bots, status.humans, status.playing, status.botPlaying,
		status.humanPlaying, status.spectators, status.connectedClients,
		status.sortedClients, status.sortedBots, status.sortedHumans,
		status.intermission, status.intermissionQueued,
		status.postIntermission, status.readyToExit, status.changeMapSet,
		status.changeMapCurrent, MarkerString(status.changeMap),
		MarkerString(status.currentMap), status.intermissionBots,
		status.pmFreezeBots, status.freecamBots, status.solidNotBots,
		status.lastBegin.attempted, status.lastBegin.success,
		status.lastBegin.botCount, status.lastBegin.reason,
		MarkerString(status.lastBegin.mapName), expectedBots, expectedHumans,
		expectedPlaying, expectedIntermission, expectedPmFreezeBots,
		expectedPostIntermission, expectedSortedBots, pass ? 1 : 0).data());
}

void BotNextMap_ResetStatus()
{
	botNextMapLastTransition = {};
}

int BotNextMap_TransitionQueuedMap()
{
	botNextMapLastTransition = {};
	botNextMapLastTransition.attempted = 1;
	botNextMapLastTransition.currentMap =
		std::string(CharArrayToStringView(level.mapName));
	botNextMapLastTransition.playQueueBefore =
		static_cast<int>(game.mapSystem.playQueue.size());
	botNextMapLastTransition.myMapQueueBefore =
		static_cast<int>(game.mapSystem.myMapQueue.size());

	if (game.mapSystem.playQueue.empty()) {
		botNextMapLastTransition.reason = "empty_queue";
		const BotNextMapStatus status = CountBotNextMapStatus();
		base_import.Com_Print(G_Fmt(
			"q3a_bot_nextmap_transition attempted={} success={} "
			"consumed={} reason={} target_map={} current_map={} "
			"play_queue_before={} mymap_queue_before={} "
			"play_queue_after={} mymap_queue_after={} "
			"override_enable_flags={} override_disable_flags={} "
			"change_map_set={}\n",
			botNextMapLastTransition.attempted,
			botNextMapLastTransition.success,
			botNextMapLastTransition.consumed,
			botNextMapLastTransition.reason,
			MarkerString(botNextMapLastTransition.targetMap),
			MarkerString(botNextMapLastTransition.currentMap),
			botNextMapLastTransition.playQueueBefore,
			botNextMapLastTransition.myMapQueueBefore, status.playQueue,
			status.myMapQueue, botNextMapLastTransition.overrideEnableFlags,
			botNextMapLastTransition.overrideDisableFlags,
			status.changeMapSet).data());
		return 0;
	}

	const QueuedMap queued = game.mapSystem.playQueue.front();
	botNextMapLastTransition.targetMap = queued.filename;
	botNextMapLastTransition.overrideEnableFlags = queued.enableFlags;
	botNextMapLastTransition.overrideDisableFlags = queued.disableFlags;

	level.changeMap = queued.filename.c_str();
	game.map.overrideEnableFlags = queued.enableFlags;
	game.map.overrideDisableFlags = queued.disableFlags;
	ExitLevel(true);
	game.mapSystem.ConsumeQueuedMap();

	botNextMapLastTransition.playQueueAfter =
		static_cast<int>(game.mapSystem.playQueue.size());
	botNextMapLastTransition.myMapQueueAfter =
		static_cast<int>(game.mapSystem.myMapQueue.size());
	botNextMapLastTransition.consumed =
		(botNextMapLastTransition.playQueueAfter <
			botNextMapLastTransition.playQueueBefore) ? 1 : 0;
	botNextMapLastTransition.success =
		!botNextMapLastTransition.targetMap.empty() ? 1 : 0;
	botNextMapLastTransition.reason =
		(botNextMapLastTransition.success && botNextMapLastTransition.consumed) ?
		"queued_exit" : "transition_failed";

	const BotNextMapStatus status = CountBotNextMapStatus();
	base_import.Com_Print(G_Fmt(
		"q3a_bot_nextmap_transition attempted={} success={} consumed={} "
		"reason={} target_map={} current_map={} play_queue_before={} "
		"mymap_queue_before={} play_queue_after={} mymap_queue_after={} "
		"override_enable_flags={} override_disable_flags={} "
		"change_map_set={}\n",
		botNextMapLastTransition.attempted,
		botNextMapLastTransition.success,
		botNextMapLastTransition.consumed,
		botNextMapLastTransition.reason,
		MarkerString(botNextMapLastTransition.targetMap),
		MarkerString(botNextMapLastTransition.currentMap),
		botNextMapLastTransition.playQueueBefore,
		botNextMapLastTransition.myMapQueueBefore,
		botNextMapLastTransition.playQueueAfter,
		botNextMapLastTransition.myMapQueueAfter,
		botNextMapLastTransition.overrideEnableFlags,
		botNextMapLastTransition.overrideDisableFlags,
		status.changeMapSet).data());
	return botNextMapLastTransition.success &&
		botNextMapLastTransition.consumed;
}

void BotNextMap_PrintStatus(int expectedBots, int expectedPlayQueue,
	int expectedMyMapQueue, int expectedLastTransitionSuccess,
	int expectedLastTransitionConsumed, int expectedChangeMapSet)
{
	const BotNextMapStatus status = CountBotNextMapStatus();
	bool pass = true;

	if (expectedBots >= 0 && status.bots != expectedBots)
		pass = false;
	if (expectedPlayQueue >= 0 && status.playQueue != expectedPlayQueue)
		pass = false;
	if (expectedMyMapQueue >= 0 && status.myMapQueue != expectedMyMapQueue)
		pass = false;
	if (expectedLastTransitionSuccess >= 0 &&
		status.lastTransition.success != expectedLastTransitionSuccess)
		pass = false;
	if (expectedLastTransitionConsumed >= 0 &&
		status.lastTransition.consumed != expectedLastTransitionConsumed)
		pass = false;
	if (expectedChangeMapSet >= 0 && status.changeMapSet != expectedChangeMapSet)
		pass = false;

	base_import.Com_Print(G_Fmt(
		"q3a_bot_nextmap_status bots={} humans={} playing={} "
		"bot_playing={} human_playing={} spectators={} "
		"connected_clients={} play_queue={} mymap_queue={} "
		"front_map={} front_social={} change_map_set={} change_map={} "
		"current_map={} last_transition_attempted={} "
		"last_transition_success={} last_transition_consumed={} "
		"last_transition_reason={} last_transition_target_map={} "
		"last_transition_current_map={} last_transition_play_queue_before={} "
		"last_transition_mymap_queue_before={} "
		"last_transition_play_queue_after={} "
		"last_transition_mymap_queue_after={} "
		"last_transition_override_enable_flags={} "
		"last_transition_override_disable_flags={} expected_bots={} "
		"expected_play_queue={} expected_mymap_queue={} "
		"expected_last_transition_success={} "
		"expected_last_transition_consumed={} expected_change_map_set={} "
		"pass={}\n",
		status.bots, status.humans, status.playing, status.botPlaying,
		status.humanPlaying, status.spectators, status.connectedClients,
		status.playQueue, status.myMapQueue, MarkerString(status.frontMap),
		MarkerString(status.frontSocial), status.changeMapSet,
		MarkerString(status.changeMap), MarkerString(status.currentMap),
		status.lastTransition.attempted, status.lastTransition.success,
		status.lastTransition.consumed, status.lastTransition.reason,
		MarkerString(status.lastTransition.targetMap),
		MarkerString(status.lastTransition.currentMap),
		status.lastTransition.playQueueBefore,
		status.lastTransition.myMapQueueBefore,
		status.lastTransition.playQueueAfter,
		status.lastTransition.myMapQueueAfter,
		status.lastTransition.overrideEnableFlags,
		status.lastTransition.overrideDisableFlags, expectedBots,
		expectedPlayQueue, expectedMyMapQueue, expectedLastTransitionSuccess,
		expectedLastTransitionConsumed, expectedChangeMapSet,
		pass ? 1 : 0).data());
}

void BotScoreboard_ResetStatus()
{
	botScoreboardLastApply = {};
}

int BotScoreboard_ApplyTestScores(int leaderScore, int runnerScore)
{
	gentity_t* leader = nullptr;
	gentity_t* runner = nullptr;
	const int botCount = FindFirstTwoPlayingBots(&leader, &runner);

	botScoreboardLastApply = {};
	botScoreboardLastApply.attempted = 1;
	botScoreboardLastApply.botCount = botCount;
	botScoreboardLastApply.leaderScore = leaderScore;
	botScoreboardLastApply.runnerScore = runnerScore;

	if (botCount < 2 || !leader || !runner || !leader->client || !runner->client) {
		botScoreboardLastApply.reason = "need_two_bots";
		const BotScoreboardStatus status = CountBotScoreboardStatus();
		base_import.Com_Print(G_Fmt(
			"q3a_bot_scoreboard_scores attempted={} bot_count={} applied={} "
			"leader_client={} runner_client={} leader_score={} "
			"runner_score={} reason={} top_client={} top_score={} "
			"sorted_bots={}\n",
			botScoreboardLastApply.attempted, botScoreboardLastApply.botCount,
			botScoreboardLastApply.applied,
			botScoreboardLastApply.leaderClient,
			botScoreboardLastApply.runnerClient,
			botScoreboardLastApply.leaderScore,
			botScoreboardLastApply.runnerScore,
			botScoreboardLastApply.reason, status.top.client,
			status.top.score, status.sortedBots).data());
		return 0;
	}

	botScoreboardLastApply.leaderClient =
		static_cast<int>(leader - g_entities) - 1;
	botScoreboardLastApply.runnerClient =
		static_cast<int>(runner - g_entities) - 1;

	leader->client->resp.score = leaderScore;
	runner->client->resp.score = runnerScore;
	CalculateRanks();

	botScoreboardLastApply.applied =
		(ClientScoreForStandings(leader->client) == leaderScore &&
			ClientScoreForStandings(runner->client) == runnerScore) ? 1 : 0;
	botScoreboardLastApply.reason =
		botScoreboardLastApply.applied ? "applied" : "score_mismatch";

	const BotScoreboardStatus status = CountBotScoreboardStatus();
	base_import.Com_Print(G_Fmt(
		"q3a_bot_scoreboard_scores attempted={} bot_count={} applied={} "
		"leader_client={} runner_client={} leader_score={} runner_score={} "
		"reason={} top_client={} top_score={} sorted_bots={}\n",
		botScoreboardLastApply.attempted, botScoreboardLastApply.botCount,
		botScoreboardLastApply.applied, botScoreboardLastApply.leaderClient,
		botScoreboardLastApply.runnerClient,
		botScoreboardLastApply.leaderScore,
		botScoreboardLastApply.runnerScore,
		botScoreboardLastApply.reason, status.top.client, status.top.score,
		status.sortedBots).data());
	return botScoreboardLastApply.applied;
}

void BotScoreboard_PrintStatus(int expectedBots, int expectedHumans,
	int expectedPlaying, int expectedSortedBots, int expectedLeaderBot,
	int expectedTopScore, int expectedSecondScore)
{
	const BotScoreboardStatus status = CountBotScoreboardStatus();
	bool pass = true;

	if (expectedBots >= 0 && status.bots != expectedBots)
		pass = false;
	if (expectedHumans >= 0 && status.humans != expectedHumans)
		pass = false;
	if (expectedPlaying >= 0 && status.playing != expectedPlaying)
		pass = false;
	if (expectedSortedBots >= 0 && status.sortedBots != expectedSortedBots)
		pass = false;
	if (expectedLeaderBot >= 0 && status.leaderBot != expectedLeaderBot)
		pass = false;
	if (expectedTopScore >= 0 && status.top.score != expectedTopScore)
		pass = false;
	if (expectedSecondScore >= 0 && status.second.score != expectedSecondScore)
		pass = false;

	base_import.Com_Print(G_Fmt(
		"q3a_bot_scoreboard_status bots={} humans={} playing={} "
		"bot_playing={} human_playing={} spectators={} voting_clients={} "
		"connected_clients={} sorted_clients={} sorted_bots={} "
		"sorted_humans={} sorted_spectators={} leader_bot={} "
		"runner_bot={} score_ordered={} rank_ordered={} "
		"top_client={} top_bot={} top_human={} top_playing={} "
		"top_score={} top_rank={} top_rank_tied={} second_client={} "
		"second_bot={} second_human={} second_playing={} "
		"second_score={} second_rank={} second_rank_tied={} "
		"last_score_attempted={} last_score_bot_count={} "
		"last_score_applied={} last_score_leader_client={} "
		"last_score_runner_client={} last_score_leader_score={} "
		"last_score_runner_score={} last_score_reason={} "
		"expected_bots={} expected_humans={} expected_playing={} "
		"expected_sorted_bots={} expected_leader_bot={} "
		"expected_top_score={} expected_second_score={} pass={}\n",
		status.bots, status.humans, status.playing, status.botPlaying,
		status.humanPlaying, status.spectators, status.votingClients,
		status.connectedClients, status.sortedClients, status.sortedBots,
		status.sortedHumans, status.sortedSpectators, status.leaderBot,
		status.runnerBot, status.scoreOrdered, status.rankOrdered,
		status.top.client, status.top.isBot, status.top.isHuman,
		status.top.isPlaying, status.top.score, status.top.rank,
		status.top.rankTied, status.second.client, status.second.isBot,
		status.second.isHuman, status.second.isPlaying, status.second.score,
		status.second.rank, status.second.rankTied,
		status.lastApply.attempted, status.lastApply.botCount,
		status.lastApply.applied, status.lastApply.leaderClient,
		status.lastApply.runnerClient, status.lastApply.leaderScore,
		status.lastApply.runnerScore, status.lastApply.reason,
		expectedBots, expectedHumans, expectedPlaying, expectedSortedBots,
		expectedLeaderBot, expectedTopScore, expectedSecondScore,
		pass ? 1 : 0).data());
}

/*
===============
G_LoadIPFilters

Executes the persisted listip.cfg commands to rebuild the runtime filters.
===============
*/
void G_LoadIPFilters()
{
	const std::filesystem::path path = ResolveIPFilterPath();
	const std::string pathStr = path.generic_string();

	std::error_code existsError;
	if (!std::filesystem::exists(path, existsError) || existsError)
		return;

	std::ifstream stream(path);
	if (!stream.is_open()) {
		gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_sgame_auto_4e4387469408", pathStr.c_str());
		return;
	}

	gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_sgame_auto_90b352aba65b", pathStr.c_str());

	std::string line;
	while (std::getline(stream, line)) {
		std::string_view view(line);
		while (!view.empty() && IsWhitespace(view.front()))
			view.remove_prefix(1);
		while (!view.empty() && IsWhitespace(view.back()))
			view.remove_suffix(1);

		if (view.empty())
			continue;
		if (view.starts_with('#') || view.starts_with("//"))
			continue;

		gi.AddCommandString(G_Fmt("{}\n", view).data());
	}

	if (stream.bad()) {
		gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_sgame_auto_630aa8bb06e1", pathStr.c_str());
	}
}
/*
===============
G_SaveIPFilters

Writes the active IP filters to disk.
===============
*/
void G_SaveIPFilters()
{
	SVCmd_WriteIP_f();
}

/*
===============
G_FilterPacket

Determines whether a given IP address should be blocked.
Respects filterBan:
- filterBan = 1 (default): matching IPs are rejected
- filterBan = 0: ONLY matching IPs are accepted
===============
*/
bool G_FilterPacket(const char* from)
{
	if (!from || !*from)
		return false;

	std::array<uint8_t, 4> in{};
	if (!ParseFromAddress(std::string_view{ from }, in))
		return false;

	const bool anyMatch = std::any_of(g_filters.begin(), g_filters.end(),
		[&](const IPFilter& f) { return Matches(f, in); });

	// If filterBan==1, a match means block; if ==0, a match means allow-only
	if (filterBan && filterBan->integer != 0)
		return anyMatch;      // match => blocked
	else
		return !anyMatch;     // no match => blocked
}

// Exercises the real sgame mover capture/scene/trace path against the packaged
// bmodel fixture. It is intentionally console-scoped so ordinary gameplay and
// network clients cannot select a synthetic rewind context.
static void SVCmd_RewindMoverSelfTest_f()
{
	LagCompensationHistoricalBrushRuntimeProbe probe{};
	const bool passed = LagCompensation_RunHistoricalBrushRuntimeProbe(&probe);
	gi.Com_PrintFmt(
		"worr_rewind_mover_selftest: status={} setup={} scene={} rotation={} rider_setup={} rider_continuity={} rider_frame_scene={} rider_provenance={} reference={} "
		"rotation_control={} baseline={} dispatched={} historical={} authority={} candidates={} rider_samples={} baseline_fraction={:.6f} "
		"historical_fraction={:.6f}\n",
		passed ? "pass" : "fail", probe.setup_ready ? 1u : 0u,
		probe.scene_ready ? 1u : 0u,
		probe.rotation_applied ? 1u : 0u,
		probe.rider_setup ? 1u : 0u,
		probe.rider_frame_continuity ? 1u : 0u,
		probe.rider_frame_scene_sealed ? 1u : 0u,
		probe.rider_provenance_sealed ? 1u : 0u,
		probe.fixture_reference_blocked ? 1u : 0u,
		probe.rotation_control_unblocked ? 1u : 0u,
		probe.baseline_unblocked ? 1u : 0u,
		probe.historical_dispatched ? 1u : 0u,
		probe.historical_blocked ? 1u : 0u,
		probe.authority_unchanged ? 1u : 0u, probe.candidate_count,
		probe.rider_continuity_samples,
		probe.baseline_fraction, probe.historical_fraction);
}

// Arms the dedicated collision fixture before normal server frames execute.
// The subsequent self-test consumes only poses captured by those normal frames.
static void SVCmd_RewindMoverArmRider_f()
{
	const bool armed = LagCompensation_ArmHistoricalBrushRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_mover_arm_rider: status={}\n",
		armed ? "pass" : "fail");
}

// Runs the real fire_rail/pierce_trace/Damage acceptance probe on a dedicated
// fixture with two engine bots. The probe itself restores its scoped player
// state before return and is never exposed through player commands.
static void SVCmd_RewindRailDamageSelfTest_f()
{
	LagCompensationRailDamageRuntimeProbe probe{};
	const bool passed = LagCompensation_RunRailDamageRuntimeProbe(&probe);
	gi.Com_PrintFmt(
		"worr_rewind_rail_damage_selftest: status={} setup={} history={} current_miss={} rejected_fallback={} rejected_no_damage={} legacy={} policy={} near_hit={} bounded_hit={} capped_hit={} damage={} geometry={} authority={} candidates={} damage_amount={} current_fraction={:.6f} near_fraction={:.6f} bounded_fraction={:.6f} capped_fraction={:.6f}\n",
		passed ? "pass" : "fail", probe.setup_ready ? 1u : 0u,
		probe.history_ready ? 1u : 0u,
		probe.current_world_miss ? 1u : 0u,
		probe.rejected_current_fallback ? 1u : 0u,
		probe.rejected_no_damage ? 1u : 0u,
		probe.legacy_rewind_selected ? 1u : 0u,
		probe.rail_policy_observed ? 1u : 0u,
		probe.near_latency_hit ? 1u : 0u,
		probe.bounded_latency_hit ? 1u : 0u,
		probe.capped_latency_hit ? 1u : 0u,
		probe.damage_applied ? 1u : 0u,
		probe.geometry_unchanged ? 1u : 0u,
		probe.query_authority_unchanged ? 1u : 0u,
		probe.candidate_count, probe.damage_amount, probe.current_fraction,
		probe.near_latency_fraction, probe.bounded_latency_fraction,
		probe.capped_latency_fraction);
}

static void SVCmd_RewindRailDamageArm_f()
{
	const bool armed = LagCompensation_ArmRailDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_rail_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms only the fixture state for the two-process canonical-command gate.
// The next real, negotiated client command must enter ClientThink and invoke
// the ordinary Railgun weapon callback; no console command can provide that
// authority or fire the weapon directly.
static void SVCmd_RewindCanonicalRailDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalRailDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_rail_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms the real-command Railgun mover-occlusion variant. The console only
// selects fixture geometry; the admitted client command and normal weapon
// callback remain the sole source of trace and damage authority.
static void SVCmd_RewindCanonicalRailMoverOcclusionArm_f()
{
	const bool armed =
		LagCompensation_ArmCanonicalRailMoverOcclusionRuntimeProbe();
	gi.Com_PrintFmt(
		"worr_rewind_canonical_rail_mover_occlusion_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms only the machinegun variant of the real-command fixture. As with the
// Railgun command, server console authority cannot construct a user command,
// choose a rewind target, or call a weapon callback.
static void SVCmd_RewindCanonicalMachinegunDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalMachinegunDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_machinegun_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms only the Chaingun variant of the real-command fixture. Its three-round
// burst remains entirely inside normal Item::weaponThink dispatch.
static void SVCmd_RewindCanonicalChaingunDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalChaingunDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_chaingun_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms only the Super Shotgun variant. Both barrels and all pellet traces stay
// in the ordinary weapon callback reached by the received attack command.
static void SVCmd_RewindCanonicalSuperShotgunDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalSuperShotgunDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_super_shotgun_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms only the Disruptor convergence variant. The received command still
// reaches the normal projectile and delayed damage-daemon lifecycle.
static void SVCmd_RewindCanonicalDisruptorDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalDisruptorDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_disruptor_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms the Rocket Launcher spawn-forward seam. The real client command still
// owns current-world rocket flight, impact, splash, and damage.
static void SVCmd_RewindCanonicalRocketDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalRocketDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_rocket_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

static void SVCmd_RewindCanonicalBfgDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalBfgDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_bfg_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

static void SVCmd_RewindCanonicalIonRipperDamageArm_f()
{
	const bool armed =
		LagCompensation_ArmCanonicalIonRipperDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_ion_ripper_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

static void SVCmd_RewindCanonicalTeslaMineDamageArm_f()
{
	const bool armed =
		LagCompensation_ArmCanonicalTeslaMineDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_tesla_mine_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

static void SVCmd_RewindCanonicalTrapDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalTrapDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_trap_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

static void SVCmd_RewindCanonicalGrappleDamageArm_f()
{
	const bool armed =
		LagCompensation_ArmCanonicalGrappleDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_grapple_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

static void SVCmd_RewindCanonicalOffhandHookArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalOffhandHookRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_offhand_hook_arm: status={}\n",
		armed ? "pass" : "fail");
}

static void SVCmd_RewindCanonicalProBallThrowArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalProBallThrowRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_proball_throw_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms the Grenade Launcher ballistic seam. The fixture can neither trace a
// trajectory nor create a bounce; the real command enters ordinary weapon and
// Toss physics only after current-world clear-path authorization.
static void SVCmd_RewindCanonicalGrenadeLauncherDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalGrenadeLauncherDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_grenade_launcher_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// The fixture only arms the real Hand Grenade release path. The no-attack
// command must leave Throw_Generic before normal Toss physics can consume its
// accepted release-time authority.
static void SVCmd_RewindCanonicalHandGrenadeDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalHandGrenadeDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_hand_grenade_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

static void SVCmd_RewindCanonicalHandGrenadeSplashArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalHandGrenadeSplashRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_hand_grenade_splash_arm: status={}\n",
		armed ? "pass" : "fail");
}

// The fixture observes a normal Proximity Launcher command and clear
// ballistic deployment only. It never calls a weapon function or mine trigger.
static void SVCmd_RewindCanonicalProxLauncherDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalProxLauncherDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_prox_launcher_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms the real mine lifecycle proof. The console command stages no collision,
// target selection, trigger, explosion, or damage result.
static void SVCmd_RewindCanonicalProxLauncherLifecycleArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalProxLauncherLifecycleRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_prox_launcher_lifecycle_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms the Rocket current-world splash seam. The fixture supplies only the
// present-world blocker; normal rocket_touch and RadiusDamage own the result.
static void SVCmd_RewindCanonicalRocketSplashDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalRocketSplashDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_rocket_splash_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms the fast Plasma Gun direct-hit seam. The real command and normal
// projectile lifecycle retain current-world direct and radius authority.
static void SVCmd_RewindCanonicalPlasmaGunDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalPlasmaGunDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_plasma_gun_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms the Plasma Gun current-world splash seam. Production projectile flight,
// touch, line-of-sight, and RadiusDamage remain the only result authority.
static void SVCmd_RewindCanonicalPlasmaGunSplashDamageArm_f()
{
	const bool armed =
		LagCompensation_ArmCanonicalPlasmaGunSplashDamageRuntimeProbe();
	gi.Com_PrintFmt(
		"worr_rewind_canonical_plasma_gun_splash_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms the shared Blaster/HyperBlaster direct-hit seam. Normal bolt flight,
// contact, radius effects, and damage remain current-world authority.
static void SVCmd_RewindCanonicalBlasterDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalBlasterDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_blaster_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms the real held-command HyperBlaster cadence seam. The shared bolt
// current-world flight/contact/direct/radius path remains production authority.
static void SVCmd_RewindCanonicalHyperBlasterDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalHyperBlasterDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_hyperblaster_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms the Chainfist hybrid melee seam. The real held command still reaches
// normal Item::weaponThink, live CanDamage, and current-authority Damage.
static void SVCmd_RewindCanonicalChainfistDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalChainfistDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_chainfist_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms the ETF flechette current-world spawn-forward seam. The real command
// and normal flechette contact remain responsible for the eventual damage.
static void SVCmd_RewindCanonicalEtfRifleDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalEtfRifleDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_etf_rifle_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms the Phalanx current-world spawn-forward seam. Normal flight, direct
// contact, splash, and damage remain owned by its production projectile path.
static void SVCmd_RewindCanonicalPhalanxDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalPhalanxDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_phalanx_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms the Phalanx current-world splash seam. The fixture supplies only the
// present-world blocker; normal phalanx_touch and RadiusDamage own the result.
static void SVCmd_RewindCanonicalPhalanxSplashDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalPhalanxSplashDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_phalanx_splash_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms only the first normal Plasma Beam tick from a real held attack. The
// repeating state machine and fire_beams path remain production-owned.
static void SVCmd_RewindCanonicalPlasmaBeamDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalPlasmaBeamDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_plasma_beam_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms a bounded held-command Plasma Beam cadence fixture. Ordinary repeating
// weapon frames must reach its exact three-tick damage contract.
static void SVCmd_RewindCanonicalPlasmaBeamHeldDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalPlasmaBeamHeldDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_plasma_beam_held_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms a bounded long-held Plasma Beam fixture. The real held command must
// sustain the ordinary repeater through the complete supplied-cell budget.
static void SVCmd_RewindCanonicalPlasmaBeamSustainedDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalPlasmaBeamSustainedDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_plasma_beam_sustained_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms the bounded Plasma Beam release contract. The client must first reach
// three ordinary ticks, then deliver a real no-attack command.
static void SVCmd_RewindCanonicalPlasmaBeamReleaseDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalPlasmaBeamReleaseDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_plasma_beam_release_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms a real Plasma Beam water crossing. The production beam path owns the
// water hit, retrace, and halved current-authority damage.
static void SVCmd_RewindCanonicalPlasmaBeamWaterRetraceDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalPlasmaBeamWaterRetraceDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_plasma_beam_water_retrace_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms only the first normal dry-world Thunderbolt tick from a real held
// attack. Main/side-ray resolution and damage de-duplication stay production-owned.
static void SVCmd_RewindCanonicalThunderboltDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalThunderboltDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_thunderbolt_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms a bounded held-command dry-world Thunderbolt cadence fixture. The
// normal main/side-ray path must retain per-tick target de-duplication.
static void SVCmd_RewindCanonicalThunderboltHeldDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalThunderboltHeldDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_thunderbolt_held_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms a bounded long-held Thunderbolt fixture. Its normal complete beam
// footprint must retain target de-duplication through the supplied-cell budget.
static void SVCmd_RewindCanonicalThunderboltSustainedDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalThunderboltSustainedDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_thunderbolt_sustained_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms the bounded dry-world Thunderbolt release contract. Its regular
// main/side-ray footprint must stop after a real no-attack command.
static void SVCmd_RewindCanonicalThunderboltReleaseDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalThunderboltReleaseDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_thunderbolt_release_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms a dry-shooter Thunderbolt water crossing. The production path owns
// water contact, retrace, the side rays, and target de-duplication.
static void SVCmd_RewindCanonicalThunderboltWaterRetraceDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalThunderboltWaterRetraceDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_thunderbolt_water_retrace_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms the current-authority underwater Thunderbolt discharge policy. A real
// client command must consume its cells and take the normal self damage.
static void SVCmd_RewindCanonicalThunderboltDischargeDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalThunderboltDischargeDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_thunderbolt_discharge_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

// Arms only the shotgun variant of the real-command fixture. The received
// attack must still reach the ordinary multi-pellet weapon callback.
static void SVCmd_RewindCanonicalShotgunDamageArm_f()
{
	const bool armed = LagCompensation_ArmCanonicalShotgunDamageRuntimeProbe();
	gi.Com_PrintFmt("worr_rewind_canonical_shotgun_damage_arm: status={}\n",
		armed ? "pass" : "fail");
}

/*
===============
ServerCommand

Dispatch "sv" commands
===============
*/
void ServerCommand()
{
	const char* cmd = gi.argv(1);
	if (!cmd || !*cmd) {
		gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_sgame_auto_4b35a84b7906");
		return;
	}

	if (Q_strcasecmp(cmd, "test") == 0) {
		Svcmd_Test_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_mover_selftest") == 0) {
		SVCmd_RewindMoverSelfTest_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_mover_arm_rider") == 0) {
		SVCmd_RewindMoverArmRider_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_rail_damage_selftest") == 0) {
		SVCmd_RewindRailDamageSelfTest_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_rail_damage_arm") == 0) {
		SVCmd_RewindRailDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_rail_damage_arm") == 0) {
		SVCmd_RewindCanonicalRailDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_rail_mover_occlusion_arm") == 0) {
		SVCmd_RewindCanonicalRailMoverOcclusionArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_machinegun_damage_arm") == 0) {
		SVCmd_RewindCanonicalMachinegunDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_chaingun_damage_arm") == 0) {
		SVCmd_RewindCanonicalChaingunDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_super_shotgun_damage_arm") == 0) {
		SVCmd_RewindCanonicalSuperShotgunDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_disruptor_damage_arm") == 0) {
		SVCmd_RewindCanonicalDisruptorDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_rocket_damage_arm") == 0) {
		SVCmd_RewindCanonicalRocketDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_bfg_damage_arm") == 0) {
		SVCmd_RewindCanonicalBfgDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_ion_ripper_damage_arm") == 0) {
		SVCmd_RewindCanonicalIonRipperDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_tesla_mine_damage_arm") == 0) {
		SVCmd_RewindCanonicalTeslaMineDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_trap_damage_arm") == 0) {
		SVCmd_RewindCanonicalTrapDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_grapple_damage_arm") == 0) {
		SVCmd_RewindCanonicalGrappleDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_offhand_hook_arm") == 0) {
		SVCmd_RewindCanonicalOffhandHookArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_proball_throw_arm") == 0) {
		SVCmd_RewindCanonicalProBallThrowArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_grenade_launcher_damage_arm") == 0) {
		SVCmd_RewindCanonicalGrenadeLauncherDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_hand_grenade_damage_arm") == 0) {
		SVCmd_RewindCanonicalHandGrenadeDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_hand_grenade_splash_arm") == 0) {
		SVCmd_RewindCanonicalHandGrenadeSplashArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_prox_launcher_damage_arm") == 0) {
		SVCmd_RewindCanonicalProxLauncherDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_prox_launcher_lifecycle_arm") == 0) {
		SVCmd_RewindCanonicalProxLauncherLifecycleArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_rocket_splash_damage_arm") == 0) {
		SVCmd_RewindCanonicalRocketSplashDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_plasma_gun_damage_arm") == 0) {
		SVCmd_RewindCanonicalPlasmaGunDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_plasma_gun_splash_damage_arm") == 0) {
		SVCmd_RewindCanonicalPlasmaGunSplashDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_blaster_damage_arm") == 0) {
		SVCmd_RewindCanonicalBlasterDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_hyperblaster_damage_arm") == 0) {
		SVCmd_RewindCanonicalHyperBlasterDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_chainfist_damage_arm") == 0) {
		SVCmd_RewindCanonicalChainfistDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_etf_rifle_damage_arm") == 0) {
		SVCmd_RewindCanonicalEtfRifleDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_phalanx_damage_arm") == 0) {
		SVCmd_RewindCanonicalPhalanxDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_phalanx_splash_damage_arm") == 0) {
		SVCmd_RewindCanonicalPhalanxSplashDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_plasma_beam_damage_arm") == 0) {
		SVCmd_RewindCanonicalPlasmaBeamDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_plasma_beam_held_damage_arm") == 0) {
		SVCmd_RewindCanonicalPlasmaBeamHeldDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_plasma_beam_sustained_damage_arm") == 0) {
		SVCmd_RewindCanonicalPlasmaBeamSustainedDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_plasma_beam_release_damage_arm") == 0) {
		SVCmd_RewindCanonicalPlasmaBeamReleaseDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_plasma_beam_water_retrace_damage_arm") == 0) {
		SVCmd_RewindCanonicalPlasmaBeamWaterRetraceDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_thunderbolt_damage_arm") == 0) {
		SVCmd_RewindCanonicalThunderboltDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_thunderbolt_held_damage_arm") == 0) {
		SVCmd_RewindCanonicalThunderboltHeldDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_thunderbolt_sustained_damage_arm") == 0) {
		SVCmd_RewindCanonicalThunderboltSustainedDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_thunderbolt_release_damage_arm") == 0) {
		SVCmd_RewindCanonicalThunderboltReleaseDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_thunderbolt_water_retrace_damage_arm") == 0) {
		SVCmd_RewindCanonicalThunderboltWaterRetraceDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_thunderbolt_discharge_damage_arm") == 0) {
		SVCmd_RewindCanonicalThunderboltDischargeDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "worr_rewind_canonical_shotgun_damage_arm") == 0) {
		SVCmd_RewindCanonicalShotgunDamageArm_f();
	}
	else if (Q_strcasecmp(cmd, "addip") == 0) {
		SVCmd_AddIP_f();
	}
	else if (Q_strcasecmp(cmd, "removeip") == 0) {
		SVCmd_RemoveIP_f();
	}
	else if (Q_strcasecmp(cmd, "listip") == 0) {
		SVCmd_ListIP_f();
	}
	else if (Q_strcasecmp(cmd, "writeip") == 0) {
		G_SaveIPFilters();
	}
	else if (Q_strcasecmp(cmd, "nextmap") == 0) {
		SVCmd_NextMap_f();
	}
	else if (Q_strcasecmp(cmd, "botlib_lifecycle_status") == 0) {
		Bot_RuntimePrintLifecycleStatus();
	}
	else if (Q_strcasecmp(cmd, "bot_team_policy_status") == 0) {
		SVCmd_BotTeamPolicyStatus_f();
	}
	else if (Q_strcasecmp(cmd, "bot_warmup_status") == 0) {
		SVCmd_BotWarmupStatus_f();
	}
	else if (Q_strcasecmp(cmd, "bot_vote_status") == 0) {
		SVCmd_BotVoteStatus_f();
	}
	else if (Q_strcasecmp(cmd, "bot_admin_audit_status") == 0) {
		SVCmd_BotAdminAuditStatus_f();
	}
	else if (Q_strcasecmp(cmd, "bot_tournament_status") == 0) {
		SVCmd_BotTournamentStatus_f();
	}
	else if (Q_strcasecmp(cmd, "bot_mapvote_status") == 0) {
		SVCmd_BotMapVoteStatus_f();
	}
	else if (Q_strcasecmp(cmd, "bot_mymap_status") == 0) {
		SVCmd_BotMyMapStatus_f();
	}
	else if (Q_strcasecmp(cmd, "bot_intermission_status") == 0) {
		SVCmd_BotIntermissionStatus_f();
	}
	else if (Q_strcasecmp(cmd, "bot_nextmap_status") == 0) {
		SVCmd_BotNextMapStatus_f();
	}
	else if (Q_strcasecmp(cmd, "bot_scoreboard_status") == 0) {
		SVCmd_BotScoreboardStatus_f();
	}
	else if (Q_strcasecmp(cmd, "bot_chat_policy_status") == 0) {
		SVCmd_BotChatPolicyStatus_f();
	}
	else {
		gi.LocClient_Print(nullptr, PRINT_HIGH, "$g_sgame_auto_14d3c73afcac", cmd);
	}
}

/*
===============
Notes

- Endian safety: we compare per-octet with masks, avoiding memcpy to uint32_t.
- Wildcard semantics: identical to legacy code; an octet value of 0 becomes mask 0.
- Capacity: up to MAX_IPFILTERS entries, de-duplicates identical filters.
- I/O: writeip now persists listip.cfg using standard file-system helpers.
- Threading: expects main thread usage like original.
*/
