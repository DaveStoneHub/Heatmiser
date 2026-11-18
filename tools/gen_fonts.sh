#!/bin/sh
# Regenerate the UI's fonts (components/ui/fonts) for the Copper design:
# Barlow Condensed numerals, Manrope labels and Lucide line icons.
# Needs Node.js and the npm registry:  tools/gen_fonts.sh
set -e
OUT=$(cd "$(dirname "$0")/../components/ui/fonts" && pwd)
WORK=$(mktemp -d)
(cd "$WORK" && npm init -y >/dev/null && npm install --silent lv_font_conv@1.5.2 \
  @fontsource/manrope@5.1.0 @fontsource/barlow-condensed@5.1.0 lucide-static@0.460.0)
CONV="$WORK/node_modules/.bin/lv_font_conv"
FS="$WORK/node_modules/@fontsource"
MR500="$FS/manrope/files/manrope-latin-500-normal.woff"
MR700="$FS/manrope/files/manrope-latin-700-normal.woff"
BC500="$FS/barlow-condensed/files/barlow-condensed-latin-500-normal.woff"
BC600="$FS/barlow-condensed/files/barlow-condensed-latin-600-normal.woff"
LUCIDE="$WORK/node_modules/lucide-static/font/lucide.ttf"

# Lucide glyphs, in the order of the HM_ICON_* macros in ui_internal.h:
# flame droplet lock lock-open wifi wifi-off triangle-alert chevron-left
# chevron-right pencil sliders-horizontal calendar copy plus minus delete
# arrow-big-up refresh-cw eye trash-2 list radio activity clock tree-palm
# rotate-ccw snowflake power thermometer monitor check x share-2 info
ICONS=0xe0d7,0xe0b8,0xe10f,0xe110,0xe1ae,0xe1af,0xe193,0xe072,0xe073,0xe1f9,0xe29a,0xe067,0xe0a2,0xe141,0xe120,0xe0b2,0xe1e4,0xe149,0xe0be,0xe18e,0xe10c,0xe146,0xe038,0xe08b,0xe281,0xe14c,0xe169,0xe144,0xe186,0xe121,0xe070,0xe1b2,0xe15a,0xe0fe

# Text: printable ASCII plus degree, middle dot, en dash and ellipsis.
TEXT=0x20-0x7E,0xB0,0xB7,0x2013,0x2026
NUM=0x20,0x2D,0x2E,0x30-0x3A,0xB0

conv() { # name size font range [fallback]
  "$CONV" --no-compress --no-prefilter --bpp 4 --size "$2" --font "$3" -r "$4" \
    --format lvgl --lv-include lvgl.h --force-fast-kern-format -o "$OUT/$1.c"
  # Keep the header comment free of this run's temporary paths.
  sed -i "s#--font [^ ]*/\([^/ ]*\) #--font \1 #; s# -o [^ ]*/\([^/ ]*\)\$# -o \1#" "$OUT/$1.c"
  if [ -n "$5" ]; then
    # Glyphs the font lacks (the icons) come from the fallback font.
    sed -i "s/^\(\s*\)\.dsc = &font_dsc/\1.fallback = \&$5,\n&/" "$OUT/$1.c"
    sed -i "0,/^#ifndef HM_/s//extern const lv_font_t $5;\n\n&/" "$OUT/$1.c"
    grep -q "\.fallback = &$5," "$OUT/$1.c"
  fi
}

lower() { # name px: drop every glyph px below the baseline, so an icon mixed
  # into Manrope text sits centred on the capitals, and raise the baseline
  # by as much, so the icon alone still fills its line.
  perl -pi -e 's/\.ofs_y = (-?\d+)\}/".ofs_y = " . ($1 - '"$2"') . "}"/e if /box_w = [1-9]/;
               s/\.base_line = (\d+),/".base_line = " . ($1 + '"$2"') . ","/e' "$OUT/$1.c"
}

conv hm_icon_18 18 "$LUCIDE" "$ICONS"
lower hm_icon_18 3
conv hm_icon_22 22 "$LUCIDE" "$ICONS"
lower hm_icon_22 5
conv hm_icon_28 28 "$LUCIDE" "$ICONS"

conv hm_mr500_15 15 "$MR500" "$TEXT" hm_icon_18
conv hm_mr700_15 15 "$MR700" "$TEXT" hm_icon_18
conv hm_mr700_17 17 "$MR700" "$TEXT" hm_icon_22
conv hm_mr700_22 22 "$MR700" "$TEXT" hm_icon_22

conv hm_bc600_20 20 "$BC600" "$TEXT"
conv hm_bc600_30 30 "$BC600" "$TEXT"
conv hm_bc600_44 44 "$BC600" "$TEXT"
conv hm_bc500_58 58 "$BC500" "$NUM,0x45"
conv hm_bc600_112 112 "$BC600" "$NUM"
conv hm_bc600_150 150 "$BC600" 0x20,0x2D,0x46,0x4E,0x4F

rm -rf "$WORK"
