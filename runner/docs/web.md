# Web disc commands

`build/runner-web.cue` is the data-only disc for the browser build. It is
the same game as `build/runner-nomusic.cue`, and it also paints a command
row into every frame. A page reads that row from the emulator's screenshot
and plays `audio/*.mp3` itself. The disc does not contain CD audio.

The row is state, repeated every frame for as long as it stays true. The
page applies the latest row it can read. A failed checksum leaves the
previous state in place.

The page that reads it is the repo's GitHub Pages player. The picker and
the player are described in the GitHub Pages section of `TODO.md`.
`npm start` in the `web/` folder serves that same page locally.

The player bypasses EmulatorJS's ROM cache so a refresh loads the current
disc. In 4.2.3, `EJS_cacheLimit = 0` only stops new cache writes; older
entries can still win a content-length comparison after a rebuild.
Core/BIOS caching and the separate memory-card database remain enabled.
The pinned version's cache lookup is in
[emulator.js](https://github.com/EmulatorJS/EmulatorJS/blob/v4.2.3/data/src/emulator.js).

## The row

The game draws 320 pixels on the bottom line, `y = 239`, `x = 0` through
`x = 319`. The page covers that line with the bezel. The screenshot the
page reads is the emulator framebuffer at 320×240, before CSS scaling and
before a CRT shader. Sample `y = 239` directly.

Each pixel is one bit. White (`0x7FFF` in the framebuffer, every channel
at least 128 in the screenshot) is 1. Black is 0. Bits run left to right,
and the first pixel of a byte is its high bit. Byte 0 is pixels 0–7.
`0x57` is the eight pixels `0,1,0,1,0,1,1,1`.

The 320 bits are 40 bytes:

| Bytes | Field |
|---|---|
| 0–3 | Magic `WEB1` (`0x57 0x45 0x42 0x31`) |
| 4 | Version, currently `1`. Ignore a row with any other version. |
| 5 | Payload length, 0–33 |
| 6 | Checksum, the sum of the payload bytes, low 8 bits |
| 7–39 | Payload, 33 bytes. Bytes past the length are 0. |

A row whose magic or checksum does not match is not a command.

## Payload

The payload is a list of commands packed back to back:

```
u8 id
u8 arg_len
u8 args[arg_len]
```

`id` 0 ends the list. An unknown id is skipped by `arg_len`, so an older
page keeps reading the commands it does know. The length byte is the end
of the list even when no 0 follows.

33 bytes hold many commands at once. A new command is a new id and its
arguments. It does not need a new pixel layout. Document it here in the
same change that sends it.

## Commands

### 1 — silence

No arguments. Stop the streamed music. The web disc sends this from the
first frame through the studio logo.

### 2 — music

Three arguments:

| Arg | Meaning |
|---|---|
| 0 | Sequence, 1–255. `0` is unused. |
| 1 | Track index in the alphabetical list of `audio/*.mp3` |
| 2 | Volume, 0–128. `128` is full. `40` is the pause duck. |

The page multiplies this volume by its own volume control.

The sequence changes only when the track should start from the beginning.
The same sequence with a new volume changes loudness and does not seek.
`web/manifest.json` lists the gameplay files in build order, followed by
the menu track. `menuAudioIndex` identifies the menu track; it plays
quietly and never shows a music credit popover.

### 3 — save

One argument: a generation, 1–255. `0` is unused, and a row that has not
had a write this session omits the command.

The argument is not the save. Story progress is a file on the PlayStation
memory card in slot 1, `bu00:UUUVVV-RUNNER`. The record layout is
`SAVE_VERSION` in `runner/src/save.h`. EmulatorJS mounts `/data/saves`
on IndexedDB and restores it before starting the core. Slot 1 is a
131,072-byte image at `/data/saves/PCSX-ReARMed/runner-web.srm`, named
after the ROM basename rather than `EJS_gameName`. Keep that basename
stable so new builds reuse existing cards.

When the generation changes, the page calls `saveSaveFiles()` to copy the
core's card into its filesystem, then awaits `FS.syncfs(false)` to commit
it to IndexedDB. Only a successful commit acknowledges the generation;
failed commits remain pending for retry. It also flushes every five
seconds, on `visibilitychange` to hidden, and on `pagehide` while the
filesystem is still mounted. The next
visit loads the card before the game boots, and the game reads the file.
No separate memory-card server or physical card is needed.

The mount deliberately disables IDBFS `autoPersist`. In EmulatorJS 4.2.3,
the exit handler writes the card and immediately unmounts `/data/saves`.
A queued automatic commit can then see an empty filesystem and delete
the stored files. Explicit commits avoid that shutdown race. Never sync
an already unmounted card directory.
The upstream mount and exit handlers are in
[GameManager.js](https://github.com/EmulatorJS/EmulatorJS/blob/v4.2.3/data/src/GameManager.js).

The card status stays hidden until Play is pressed. It shows
“Memory card ready” after validating and committing the
card, “Progress saved” after a game write, or “Memory card save failed”.
`window.__card` and browser console messages expose the path, generation,
and persistence errors for diagnosis. A game save is triggered by newly
unlocked story progress, not by moving around the menus.

Cards belong to the browser profile and origin (scheme, host, and port).
The hosted site, `localhost`, and `127.0.0.1` therefore have separate
cards. Clearing site storage removes its card; private browsing may
discard it when the session closes.

A different record version is still the same card image. The page does
not learn the new fields. The game ignores an older version and will not
overwrite a newer one. Renaming the ROM would store a second card
and leave the old one behind.

### Browser regression check

`runner/tools/card_probe` builds a small isolated ROM using the production
web save code. Its Chrome check writes progress, commits it through the
page's card bridge, reloads the whole emulator, and verifies that the game
reads the saved value without rewriting it. It uses a separate browser
profile and card name, never a player's card:

```
cmake -S runner/tools/card_probe -B runner/build/card-probe -G Ninja
cmake --build runner/build/card-probe
node runner/tools/card_probe/check.cjs
node runner/tools/card_probe/check.cjs --player
```

`--player` boots the real page and web disc in the isolated browser. It
seeds a stale cached ROM, checks that card status is hidden before Play,
and sends Q/E and standard Gamepad API shoulder-button events. It checks
the actual command pixels and HTML audio playback after each song change.

## When the game sends them

This matches the CD build's music, with the page holding the audio files.

- The logo is silence. The title and pre-game menus play the menu track.
  Entering a stage starts the shuffled gameplay playlist.
- L1 selects the previous song and R1 the next, preserving playlist order.
  Keyboard Q/E send those same buttons in the web player.
- Pause sends the same sequence at volume 40. Continue, or quit to the
  title from the pause menu, sends volume 128.
- The game counts `round(seconds × 60)` frames at 60 Hz, using the real
  mp3 duration, then advances the shuffle and sends a new sequence. The
  next track is a different one from the track that just ended.
- Menu sounds stay in the game. They are not commands.
- Clearing a newly reached story stage writes the memory card. The row
  then carries `save` with a new generation until the next write.
