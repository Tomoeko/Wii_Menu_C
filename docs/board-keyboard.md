# Memo software keyboard

Create Message → Memo → Write a memo opens the source-backed QWERTY keyboard.
It also supports the telephone layout, four Latin modes, symbols, dictionary
controls, pointer caret selection, and physical keyboard input.

## Editing and transitions

Back and OK keep the draft and close through the thirty-frame keyboard/editor
transition. Both use the Space key cue. The Memo scroll offset eases into its
display bounds during descent, including when dismissal interrupts scrolling.
Enter markers remain visible in unposted text. Memo scroll arrows become
interactive after keyboard entry and fade with exit.

Clicking visible text in an unposted Memo opens the keyboard at the nearest
UTF-8 insertion boundary. Text can also be selected while the keyboard is open.
Physical arrow keys move across characters or adjacent rendered lines.
Insertion, deletion, multi-tap, and completion preserve text after the caret.
The editor follows the caret into its two-line window; movement that starts
scrolling uses `WIPL_SE_LINE_SCROLL`. Visible caret selections are silent.
A pending completion is committed by the first text click; a fresh click then
moves the caret.

Physical Shift and Caps Lock update keytops while held or latched. Their focus
poses ease in and out. On-screen modifier presses are ignored while the matching
physical modifier is active. Layout, phone mode, dictionary state, and language
survive a keyboard reset within the current session.

## Hover, press, and draw order

Each key keeps its own focus and press pose. Pointer exit interrupts the click
pulse and begins focus exit from the current pose. The key stays above its
neighbors until settled; rapid clicks retain earlier return motion. Exit uses
the authored eight-frame curve, or seven frames for prediction controls, with
scale/color blended from the interrupted pose for continuity.

Focused key branches draw at the front of their layout. Toolbar buttons rise
within their group, leaving the wide background behind the layout selectors.
The upper strip moves and fades with the keyboard entrance.

Telephone keys support forward/reverse multi-tap; leaving a key commits its
pending character. A pending space displays the source marker through U+2423
Open Box while the stored text retains an ordinary space.

## Symbols and prediction

More opens ten US symbol pages through an eighteen-frame entrance. Twenty
symbols, Close, and page arrows use source hit areas and focus/press clips.
Arrows use the inverse-named twenty-frame scroll clips, including wraparound;
Close and physical Back use the thirteen-frame exit. Page flips play
`WSD_SELECT`. The modal symbol panel blocks underlying Memo arrows and physical
text input; insertion and deletion operate on complete UTF-8 characters.

Prediction defaults to off. When enabled, it offers local completions for the
current non-whitespace run and digit matching for telephone input. Preparation
extracts OEM word containers; validated words augment the built-in fallback
list, and words from the draft take priority. Missing or invalid containers
leave the fallback available. This provider does not implement Zi8's algorithm.

A prediction run begins with new input and ends on a delimiter or completion;
enabling the dictionary does not recompose existing text. Digits, punctuation,
and common accented Latin letters participate in matching. Without a completion,
the typed run remains selectable.

Candidate pages overlap the partially visible last word, move over fifteen
frames, and accept another page at frame sixteen. Held arrows request another
page each update except every twentieth; requests during movement are ignored.
Text is clipped to the source area and the prediction window draws once.
The presentation supports forty candidate values over twenty source panes;
the local provider returns at most twenty completions.
