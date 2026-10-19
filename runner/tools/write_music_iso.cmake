# Writes music_tracks.h and iso.xml for the current AUDIO_MP3S list.
# Expected: AUDIO_MP3S, MUSIC_TRACK_COUNT, RUNNER_XA_MUSIC,
# CMAKE_CURRENT_SOURCE_DIR, CMAKE_CURRENT_BINARY_DIR, PROJECT_SOURCE_DIR
# FFPROBE and PYTHON3 are required. PYTHON3 rounds the web-disc frame counts.

set(_hdr "${CMAKE_CURRENT_BINARY_DIR}/music_tracks.h")
set(_iso "${CMAKE_CURRENT_BINARY_DIR}/iso.xml")

file(WRITE "${_hdr}"
"/* Generated from audio/*.mp3 — do not edit. */
#ifndef MUSIC_TRACKS_H
#define MUSIC_TRACKS_H

#define MUSIC_TRACK_COUNT ${MUSIC_TRACK_COUNT}
")

if(NOT RUNNER_XA_MUSIC)
	file(APPEND "${_hdr}" "\n/* Approx 60Hz frame counts per CDDA track (duration + short pregap). */\n")
	file(APPEND "${_hdr}" "static const int music_cd_frames[MUSIC_TRACK_COUNT] __attribute__((unused)) = {\n")
	foreach(_mp3 ${AUDIO_MP3S})
		execute_process(
			COMMAND "${FFPROBE}" -v quiet -show_entries format=duration -of csv=p=0 "${_mp3}"
			OUTPUT_VARIABLE _dur
			OUTPUT_STRIP_TRAILING_WHITESPACE
			COMMAND_ERROR_IS_FATAL ANY
		)
		# Whole seconds only; +50 matches the old single-track pregap fudge.
		string(REGEX REPLACE "\\..*" "" _secs "${_dur}")
		if(_secs STREQUAL "")
			set(_secs 0)
		endif()
		math(EXPR _frames "${_secs} * 60 + 50")
		file(APPEND "${_hdr}" "\t${_frames},\n")
	endforeach()
	file(APPEND "${_hdr}" "};\n")
endif()

file(APPEND "${_hdr}" "\n/* Web disc clock: round(mp3 seconds * 60) frames at 60 Hz. */\n")
file(APPEND "${_hdr}" "static const int music_web_frames[MUSIC_TRACK_COUNT] __attribute__((unused)) = {\n")
foreach(_mp3 ${AUDIO_MP3S})
	execute_process(
		COMMAND "${FFPROBE}" -v quiet -show_entries format=duration -of csv=p=0 "${_mp3}"
		OUTPUT_VARIABLE _dur
		OUTPUT_STRIP_TRAILING_WHITESPACE
		COMMAND_ERROR_IS_FATAL ANY
	)
	execute_process(
		COMMAND "${PYTHON3}" -c "import math,sys; print(math.floor(float(sys.argv[1]) * 60 + 0.5))" "${_dur}"
		OUTPUT_VARIABLE _web_frames
		OUTPUT_STRIP_TRAILING_WHITESPACE
		COMMAND_ERROR_IS_FATAL ANY
	)
	file(APPEND "${_hdr}" "\t${_web_frames},\n")
endforeach()
file(APPEND "${_hdr}" "};\n")

file(APPEND "${_hdr}" "\n#endif\n")

file(WRITE "${_iso}" "<?xml version=\"1.0\" encoding=\"utf-8\"?>
<iso_project>
	<track type=\"data\">
		<identifiers
			system			=\"PLAYSTATION\"
			volume			=\"RUNNER\"
			volume_set		=\"RUNNER\"
			publisher		=\"PARASCENE\"
			data_preparer	=\"PSN00BSDK\"
			application		=\"PLAYSTATION\"
		/>
		<directory_tree>
			<file name=\"SYSTEM.CNF\" type=\"data\" source=\"${PROJECT_SOURCE_DIR}/system.cnf\" />
			<file name=\"runner.exe\" type=\"data\" source=\"runner.exe\" />
")

if(RUNNER_XA_MUSIC)
	set(_i 0)
	while(_i LESS MUSIC_TRACK_COUNT)
		if(_i LESS 10)
			set(_id "0${_i}")
		else()
			set(_id "${_i}")
		endif()
		file(APPEND "${_iso}"
			"			<file name=\"MUSIC${_id}.XA\" type=\"mixed\" source=\"${CMAKE_CURRENT_BINARY_DIR}/MUSIC${_id}.xa\" />\n")
		math(EXPR _i "${_i} + 1")
	endwhile()
	file(APPEND "${_iso}"
"			<!-- Padding so the XA end callback sees non-audio sectors. -->
			<dummy sectors=\"1024\"/>
		</directory_tree>
	</track>
</iso_project>
")
else()
	file(APPEND "${_iso}"
"		</directory_tree>
	</track>
")
	set(_i 0)
	while(_i LESS MUSIC_TRACK_COUNT)
		if(_i LESS 10)
			set(_id "0${_i}")
		else()
			set(_id "${_i}")
		endif()
		file(APPEND "${_iso}"
			"	<track type=\"audio\" source=\"${CMAKE_CURRENT_BINARY_DIR}/TRACK${_id}.wav\" />\n")
		math(EXPR _i "${_i} + 1")
	endwhile()
	file(APPEND "${_iso}" "</iso_project>\n")
endif()

# Same executable name on the disc. The file on disk is the no-music build.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/iso_nomusic.xml" "<?xml version=\"1.0\" encoding=\"utf-8\"?>
<iso_project>
	<track type=\"data\">
		<identifiers
			system			=\"PLAYSTATION\"
			volume			=\"RUNNER\"
			volume_set		=\"RUNNER\"
			publisher		=\"PARASCENE\"
			data_preparer	=\"PSN00BSDK\"
			application		=\"PLAYSTATION\"
		/>
		<directory_tree>
			<file name=\"SYSTEM.CNF\" type=\"data\" source=\"${PROJECT_SOURCE_DIR}/system.cnf\" />
			<file name=\"runner.exe\" type=\"data\" source=\"runner_nomusic.exe\" />
		</directory_tree>
	</track>
</iso_project>
")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/iso_web.xml" "<?xml version=\"1.0\" encoding=\"utf-8\"?>
<iso_project>
	<track type=\"data\">
		<identifiers
			system			=\"PLAYSTATION\"
			volume			=\"RUNNER\"
			volume_set		=\"RUNNER\"
			publisher		=\"PARASCENE\"
			data_preparer	=\"PSN00BSDK\"
			application		=\"PLAYSTATION\"
		/>
		<directory_tree>
			<file name=\"SYSTEM.CNF\" type=\"data\" source=\"${PROJECT_SOURCE_DIR}/system.cnf\" />
			<file name=\"runner.exe\" type=\"data\" source=\"runner_web.exe\" />
		</directory_tree>
	</track>
</iso_project>
")

get_filename_component(_repo "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(_audio_json "")
set(_audio_first 1)
foreach(_mp3 ${AUDIO_MP3S})
	cmake_path(GET _mp3 FILENAME _base)
	if(_audio_first)
		set(_audio_first 0)
	else()
		string(APPEND _audio_json ",\n")
	endif()
	string(APPEND _audio_json "      \"runner/audio/${_base}\"")
endforeach()
file(WRITE "${_repo}/web/manifest.json" "{
  \"games\": [
    {
      \"id\": \"runner\",
      \"title\": \"Runner\",
      \"bin\": \"build/runner-web.bin\",
      \"download\": \"build/runner.zip\",
      \"width\": 320,
      \"height\": 240,
      \"audio\": [
${_audio_json}
      ]
    }
  ]
}
")
