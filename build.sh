#!/bin/sh
set -eu
cd "$(dirname "$0")"
VERSION=$(python3 -c '
import json, re
with open("mod.json", encoding="utf-8") as source:
    version = json.load(source)["version"]
if not isinstance(version, str) or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:[-+][A-Za-z0-9.-]+)?", version):
    raise SystemExit("Invalid version in mod.json")
print(version)
')
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
# Every archive starts from an empty directory; old staging files are ignored.
PACKAGE_DIR=$(mktemp -d "$PWD/build/package.XXXXXX")
trap 'rm -rf -- "$PACKAGE_DIR"' EXIT
trap 'exit 1' HUP INT TERM
mkdir "$PACKAGE_DIR/SokuLobbyAssist"
cp build/TrueRelative.dll mod.json SokuLobbyAssist.ini relative-levels.txt ratings-map.tsv LICENSE \
  third_party/curl/curl-ca-bundle.crt third_party/libcurl/COPYING.txt third_party/libcurl/MBEDTLS-LICENSE.txt '相对等级阈值说明.txt' "$PACKAGE_DIR/SokuLobbyAssist/"
(cd "$PACKAGE_DIR" && zip -qr installer.zip SokuLobbyAssist)
mv "$PACKAGE_DIR/installer.zip" "build/TrueRelative-$VERSION-install.zip"
