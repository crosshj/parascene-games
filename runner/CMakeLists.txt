cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
	if(DEFINED ENV{PSN00BSDK})
		set(CMAKE_TOOLCHAIN_FILE "$ENV{PSN00BSDK}/lib/libpsn00b/cmake/sdk.cmake" CACHE FILEPATH "" FORCE)
	else()
		set(CMAKE_TOOLCHAIN_FILE "$ENV{HOME}/psn00bsdk/lib/libpsn00b/cmake/sdk.cmake" CACHE FILEPATH "" FORCE)
	endif()
endif()

project(runner LANGUAGES C VERSION 0.1.0 DESCRIPTION "A flat runner on a 3D road")

find_program(FFMPEG ffmpeg REQUIRED)
find_program(FFPROBE ffprobe REQUIRED)
find_program(PYTHON3 python3 REQUIRED)

# CDDA (Red Book) is the default. XA is optional: smaller disc, PS1-ish quality.
# Every non-profile build also writes runner-nomusic and runner-web.
option(RUNNER_XA_MUSIC "Encode music as XA ADPCM instead of CDDA" OFF)

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

file(GLOB AUDIO_MP3S CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/audio/*.mp3")
list(SORT AUDIO_MP3S)
list(LENGTH AUDIO_MP3S MUSIC_TRACK_COUNT)
if(MUSIC_TRACK_COUNT EQUAL 0)
	message(FATAL_ERROR "runner/audio needs at least one .mp3")
endif()
if(MUSIC_TRACK_COUNT GREATER 32)
	message(FATAL_ERROR "runner/audio has ${MUSIC_TRACK_COUNT} tracks; max is 32")
endif()

if(RUNNER_XA_MUSIC)
	find_program(CMAKE_C_COMPILER_HOST NAMES cc clang gcc REQUIRED)
	set(WAV2XA "${CMAKE_CURRENT_BINARY_DIR}/wav2xa")
	add_custom_command(
		OUTPUT "${WAV2XA}"
		COMMAND ${CMAKE_C_COMPILER_HOST} -O2 -std=c99
			-I"${CMAKE_CURRENT_SOURCE_DIR}/tools"
			-o "${WAV2XA}"
			"${CMAKE_CURRENT_SOURCE_DIR}/tools/wav2xa.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/tools/libpsxav/adpcm.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/tools/libpsxav/cdrom.c"
		DEPENDS
			"${CMAKE_CURRENT_SOURCE_DIR}/tools/wav2xa.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/tools/libpsxav/adpcm.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/tools/libpsxav/cdrom.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/tools/libpsxav/libpsxav.h"
		VERBATIM
	)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/tools/write_music_iso.cmake")

set(MUSIC_DISC_DEPS "")
set(_music_i 0)
foreach(_mp3 ${AUDIO_MP3S})
	if(_music_i LESS 10)
		set(_music_id "0${_music_i}")
	else()
		set(_music_id "${_music_i}")
	endif()
	if(RUNNER_XA_MUSIC)
		set(_music_wav "${CMAKE_CURRENT_BINARY_DIR}/MUSIC${_music_id}.wav")
		set(_music_out "${CMAKE_CURRENT_BINARY_DIR}/MUSIC${_music_id}.xa")
		add_custom_command(
			OUTPUT "${_music_wav}"
			COMMAND ${FFMPEG} -y -i "${_mp3}"
				-ac 2 -ar 37800 -c:a pcm_s16le "${_music_wav}"
			DEPENDS "${_mp3}"
			VERBATIM
		)
		add_custom_command(
			OUTPUT "${_music_out}"
			COMMAND "${WAV2XA}" -f 37800 -i 1 -F 1 -C 0 "${_music_wav}" "${_music_out}"
			DEPENDS "${WAV2XA}" "${_music_wav}"
			VERBATIM
		)
	else()
		set(_music_out "${CMAKE_CURRENT_BINARY_DIR}/TRACK${_music_id}.wav")
		add_custom_command(
			OUTPUT "${_music_out}"
			COMMAND ${FFMPEG} -y -i "${_mp3}"
				-ac 2 -ar 44100 -c:a pcm_s16le "${_music_out}"
			DEPENDS "${_mp3}"
			VERBATIM
		)
	endif()
	list(APPEND MUSIC_DISC_DEPS "${_music_out}")
	math(EXPR _music_i "${_music_i} + 1")
endforeach()

set(TRACK_H "${CMAKE_CURRENT_SOURCE_DIR}/src/track_data.h")
set(TRACK_JSON "${CMAKE_CURRENT_SOURCE_DIR}/tools/trackview/baked.json")
add_custom_command(
	OUTPUT "${TRACK_H}" "${TRACK_JSON}"
	COMMAND "${PYTHON3}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/bake_tracks.py"
		--header "${TRACK_H}" --json "${TRACK_JSON}"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/bake_tracks.py"
		"${CMAKE_CURRENT_SOURCE_DIR}/tracks/catalog.json"
	VERBATIM
)

file(GLOB RUNNER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
psn00bsdk_add_executable(runner NOGPREL ${RUNNER_SOURCES})
target_include_directories(runner PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
# The road's hot path crosses path.c, road.c, and world.c; whole-program
# inlining takes about a sixth off the heaviest frames.
target_compile_options(runner PRIVATE -flto)
target_link_options(runner PRIVATE -flto -O2)

# A disc that runs each level with collisions off and prints frame costs
# to the TTY. Configure it in its own folder, such as runner/build/profile;
# it keeps its disc there instead of copying it to the repository root.
option(RUNNER_PROFILE "Build the frame-cost profiling disc" OFF)
if(RUNNER_PROFILE)
	target_compile_definitions(runner PRIVATE RUNNER_PROFILE)
endif()
if(RUNNER_XA_MUSIC)
	target_compile_definitions(runner PRIVATE RUNNER_XA_MUSIC)
endif()
set_source_files_properties(src/main.c PROPERTIES
	OBJECT_DEPENDS "${SPRITE_H};${SCREEN_H};${SOUND_H};${CMAKE_CURRENT_BINARY_DIR}/music_tracks.h")
set_source_files_properties(src/level_lumenwild.c PROPERTIES
	OBJECT_DEPENDS "${LUMEN_SKY_H}")
set_source_files_properties(src/path.c PROPERTIES
	OBJECT_DEPENDS "${TRACK_H}")
add_custom_target(sprite_sheet DEPENDS "${SPRITE_H}" "${SCREEN_H}" "${SOUND_H}" "${LUMEN_SKY_H}")
add_dependencies(runner sprite_sheet)

psn00bsdk_add_cd_image(
	runner_iso
	runner
	"${CMAKE_CURRENT_BINARY_DIR}/iso.xml"
	DEPENDS runner ${MUSIC_DISC_DEPS} ${CMAKE_CURRENT_SOURCE_DIR}/system.cnf
)

# The repo-root build folder receives the discs only. CMake's own files stay
# in the binary directory (runner/build). The profile disc stays in its folder.
get_filename_component(DISC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../build" ABSOLUTE)
if(NOT RUNNER_PROFILE)
	# Same sources, music playback compiled out. Part of the default build.
	psn00bsdk_add_executable(runner_nomusic NOGPREL ${RUNNER_SOURCES})
	target_include_directories(runner_nomusic PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
	target_compile_options(runner_nomusic PRIVATE -flto)
	target_link_options(runner_nomusic PRIVATE -flto -O2)
	target_compile_definitions(runner_nomusic PRIVATE RUNNER_NO_MUSIC)
	add_dependencies(runner_nomusic sprite_sheet)
	psn00bsdk_add_cd_image(
		runner_nomusic_iso
		runner-nomusic
		"${CMAKE_CURRENT_BINARY_DIR}/iso_nomusic.xml"
		DEPENDS runner_nomusic ${CMAKE_CURRENT_SOURCE_DIR}/system.cnf
	)
	add_dependencies(runner_iso runner_nomusic_iso)

	# Same sources. The page plays the mp3s; this disc only paints the command row.
	psn00bsdk_add_executable(runner_web NOGPREL ${RUNNER_SOURCES})
	target_include_directories(runner_web PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
	target_compile_options(runner_web PRIVATE -flto)
	target_link_options(runner_web PRIVATE -flto -O2)
	target_compile_definitions(runner_web PRIVATE RUNNER_WEB)
	add_dependencies(runner_web sprite_sheet)
	psn00bsdk_add_cd_image(
		runner_web_iso
		runner-web
		"${CMAKE_CURRENT_BINARY_DIR}/iso_web.xml"
		DEPENDS runner_web ${CMAKE_CURRENT_SOURCE_DIR}/system.cnf
	)
	add_dependencies(runner_iso runner_web_iso)

	add_custom_command(
		TARGET runner_iso POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${DISC_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_BINARY_DIR}/runner.bin" "${DISC_DIR}/runner.bin"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_BINARY_DIR}/runner.cue" "${DISC_DIR}/runner.cue"
		VERBATIM
	)
	add_custom_command(
		TARGET runner_nomusic_iso POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${DISC_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_BINARY_DIR}/runner-nomusic.bin" "${DISC_DIR}/runner-nomusic.bin"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_BINARY_DIR}/runner-nomusic.cue" "${DISC_DIR}/runner-nomusic.cue"
		VERBATIM
	)
	add_custom_command(
		TARGET runner_web_iso POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${DISC_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_BINARY_DIR}/runner-web.bin" "${DISC_DIR}/runner-web.bin"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_BINARY_DIR}/runner-web.cue" "${DISC_DIR}/runner-web.cue"
		VERBATIM
	)
endif()
