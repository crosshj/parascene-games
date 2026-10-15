cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
	if(DEFINED ENV{PSN00BSDK})
		set(CMAKE_TOOLCHAIN_FILE "$ENV{PSN00BSDK}/lib/libpsn00b/cmake/sdk.cmake" CACHE FILEPATH "" FORCE)
	else()
		set(CMAKE_TOOLCHAIN_FILE "$ENV{HOME}/psn00bsdk/lib/libpsn00b/cmake/sdk.cmake" CACHE FILEPATH "" FORCE)
	endif()
endif()

project(runner LANGUAGES C VERSION 0.1.0 DESCRIPTION "A flat runner on a 3D road")

find_program(AFCONVERT afconvert REQUIRED)
find_program(PYTHON3 python3 REQUIRED)

file(GLOB SPRITE_PNGS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/sprites/*.png")
file(GLOB SPRITE_TXTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/sprites/*.txt")
if(NOT SPRITE_PNGS OR NOT SPRITE_TXTS)
	message(FATAL_ERROR "runner/sprites needs a base PNG, a same-named text file, and a character named name.base.png")
endif()

set(SPRITE_H "${CMAKE_CURRENT_BINARY_DIR}/sprite_sheet.h")
add_custom_command(
	OUTPUT "${SPRITE_H}"
	COMMAND "${PYTHON3}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_sprites.py"
		"${CMAKE_CURRENT_SOURCE_DIR}/sprites" "${SPRITE_H}"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_sprites.py" ${SPRITE_PNGS} ${SPRITE_TXTS}
	VERBATIM
)

set(SCREEN_H "${CMAKE_CURRENT_BINARY_DIR}/screen_art.h")
add_custom_command(
	OUTPUT "${SCREEN_H}"
	COMMAND "${PYTHON3}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_screens.py"
		"${CMAKE_CURRENT_SOURCE_DIR}/screens" "${SCREEN_H}"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_screens.py"
		"${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_sprites.py"
		"${CMAKE_CURRENT_SOURCE_DIR}/screens/parascene.png"
		"${CMAKE_CURRENT_SOURCE_DIR}/screens/title.png"
	VERBATIM
)

set(SOUND_H "${CMAKE_CURRENT_BINARY_DIR}/sound_bank.h")
add_custom_command(
	OUTPUT "${SOUND_H}"
	COMMAND "${PYTHON3}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_sounds.py" "${SOUND_H}"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_sounds.py"
	VERBATIM
)

set(LUMEN_SKY_H "${CMAKE_CURRENT_BINARY_DIR}/lumen_sky.h")
add_custom_command(
	OUTPUT "${LUMEN_SKY_H}"
	COMMAND "${PYTHON3}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_lumen_sky.py" "${LUMEN_SKY_H}"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_lumen_sky.py"
		"${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_sprites.py"
	VERBATIM
)

set(TRACK_WAV ${CMAKE_CURRENT_BINARY_DIR}/track.wav)
add_custom_command(
	OUTPUT ${TRACK_WAV}
	COMMAND ${AFCONVERT} -f WAVE -d LEI16@44100 -c 2
		${CMAKE_CURRENT_SOURCE_DIR}/audio/runner1.mp3 ${TRACK_WAV}
	DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/audio/runner1.mp3
	VERBATIM
)

file(GLOB RUNNER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
psn00bsdk_add_executable(runner NOGPREL ${RUNNER_SOURCES})
target_include_directories(runner PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# A disc that runs each level with collisions off and prints frame costs
# to the TTY. Configure it in its own folder, such as runner/build/profile;
# it keeps its disc there instead of copying it to the repository root.
option(RUNNER_PROFILE "Build the frame-cost profiling disc" OFF)
if(RUNNER_PROFILE)
	target_compile_definitions(runner PRIVATE RUNNER_PROFILE)
endif()
set_source_files_properties(src/main.c PROPERTIES
	OBJECT_DEPENDS "${SPRITE_H};${SCREEN_H};${SOUND_H}")
set_source_files_properties(src/level_lumenwild.c PROPERTIES
	OBJECT_DEPENDS "${LUMEN_SKY_H}")
add_custom_target(sprite_sheet DEPENDS "${SPRITE_H}" "${SCREEN_H}" "${SOUND_H}" "${LUMEN_SKY_H}")
add_dependencies(runner sprite_sheet)

psn00bsdk_add_cd_image(
	runner_iso
	runner
	iso.xml
	DEPENDS runner ${TRACK_WAV} ${CMAKE_CURRENT_SOURCE_DIR}/system.cnf
)

# The repo-root build folder receives the disc only. CMake's own files stay
# in the binary directory (runner/build).
get_filename_component(DISC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../build" ABSOLUTE)
if(NOT RUNNER_PROFILE)
add_custom_command(
	TARGET runner_iso POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${DISC_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_CURRENT_BINARY_DIR}/runner.bin" "${DISC_DIR}/runner.bin"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_CURRENT_BINARY_DIR}/runner.cue" "${DISC_DIR}/runner.cue"
	VERBATIM
)
endif()
