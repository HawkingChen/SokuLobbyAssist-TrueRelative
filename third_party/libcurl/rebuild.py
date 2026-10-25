"""Rebuild the vendored x86 Windows static libraries with Zig, CMake and Ninja.

Run from any directory. Override ZIG, CMAKE or NINJA if they are not on PATH.
Source archives are vendored so the build does not need network access.
"""
import hashlib
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tarfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
BUILD = ROOT / 'build' / 'network'
BUILD.mkdir(parents=True, exist_ok=True)


def tool(name, fallback=None):
    value = os.environ.get(name.upper()) or shutil.which(name) or fallback
    if not value:
        raise SystemExit(f'{name} is required; set {name.upper()} to its path')
    return str(Path(value).resolve())


zig = tool('zig', ROOT / 'zig-x86_64-linux-0.14.1' / 'zig')
cmake = tool('cmake')
ninja = tool('ninja')
for line in (HERE / 'SHA256SUMS').read_text().splitlines():
    digest, name = line.split()
    archive = HERE / name
    if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
        raise SystemExit(f'Checksum mismatch: {name}')
    with tarfile.open(archive) as source:
        source.extractall(BUILD, filter='data')

for name, args in [('cc', 'cc -target x86-windows-gnu'), ('ar', 'ar'),
                   ('ranlib', 'ranlib'), ('rc', 'rc')]:
    wrapper = BUILD / ('zig-' + name)
    wrapper.write_text(f'#!/bin/sh\nexec {shlex.quote(zig)} {args} "$@"\n')
    wrapper.chmod(0o755)

common = ['-G', 'Ninja', f'-DCMAKE_MAKE_PROGRAM={ninja}',
          '-DCMAKE_SYSTEM_NAME=Windows', '-DCMAKE_BUILD_TYPE=Release']
for name, variable in [('cc', 'C_COMPILER'), ('ar', 'AR'),
                       ('ranlib', 'RANLIB'), ('rc', 'RC_COMPILER')]:
    common.append(f'-DCMAKE_{variable}={BUILD / ("zig-" + name)}')


def build(source, destination, options):
    subprocess.run([cmake, '-S', str(source), '-B', str(destination),
                    *common, *options], check=True)
    subprocess.run([cmake, '--build', str(destination), '--parallel', '4'], check=True)


curl_build = BUILD / 'curl-build'
build(BUILD / 'curl-8.23.0', curl_build, [
    '-DBUILD_SHARED_LIBS=OFF', '-DBUILD_STATIC_LIBS=ON', '-DBUILD_CURL_EXE=OFF',
    '-DBUILD_TESTING=OFF', '-DBUILD_EXAMPLES=OFF', '-DBUILD_LIBCURL_DOCS=OFF',
    '-DBUILD_MISC_DOCS=OFF', '-DCURL_USE_SCHANNEL=ON', '-DCURL_USE_OPENSSL=OFF',
    '-DCURL_USE_MBEDTLS=OFF', '-DCURL_USE_LIBPSL=OFF', '-DCURL_USE_LIBSSH2=OFF',
    '-DCURL_ZLIB=OFF', '-DCURL_BROTLI=OFF', '-DCURL_ZSTD=OFF', '-DUSE_NGHTTP2=OFF',
])
shutil.copy2(curl_build / 'lib/libcurl.a', HERE)
shutil.copytree(BUILD / 'curl-8.23.0/include/curl', HERE / 'include/curl', dirs_exist_ok=True)
print('Static libraries rebuilt. Run sh build.sh from the project root.')
