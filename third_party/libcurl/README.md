# Static HTTPS dependencies

- curl 8.23.0: https://curl.se/download/curl-8.23.0.tar.xz (curl license, COPYING.txt).
- Mbed TLS 3.6.7 LTS: https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.7
  (used under Apache-2.0; full license in MBEDTLS-LICENSE.txt).

The .a files target x86-windows-gnu and were built with Zig 0.14.1.
They are statically linked into TrueRelative.dll; no curl.exe or libcurl DLL is needed.
TLS uses Mbed TLS and the bundled CA file, independent of Windows/Wine Schannel.
Source archive checksums are recorded in SHA256SUMS.

To rebuild on Linux, install CMake, Ninja and Zig 0.14.1, then run:

```sh
python3 third_party/libcurl/rebuild.py
sh build.sh
```

ZIG, CMAKE and NINJA can specify the executable paths. The dependency rebuild
uses only the included archives and performs no network downloads.
