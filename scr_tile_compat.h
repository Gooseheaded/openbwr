#ifndef BWGAME_SCR_TILE_COMPAT_H
#define BWGAME_SCR_TILE_COMPAT_H

#include "util.h"

#include <array>

namespace bwgame {

struct map_version_semantics_t {
	bool supported = false;
	bool requires_scr_semantic_pack = false;
	bool classic_chunk_layout = false;
};

static inline map_version_semantics_t map_version_semantics(uint16_t version) {
	if (version == 59 || version == 63) return {true, false, true};
	if (version == 205) return {true, false, false};
	if (version == 64 || version >= 206) return {true, true, false};
	return {};
}

struct scr_tileset_requirement_t {
	const char* name;
	size_t min_cv5_entries;
	size_t min_vf4_entries;
};

static const std::array<scr_tileset_requirement_t, 8> scr_tileset_requirements = {{
	{"badlands", 1979, 6172},
	{"platform", 2046, 5711},
	{"install", 1265, 1431},
	{"ashworld", 1418, 4161},
	{"jungle", 2046, 7038},
	{"desert", 2046, 9130},
	{"ice", 2039, 7174},
	{"twilight", 2047, 8552},
}};

static inline a_string validate_tileset_semantic_data(const scr_tileset_requirement_t& requirement, const a_vector<uint8_t>& cv5_data, const a_vector<uint8_t>& vf4_data) {
	if (cv5_data.size() % 52 != 0) {
		return format("tileset %s cv5 size %u is not a multiple of 52", requirement.name, (unsigned int)cv5_data.size());
	}
	if (vf4_data.size() % 32 != 0) {
		return format("tileset %s vf4 size %u is not a multiple of 32", requirement.name, (unsigned int)vf4_data.size());
	}

	size_t cv5_entries = cv5_data.size() / 52;
	size_t vf4_entries = vf4_data.size() / 32;
	if (cv5_entries < requirement.min_cv5_entries) {
		return format("tileset %s has %u cv5 entries, expected at least %u", requirement.name, (unsigned int)cv5_entries, (unsigned int)requirement.min_cv5_entries);
	}
	if (vf4_entries < requirement.min_vf4_entries) {
		return format("tileset %s has %u vf4 entries, expected at least %u", requirement.name, (unsigned int)vf4_entries, (unsigned int)requirement.min_vf4_entries);
	}

	for (size_t cv5_index = 0; cv5_index != cv5_entries; ++cv5_index) {
		const uint8_t* entry = cv5_data.data() + cv5_index * 52;
		for (size_t subtile_index = 0; subtile_index != 16; ++subtile_index) {
			size_t offset = 20 + subtile_index * 2;
			uint16_t megatile_index = (uint16_t)entry[offset] | (uint16_t)entry[offset + 1] << 8;
			if (megatile_index >= vf4_entries) {
				return format("tileset %s cv5 entry %u subtile %u references vf4 index %u out of range [0, %u)", requirement.name, (unsigned int)cv5_index, (unsigned int)subtile_index, megatile_index, (unsigned int)vf4_entries);
			}
		}
	}

	return {};
}

template<typename cv5_set_T, typename vf4_set_T>
static inline a_string validate_scr_semantic_pack(const cv5_set_T& tileset_cv5, const vf4_set_T& tileset_vf4) {
	if (tileset_cv5.size() < scr_tileset_requirements.size() || tileset_vf4.size() < scr_tileset_requirements.size()) {
		return format("missing or malformed SCR semantic pack: expected %u tilesets", (unsigned int)scr_tileset_requirements.size());
	}
	for (size_t i = 0; i != scr_tileset_requirements.size(); ++i) {
		a_string err = validate_tileset_semantic_data(scr_tileset_requirements[i], tileset_cv5.at(i), tileset_vf4.at(i));
		if (!err.empty()) return err;
	}
	return {};
}

template<typename cv5_set_T, typename vf4_set_T>
static inline a_string check_map_version_compatibility(uint16_t version, const cv5_set_T& tileset_cv5, const vf4_set_T& tileset_vf4) {
	auto semantics = map_version_semantics(version);
	if (!semantics.supported) {
		return format("unsupported map version %d", version);
	}
	if (semantics.requires_scr_semantic_pack) {
		a_string err = validate_scr_semantic_pack(tileset_cv5, tileset_vf4);
		if (!err.empty()) {
			return format("SCR semantic pack is required for map version %d: %s", version, err);
		}
	}
	return {};
}

}

#endif
