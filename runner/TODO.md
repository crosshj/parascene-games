# TODO

## Web disc

Build `build/runner-web.bin` and `build/runner-web.cue` on every default
build, the same way `runner-nomusic` is built. Profile builds skip it.
`docs/web.md` is the command contract. Implement that row, and when a
command is added or changed, update that doc in the same change.

- Add a third executable under `RUNNER_WEB`. Data-only iso, copied from
  the no-music iso. The file on the disc is still `runner.exe`.
- Copy the bin and cue to the repo-root `build/` from that iso target.
  Depend on it from `runner_iso`, so one build writes all three discs.
- Leave CD audio and XA off this disc. Menu sounds stay in the program.
- Keep a single playlist. The web backend does not call the CD.
  `music_start`, `music_volume`, and the end-of-track advance stay the
  call sites the CD build already uses.
- Time a track with `round(seconds × 60)` frames from the mp3 duration.
  Generate that table with the other music tables. The CDDA table is a
  different clock (whole seconds, plus 50 frames) and is the wrong one
  here.
- Track index is the sorted `audio/*.mp3` list.
- Paint the 40 bytes with one `LoadImage` of 320 15-bit pixels into the
  bottom row of the framebuffer, after that buffer has been cleared and
  drawn. Black is `0x0000`, white is `0x7FFF`. A few hundred tile
  primitives would spend the frame's 32 KB primitive buffer.
- Send `silence` from the first frame through the logo. `music_start`
  sends `music`. Pause changes the volume argument only.
- Add a host test that packs a silence row and a music row and checks
  the 320 bits, including the checksum. Preview stills will show the
  row; leave it. The page is what covers it.
- In `README.txt`, name `build/runner-web.cue` next to the no-music cue.

## GitHub Pages

Publish a project site for this repo. The root page is a picker of games
that have a web disc. Choosing one opens a player built the way ZORB's
page plays a PlayStation disc. The working example is
`mobile-web-emu-zorb` (`index.html` and `serve.py`): EmulatorJS, no BIOS
file, and the browser playing the audio.

- Turn on GitHub Pages for the repo. Put a `.nojekyll` file at the site
  root so Jekyll does not drop the disc images. The site is a project
  page, so every asset URL has to work under
  `https://<owner>.github.io/<repo>/`, not at the domain root.
- Publish the site files, each game's `*-web.bin`, and that game's mp3s.
  Leave `runner/build/` and the CD-audio discs out. The web bin is the
  one that loads quickly.
- The picker reads one manifest. Each entry has an id, a title, the web
  bin path, the screen size, and the mp3 list in track-index order.
  Runner is the first entry: `build/runner-web.bin`, 320×240, and
  `runner/audio/*.mp3` in the same sorted order as the disc. A game is
  listed only when its web bin is published. Adding a later game is a
  new manifest entry, not a new player.
- One play page, `?game=<id>`, serves every entry. It loads EmulatorJS
  4.2.3 from `https://cdn.emulatorjs.org/4.2.3/data/`, sets `EJS_core` to
  `psx`, and sets `EJS_gameUrl` to that game's web bin. PCSX-ReARMed's
  HLE BIOS is enough; do not ship a BIOS. Start when the bin has loaded.
  Append `audio_sync = false` to the RetroArch config the way the ZORB
  page patches `getRetroArchCfg`, or the frame waits until the browser
  unlocks audio. Set the EmulatorJS cache limit to 0 so a rebuilt bin of
  the same size is not hidden. Resume every AudioContext on the first
  pointer or key.
- Buttons go in through `gameManager.simulateInput(0, retroPadId, 0 or 1)`.
  The pad ids the ZORB page uses are the ones PCSX-ReARMed expects:
  ✕ 0, □ 1, Select 2, Start 3, Up 4, Down 5, Left 6, Right 7, ○ 8, △ 9.
  Runner's controls are in `README.txt`.
- A few times a second, read `gameManager.screenshot()` at the game's
  native size and decode `y = 239` as `docs/web.md` describes. Cover that
  row in the player so it is not on screen. Do not run that row through
  a CRT shader; the bits have to survive. `silence` stops the mp3.
  `music` starts `audio[track]` from the beginning when the sequence
  changes, and otherwise only sets volume to `arg / 128` times the page's
  own volume. The mp3 URL is the file in the repo, so the browser streams
  it.
- GitHub Pages cannot send the headers the core needs. ZORB's `serve.py`
  sends `Cross-Origin-Opener-Policy: same-origin` and
  `Cross-Origin-Embedder-Policy: credentialless`, because the threaded
  core needs `SharedArrayBuffer` and the EmulatorJS scripts come from
  another origin. Ship a service worker that applies those two headers
  and reloads once, and start the emulator only after
  `window.crossOriginIsolated` is true. Use credentialless, as `serve.py`
  does, so the CDN scripts still load.

## Characters and sprite pages

- Upload only the chosen character. `upload_sprite()` copies every
  character into video memory at boot, but a run draws only one. Upload the
  selected character when the player confirms on the select screen, or on
  each left/right press so the preview follows. About 20 KB is well under a
  frame's worth of transfer. The packer should then start each character on
  its own pages so they can share the same slots.
- Keep unused characters out of main RAM. Every character is compiled into
  the program as `spr_tex` (about 20 KB each at 52x64, 4 bits per pixel).
  With many characters, put each on the disc as its own file and read it
  from CD when it is picked.
- Plan video memory for more sprite pages. `pack_sprites.py` has four
  4-bit page slots (640,0), (640,256), (384,0), (384,256); two 52x64
  characters use three. A character takes a page and a half, so a third one
  would not fit today. Other sprites will need pages too, so give the
  packer an allocator over free VRAM instead of a fixed slot list, and
  check every upload against it. A CLUT once landed inside a sprite page
  and drew a line through the character. The map today, in VRAM units:
  - framebuffers 0,0 320x480
  - screen art 512,0 128x98 and 512,256 120x67
  - CLUTs under the framebuffers: screen art rows 480 and 482, lumen sky
    row 481, each 256 wide at x 0
  - lumen sky 768,64 160x64
  - shadow 768,256 64x32
  - sprite pages 640,0, 640,256, 384,0 (used) and 384,256 (free), each
    64x256; sprite CLUTs 896,0 16x2
  - font 960,0
