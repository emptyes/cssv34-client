import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    position = opening + 1
    while depth:
        if source[position] == '{':
            depth += 1
        elif source[position] == '}':
            depth -= 1
        position += 1
    return source[start:position] + '\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (args.source or root / 'game/client/cstrike/VGUI/cstrikeclientscoreboard.cpp').read_text()
    signatures = ('static bool HasClientModMarker(', 'static bool HasPossibleClientModMarker(',
                  'CON_COMMAND( cl_clientmod_list,')
    extracted = '\n'.join(function(source, name) for name in signatures)
    assert 'pUTF8Clan = HasClientModMarker( pi ) ? "CM" : ( HasPossibleClientModMarker( pi ) ? "CM?" : "" );' in source
    with tempfile.TemporaryDirectory(prefix='cssv34-clientmod-') as directory:
        output = Path(directory)
        (output / 'clientmod_under_test.inc').write_text(extracted)
        executable = output / 'clientmod_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++11', '-O2', '-g', '-Wall',
                   '-Wextra', '-Werror', '-I', str(output),
                   str(Path(__file__).with_name('clientmod_tests.cpp')), '-o', str(executable)]
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True, timeout=30)


if __name__ == '__main__':
    main()
