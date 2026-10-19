#!/bin/sh
# The 1200x630 card that link previews show for a game, made from its cover.
#
#   web/share-image.sh runner/cover.png runner/share.png
set -e
cover="$1"
out="$2"
magick \
  \( "$cover" -resize 1200x900^ -gravity center -extent 1200x630 -blur 0x24 -fill '#120a1a' -colorize 55% \) \
  \( "$cover" -filter point -resize 800x600 -bordercolor '#38244c' -border 3 \) \
  -gravity center -composite -depth 8 -strip "$out"
