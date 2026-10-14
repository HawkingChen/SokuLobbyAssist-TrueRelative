#!/bin/sh
set -eu
cd "$(dirname "$0")"
if [ -z "${ZIG:-}" ]; then
  if command -v zig >/dev/null 2>&1; then
    ZIG=zig
  else
    ZIG=./zig-x86_64-linux-0.14.1/zig
  fi
fi
mkdir -p build
"$ZIG" cc -target x86-windows-gnu -O2 -Wall -Wextra -Werror -shared \
  TrueRelative.c Ratings.c -luser32 -ld3d9 -ladvapi32 \
  -o build/TrueRelative.dll
"$ZIG" cc -target x86-windows-gnu -O0 -UNDEBUG -Wall -Wextra -Werror \
  menu_test.c Ratings.c -luser32 -ld3d9 -ladvapi32 -o build/MenuTest.exe
"$ZIG" cc -target x86-windows-gnu -O0 -UNDEBUG -Wall -Wextra -Werror \
  rating_test.c -o build/RatingTest.exe
cp third_party/curl/curl.exe third_party/curl/curl-ca-bundle.crt build/
mkdir -p build/package
cp build/TrueRelative.dll mod.json SokuLobbyAssist.ini relative-levels.txt ratings-map.tsv third_party/curl/curl.exe \
  third_party/curl/curl-ca-bundle.crt third_party/curl/COPYING.txt '相对等级阈值说明.txt' build/package/
mkdir -p build/launcher-package/SokuLobbyAssist
cp build/package/* build/launcher-package/SokuLobbyAssist/
rm -f build/TrueRelative-0.7.7-install.zip
(cd build/launcher-package && zip -qr ../TrueRelative-0.7.7-install.zip SokuLobbyAssist)
