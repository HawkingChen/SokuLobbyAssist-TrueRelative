"""Run the rating overlay's Windows fixture tests under the installed Proton."""
import os
import subprocess

ROOT = os.path.dirname(os.path.abspath(__file__))
WINE = '/home/deck/.local/share/Steam/compatibilitytools.d/GE-Proton10-12/files/bin/wine'
ENV = dict(os.environ, WINEDEBUG='-all',
           WINEPREFIX='/home/deck/.local/share/Steam/steamapps/compatdata/4198759020/pfx')


def execute(name, *args):
    result = subprocess.run([WINE, os.path.join(ROOT, 'build', name), *args],
                            env=ENV, capture_output=True, text=True, timeout=25)
    assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
    print(result.stdout.strip(), flush=True)


def windows_path(path):
    return 'Z:' + path.replace('/', '\\')


execute('MenuTest.exe',
        windows_path('/run/media/deck/share/fxtz/th123/modules/SokuLobbiesMod/SokuLobbiesMod.dll'),
        windows_path('/home/deck/SokuLobbiesMod-0.7.5.0-backup-20260920/SokuLobbiesMod.dll'))
execute('RatingTest.exe')
execute('RatingTest.exe', '--claim')
if os.path.exists('/tmp/soku-rating.json'):
    execute('RatingTest.exe', windows_path('/tmp/soku-rating.json'))
