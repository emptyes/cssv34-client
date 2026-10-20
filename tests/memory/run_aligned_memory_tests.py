import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--debug', action='store_true')
    parser.add_argument('--allow-baseline-casts', action='store_true')
    args = parser.parse_args()
    root = args.source_root or Path(__file__).resolve().parents[2]
    source = (root / 'public/tier1/utlmemory.h').read_text()
    first = source.index('template< class T, int nAlignment >\nclass CUtlMemoryAligned')
    last = source.index('#include "tier0/memdbgoff.h"', first)
    with tempfile.TemporaryDirectory(prefix='cssv34-aligned-memory-') as directory:
        output = Path(directory)
        (output / 'memory_under_test.inc').write_text(source[first:last])
        executable = output / 'aligned_memory_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++17', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                   '-I', str(output), str(Path(__file__).with_name('aligned_memory_tests.cpp')), '-o', str(executable)]
        if args.debug:
            command.append('-DTEST_DEBUG')
        if args.allow_baseline_casts:
            command += ['-fpermissive', '-Wno-error=pointer-to-int-cast', '-Wno-error']
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True, timeout=30)


if __name__ == '__main__':
    main()
