# TrueRelative verification notes

## Current version: 0.8.0

F9 or Start+Select opens the lobby settings menu. F8 selects nearby players
in manual mode; F10 switches automatic display on/off independently of the
local player card. The menu controls the local card, relative labels, card
placement, rating thresholds, and optional all-player markers. Settings are
saved to `SokuLobbyAssist.ini` and `relative-levels.txt`.

Relative labels and all-player markers start disabled. The packaged relative
boundaries are 2800/2500/2300/2000/1800. The newbie rule is rating <= 1400 and
matches <= 100; other ratings below the relative-3 boundary show 相对<3.
The cards show the three most-played characters with abbreviated Chinese names.
Repeated lobby records are merged by player ID. Only the selected nearby
player and optional local-player card receive cards; there are no additional
compact waiting-player cards. Markers use a green halo or a red frame with !!!.
F7 has no assigned action in this version.

## Build and packaging

Run `sh build.sh` with Python 3, zip, and Zig 0.14.1 available. The script uses
`ZIG` if set, otherwise Zig from PATH or `zig-x86_64-linux-0.14.1/zig`.
It reads the version from `mod.json` and creates
`build/TrueRelative-<version>-install.zip` (currently 0.8.0).
Each package uses a new temporary directory and an explicit file list, so
files left in previous staging directories cannot enter the archive. A complete
ZIP replaces the local output only after packaging succeeds.
The archive has a single `SokuLobbyAssist/` top-level directory and includes
MIT `LICENSE`, curl `COPYING.txt`, and `MBEDTLS-LICENSE.txt`.
The MIT file was added after the published v0.8.0 tag; the existing published
0.8.0 installer has not been replaced with this newly packaged archive.

## Lobby compatibility

The plugin accepts these verified Chinese lobby binaries:

| Lobby version | SHA-256 |
| --- | --- |
| 0.7.5.2 | `dbce5a6a59e94e2d4c43bcfbd69fce35ad9804a4dd20e76aa28417a3c6ab28cd` |
| 0.7.5.4 | `526d8437c00a85cdbeb5388ea8bfc18229cb649278f079e000e2799d5de50c87` |

Each supported binary has a checked vtable entry set; other binaries are
rejected. `InLobbyMenu` has its vtable at RVA `0x2129a4`. The four entries are
`0x104f60`, `0x1059b0`, `0x119d80`, `0x11bbe0` in 0.7.5.2 and
`0x104f60`, `0x1059b0`, `0x119dc0`, `0x11bc20` in 0.7.5.4.

The player vector at menu offset `0x220` uses 100-byte records: profile name
at `+4`, position at `+0x3c/+0x40`, and battle status at `+0x44`.
Render offsets are at `+0x198/+0x19c`; the chat-editing flag is at `+0x1154`.
These layouts were checked using disassembly and synthetic memory fixtures.
The user confirmed the updated lobby works in the normal game launch.
The supplied 0.8.0 screenshots show the menu, rating cards, green halo and
red danger marker in the lobby; redacted examples are in `docs/screenshots/`.
These observations do not replace tests of every avatar size or controller mode.

## Ratings and HTTPS

HTTPS runs inside the 32-bit DLL using static libcurl 8.23.0 and Mbed TLS 3.6.7
under its Apache-2.0 option. Sources, licenses and rebuild instructions are in
`third_party/libcurl/`. Keep `curl-ca-bundle.crt` beside the plugin.
Certificate and hostname verification are enabled. Transfers are limited to
HTTPS, 12 MiB, five redirects and 25 seconds, and serialized for the TLS RNG.
`TrueRelative-network.log` records transport, HTTP, cache and parse failures.

The worker fetches `https://soku.cleista.cc/api/v1/ratings/export.json` and
refreshes after 10 minutes using `If-None-Match` when an ETag is available.
HTTP 304 reuses local records and renews their cache timestamp. Failed refreshes
retain the old data and retry after one minute. Stale cards show the cache time.
The leaderboard cache is `ratings-cache.json` beside the plugin.

Unique profile (`playedAs`) matches take priority over display-name matches;
ambiguous matches remain unknown. `ratings-map.tsv` supports explicit local
profile-to-account associations. An explicit local account setting overrides
automatic lookup. Otherwise unmatched local and nearby player profiles are
looked up asynchronously, with positive and negative claimed-name results cached
for one day. Other-player mappings are stored in `other-claim-cache.tsv`.

## Verification record and limits

Before the 0.8.0 release, `sh build.sh` and `python3 test_ratings.py` passed
under GE-Proton10-12. The fixture suite checks version rejection, player layout,
selection across movement, camera-relative local-player exclusion, profile and
account matching, ambiguous-name rejection, stale cache, display mode transitions,
local-card independence, input masking, and comment-preserving config writes.
Network checks cover cache read/write, response-size limits, rejection of a
missing CA bundle, and queued claimed-name lookups. Export revalidation returned
HTTP 304. The suite uses the installed supported lobby DLL and the rejected
0.7.5.0 fixture in `test_fixtures/`.

Earlier direct-Wine launches of an isolated game copy failed during lobby
initialization even without this plugin. That isolated-launch problem was not
resolved; the subsequent normal Steam launch and user screenshots are separate
positive evidence. Native Windows antivirus coverage and a complete SteamOS
desktop/game-mode controller test matrix have not been recorded for 0.8.0.

Disable 真实相对 in SokuLauncher plugin settings to turn the plugin off;
re-enable it to restore it. No on-disk lobby DLL is modified, and the former
network preflight feature is absent.

## 自动目标切换缓冲

自动模式下，新候选连续最近达到 300 毫秒才切换；当前目标不在附近前三名时立即重新选择。无人、退出大厅、切换显示模式或大厅缩放隐藏评分框时清空缓冲。手动 F8 选择保持原逻辑。

可在 Linux 原生运行：`cc -std=c11 -Wall -Wextra -Werror auto_target_test.c -o /tmp/auto_target_test && /tmp/auto_target_test`。测试覆盖首次选择、延迟边界、反复交错、候选替换、目标离开、无人和计时器回绕。游戏内走动切换效果仍需本地验证。

## 手柄 Start+Select 菜单入口修正

原实现只检查游戏映射后的 pause/select 动作，不能据此识别物理 Start/Select。新增动态加载 XInput 并逐台检查 Start+Back（Select）组合，保留游戏动作兼容路径。无 XInput 时仍可用 F9 和原动作映射。聊天框打开及游戏不在前台时不触发；按住仅切换一次，释放后才能再次切换。

Linux 可运行 `cc -std=c11 -Wall -Wextra -Werror pad_shortcut_test.c -o /tmp/pad_shortcut_test && /tmp/pad_shortcut_test`，验证实体按钮掩码、单键不触发、按住去重、释放重按、聊天及前台限制。MenuTest 新增游戏 pause/select 均未绑定时的物理组合开关回归。Steam Deck 的 Steam Input/Proton 实机尚需用户验证；纯 DirectInput 设备若未转换为 XInput，物理读取路径不覆盖。
