/*Copyright (c) 2024 The DarkMatter Project
Licensed under the GNU General Public License 2.0.

g_combat_heatmap.cpp (Game Combat Heatmap) This file implements a sophisticated heatmap system
to track combat intensity across the map in deathmatch modes. It records events like damage and
death, creating a "heat" value for different map areas. This data is used by other systems to
make more intelligent decisions. Key Responsibilities: - Event Tracking: `HM_AddEvent` is called
by combat functions to add "heat" to a specific location on the map, with a radial falloff
effect. - Data Management: Stores heat data in a grid-based spatial hash map and periodically
decays the heat values over time in `HM_Think`. - Spatial Queries: The `HM_Query` function
allows other systems, like player spawning logic, to query the "danger level" of a specific area
to avoid placing players in overly active combat zones.*/

#include "../g_local.hpp"
#include <algorithm>
#include <unordered_map>

// Tunables (cvars can be promoted later)
static constexpr float HM_CELL_SIZE = 256.0f;    // world units
static constexpr float HM_EVENT_RADIUS = 512.0f;    // falloff radius for a single event
static constexpr float HM_DECAY_PER_SECOND = 4.0f;      // linear decay per second
static constexpr float HM_MIN_CELL_HEAT = 0.01f;     // prune threshold
static constexpr float HM_QUERY_DEFAULT_RAD = 320.0f;    // used by spawns if not overridden
static constexpr float HM_MAX_EVENT_HEAT = 120.0f;
static constexpr float HM_FULL_DANGER_HEAT = 75.0f;

struct HMCell {
	float   heat = 0.0f;    // current accumulated heat
	GameTime touched = 0_ms; // last time the cell was updated or queried
};

struct HMKey {
	int32_t x = 0, y = 0;
	bool operator==(const HMKey& o) const noexcept { return x == o.x && y == o.y; }
};

struct HMKeyHash {
	size_t operator()(const HMKey& k) const noexcept {
		// 32-bit mix, avoid collisions
		uint32_t a = static_cast<uint32_t>(k.x);
		uint32_t b = static_cast<uint32_t>(k.y);
		a ^= b + 0x9e3779b9u + (a << 6) + (a >> 2);
		return size_t(a);
	}
};

static std::unordered_map<HMKey, HMCell, HMKeyHash> g_hm;
static size_t g_hm_prune_cursor = 0;

/*
===============
cell_from_pos
===============
*/
[[maybe_unused]] static inline HMKey cell_from_pos(const Vector3& p) {
	return HMKey{
		static_cast<int32_t>(floorf(p[0] / HM_CELL_SIZE)),
		static_cast<int32_t>(floorf(p[1] / HM_CELL_SIZE))
	};
}

/*
===============
apply_decay
Decay a single cell to 'now'.
===============
*/
static inline void apply_decay(HMCell& c, const GameTime& now) {
	if (!c.touched) {
		c.touched = now;
		return;
	}
	float dt = (now - c.touched).seconds<float>();
	if (dt > 0.0f && c.heat > 0.0f) {
		c.heat = std::max(0.0f, c.heat - HM_DECAY_PER_SECOND * dt);
		c.touched = now;
	}
}

/*
===============
HM_Init
===============
*/
void HM_Init() {
	g_hm.clear();
	g_hm_prune_cursor = 0;
}

/*
===============
HM_ResetForNewLevel
===============
*/
void HM_ResetForNewLevel() {
	g_hm.clear();
	g_hm_prune_cursor = 0;
}

/*
===============
deposit
Adds heat to a single cell with decay accounted for.
===============
*/
static inline void deposit(const HMKey& key, float add, const GameTime& now) {
	auto& cell = g_hm[key]; // creates if missing
	// initialize fresh cells
	if (!cell.touched) {
		cell.heat = 0.0f;
		cell.touched = now;
	}
	apply_decay(cell, now);
	cell.heat += add;
}

/*
===============
radial_falloff
Smooth falloff [0..1] within HM_EVENT_RADIUS (cosine ease).
===============
*/
static inline float radial_falloff(float d) {
	if (d >= HM_EVENT_RADIUS) return 0.0f;
	float t = 1.0f - (d / HM_EVENT_RADIUS);
	// 0.5 - 0.5*cos(pi*t) gives a nice hump
	return 0.5f - 0.5f * cosf(M_PI * t);
}

/*
===============
HM_AddEvent
===============
*/
void HM_AddEvent(const Vector3& pos, float amount) {
	if (!deathmatch->integer) {
		// Heatmap only used in deathmatch
		return;
	}

	if (amount <= 0.0f) return;
	amount = std::min(amount, HM_MAX_EVENT_HEAT);

	const GameTime now = level.time;

	// Cover a square of cells that intersect the event radius.
	const float r = HM_EVENT_RADIUS;
	const int cx = static_cast<int>(floorf(pos[0] / HM_CELL_SIZE));
	const int cy = static_cast<int>(floorf(pos[1] / HM_CELL_SIZE));
	const int rx = static_cast<int>(ceilf(r / HM_CELL_SIZE)) + 1;
	const int ry = rx;

	for (int dy = -ry; dy <= ry; ++dy) {
		for (int dx = -rx; dx <= rx; ++dx) {
			HMKey k{ cx + dx, cy + dy };
			// Compute the world-space center of this cell for distance
			Vector3 center = {
				(k.x + 0.5f) * HM_CELL_SIZE,
				(k.y + 0.5f) * HM_CELL_SIZE,
				pos[2]
			};
			float d = (center - pos).length();
			float w = radial_falloff(d);
			if (w <= 0.0f) continue;

			deposit(k, amount * w, now);
		}
	}
}

/*
===============
HM_Query
===============
*/
float HM_Query(const Vector3& pos, float radius) {
	if (!deathmatch->integer) {
		// Heatmap only used in deathmatch
		return 0.0f;
	}

	const GameTime now = level.time;
	float sum = 0.0f;

	float r = (radius > 0.0f) ? radius : HM_QUERY_DEFAULT_RAD;
	const int cx = static_cast<int>(floorf(pos[0] / HM_CELL_SIZE));
	const int cy = static_cast<int>(floorf(pos[1] / HM_CELL_SIZE));
	const int rx = static_cast<int>(ceilf(r / HM_CELL_SIZE)) + 1;
	const int ry = rx;

	for (int dy = -ry; dy <= ry; ++dy) {
		for (int dx = -rx; dx <= rx; ++dx) {
			HMKey k{ cx + dx, cy + dy };
			auto it = g_hm.find(k);
			if (it == g_hm.end()) continue;

			HMCell& c = it->second;
			apply_decay(c, now);

			// Simple box aggregation; weight by distance so closer cells count more
			Vector3 center = {
				(k.x + 0.5f) * HM_CELL_SIZE,
				(k.y + 0.5f) * HM_CELL_SIZE,
				pos[2]
			};
			float d = (center - pos).length();
			if (d > r) continue;

			float weight = 1.0f - (d / r);
			sum += c.heat * weight;
		}
	}
	return sum;
}

/*
===============
HM_Think
===============
*/
void HM_Think() {
	if (!deathmatch->integer) {
		// Heatmap only used in deathmatch
		return;
	}

	const GameTime now = level.time;

	const size_t kMaxChecksPerFrame = 128;

	if (g_hm.empty())
		return;

	if (g_hm_prune_cursor >= g_hm.size())
		g_hm_prune_cursor = 0;

	auto it = g_hm.begin();
	std::advance(it, g_hm_prune_cursor);

	for (size_t i = 0; i < kMaxChecksPerFrame && !g_hm.empty(); ++i) {
		if (it == g_hm.end()) {
			it = g_hm.begin();
			g_hm_prune_cursor = 0;
		}

		auto current = it++;
		apply_decay(current->second, now);
		if (current->second.heat <= HM_MIN_CELL_HEAT) {
			g_hm.erase(current);
		} else {
			++g_hm_prune_cursor;
		}

		if (!g_hm.empty() && g_hm_prune_cursor >= g_hm.size())
			g_hm_prune_cursor = 0;
	}
}

/*
===============
HM_DangerAt
Convenience wrapper around HM_Query, returns a 0..1 danger value
===============
*/
float HM_DangerAt(const Vector3& pos) {
	if (!deathmatch->integer) {
		// Heatmap only used in deathmatch
		return 0.0f;
	}

	float raw = HM_Query(pos, 320.0f);
	return std::clamp(raw / (raw + HM_FULL_DANGER_HEAT), 0.0f, 1.0f);
}

/*
===============
HM_Debug_Draw
===============
*/
void HM_Debug_Draw() {
#if 0
	if (!deathmatch->integer) {
		// Heatmap only used in deathmatch
		return;
	}

	for (const auto& p : g_hm) {
		const HMKey& k = p.first;
		const HMCell& c = p.second;
		if (c.heat <= 0.0f) continue;

		Vector3 center = {
			(k.x + 0.5f) * HM_CELL_SIZE,
			(k.y + 0.5f) * HM_CELL_SIZE,
			32.0f
		};
		Vector3 up = { 0, 0, 1 };
		int dmg = std::min(255, int(c.heat));
		SpawnDamage(TE_SPARKS, center, up, dmg);
	}
#endif
}
