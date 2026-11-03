# Local channel placement

The C app loads `.channel-layout.json` beside the prepared `channels.json` after
loading the base catalog, imported saved placement, and local channel additions.
The default location is `Files/.local/native-assets/.channel-layout.json`. Keep
custom prepared assets under `Files/` too. This config filename is git ignored.
It is independent of the prepared-asset integrity manifest and works with both
graphics backends. Layout inspection with `--layout` does not load or save it.

After a completed channel move, normal shutdown saves all occupied positions
using atomic file replacement. Window close, Quit, Ctrl+C, and SIGTERM use the
same cleanup path. A forced process kill cannot run this cleanup. Only a layout
that differs from startup is written; cancelled drags, metadata changes, and
moving a channel away and back do not write. An untouched first run does not
create a config. A config edited externally while the app runs is preserved;
the app warns instead of overwriting those edits with its own moves.

Close the app before manually editing the config. Entries use stable channel IDs
from `channels.json` or `channels.local.json`, rather than display titles.
For example, this partial layout moves one installed channel to page 2, slot 6:

```json
{
    "schemaVersion": 1,
    "slots": [
        {
            "id": "0001000248414341",
            "page": 1,
            "index": 5
        }
    ]
}
```

Pages are numbered 0–3. Indices are numbered 0–11 in row order across each 4×3
page. The example only applies if that ID is installed and its destination is
free. A saved config includes the Disc Channel at page 0, index 0 and an entry
for every other occupied channel. Missing entries keep their catalog positions.
To swap two channels manually, change both destinations in the same edit.

The loader checks the entire layout before changing any position. Two channels
cannot share a slot, including a channel whose entry was omitted. An ID cannot
appear twice, and the Disc Channel cannot move or share its fixed slot. Unknown
IDs (including removed channels), out-of-range or noninteger positions, malformed
JSON, duplicate or unrecognized fields, and unsupported schema versions also
reject the whole config. Every entry must have exactly `id`, `page`, and `index`;
the root must have exactly `schemaVersion` and `slots`.

Rejected configs print a warning to standard error and leave every channel at
its catalog position. They are not automatically rewritten: correct or delete
the file while the app is closed, or make a real channel move and exit to save a
new valid layout. Unreadable, oversized (over 64 KiB), or nonregular config files
also warn and keep defaults, with saving disabled for that session.

These are verified local C app behaviors. This JSON format and its recovery
rules are not a claim about native Wii save behavior.
