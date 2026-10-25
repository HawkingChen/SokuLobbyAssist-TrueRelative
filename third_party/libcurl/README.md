# Static HTTPS dependency

- curl 8.23.0 (curl license, COPYING.txt).
- Windows TLS backend: Schannel, using the Windows/Wine system certificate store.
- Certificate and hostname verification remain enabled. No curl.exe or CA bundle is needed.
- libcurl.a targets x86-windows-gnu and is built with Zig 0.14.1. Mbed TLS is not linked.

Rebuild locally with `python3 third_party/libcurl/rebuild.py`, then `sh build.sh`.
CMAKE, NINJA and ZIG can point to installed tools. Archives are vendored and
checked against SHA256SUMS; no network download is needed. The older Mbed TLS
archive and libraries may still be present as retained build inputs but are unused.

Proton networking tests must run inside Steam Linux Runtime, which supplies
the GnuTLS libraries used by Wine Schannel. Directly invoking Wine from a bare
host shell can report SEC_E_SECPKG_NOT_FOUND even if the game runtime works.
