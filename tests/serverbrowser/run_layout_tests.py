import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def function(source, signature):
    first = source.index(signature)
    opening = source.index('{', first)
    depth = 1
    position = opening + 1
    while depth:
        if source[position] == '{':
            depth += 1
        elif source[position] == '}':
            depth -= 1
        position += 1
    return source[first:position] + '\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (args.source or root / 'serverbrowser/BaseGamesPage.cpp').read_text()
    body = function(source, 'void CBaseGamesPage::UpdateFilterAndQuickListVisibility()')
    with tempfile.TemporaryDirectory(prefix='cssv34-layout-tests-') as directory:
        output = Path(directory)
        (output / 'layout_under_test.inc').write_text(body)
        executable = output / 'layout_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++11', '-O1', '-g',
                   '-Wall', '-Wextra', '-Werror', '-I', str(output),
                   str(Path(__file__).with_name('layout_tests.cpp')),
                   '-o', str(executable)]
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True, timeout=30)


if __name__ == '__main__':
    main()
