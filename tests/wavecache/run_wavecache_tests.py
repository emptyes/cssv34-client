import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, position = 1, opening + 1
    while depth:
        depth += (source[position] == '{') - (source[position] == '}')
        position += 1
    return source[start:position] + '\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--case', choices=['all', 'valid', 'destination', 'source', 'partial', 'callback', 'invalid', 'handles'], default='all')
    args = parser.parse_args()
    root = args.source_root or Path(__file__).resolve().parents[2]
    source = (root / 'engine/audio/snd_wave_data.cpp').read_text()
    extracted = ''.join(function(source, signature) for signature in (
        'void CAsyncWaveData::OnAsyncCompleted(',
        'bool CAsyncWaveData::BlockingCopyData(',
        'bool CAsyncWaveData::BlockingGetDataPointer(',
    ))
    with tempfile.TemporaryDirectory(prefix='cssv34-wavecache-') as directory:
        out = Path(directory)
        (out / 'wavecache_under_test.inc').write_text(extracted)
        executable = out / 'wavecache_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++17', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-parameter', '-I', str(out), str(Path(__file__).with_name('wavecache_tests.cpp')), '-o', str(executable)]
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable), args.case], check=True, timeout=30)


if __name__ == '__main__':
    main()
