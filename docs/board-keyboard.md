# Memo software keyboard

Create Message → Memo → Write a memo opens the first-party QWERTY keyboard.
The C scene loads the `fs_VK_ascii_keytop_a`, `fs_VK_toolbar_a`,
`fs_VK_predictInput_a`, and `fs_signWindow_a` layouts from locally prepared
USA v4.3 resources. Its letter and number keys, Shift, Caps, Space, Return,
Delete, Back, OK, and More use the original pane hit areas. Back and OK both
keep the draft; they close through the 30-frame keyboard and editor
transition and use the same key cue as Space. The memo's scroll offset eases to
its display bounds during that descent, including when dismissal interrupts an
editor scroll. Enter markers
remain visible with the unposted draft after the keyboard closes. The Memo
scroll arrows become interactive after the keyboard
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

The C scene uses the prepared 16:9 layout geometry for key hit areas and
draws the keytop, toolbar, prediction strip, and Memo body in an explicit
order. Resource-backed render tests check the relevant C geometry and draw
commands. A 1920 × 1080 Metal check exercised on-screen QWERTY typing, Shift,
and OK. No aligned native Wii capture comparison has been made.

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
the built-in vocabulary available. This local fallback does not implement
Zi8's candidate-generation algorithm. A predictive run starts at newly typed
text, ends on a delimiter or explicit
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
malformed offsets, UTF-16 termination and surrogates, and C word filtering.

Keyboard layout, phone mode, dictionary state, and language survive a Memo
keyboard reset in the current session. Clicking visible text in an unposted
Memo selects the nearest UTF-8 insertion boundary and opens the keyboard at
that position. Physical arrow keys move the caret across UTF-8 characters or
adjacent rendered lines. Physical Shift and Caps Lock update the visible keytop
state while held or latched, with focus easing into and out of each state. The
editor follows the selected caret into its two-line window with
`WIPL_SE_LINE_SCROLL` whenever following starts page movement. Visible caret
selections stay silent, and typing sounds remain intact when a key also starts
scrolling. Insertion, deletion, phone multi-tap, and completion retain text
after the caret. Text can also be selected while the keyboard is open. If a
completion is pending, the first text click commits it and a fresh click moves
the caret.
The top toolbar strip remains visible during its downward entrance, using
the same smooth progress and opacity as the bottom toolbar. Focused
render-command and editing tests cover these flows. Native Zi8 working-memory
behavior and durable keyboard preferences remain unverified. A live
1920 × 1080 Metal check showed
the More panel on page 1/10, page 2/10 after the next arrow, `[` inserted
into Memo text, and Close returning to QWERTY; it predates the dictionary and
telephone changes. Keyboard text, glyphs, hover phases, and candidate
presentation still need aligned frame and pixel comparisons with native
captures, including 4:3; native fidelity remains unverified.
