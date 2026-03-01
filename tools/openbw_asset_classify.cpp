#include "../data_loading.h"
#include "../replay.h"
#include "../scr_tile_compat.h"

#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

using namespace bwgame;

enum class input_kind_t {
	map,
	replay,
	chk,
	unknown,
};

input_kind_t detect_input_kind(const std::string& path) {
	auto lower = path;
	for (char& c : lower) c = (char)std::tolower((unsigned char)c);
	if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".rep") return input_kind_t::replay;
	if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".scm") return input_kind_t::map;
	if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".scx") return input_kind_t::map;
	if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".chk") return input_kind_t::chk;
	return input_kind_t::unknown;
}

const char* input_kind_name(input_kind_t kind) {
	switch (kind) {
	case input_kind_t::map: return "map";
	case input_kind_t::replay: return "replay";
	case input_kind_t::chk: return "chk";
	default: return "unknown";
	}
}

std::string json_escape(const std::string& v) {
	std::string out;
	out.reserve(v.size() + 8);
	for (char c : v) {
		switch (c) {
		case '\\': out += "\\\\"; break;
		case '"': out += "\\\""; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default: out += c; break;
		}
	}
	return out;
}

using z_alloc_func = void* (*)(void*, unsigned int, unsigned int);
using z_free_func = void (*)(void*, void*);

struct z_stream_t {
	unsigned char* next_in = nullptr;
	unsigned int avail_in = 0;
	unsigned long total_in = 0;
	unsigned char* next_out = nullptr;
	unsigned int avail_out = 0;
	unsigned long total_out = 0;
	char* msg = nullptr;
	void* state = nullptr;
	z_alloc_func zalloc = nullptr;
	z_free_func zfree = nullptr;
	void* opaque = nullptr;
	int data_type = 0;
	unsigned long adler = 0;
	unsigned long reserved = 0;
};

struct zlib_api_t {
	using inflate_init_fn = int (*)(z_stream_t*, const char*, int);
	using inflate_fn = int (*)(z_stream_t*, int);
	using inflate_end_fn = int (*)(z_stream_t*);
	using zlib_version_fn = const char* (*)();

	bool loaded = false;
	a_string error;
	inflate_init_fn inflate_init = nullptr;
	inflate_fn inflate = nullptr;
	inflate_end_fn inflate_end = nullptr;
	zlib_version_fn zlib_version = nullptr;

#if defined(_WIN32)
	HMODULE handle = nullptr;
#else
	void* handle = nullptr;
#endif
};

static const int z_no_flush = 0;
static const int z_ok = 0;
static const int z_stream_end = 1;

zlib_api_t load_zlib_api() {
	zlib_api_t api;

#if defined(_WIN32)
	const std::array<const char*, 3> library_names = {{"zlib1.dll", "libz.dll", "z.dll"}};
	auto load_library = [](const char* name) { return LoadLibraryA(name); };
	auto load_symbol = [](HMODULE h, const char* sym) { return (void*)GetProcAddress(h, sym); };
#else
	const std::array<const char*, 3> library_names = {{"libz.so.1", "libz.so", "libz.dylib"}};
	auto load_library = [](const char* name) { return dlopen(name, RTLD_LAZY); };
	auto load_symbol = [](void* h, const char* sym) { return dlsym(h, sym); };
#endif

	for (auto name : library_names) {
		auto h = load_library(name);
		if (!h) continue;
		auto init = (zlib_api_t::inflate_init_fn)load_symbol(h, "inflateInit_");
		auto inflate = (zlib_api_t::inflate_fn)load_symbol(h, "inflate");
		auto end = (zlib_api_t::inflate_end_fn)load_symbol(h, "inflateEnd");
		auto ver = (zlib_api_t::zlib_version_fn)load_symbol(h, "zlibVersion");
		if (init && inflate && end && ver) {
			api.loaded = true;
			api.handle = h;
			api.inflate_init = init;
			api.inflate = inflate;
			api.inflate_end = end;
			api.zlib_version = ver;
			api.error = format("loaded %s", name);
			return api;
		}
#if defined(_WIN32)
		FreeLibrary(h);
#else
		dlclose(h);
#endif
	}

	api.error = "zlib runtime library not found";
	return api;
}

const zlib_api_t& get_zlib_api() {
	static zlib_api_t api = load_zlib_api();
	return api;
}

void load_chk_from_map_file(const std::string& path, a_vector<uint8_t>& chk_data) {
	data_loading::mpq_file<> map_file(path.c_str());
	map_file(chk_data, "staredit/scenario.chk");
}

void load_chk_from_chk_file(const std::string& path, a_vector<uint8_t>& chk_data) {
	data_loading::file_reader<> file(path.c_str());
	chk_data.resize(file.size());
	if (!chk_data.empty()) file.get_bytes(chk_data.data(), chk_data.size());
}

struct replay_extract_layout_t {
	const char* mode;
	size_t skip_u32_count;
	bool size_prefixed_game_info;
	size_t fixed_game_info_size;
};

struct chk_tile_heuristics_t {
	bool has_era = false;
	bool has_mtxm = false;
	size_t tileset_index = 0;
	size_t max_group_index = 0;
	size_t classic_max_group_index = 0;
	bool uses_new_tiles = false;
};

static const std::array<size_t, 8> classic_max_group_by_tileset = {{
	1664, // badlands
	1512, // platform
	1264, // install
	1261, // ashworld
	1577, // jungle
	1519, // desert
	1414, // ice
	1493, // twilight
}};

bool is_likely_chk(const a_vector<uint8_t>& chk_data);

bool looks_like_zlib_stream(const a_vector<uint8_t>& data) {
	if (data.size() < 2) return false;
	if (data[0] != 0x78) return false;
	uint16_t hdr = (uint16_t)data[0] << 8 | data[1];
	if (hdr % 31 != 0) return false;
	if ((data[0] & 0x0f) != 8) return false;
	return true;
}

size_t file_remaining(data_loading::file_reader<>& file) {
	return file.size() - file.tell();
}

bool zlib_decompress_dynamic(const a_vector<uint8_t>& input, a_vector<uint8_t>& output, a_string& err) {
	const auto& z = get_zlib_api();
	if (!z.loaded) {
		err = z.error;
		return false;
	}

	z_stream_t zs{};
	zs.next_in = (unsigned char*)input.data();
	zs.avail_in = (unsigned int)input.size();
	if (z.inflate_init(&zs, z.zlib_version(), (int)sizeof(z_stream_t)) != z_ok) {
		err = "inflateInit failed";
		return false;
	}

	const size_t chunk_size = 64 * 1024;
	a_vector<uint8_t> tmp(chunk_size);
	output.clear();
	int ret = z_ok;
	while (ret == z_ok) {
		zs.next_out = tmp.data();
		zs.avail_out = (unsigned int)tmp.size();
		ret = z.inflate(&zs, z_no_flush);
		if (ret != z_ok && ret != z_stream_end) {
			err = format("inflate failed with code %d", ret);
			z.inflate_end(&zs);
			output.clear();
			return false;
		}
		size_t produced = tmp.size() - zs.avail_out;
		output.insert(output.end(), tmp.begin(), tmp.begin() + produced);
	}
	z.inflate_end(&zs);
	return ret == z_stream_end;
}

bool read_scr_stream_block(data_loading::file_reader<>& file, a_vector<uint8_t>& output, uint32_t& crc, uint32_t& segments, a_string& err) {
	if (file_remaining(file) < 8) {
		err = "unexpected end of replay while reading block header";
		return false;
	}
	crc = file.get<uint32_t>();
	segments = file.get<uint32_t>();
	if (segments == 0 || segments > 10000) {
		err = format("invalid segment count %u", segments);
		return false;
	}

	output.clear();
	for (size_t i = 0; i != segments; ++i) {
		if (file_remaining(file) < 4) {
			err = format("unexpected end while reading segment %u size", (unsigned int)i);
			return false;
		}
		size_t compressed_size = file.get<uint32_t>();
		if (compressed_size > file_remaining(file)) {
			err = format("segment %u size %u exceeds remaining bytes %u", (unsigned int)i, (unsigned int)compressed_size, (unsigned int)file_remaining(file));
			return false;
		}

		a_vector<uint8_t> compressed;
		compressed.resize(compressed_size);
		if (!compressed.empty()) file.get_bytes(compressed.data(), compressed.size());

		a_vector<uint8_t> decompressed;
		if (looks_like_zlib_stream(compressed)) {
			a_string zerr;
			if (!zlib_decompress_dynamic(compressed, decompressed, zerr)) {
				err = format("segment %u zlib decompress failed: %s", (unsigned int)i, zerr);
				return false;
			}
		} else {
			decompressed = std::move(compressed);
		}

		output.insert(output.end(), decompressed.begin(), decompressed.end());
	}
	return true;
}

bool try_load_chk_from_scr_stream_replay(const std::string& path, a_vector<uint8_t>& chk_data, uint32_t& replay_identifier, a_string& parse_error, a_string& parse_mode) {
	try {
		data_loading::file_reader<> file(path.c_str());
		a_vector<uint8_t> block_out;
		uint32_t crc = 0;
		uint32_t segments = 0;
		a_string err;

		if (!read_scr_stream_block(file, block_out, crc, segments, err)) {
			parse_error = format("id block parse failed: %s", err);
			return false;
		}
		if (block_out.size() < 4) {
			parse_error = format("id block too small (%u bytes)", (unsigned int)block_out.size());
			return false;
		}
		replay_identifier = data_loading::value_at<uint32_t, true>(block_out.data());
		if (replay_identifier != 0x53526573) {
			parse_error = format("not an SCR stream replay identifier (%#x)", replay_identifier);
			return false;
		}

		uint32_t prefix_size = 0;
		if (file_remaining(file) >= 4) {
			prefix_size = file.get<uint32_t>();
		}

		if (!read_scr_stream_block(file, block_out, crc, segments, err)) {
			parse_error = format("game_info block parse failed: %s", err);
			return false;
		}

		if (!read_scr_stream_block(file, block_out, crc, segments, err)) {
			parse_error = format("actions_size block parse failed: %s", err);
			return false;
		}
		if (block_out.size() < 4) {
			parse_error = "actions_size block output too small";
			return false;
		}
		size_t actions_size = data_loading::value_at<uint32_t, true>(block_out.data());
		if (actions_size > 256 * 1024 * 1024) {
			parse_error = format("unreasonable actions_size %u", (unsigned int)actions_size);
			return false;
		}

		if (!read_scr_stream_block(file, block_out, crc, segments, err)) {
			parse_error = format("actions block parse failed: %s", err);
			return false;
		}
		if (block_out.size() < actions_size) {
			parse_error = format("actions block too small: have %u expected %u", (unsigned int)block_out.size(), (unsigned int)actions_size);
			return false;
		}

		if (!read_scr_stream_block(file, block_out, crc, segments, err)) {
			parse_error = format("map_size block parse failed: %s", err);
			return false;
		}
		if (block_out.size() < 4) {
			parse_error = "map_size block output too small";
			return false;
		}
		size_t map_size = data_loading::value_at<uint32_t, true>(block_out.data());
		if (map_size == 0 || map_size > 256 * 1024 * 1024) {
			parse_error = format("unreasonable map_size %u", (unsigned int)map_size);
			return false;
		}

		if (!read_scr_stream_block(file, block_out, crc, segments, err)) {
			parse_error = format("map_data block parse failed: %s", err);
			return false;
		}
		if (block_out.size() < map_size) {
			parse_error = format("map_data block too small: have %u expected %u", (unsigned int)block_out.size(), (unsigned int)map_size);
			return false;
		}

		chk_data.assign(block_out.begin(), block_out.begin() + map_size);
		if (!is_likely_chk(chk_data)) {
			parse_error = "SCR stream extracted map payload is not a valid CHK stream";
			chk_data.clear();
			return false;
		}

		parse_mode = format("scr_stream_zlib(prefix=%u)", prefix_size);
		return true;
	} catch (const exception& e) {
		parse_error = e.what();
		chk_data.clear();
		return false;
	}
}

bool is_likely_chk(const a_vector<uint8_t>& chk_data) {
	if (chk_data.size() < 8) return false;
	bool has_ver = false;
	data_loading::data_reader_le r(chk_data.data(), chk_data.data() + chk_data.size());
	while (r.left() >= 8) {
		auto tag = r.get<std::array<char, 4>>();
		uint32_t len = r.get<uint32_t>();
		if (len > r.left()) return false;
		if (tag == std::array<char, 4>{{'V', 'E', 'R', ' '}}) has_ver = true;
		r.skip(len);
	}
	return has_ver;
}

bool try_load_chk_from_replay_file(const std::string& path, const replay_extract_layout_t& layout, a_vector<uint8_t>& chk_data, uint32_t& replay_identifier, a_string& parse_error) {
	try {
		data_loading::file_reader<> file(path.c_str());
		auto replay_reader = data_loading::make_replay_file_reader(file);

		replay_identifier = replay_reader.get<uint32_t>();
		if (!is_valid_replay_identifier(replay_identifier)) {
			error("replay: invalid identifier %#x", replay_identifier);
		}

		for (size_t i = 0; i != layout.skip_u32_count; ++i) {
			(void)replay_reader.get<uint32_t>();
		}

		if (layout.size_prefixed_game_info) {
			size_t game_info_size = replay_reader.get<uint32_t>();
			if (game_info_size == 0 || game_info_size > 16 * 1024) {
				error("replay: unreasonable game info size %u", (unsigned int)game_info_size);
			}
			a_vector<uint8_t> game_info_buffer;
			game_info_buffer.resize(game_info_size);
			replay_reader.get_bytes(game_info_buffer.data(), game_info_buffer.size());
		} else {
			if (layout.fixed_game_info_size == 0 || layout.fixed_game_info_size > 16 * 1024) {
				error("replay: invalid fixed game info size %u", (unsigned int)layout.fixed_game_info_size);
			}
			a_vector<uint8_t> game_info_buffer;
			game_info_buffer.resize(layout.fixed_game_info_size);
			replay_reader.get_bytes(game_info_buffer.data(), game_info_buffer.size());
		}

		size_t actions_size = replay_reader.get<uint32_t>();
		if (actions_size > 256 * 1024 * 1024) error("replay: unreasonable actions size %u", (unsigned int)actions_size);
		a_vector<uint8_t> actions;
		actions.resize(actions_size);
		if (!actions.empty()) replay_reader.get_bytes(actions.data(), actions.size());

		size_t map_size = replay_reader.get<uint32_t>();
		if (map_size == 0 || map_size > 128 * 1024 * 1024) error("replay: unreasonable map size %u", (unsigned int)map_size);
		chk_data.resize(map_size);
		replay_reader.get_bytes(chk_data.data(), chk_data.size());
		if (!is_likely_chk(chk_data)) {
			error("replay: extracted map payload is not a valid CHK stream");
		}
		return true;
	} catch (const exception& e) {
		parse_error = e.what();
		chk_data.clear();
		return false;
	}
}

bool load_chk_from_replay_file(const std::string& path, a_vector<uint8_t>& chk_data, uint32_t& replay_identifier, a_string& parse_error, a_string& parse_mode) {
	const std::array<replay_extract_layout_t, 18> layouts = {{
		{"fixed_633", 0, false, 633},
		{"fixed_633_skip1", 1, false, 633},
		{"fixed_633_skip2", 2, false, 633},
		{"fixed_640", 0, false, 640},
		{"fixed_640_skip1", 1, false, 640},
		{"fixed_640_skip2", 2, false, 640},
		{"fixed_1024", 0, false, 1024},
		{"fixed_1024_skip1", 1, false, 1024},
		{"fixed_1024_skip2", 2, false, 1024},
		{"fixed_1536", 0, false, 1536},
		{"fixed_1536_skip1", 1, false, 1536},
		{"fixed_1536_skip2", 2, false, 1536},
		{"fixed_2048", 0, false, 2048},
		{"fixed_2048_skip1", 1, false, 2048},
		{"fixed_2048_skip2", 2, false, 2048},
		{"size_prefixed", 0, true, 0},
		{"size_prefixed_skip1", 1, true, 0},
		{"size_prefixed_skip2", 2, true, 0},
	}};

	a_vector<a_string> errors;
	errors.reserve(layouts.size() + 1);
	uint32_t identifier_any = 0;

	a_string scr_err;
	a_string scr_mode;
	uint32_t scr_id = 0;
	if (try_load_chk_from_scr_stream_replay(path, chk_data, scr_id, scr_err, scr_mode)) {
		replay_identifier = scr_id;
		parse_mode = scr_mode;
		return true;
	}
	if (scr_id) identifier_any = scr_id;
	errors.push_back(format("scr_stream: %s", scr_err));

	for (const auto& layout : layouts) {
		a_string err;
		uint32_t id = 0;
		if (try_load_chk_from_replay_file(path, layout, chk_data, id, err)) {
			replay_identifier = id;
			parse_mode = layout.mode;
			return true;
		}
		if (id) identifier_any = id;
		errors.push_back(format("%s: %s", layout.mode, err));
	}

	replay_identifier = identifier_any;
	parse_mode = "none";
	parse_error = "all replay extraction layouts failed";
	for (size_t i = 0; i != errors.size() && i < 4; ++i) {
		parse_error += format("; %s", errors[i]);
	}
	return false;
}

uint16_t parse_chk_version(const a_vector<uint8_t>& chk_data) {
	if (chk_data.size() < 8) error("CHK data is too small (%u bytes)", (unsigned int)chk_data.size());
	data_loading::data_reader_le r(chk_data.data(), chk_data.data() + chk_data.size());
	while (r.left() >= 8) {
		auto tag = r.get<std::array<char, 4>>();
		uint32_t len = r.get<uint32_t>();
		if (len > r.left()) error("CHK chunk length %u exceeds remaining bytes %u", len, (unsigned int)r.left());
		if (tag == std::array<char, 4>{{'V', 'E', 'R', ' '}}) {
			if (len < 2) error("CHK VER chunk size %u is invalid", len);
			auto v = data_loading::data_reader_le(r.ptr, r.ptr + len).get<uint16_t>();
			return v;
		}
		r.skip(len);
	}
	error("CHK is missing required VER chunk");
	return 0;
}

chk_tile_heuristics_t parse_chk_tile_heuristics(const a_vector<uint8_t>& chk_data) {
	chk_tile_heuristics_t r;
	data_loading::data_reader_le dr(chk_data.data(), chk_data.data() + chk_data.size());
	while (dr.left() >= 8) {
		auto tag = dr.get<std::array<char, 4>>();
		uint32_t len = dr.get<uint32_t>();
		if (len > dr.left()) error("CHK chunk length %u exceeds remaining bytes %u", len, (unsigned int)dr.left());

		if (tag == std::array<char, 4>{{'E', 'R', 'A', ' '}}) {
			if (len >= 2) {
				auto tr = data_loading::data_reader_le(dr.ptr, dr.ptr + len);
				r.tileset_index = tr.get<uint16_t>() % 8;
				r.has_era = true;
				r.classic_max_group_index = classic_max_group_by_tileset[r.tileset_index];
			}
		} else if (tag == std::array<char, 4>{{'M', 'T', 'X', 'M'}}) {
			r.has_mtxm = true;
			auto tr = data_loading::data_reader_le(dr.ptr, dr.ptr + len);
			while (tr.left() >= 2) {
				uint16_t raw = tr.get<uint16_t>();
				size_t group_index = (raw >> 4) & 0x7ff;
				if (group_index > r.max_group_index) r.max_group_index = group_index;
			}
		}

		dr.skip(len);
	}

	if (r.has_era && r.has_mtxm) {
		r.uses_new_tiles = r.max_group_index > r.classic_max_group_index;
	}
	return r;
}

const char* era_name(const map_version_semantics_t& semantics) {
	if (!semantics.supported) return "unsupported";
	if (semantics.requires_scr_semantic_pack) return "remastered";
	return "classic";
}

const char* era_name_from_replay_identifier(uint32_t replay_identifier) {
	if (replay_identifier == 0x53526573) return "remastered";
	if (replay_identifier == 0x53526572) return "classic";
	return "unsupported";
}

const char* effective_era_name(const char* version_era, bool has_heuristics, bool uses_new_tiles) {
	if (has_heuristics && uses_new_tiles && std::string(version_era) == "classic") return "remastered";
	return version_era;
}

int usage() {
	std::cerr << "Usage: openbw_asset_classify <path> [--json]\n";
	std::cerr << "Classifies map/replay assets using CHK VER policy.\n";
	return 2;
}

}

int main(int argc, char** argv) {
	try {
		if (argc < 2) return usage();

		std::string path;
		bool json = false;
		for (int i = 1; i < argc; ++i) {
			std::string arg = argv[i];
			if (arg == "--json") json = true;
			else if (arg == "-h" || arg == "--help") return usage();
			else if (!path.empty()) return usage();
			else path = std::move(arg);
		}
		if (path.empty()) return usage();

		input_kind_t kind = detect_input_kind(path);
		if (kind == input_kind_t::unknown) {
			error("unsupported file extension for '%s' (expected .rep, .scm, .scx, or .chk)", path.c_str());
		}

		a_vector<uint8_t> chk_data;
		uint16_t version = std::numeric_limits<uint16_t>::max();
		bool has_map_version = false;
		const char* version_era = "unsupported";
		const char* effective_era = "unsupported";
		a_string note;
		chk_tile_heuristics_t heuristics;
		bool has_heuristics = false;

		if (kind == input_kind_t::replay) {
			uint32_t replay_identifier = 0;
			a_string replay_parse_error;
			a_string replay_parse_mode;
			bool extracted_chk = load_chk_from_replay_file(path, chk_data, replay_identifier, replay_parse_error, replay_parse_mode);
			if (extracted_chk) {
				version = parse_chk_version(chk_data);
				has_map_version = true;
				heuristics = parse_chk_tile_heuristics(chk_data);
				has_heuristics = heuristics.has_era && heuristics.has_mtxm;
				auto semantics = map_version_semantics(version);
				version_era = era_name(semantics);
				note = format("replay identifier %#x, classification by CHK VER (%s)", replay_identifier, replay_parse_mode);
			} else {
				if (replay_identifier == 0) {
					error("replay map extraction failed before identifier decode: %s", replay_parse_error);
				}
				version_era = era_name_from_replay_identifier(replay_identifier);
				note = format("replay map extraction failed (%s); fallback classification by replay identifier %#x", replay_parse_error, replay_identifier);
			}
		} else if (kind == input_kind_t::map) load_chk_from_map_file(path, chk_data);
		else load_chk_from_chk_file(path, chk_data);

		if (kind != input_kind_t::replay) {
			version = parse_chk_version(chk_data);
			has_map_version = true;
			heuristics = parse_chk_tile_heuristics(chk_data);
			has_heuristics = heuristics.has_era && heuristics.has_mtxm;
			auto semantics = map_version_semantics(version);
			version_era = era_name(semantics);
			note = "classification by CHK VER";
		}

		if (has_heuristics) {
			note += format("; heuristic max_group_index=%u classic_max=%u uses_new_tiles=%s",
				(unsigned int)heuristics.max_group_index,
				(unsigned int)heuristics.classic_max_group_index,
				heuristics.uses_new_tiles ? "true" : "false");
		}

		effective_era = effective_era_name(version_era, has_heuristics, has_heuristics && heuristics.uses_new_tiles);

		if (json) {
			std::cout << "{";
			std::cout << "\"path\":\"" << json_escape(path) << "\",";
			std::cout << "\"file_type\":\"" << input_kind_name(kind) << "\",";
			std::cout << "\"map_version\":";
			if (has_map_version) std::cout << version;
			else std::cout << "null";
			std::cout << ",";
			std::cout << "\"version_classification\":\"" << version_era << "\",";
			std::cout << "\"effective_classification\":\"" << effective_era << "\",";
			std::cout << "\"classification\":\"" << effective_era << "\",";
			std::cout << "\"requires_scr_semantic_pack\":" << (std::string(effective_era) == "remastered" ? "true" : "false") << ",";
			std::cout << "\"tileset_index\":";
			if (has_heuristics) std::cout << heuristics.tileset_index;
			else std::cout << "null";
			std::cout << ",";
			std::cout << "\"max_group_index\":";
			if (has_heuristics) std::cout << heuristics.max_group_index;
			else std::cout << "null";
			std::cout << ",";
			std::cout << "\"classic_max_group_index\":";
			if (has_heuristics) std::cout << heuristics.classic_max_group_index;
			else std::cout << "null";
			std::cout << ",";
			std::cout << "\"uses_new_tiles\":";
			if (has_heuristics) std::cout << (heuristics.uses_new_tiles ? "true" : "false");
			else std::cout << "null";
			std::cout << ",";
			std::cout << "\"note\":\"" << json_escape(note) << "\"";
			std::cout << "}" << std::endl;
		} else {
			std::cout << "path: " << path << "\n";
			std::cout << "file_type: " << input_kind_name(kind) << "\n";
			if (has_map_version) std::cout << "map_version: " << version << "\n";
			else std::cout << "map_version: unknown\n";
			std::cout << "version_classification: " << version_era << "\n";
			std::cout << "classification: " << effective_era << "\n";
			std::cout << "requires_scr_semantic_pack: " << (std::string(effective_era) == "remastered" ? "yes" : "no") << "\n";
			if (has_heuristics) {
				std::cout << "tileset_index: " << heuristics.tileset_index << "\n";
				std::cout << "max_group_index: " << heuristics.max_group_index << "\n";
				std::cout << "classic_max_group_index: " << heuristics.classic_max_group_index << "\n";
				std::cout << "uses_new_tiles: " << (heuristics.uses_new_tiles ? "yes" : "no") << "\n";
			} else {
				std::cout << "uses_new_tiles: unknown\n";
			}
			std::cout << "note: " << note << "\n";
		}

		return 0;
	} catch (const std::exception& e) {
		std::cerr << "error: " << e.what() << "\n";
		return 1;
	}
}
