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
CURL_ROOT=third_party/libcurl
CURL_FLAGS="-DCURL_STATICLIB -I$CURL_ROOT/include"
CURL_LIBS="$CURL_ROOT/libcurl.a $CURL_ROOT/libmbedtls.a $CURL_ROOT/libmbedx509.a $CURL_ROOT/libmbedcrypto.a -lws2_32 -lcrypt32 -lsecur32 -lbcrypt -liphlpapi -lnormaliz -lwldap32 -ladvapi32"
"$ZIG" cc -target x86-windows-gnu -O2 -Wall -Wextra -Werror -DTRUE_RELATIVE_VISUALS -shared \
  TrueRelative.c LobbyVisual.c Ratings.c $CURL_FLAGS $CURL_LIBS -luser32 -ld3d9 -ladvapi32 \
  -o build/TrueRelative.dll
"$ZIG" cc -target x86-windows-gnu -O0 -UNDEBUG -Wall -Wextra -Werror -DTRUE_RELATIVE_VISUALS \
  menu_test.c LobbyVisual.c Ratings.c $CURL_FLAGS $CURL_LIBS -luser32 -ld3d9 -ladvapi32 -o build/MenuTest.exe
"$ZIG" cc -target x86-windows-gnu -O0 -UNDEBUG -Wall -Wextra -Werror \
  rating_test.c $CURL_FLAGS $CURL_LIBS -o build/RatingTest.exe
cp third_party/curl/curl-ca-bundle.crt build/
mkdir -p build/package
# Remove the obsolete external downloader from reused staging directories.
rm -f build/package/curl.exe build/launcher-package/SokuLobbyAssist/curl.exe
cp build/TrueRelative.dll mod.json SokuLobbyAssist.ini relative-levels.txt ratings-map.tsv \
  third_party/curl/curl-ca-bundle.crt third_party/libcurl/COPYING.txt third_party/libcurl/MBEDTLS-LICENSE.txt '相对等级阈值说明.txt' build/package/
mkdir -p build/launcher-package/SokuLobbyAssist
cp build/package/* build/launcher-package/SokuLobbyAssist/
rm -f build/TrueRelative-0.7.9-install.zip
(cd build/launcher-package && zip -qr ../TrueRelative-0.7.9-install.zip SokuLobbyAssist)
