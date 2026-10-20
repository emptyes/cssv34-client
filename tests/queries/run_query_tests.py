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
    source = (args.source or root / 'engine/baseclientstate.cpp').read_text()
    assert 'static ConVar cl_query_trace( "cl_query_trace", "0", FCVAR_NONE,' in source
    extracted = function(source, 'bool CBaseClientState::ProcessGetCvarValue(')
    with tempfile.TemporaryDirectory(prefix='cssv34-query-') as directory:
        output = Path(directory)
        (output / 'query_under_test.inc').write_text(extracted)
        executable = output / 'query_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++11', '-O2', '-g', '-Wall',
                   '-Wextra', '-Werror', '-I', str(output),
                   str(Path(__file__).with_name('query_tests.cpp')), '-o', str(executable)]
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True, timeout=30)


if __name__ == '__main__':
    main()
