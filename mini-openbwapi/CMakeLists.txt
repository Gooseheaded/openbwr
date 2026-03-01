cmake_minimum_required(VERSION 3.1)

option(OPENBW_ENABLE_UI "Enables support for the graphical user interface, requires SDL2")
option(OPENBW_ENABLE_TESTS "Enables contract tests for tile compatibility" ON)
option(OPENBW_ENABLE_TOOLS "Builds utility CLI tools" ON)
option(OPENBW_ENABLE_LOCAL_FIXTURES "Enables fixture-data tests that require local replay/map assets" OFF)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/../cmake")

if (OPENBW_ENABLE_UI)
	find_package(SDL2 REQUIRED)
endif()

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/..
	${SDL2_INCLUDE_DIR}
)

add_definitions(-DOPENBW_NO_SDL_IMAGE)
add_definitions(-DOPENBW_NO_SDL_MIXER)

set(mini-openbwapi_srcs openbwapi.cpp)

if (OPENBW_ENABLE_UI)
	add_definitions(-DOPENBW_ENABLE_UI)
	list(APPEND mini-openbwapi_srcs ../ui/sdl2.cpp)
endif()

add_library(mini-openbwapi ${mini-openbwapi_srcs})

target_link_libraries(mini-openbwapi
	${SDL2_LIBRARY}
)

if (OPENBW_ENABLE_TESTS)
	include(CTest)
	if (BUILD_TESTING)
		add_executable(openbw_contract_tests ../tests/scr_compat_contract_tests.cpp)
		target_include_directories(openbw_contract_tests PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}/..
		)

		set(openbw_contract_test_names
			contract.version.classic
			contract.version.scr
			contract.version.unsupported
			contract.scr_pack.missing
			contract.scr_pack.valid
			contract.indices.out_of_range
		)

		foreach(test_name ${openbw_contract_test_names})
			add_test(NAME ${test_name} COMMAND openbw_contract_tests ${test_name})
		endforeach()
	endif()
endif()

if (OPENBW_ENABLE_TOOLS)
	add_executable(openbw_asset_classify ../tools/openbw_asset_classify.cpp)
	target_include_directories(openbw_asset_classify PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/..
	)

	if (OPENBW_ENABLE_TESTS AND BUILD_TESTING)
		if (OPENBW_ENABLE_LOCAL_FIXTURES)
			set(openbw_negative_fixtures_root "${CMAKE_CURRENT_SOURCE_DIR}/../tests/testdata/negative")
			set(openbw_fixtures_root "${CMAKE_CURRENT_SOURCE_DIR}/../tests/testdata")

			if (EXISTS "${openbw_negative_fixtures_root}/map/negative_map_001.scx")
				add_test(NAME classifier.negative.map_001 COMMAND openbw_asset_classify "${openbw_negative_fixtures_root}/map/negative_map_001.scx" --json)
				set_tests_properties(classifier.negative.map_001 PROPERTIES WILL_FAIL TRUE)
			endif()
			if (EXISTS "${openbw_negative_fixtures_root}/map/negative_map_002.scx")
				add_test(NAME classifier.negative.map_002 COMMAND openbw_asset_classify "${openbw_negative_fixtures_root}/map/negative_map_002.scx" --json)
				set_tests_properties(classifier.negative.map_002 PROPERTIES WILL_FAIL TRUE)
			endif()
			if (EXISTS "${openbw_negative_fixtures_root}/map/negative_map_003.scx")
				add_test(NAME classifier.negative.map_003 COMMAND openbw_asset_classify "${openbw_negative_fixtures_root}/map/negative_map_003.scx" --json)
				set_tests_properties(classifier.negative.map_003 PROPERTIES WILL_FAIL TRUE)
			endif()
			if (EXISTS "${openbw_negative_fixtures_root}/map/negative_map_004.scx")
				add_test(NAME classifier.negative.map_004 COMMAND openbw_asset_classify "${openbw_negative_fixtures_root}/map/negative_map_004.scx" --json)
				set_tests_properties(classifier.negative.map_004 PROPERTIES WILL_FAIL TRUE)
			endif()
			if (EXISTS "${openbw_negative_fixtures_root}/map/negative_map_005.scm")
				add_test(NAME classifier.negative.map_005 COMMAND openbw_asset_classify "${openbw_negative_fixtures_root}/map/negative_map_005.scm" --json)
				set_tests_properties(classifier.negative.map_005 PROPERTIES WILL_FAIL TRUE)
			endif()
			if (EXISTS "${openbw_negative_fixtures_root}/replay/negative_replay_001.rep")
				add_test(NAME classifier.negative.replay_001 COMMAND openbw_asset_classify "${openbw_negative_fixtures_root}/replay/negative_replay_001.rep" --json)
				set_tests_properties(classifier.negative.replay_001 PROPERTIES WILL_FAIL TRUE)
			endif()

			find_package(Python3 COMPONENTS Interpreter)
			if (Python3_Interpreter_FOUND AND EXISTS "${openbw_fixtures_root}/maps/manifest.csv" AND EXISTS "${openbw_fixtures_root}/replays/manifest.csv" AND EXISTS "${openbw_fixtures_root}/negative/manifest.csv")
				add_test(
					NAME classifier.fixtures.validate
					COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/../tools/validate_scr_fixture_inventory.py"
						--repo-root "${CMAKE_CURRENT_SOURCE_DIR}/.."
						--classifier "${CMAKE_CURRENT_BINARY_DIR}/openbw_asset_classify${CMAKE_EXECUTABLE_SUFFIX}"
				)
				if (EXISTS "${openbw_fixtures_root}/maps/snapshot.sha256")
					add_test(
						NAME snapshot.maps.digest
						COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/../tools/validate_map_snapshot.py"
							--repo-root "${CMAKE_CURRENT_SOURCE_DIR}/.."
							--classifier "${CMAKE_CURRENT_BINARY_DIR}/openbw_asset_classify${CMAKE_EXECUTABLE_SUFFIX}"
					)
				endif()
				if (EXISTS "${openbw_fixtures_root}/replays/smoke.sha256")
					add_test(
						NAME smoke.replay.determinism
						COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/../tools/replay_determinism_smoke.py"
							--repo-root "${CMAKE_CURRENT_SOURCE_DIR}/.."
							--classifier "${CMAKE_CURRENT_BINARY_DIR}/openbw_asset_classify${CMAKE_EXECUTABLE_SUFFIX}"
					)
				endif()
			endif()
		endif()
	endif()
endif()
