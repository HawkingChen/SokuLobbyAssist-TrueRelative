# TrueRelative

`sh build.sh` creates `build/package/` with the plugin DLL, `mod.json`, a manual
mapping file, and an HTTPS helper. The helper is the [official curl for Windows
8.22.0_3](https://curl.se/windows/) x64 build with its CA bundle and COPYING
file. WinHTTP and WinINet could not establish HTTPS in this Proton prefix; the
bundled curl downloaded and parsed the public ratings JSON successfully.

The plugin accepts only the verified Chinese lobby 0.7.5.2 DLL with SHA-256
`dbce5a6a59e94e2d4c43bcfbd69fce35ad9804a4dd20e76aa28417a3c6ab28cd` or the
installed 0.7.5.4 DLL with SHA-256
`526d8437c00a85cdbeb5388ea8bfc18229cb649278f079e000e2799d5de50c87`.
The 0.7.5.4 `InLobbyMenu` vtable was located from its MSVC RTTI and checked
against the four function pointers at RVA `0x2129a4`: `0x104f60`, `0x1059b0`,
`0x119dc0`, and `0x11bc20`. The vtable location and first two methods remain
unchanged from 0.7.5.2; the last two methods moved by `0x40` bytes. The plugin
has a separate exact vtable table for each supported hash and rejects any other
binary. Player records are still expected to use the 0.7.5.2 layout; the new
build's lobby menu accesses the same nearby state offsets, but live multiplayer
rendering on 0.7.5.4 still needs an in-game check.
The player vector at menu offset `0x220` uses 100-byte
records: game profile name at `+4`, position at `+0x3c/+0x40`, and battle
status at `+0x44`. The render offset is read from `+0x198/+0x19c`; the
chat-editing flag from `+0x1154` gates F8. These offsets were checked against
the 0.7.5.2 DLL disassembly and synthetic memory fixtures. The in-game
visual positions still require observation.

Version 0.7.2 merges repeated lobby records with the same player ID before
selecting or drawing cards, preferring a waiting record. This prevents one
player from receiving both a compact and an F8 detail card in one frame.
Version 0.7.4 draws detailed cards first and moves compact cards to free nearby
positions, suppressing a compact card when no clear position is available.
Detailed cards label the overall rating, separate it from the character list with
a thin line, and use the 2250 boundary between 相对5 and 相对6.

The ratings worker fetches the full public endpoint
`https://soku.cleista.cc/api/ratings?min=1&status=all` once and refreshes it
after 24 hours. It stores `ratings-cache.json` next to the plugin. A failed
refresh retains the old records and retries after one minute. The overlay
labels stale results with the cache timestamp. A unique `playedAs` match takes
priority; a unique display-name match is accepted, while duplicates are unknown.
`ratings-map.tsv` accepts `local game profile<TAB>public handle or unique name` lines for
manual association after a game restart.

Version 0.7 shows a player name, score and relative level in a compact translucent card.
The total-score row also shows RD after the score in a smaller font. `relative-levels.txt`
holds editable score thresholds and the 相对1萌新 game-count rule; edits take effect
after restarting the game.
F8 details show the three most played characters using Chinese names; F9 toggles
the local player's separate card. F10 toggles automatic selection of the nearest
player and the local-player card together, both enabled by default; the distance is
measured from the local avatar. With automatic mode off, F8 manually cycles nearby
players. Only the selected nearby player and optional local-player card are drawn;
compact waiting-player cards are disabled. `SokuLobbyAssist.ini` sets avatar card placement
(`above`, `center`, or `right`), offsets, the own-card screen position, and an optional
public account name or handle for the local player.
When no own account is configured, the plugin asks the public name lookup endpoint
for the local game profile name once, then caches the claimed handle for one day.
The lookup runs off the render thread; an explicit account setting still overrides it.
For other visible players whose direct leaderboard lookup fails, the plugin queues one
public claimed-name lookup per unique profile. It processes requests serially and caches
positive and negative answers for a day in `other-claim-cache.tsv`. Duplicate names
visible in the lobby remain unknown. A score below 1400 with fewer than 100 total
games is shown as 相对1萌新; the remaining scores below 1600 are 相对<3.
The score card omits matching diagnostics. F7 toggles the relative-tier label; it is
enabled by default. The network preflight code is no longer part of this plugin; no
game join function is patched.

`python3 test_ratings.py` passed under GE-Proton10-12 with assertions enabled:
version rejection, player layout, stable F8 selection across movement,
local-player exclusion while the camera pans, rating matching by game profile
and explicit account, duplicate rejection, and stale cache.
`RatingTest.exe --fetch`
downloaded and parsed 6,693 live records through the bundled curl helper.

For direct GE-Proton launches from this shell, the 32-bit Steam runtime library
path must be added after `/usr/lib32:/usr/lib` in `LD_LIBRARY_PATH`; otherwise
the game exits during graphics initialization. With that environment, the
isolated game copy reached a window with all optional modules disabled. The
original lobby 0.7.5.2 alone exits during its module initialization, with the
test plugin disabled. The full original module set also exits at lobby
initialization. A prior isolated startup loaded the plugin and passed its
version/vtable guard, but the game exited before a lobby could be entered.
The cause of the lobby startup failure under direct Wine remains unresolved.
Therefore desktop multiplayer, controller, game mode, and avatar placement remain
unverified in the isolated environment. After the user confirmed the normal Steam launch reaches the lobby,
the test package was installed and enabled for the next start. The previous
`ModLoaderSettings.json` is saved at
`build/ModLoaderSettings.before-SokuLobbyAssist.json`.

After an eventual installation, disable by setting
`modules/SokuLobbyAssist/TrueRelative.dll` to `"enabled": false` in
`ModLoaderSettings.json`; restoration is enabling that entry again. Removing
the `SokuLobbyAssist` folder also removes the plugin. The existing lobby DLL
is never modified.
