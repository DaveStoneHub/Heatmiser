# Fonts

Generated with [lv_font_conv](https://github.com/lvgl/lv_font_conv) 1.5.2 by
`tools/gen_fonts.sh` for the Copper design; do not edit by hand.

| File | Font | Glyphs |
| --- | --- | --- |
| `hm_mr500_15.c` | Manrope Medium 15 (`@fontsource/manrope` 5.1.0) | ASCII, `°`, `·`, `–`, `…`; icons from `hm_icon_18` |
| `hm_mr700_15.c`, `hm_mr700_17.c`, `hm_mr700_22.c` | Manrope Bold 15, 17, 22 | as above; icons from `hm_icon_18` (15) or `hm_icon_22` (17, 22) |
| `hm_bc600_20.c`, `hm_bc600_30.c`, `hm_bc600_44.c` | Barlow Condensed SemiBold 20, 30, 44 (`@fontsource/barlow-condensed` 5.1.0) | as Manrope |
| `hm_bc500_58.c` | Barlow Condensed Medium 58 | space, `-`, `.`, `0`-`9`, `:`, `°`, `E` (tile temperatures, fault codes) |
| `hm_bc600_112.c` | Barlow Condensed SemiBold 112 | space, `-`, `.`, `0`-`9`, `:`, `°` (setpoint dial, schedule editor) |
| `hm_bc600_150.c` | Barlow Condensed SemiBold 150 | space, `-`, `F`, `N`, `O` (hot water ON / OFF) |
| `hm_icon_18.c`, `hm_icon_22.c`, `hm_icon_28.c` | Lucide (`lucide-static` 0.460.0) | the 34 icons listed in `tools/gen_fonts.sh`, in the order of the `HM_ICON_*` macros in `ui_internal.h` |

The Manrope fonts fall back to an icon font, so a label can mix an icon and
text. The 18 and 22 px icons sit 3 and 5 px lower than Lucide draws them, so
they centre on the capitals of the text beside them.

Manrope and Barlow Condensed are SIL OFL 1.1; Lucide is ISC licensed.
