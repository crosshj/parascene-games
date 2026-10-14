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

set(TRACK_WAV ${CMAKE_CURRENT_BINARY_DIR}/track.wav)
add_custom_command(
	OUTPUT ${TRACK_WAV}
	COMMAND ${AFCONVERT} -f WAVE -d LEI16@44100 -c 2
		${CMAKE_CURRENT_SOURCE_DIR}/runner1.mp3 ${TRACK_WAV}
	DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/runner1.mp3
	VERBATIM
)

psn00bsdk_add_executable(runner NOGPREL src/main.c)
target_include_directories(runner PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
set_source_files_properties(src/main.c PROPERTIES OBJECT_DEPENDS "${SPRITE_H}")
add_custom_target(sprite_sheet DEPENDS "${SPRITE_H}")
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
add_custom_command(
	TARGET runner_iso POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${DISC_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_CURRENT_BINARY_DIR}/runner.bin" "${DISC_DIR}/runner.bin"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_CURRENT_BINARY_DIR}/runner.cue" "${DISC_DIR}/runner.cue"
	VERBATIM
)
