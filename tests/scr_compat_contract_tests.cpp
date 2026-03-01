#include "../scr_tile_compat.h"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace bwgame;

struct semantic_pack_data_t {
	std::array<a_vector<uint8_t>, 8> cv5;
	std::array<a_vector<uint8_t>, 8> vf4;
};

semantic_pack_data_t make_valid_scr_pack() {
	semantic_pack_data_t pack;
	for (size_t i = 0; i != scr_tileset_requirements.size(); ++i) {
		pack.cv5[i].resize(scr_tileset_requirements[i].min_cv5_entries * 52);
		pack.vf4[i].resize(scr_tileset_requirements[i].min_vf4_entries * 32);
	}
	return pack;
}

void require_true(bool condition, const char* message) {
	if (!condition) throw std::runtime_error(message);
}

void require_contains(const a_string& value, const char* needle) {
	if (value.find(needle) == a_string::npos) {
		throw std::runtime_error(format("expected '%s' to contain '%s'", value, needle).c_str());
	}
}

void test_version_policy_classic() {
	auto v59 = map_version_semantics(59);
	require_true(v59.supported, "59 should be supported");
	require_true(v59.classic_chunk_layout, "59 should use classic chunk layout");
	require_true(!v59.requires_scr_semantic_pack, "59 should not require SCR semantic pack");

	auto v205 = map_version_semantics(205);
	require_true(v205.supported, "205 should be supported");
	require_true(!v205.classic_chunk_layout, "205 should use broodwar-style chunk layout");
	require_true(!v205.requires_scr_semantic_pack, "205 should not require SCR semantic pack");
}

void test_version_policy_scr() {
	auto v64 = map_version_semantics(64);
	require_true(v64.supported, "64 should be supported");
	require_true(v64.requires_scr_semantic_pack, "64 should require SCR semantic pack");
	require_true(!v64.classic_chunk_layout, "64 should use broodwar-style chunk layout");

	auto v206 = map_version_semantics(206);
	require_true(v206.supported, "206 should be supported");
	require_true(v206.requires_scr_semantic_pack, "206 should require SCR semantic pack");
}

void test_version_policy_unsupported() {
	auto v58 = map_version_semantics(58);
	require_true(!v58.supported, "58 should be unsupported");

	auto v65 = map_version_semantics(65);
	require_true(!v65.supported, "65 should be unsupported");
}

void test_missing_scr_pack_error() {
	std::array<a_vector<uint8_t>, 8> cv5{};
	std::array<a_vector<uint8_t>, 8> vf4{};
	a_string err = check_map_version_compatibility(206, cv5, vf4);
	require_true(!err.empty(), "missing SCR semantic pack should fail for 206");
	require_contains(err, "SCR semantic pack is required");
	require_contains(err, "map version 206");
}

void test_valid_scr_pack_passes() {
	semantic_pack_data_t pack = make_valid_scr_pack();
	a_string err = check_map_version_compatibility(206, pack.cv5, pack.vf4);
	require_true(err.empty(), "valid SCR semantic pack should pass for 206");
}

void test_out_of_range_megatile_error() {
	a_vector<uint8_t> cv5(52);
	a_vector<uint8_t> vf4(32);

	// cv5 entry 0, subtile 0 -> megatile index 1 (out of range for vf4_entries=1)
	cv5[20] = 1;
	cv5[21] = 0;

	scr_tileset_requirement_t requirement{"unit-test", 1, 1};
	a_string err = validate_tileset_semantic_data(requirement, cv5, vf4);
	require_true(!err.empty(), "out-of-range megatile reference should fail validation");
	require_contains(err, "out of range");
}

struct test_case_t {
	const char* name;
	void (*fn)();
};

const std::array<test_case_t, 6> tests = {{
	{"contract.version.classic", &test_version_policy_classic},
	{"contract.version.scr", &test_version_policy_scr},
	{"contract.version.unsupported", &test_version_policy_unsupported},
	{"contract.scr_pack.missing", &test_missing_scr_pack_error},
	{"contract.scr_pack.valid", &test_valid_scr_pack_passes},
	{"contract.indices.out_of_range", &test_out_of_range_megatile_error},
}};

}

int main(int argc, char** argv) {
	std::vector<const test_case_t*> selected;
	if (argc <= 1) {
		for (const auto& test : tests) selected.push_back(&test);
	} else {
		for (const auto& test : tests) {
			if (std::string(test.name) == argv[1]) {
				selected.push_back(&test);
				break;
			}
		}
		if (selected.empty()) {
			std::cerr << "unknown test: " << argv[1] << "\n";
			std::cerr << "available tests:\n";
			for (const auto& test : tests) std::cerr << "  " << test.name << "\n";
			return 2;
		}
	}

	for (const auto* test : selected) {
		try {
			test->fn();
			std::cout << "PASS " << test->name << "\n";
		} catch (const std::exception& e) {
			std::cerr << "FAIL " << test->name << ": " << e.what() << "\n";
			return 1;
		}
	}

	return 0;
}
