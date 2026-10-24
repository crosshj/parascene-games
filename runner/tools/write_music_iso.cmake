# Writes music_tracks.h and iso.xml for the current AUDIO_MP3S list.
# Expected: AUDIO_MP3S, MUSIC_TRACK_COUNT, RUNNER_XA_MUSIC,
# CMAKE_CURRENT_SOURCE_DIR, CMAKE_CURRENT_BINARY_DIR, PROJECT_SOURCE_DIR
# FFPROBE and PYTHON3 are required. PYTHON3 rounds the web-disc frame counts.

set(_hdr "${CMAKE_CURRENT_BINARY_DIR}/music_tracks.h")
set(_iso "${CMAKE_CURRENT_BINARY_DIR}/iso.xml")

file(WRITE "${_hdr}"
"/* Generated from audio MP3 filenames — do not edit. */
#ifndef MUSIC_TRACKS_H
#define MUSIC_TRACKS_H

#define MUSIC_TRACK_COUNT ${MUSIC_TRACK_COUNT}
#define MUSIC_MENU_TRACK ${MUSIC_MENU_TRACK}
#define MUSIC_TOTAL_TRACK_COUNT ${MUSIC_TOTAL_TRACK_COUNT}
")

file(APPEND "${_hdr}" "\n/* Display credits from the original audio filenames. */\n")
file(APPEND "${_hdr}" "static const char *const music_artist[MUSIC_TRACK_COUNT] = {\n")
foreach(_mp3 ${AUDIO_MP3S})
	cmake_path(GET _mp3 FILENAME _base)
	string(REGEX REPLACE "\\.[^.]*$" "" _credit "${_base}")
	string(FIND "${_credit}" " - " _split)
	if(_split GREATER_EQUAL 0)
		string(SUBSTRING "${_credit}" 0 ${_split} _artist)
	else()
		set(_artist "")
	endif()
	string(REPLACE "\\" "\\\\" _artist "${_artist}")
	string(REPLACE "\"" "\\\"" _artist "${_artist}")
	file(APPEND "${_hdr}" "\t\"${_artist}\",\n")
endforeach()
file(APPEND "${_hdr}" "};\nstatic const char *const music_title[MUSIC_TRACK_COUNT] = {\n")
foreach(_mp3 ${AUDIO_MP3S})
	cmake_path(GET _mp3 FILENAME _base)
	string(REGEX REPLACE "\\.[^.]*$" "" _credit "${_base}")
	string(FIND "${_credit}" " - " _split)
	if(_split GREATER_EQUAL 0)
		math(EXPR _song_at "${_split} + 3")
		string(SUBSTRING "${_credit}" ${_song_at} -1 _title)
	else()
		set(_title "${_credit}")
	endif()
	string(REPLACE "\\" "\\\\" _title "${_title}")
	string(REPLACE "\"" "\\\"" _title "${_title}")
	file(APPEND "${_hdr}" "\t\"${_title}\",\n")
endforeach()
file(APPEND "${_hdr}" "};\n")

cmake_path(GET MENU_AUDIO_MP3 FILENAME _menu_base)
string(REGEX REPLACE "\\.[^.]*$" "" _menu_credit "${_menu_base}")
string(FIND "${_menu_credit}" " - " _menu_split)
if(_menu_split GREATER_EQUAL 0)
	string(SUBSTRING "${_menu_credit}" 0 ${_menu_split} _menu_artist)
	math(EXPR _menu_song_at "${_menu_split} + 3")
	string(SUBSTRING "${_menu_credit}" ${_menu_song_at} -1 _menu_title)
else()
	set(_menu_artist "")
	set(_menu_title "${_menu_credit}")
endif()
string(REPLACE "\\" "\\\\" _menu_artist "${_menu_artist}")
string(REPLACE "\"" "\\\"" _menu_artist "${_menu_artist}")
string(REPLACE "\\" "\\\\" _menu_title "${_menu_title}")
string(REPLACE "\"" "\\\"" _menu_title "${_menu_title}")
file(APPEND "${_hdr}" "static const char music_menu_artist[] = \"${_menu_artist}\";\n")
file(APPEND "${_hdr}" "static const char music_menu_title[] = \"${_menu_title}\";\n")
execute_process(
	COMMAND "${FFPROBE}" -v quiet -show_entries format=duration -of csv=p=0 "${MENU_AUDIO_MP3}"
	OUTPUT_VARIABLE _menu_duration OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(
	COMMAND "${PYTHON3}" -c "import math,sys; d=float(sys.argv[1])*60; print(math.floor(d+0.5), math.floor(d+0.5))" "${_menu_duration}"
	OUTPUT_VARIABLE _menu_frame_pair OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
string(REGEX MATCH "^([0-9]+) ([0-9]+)$" _menu_frame_match "${_menu_frame_pair}")
set(_menu_cd_frames "${CMAKE_MATCH_1}")
set(_menu_web_frames "${CMAKE_MATCH_2}")
file(APPEND "${_hdr}" "static const int music_menu_cd_frames = ${_menu_cd_frames};\n")
file(APPEND "${_hdr}" "static const int music_menu_web_frames = ${_menu_web_frames};\n")

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
	while(_i LESS MUSIC_TOTAL_TRACK_COUNT)
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
	file(APPEND "${_iso}" "\t<track type=\"audio\" source=\"${MENU_TRACK_OUT}\" />\n")
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
cmake_path(GET MENU_AUDIO_MP3 FILENAME _menu_base)
string(APPEND _audio_json ",\n      \"runner/audio/menu/${_menu_base}\"")
file(WRITE "${_repo}/web/manifest.json" "{
  \"games\": [
    {
      \"id\": \"runner\",
      \"title\": \"Runner\",
      \"bin\": \"build/runner-web.bin\",
      \"download\": \"build/runner.zip\",
      \"cover\": \"runner/cover.png\",
      \"share\": \"runner/share.png\",
      \"blurb\": \"Pick a runner and hit the road. A PlayStation game you can play right in your browser.\",
      \"width\": 320,
      \"height\": 240,
      \"menuAudioIndex\": ${MUSIC_MENU_TRACK},
      \"audio\": [
${_audio_json}
      ]
    }
  ]
}
")
