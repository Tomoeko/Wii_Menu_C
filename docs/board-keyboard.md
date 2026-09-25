# Memo software keyboard

Create Message → Memo → Write a memo opens the first-party QWERTY keyboard.
The C scene loads the `fs_VK_ascii_keytop_a`, `fs_VK_toolbar_a`,
`fs_VK_predictInput_a`, and `fs_signWindow_a` layouts from locally prepared
USA v4.3 resources. Its letter and number keys, Shift, Caps, Space, Return,
Delete, Back, OK, and More use the original pane hit areas. Back and OK both
keep the draft; they close through the 30-frame keyboard and editor
transition. The Memo scroll arrows become interactive after the keyboard
finishes entering and fade with its exit.

More opens the ten US symbol pages after the source 18-frame entrance.
Twenty symbols, Close, and both page arrows use the WAD hit areas and focus
and pushed clips. The page arrows use the WAD's inverse-named 20-frame scroll
clips, including wraparound; Close and physical Back use the 13-frame exit.
The source `WSD_SELECT` cue plays on a page flip, with the matching symbol
open, character input, close, and arrow hover cues. The second set of twenty
WAD text panes shows the incoming page during a scroll. UTF-8 symbol input
and deletion operate on whole Unicode characters. The symbol panel is modal:
the underlying Memo scroll arrows and physical text input are inactive until
it closes.

The 16:9 source geometry was checked against the maintained HTML scene in a
1280 × 720 browser viewport and a 1920 × 1080 Metal content area. The first
number key, first letter key, and Back button occupy matching positions after
the 1.5× scale. The source keytop, toolbar, prediction strip, and Memo body
share the HTML scene's draw order. On-screen QWERTY typing, Shift, and OK were
also checked in the Metal build. This is browser geometry parity evidence;
it is not a native Wii capture comparison.

The C keyboard also exposes the telephone layout, its four Latin modes,
forward and reverse multi-tap, and the QWERTY and telephone dictionary
controls. Moving focus away from a phone key commits its pending character;
the source private-use marker for a pending literal space is presented as
U+2423 Open Box while the stored draft retains an ordinary space. The C font
decoder maps U+2423 to the original marker glyph. Focus entrance settles to
the source Roll_over clip, and focused key branches move to the front of their
layout's draw order. Resource-backed tests cover the telephone Eng hit area,
hover geometry, multi-tap, commitment, and marker mapping.

The dictionary button opens the three-language source selector. The default
prediction state is off. Turning it on shows local completion candidates for
the current non-whitespace run, including digits and punctuation, and uses
local phone digit prediction. Preparation reads the OEM word containers from
the user's WAD into ignored local assets. At startup the keyboard adds their
validated UTF-16BE words after the built-in fallback list; complete words
from the current draft take priority. Missing or invalid OEM containers leave
the built-in vocabulary available. This matches the maintained HTML project's
embedded word-list fallback, not Zi8's candidate-generation algorithm. A
predictive run starts at newly typed text, ends on a delimiter or explicit
completion, and does not recompose text that was already present when the
dictionary was enabled. Phone digit matching recognizes common accented Latin
letters. When there is no completion, the typed run remains a selectable
literal. The candidate strip
uses the WAD's previous/next arrow hit areas and focus/pushed clips. Pages
overlap the partially visible last word, animate with the reference 15-frame
smoothstep, and accept another page at frame 16. A held arrow requests another
page on each update except every twentieth; the strip ignores requests while
already moving. Candidate text is clipped to the source text area while the
prediction window draws once. The C scene can map forty candidate values onto
the twenty authored text panes, though this local word-list provider returns
at most twenty completions. Resource-backed tests cover the arrow boundaries,
focus continuity, movement lockout, source text-area clip, learned words,
accented phone typing, and prepared OEM loading. Synthetic parser tests cover
malformed offsets, UTF-16 termination and surrogates, and the HTML word filter.

Keyboard layout, phone mode, dictionary state, and language survive a Memo
keyboard reset in the current session. Native Zi8 working-memory behavior,
durable keyboard preferences, and text-point caret selection remain open C
parity work. A live 1920 × 1080 Metal check showed
the More panel on page 1/10, page 2/10 after the next arrow, `[` inserted
into Memo text, and Close returning to QWERTY; it predates the dictionary and
telephone changes. Keyboard text, glyphs, hover phases, and candidate
presentation still need aligned frame and pixel comparisons against HTML and
native captures, including 4:3.
