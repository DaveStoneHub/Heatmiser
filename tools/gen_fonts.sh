#!/bin/sh
# Regenerate the UI's fonts (components/ui/fonts). Needs Node.js and an LVGL
# checkout for Montserrat:  tools/gen_fonts.sh /path/to/lvgl
set -e
LVGL=${1:?usage: gen_fonts.sh /path/to/lvgl}
OUT=$(cd "$(dirname "$0")/../components/ui/fonts" && pwd)
WORK=$(mktemp -d)
(cd "$WORK" && npm init -y >/dev/null && npm install --silent lv_font_conv@1.5.2 @fortawesome/fontawesome-free@6.5.2)
CONV="$WORK/node_modules/.bin/lv_font_conv"
FA="$WORK/node_modules/@fortawesome/fontawesome-free/webfonts/fa-solid-900.ttf"
MS="$LVGL/scripts/built_in_font/Montserrat-Medium.ttf"
ICONS=0xf06d,0xf2dc,0xf023,0xf043,0xf011,0xf1eb,0xf071,0xf017,0xf2c9,0xf013,0xf0e7
for s in 20 32; do
  "$CONV" --no-compress --no-prefilter --bpp 4 --size $s --font "$FA" -r $ICONS \
    --format lvgl --lv-include lvgl.h --force-fast-kern-format -o "$OUT/hm_icons_$s.c"
done
for s in 64 96; do
  "$CONV" --no-compress --no-prefilter --bpp 4 --size $s --font "$MS" -r 0x20,0x2D,0x2E,0x30-0x39,0xB0 \
    --format lvgl --lv-include lvgl.h --force-fast-kern-format -o "$OUT/hm_num_$s.c"
done
rm -rf "$WORK"
