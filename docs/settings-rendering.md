# Wii Settings presentation in C

The C scene draws the source 608×456 Settings document. In the 16:9 menu,
it fits that document between two 112×456 side panels in the original
832×456 composition; the complete surface is then fitted into the fixed
640×456 framebuffer. In 4:3 mode the document remains at x=16 without the
side panels. Settings input uses the inverse of the draw projection. Local asset
preparation reads the source USA 4.3 WAD's `html/US2/iplsetting.ash` and
`html/BG_16x9.tpl`. `wm-settings-export` decodes selected GIFs and PNGs and the
side-panel TPL with first-party C decoders, then writes RGBA resources into the
ignored local asset directory. Nothing from the WAD is part of the repository.
The sampled `00000097.app` SHA-256 was
`64bc053c764d5be5107b03675ee487a8f83258e9f5518b5df10a135d23ded237`.

The eight-pixel source background tile is expanded into one 608×456 texture
at preparation time. This preserves its exact horizontal repeat while keeping
the steady Settings draw to one background submission. The title tab, page
arrows and their focus art, page markers, index row background and focus PNG,
and footer background and focus PNG also use decoded source images. Category
choice rows use the original normal, focus, and selected-side artwork. The
Widescreen detail uses its original two button images and 160-pixel selection
flames. Screen Position uses the original arrow and side flame artwork.
WiiConnect24's unavailable rows use its original dark row artwork. Source CSS
positions for the index controls remain in the scene; the page markers display
1, 2, 3 from left to right. Category headers and footers reuse the original
shared artwork. Hovered index rows, Back, and page arrows switch to their
source rollover images on the first hovered frame, with no hover fade.
Category/detail document swaps use the source twenty-update integer alpha
progression. The index page scroll uses the original 41-frame WAD curve and
moves the complete widescreen composition, including both side panels, across
the framebuffer. After a page turn, the stationary pointer is tested against
the newly displayed controls so an arrow beneath it acquires focus immediately.
The title, shared rows, and footer baselines follow the source HTML positions
to within about two pixels in the 608×456 document comparison; font raster
and frame-aligned native pixels still need verification.
The Date detail uses the USA English stylesheet's Month x=88, Day x=224, and
Year x=400 controls and the WAD's original 72×72 up/down artwork. These
positions also drive input; the shared `US/COM` stylesheet places them
differently. The Time detail retains its original x=200 and x=336 controls.
The Sensor Bar Sensitivity meter now uses the source 32×32 minus and plus
images centered in their 132×48 cells, its 296×48 gauge, and one visible
56×56 rank diamond at the source CSS position for the selected level. The
`US/COM` stylesheet's `.List` rule centers those button backgrounds without
repeating them. The source meter's `DPDPosition` element is empty, so the
earlier blue placeholder is gone. This page also omits the generic footer,
as in `DPD_Sensitive02.html`; clicking its bottom instruction line supplies a
local pointer action for the Wii Remote A confirmation. The preceding
instruction page retains its source footer and six-line layout, with readable
keyboard names in place of Wii Remote glyphs.
The C state machine and WAD `SceenChange_b` page transition remain separate
from this raster preparation. The renderer receives ordinary backend-neutral
quads, so the same scene runs through GLES2 and Metal.

This is a visual improvement, not yet a 1:1 Settings port. The first-party
`wm-outline-font-export` tool now extracts the original collection into an
ignored local `fonts/settings-latin.ttc` file. Settings reads its proportional
face and draws the English and Latin-1 labels from bounded TrueType `cmap`,
`hmtx`, `loca`, and `glyf` parsing. Simple contours and translated/scaled
compound glyphs are flattened and rendered with 4×4 coverage antialiasing.
The exporter validates face 1 and representative rasterized Latin glyphs
before writing, refuses symlink output directories and existing targets, and
creates its output file exclusively.
Each used text size receives a cached Latin atlas; normal frames reuse its GPU
texture without decoding or allocating glyphs. If that optional local file is
not prepared, the existing BRFNT menu face remains a visible fallback.
The original USA WAD's `0000000a.app` contains `WiiNTLG-Regular.ttc` at offset
`0x60` (SHA-256
`006abc0517bba72d6135ad1d04f911e99fafc6f6aff27ce71917a38acd8803f2`).
It has two TrueType faces with `glyf`, `loca`, `cmap`, `hmtx`, `GSUB`, `mort`,
and TrueType hint-program tables. The C raster adds a one-pixel rightward
coverage pass for 24- and 26-pixel labels whose source CSS requests bold. It
does not run font hint programs, apply kerning or script shaping, or emulate
Opera's exact bold weight, antialiasing and line layout. Other source languages may
need a broader Unicode atlas and their exact family selection. These gaps
prevent a pixel-level font parity claim. No outline bytes are committed.
Detail pages retain authored C approximations of HTML layout and local
substitute actions. They do not include the HTML project's complete original
navigation engine, keyboard, Internet form flow, or every category icon.
The Sensitivity page has no live sensor-bar dot display, and its bottom-line
click and keyboard labels are local host substitutes for Wii Remote input.
The current crossfade draws direct C commands instead of first composing and
RGB565-quantizing a complete document raster, so edge and text pixels may
differ. The current wide switch is selected by the app's 16:9 presentation;
the in-Settings 4:3 choice does not reconfigure the host window. Frame-aligned
comparisons against the HTML implementation and native capture are still
needed for visual parity.

`wii-menu-settings-gif` and `wii-menu-settings-png` test first-party image
decoding and malformed streams.
`wii-menu-outline-font` tests bounded SFNT parsing, simple and compound
contours, raster coverage, text metrics, and atlas reuse with a synthetic font.
`wii-menu-outline-font-export` rejects a malformed collection within a
synthetic U8 archive before creating output. With `WM_OUTLINE_TEST_SOURCE`
pointing to an ignored original resource archive, it also checks export,
symlink rejection, and that an existing font file is not overwritten.
Setting `WM_OUTLINE_TEST_FONT` to a locally exported collection path also
rasterizes the source face's Latin range without shipping its bytes.
`wii-menu-settings-scene` tests 4:3 and 16:9 input geometry, side-panel quad
and clip placement, category/detail opacity, and the source page-change curve.
Its Sensitivity render assertion checks the selected rank and the source
button/gauge placement in 16:9 projection, with no footer artwork on the
meter page.
During development, all 19 initial decoded GIF and PNG resources were compared
against the original source with an independent local decoder; all RGBA bytes
matched. The subsequently added dark WiiConnect24 row, Screen Position,
Widescreen, and Date/Time arrow art are decoded by the same first-party paths
and pass the parser's focused tests.
