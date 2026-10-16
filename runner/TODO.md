# TODO

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
