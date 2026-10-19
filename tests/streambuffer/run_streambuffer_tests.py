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
        depth += (source[position] == '{') - (source[position] == '}')
        position += 1
    return source[first:position] + '\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    production = args.source_root or root
    stream = (production / 'tier2/utlstreambuffer.cpp').read_text(encoding='latin-1')
    header = (root / 'public/tier2/utlstreambuffer.h').read_text(encoding='latin-1')
    buffer = (root / 'tier1/utlbuffer.cpp').read_text(encoding='latin-1')
    methods = '\n'.join(function(stream, signature) for signature in (
        'CUtlStreamBuffer::CUtlStreamBuffer( )',
        'CUtlStreamBuffer::CUtlStreamBuffer( const char',
        'void CUtlStreamBuffer::Close()', 'CUtlStreamBuffer::~CUtlStreamBuffer()',
        'void CUtlStreamBuffer::Open(', 'bool CUtlStreamBuffer::IsOpen()',
        'void CUtlStreamBuffer::GrowAllocatedSize(',
        'bool CUtlStreamBuffer::StreamPutOverflow(',
        'int CUtlStreamBuffer::ReadBytesFromFile(',
        'FileHandle_t CUtlStreamBuffer::OpenFile('))
    methods += '\n'.join(function(buffer, signature) for signature in (
        'bool CUtlBuffer::CheckPut(', 'void CUtlBuffer::Put( const void',
        'void CUtlBuffer::SeekPut(', 'void CUtlBuffer::AddNullTermination( int'))
    with tempfile.TemporaryDirectory(prefix='cssv34-stream-tests-') as directory:
        out = Path(directory)
        (out / 'stream_declaration.inc').write_text(function(header, 'class CUtlStreamBuffer')[:-1] + ';\n')
        (out / 'stream_under_test.inc').write_text(methods)
        executable = out / 'streambuffer_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++11', '-O2', '-g', '-Wall',
                   '-Wextra', '-Werror', '-I', str(out),
                   str(Path(__file__).with_name('streambuffer_tests.cpp')), '-o', str(executable)]
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True, timeout=60)


if __name__ == '__main__':
    main()
