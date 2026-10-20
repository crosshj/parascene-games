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
Today the files are `runner1.mp3` (index 0) and `runner2-lumenwild.mp3`
(index 1).

### 3 — save

One argument: a generation, 1–255. `0` is unused, and a row that has not
had a write this session omits the command.

The argument is not the save. Story progress is a file on the PlayStation
memory card in slot 1, `bu00:UUUVVV-RUNNER`. The record layout is
`SAVE_VERSION` in `runner/src/save.h`. The browser keeps that card by
storing the emulator's slot-1 image under the game id `runner`.

When the generation changes, the page flushes that card into browser
storage. The page also flushes it every few seconds, and again when the
page is hidden. The next visit loads the card back into the emulator
before the game boots, and the game reads the file.

A different record version is still the same card image. The page does
not learn the new fields. The game ignores an older version and will not
overwrite a newer one. Renaming the game id would store a second card
and leave the old one behind.

## When the game sends them

This matches the CD build's music, with the page holding the audio files.

- The logo is silence. Music starts once, when the title screen appears.
  The game shuffles the mp3 list and sends `music` for the first track.
- The same track keeps playing through runner select, the run, a wipeout,
  and a return to the title. Those screens do not restart it.
- Pause sends the same sequence at volume 40. Continue, or quit to the
  title from the pause menu, sends volume 128.
- The game counts `round(seconds × 60)` frames at 60 Hz, using the real
  mp3 duration, then advances the shuffle and sends a new sequence. The
  next track is a different one from the track that just ended.
- Menu sounds stay in the game. They are not commands.
- Clearing a newly reached story stage writes the memory card. The row
  then carries `save` with a new generation until the next write.
