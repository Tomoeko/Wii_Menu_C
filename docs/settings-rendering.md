# Wii Settings rendering

Settings uses locally exported artwork and outline fonts from the USA 4.3
System Menu WAD. Metal and GLES2 use the same scene.

## Geometry and transitions

The source document is 608×456. In 16:9 it sits within an 832×456 composition
projected into the 640×456 logical framebuffer; in 4:3 it starts at X=16.
Input uses the inverse draw projection. The side artwork fills the framebuffer
beneath controls and stays fixed during page turns.

The exporter decodes the title tab, rows, arrows, markers, footer, and detail
controls from the original resources. Hovered index rows, Back, and page
arrows use their source rollover images immediately. Document changes use
the source twenty-update integer alpha progression; index-page scrolling
uses the 41-frame `SceenChange_b` curve. After a page turn, hit testing updates
focus under a stationary pointer.

| Detail | Source geometry used by the C scene |
| --- | --- |
| Date | Month X=88, Day X=224, Year X=400; 72×72 up/down artwork |
| Time | Value controls at X=200 and X=336 |
| Sensitivity | 32×32 plus/minus art centered in 132×48 cells, a 296×48 gauge, and one 56×56 rank diamond |
| Format prompts | Centered 24-pixel bold text, 33-pixel line spacing, shared vertical center |

The Sensitivity meter omits the generic footer. Clicking its bottom instruction
line substitutes for Wii Remote A input; keyboard names substitute for Remote
glyphs. The Format flow retains the local-preview line and resets local menu
settings only; it does not erase NAND or channel data.

## Console Nickname

The white field accepts pointer placement at character boundaries. A black
caret appears before the keyboard opens and disappears when it closes.
The software keyboard has a red insertion caret, no dictionary, and no More
controls. Clicking its text box moves the caret. The text box rises and falls
with the keyboard over thirty updates in each direction.

Physical Shift and Caps Lock update visible keytops. Quit restores the field's
prior value while retaining edited text inside the departing keyboard until
its exit finishes. Quit, OK, and Space share the same key cue. Physical text
entry accepts printable ASCII. Shared key behavior is described in
[board-keyboard.md](board-keyboard.md).

## Fonts

`wm-outline-font-export` writes `fonts/settings-latin.ttc` from the WAD.
Settings reads proportional face 1 through bounded `cmap`, `hmtx`, `loca`,
and `glyf` parsing. Simple and translated/scaled compound contours use 4×4
coverage antialiasing. Used sizes have cached Latin atlases; normal frames
reuse textures. Bold 24- and 26-pixel labels, including all three Format
prompts, receive an additional rightward coverage pass. The menu BRFNT face
is the fallback when the outline font is unavailable.

The rasterizer does not execute hint programs, apply kerning or script
shaping, or reproduce the original browser's exact bold weight and line
layout. No external OTF is required for these Settings labels.

## Limits

Detail pages are C implementations of the prepared document, not an embedded
browser. Crossfades submit direct draw commands instead of RGB565-quantizing
a complete document raster. The app's presentation chooses the wide projection;
the in-Settings aspect choice does not resize the host window.

Internet and console-information screens use local flows and placeholder
identifiers; network tests, USB connector registration, updates, and retired
services do not perform native console operations. Sensitivity has no live
sensor-dot display. Sound mappings are described in
[audio-accuracy.md](audio-accuracy.md).
