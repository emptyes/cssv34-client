import argparse
import os
from pathlib import Path
import re
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
    parser.add_argument('--header', type=Path)
    parser.add_argument('--case', default='all')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--debug', action='store_true')
    parser.add_argument('--optimization', choices=['0', '1', '2', '3'], default='2')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    header = (args.header or root / 'public/tier1/bitbuf.h').read_text()
    header = re.sub(r'^#include[^\n]*', '', header, flags=re.MULTILINE)
    source = (root / 'tier1/bitbuf.cpp').read_text()
    signatures = [
        'bf_write::bf_write()',
        'bf_write::bf_write( void *pData, int nBytes, int nBits )',
        'void bf_write::StartWriting(',
        'void bf_write::SeekToBit(',
        'const char* bf_write::GetDebugName()',
        'bool bf_write::WriteBitsFromBuffer(',
        'bf_read::bf_read()',
        'bf_read::bf_read( const void *pData, int nBytes, int nBits )',
        'bf_read::bf_read( const char *pDebugName, const void *pData, int nBytes, int nBits )',
        'void bf_read::StartReading(',
        'void bf_read::SetOverflowFlag()',
    ]
    with tempfile.TemporaryDirectory(prefix='cssv34-bitbuf-tests-') as directory:
        output = Path(directory)
        (output / 'bitbuf_under_test.h').write_text(header)
        (output / 'bitbuf_under_test.inc').write_text('\n'.join(function(source, s) for s in signatures))
        executable = output / 'bitbuf_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++11', '-O' + args.optimization, '-g',
                   '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                   '-I', str(output), str(Path(__file__).with_name('bitbuf_tests.cpp')),
                   '-o', str(executable)]
        if args.debug:
            command[1:1] = ['-D_DEBUG']
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable), args.case], check=True, timeout=20)


if __name__ == '__main__':
    main()
