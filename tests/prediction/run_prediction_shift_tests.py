"""Test extracted production pointer rotation against an independent index oracle."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def block(text, signature):
    first = text.index(signature)
    opening = text.index('{', first)
    depth, pos = 1, opening + 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[first:pos] + '\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--debug', action='store_true')
    parser.add_argument('--case', choices=['all', 'behavior', 'measure', 'allocations'], default='all')
    parser.add_argument('--no-prediction', action='store_true')
    args = parser.parse_args()
    root = args.source_root or Path(__file__).resolve().parents[2]
    source = (root / 'game/client/c_baseentity.cpp').read_text()
    header = (root / 'game/client/c_baseentity.h').read_text()
    limit = re.search(r'^#define\s+MULTIPLAYER_BACKUP\s+\d+', header, re.MULTILINE).group()
    body = block(source, 'void C_BaseEntity::ShiftIntermediateDataForward(')
    with tempfile.TemporaryDirectory(prefix='cssv34-prediction-shift-') as directory:
        out = Path(directory)
        (out / 'shift_under_test.inc').write_text(limit + '\n' + body)
        (out / 'shift_limit.inc').write_text(limit + '\n')
        executable = out / 'prediction_shift_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++11', '-O2', '-g', '-Wall', '-Wextra',
                   '-fno-builtin-malloc', '-fno-builtin-realloc', '-fno-builtin-free',
                   '-Wno-unused-parameter', '-Wno-unused-local-typedefs', '-Wno-deprecated-copy',
                   '-DPOSIX', '-DLINUX', '-D_LINUX', '-DGNUC', '-DCOMPILER_GCC', '-DPLATFORM_64BITS',
                   '-DNO_MALLOC_OVERRIDE', '-isystem', str(root / 'public'), '-I', str(out),
                   str(Path(__file__).with_name('prediction_shift_tests.cpp')),
                   '-Wl,--wrap=malloc', '-Wl,--wrap=realloc', '-Wl,--wrap=free',
                   '-Wl,--wrap=_Znwm', '-Wl,--wrap=_Znam', '-o', str(executable)]
        if args.debug:
            command.append('-DTEST_DEBUG_ASSERTS')
        if args.no_prediction:
            command.append('-DNO_ENTITY_PREDICTION')
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                             '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable), args.case], check=True, timeout=60)


if __name__ == '__main__':
    main()
