"""Real SDK sample owners/math; extracted production Hermite helpers."""
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
    parser.add_argument('--case', choices=['all', 'behavior', 'measure', 'allocations', 'direct'], default='all')
    args = parser.parse_args()
    root = args.source_root or Path(__file__).resolve().parents[2]
    source = (root / 'game/client/interpolatedvar.h').read_text()
    names = ['TimeFixup2_Hermite', 'TimeFixup_Hermite', '_Interpolate_Hermite',
             '_Derivative_Hermite', '_Derivative_Hermite_SmoothVelocity']
    entries = source[source.index('template< typename Type, bool IS_ARRAY >\nstruct CInterpolatedVarEntryBase'):source.index('template<typename T>\nclass CSimpleRingBuffer')]
    methods = ''.join('template< typename Type, bool IS_ARRAY >\n' + block(source, 'inline void CInterpolatedVarArrayBase<Type, IS_ARRAY>::' + n + '(') for n in names)
    declarations = '\n'.join(re.search(r'\bvoid\s+' + re.escape(n) + r'\([^;]+;', source).group() for n in names)
    with tempfile.TemporaryDirectory(prefix='cssv34-hermite-') as directory:
        out = Path(directory)
        (out / 'entries.inc').write_text(entries)
        (out / 'declarations.inc').write_text(declarations)
        (out / 'methods.inc').write_text(methods)
        executable = out / 'hermite_fixup_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++17', '-O2', '-g', '-Wall', '-Wextra',
                   '-fno-builtin-malloc', '-fno-builtin-free', '-Wno-unused-parameter',
                   '-Wno-unused-local-typedefs', '-Wno-deprecated-copy', '-DPOSIX', '-DLINUX',
                   '-D_LINUX', '-DGNUC', '-DCOMPILER_GCC', '-DPLATFORM_64BITS', '-DNO_MALLOC_OVERRIDE',
                   '-isystem', str(root / 'public'), '-isystem', str(root / 'public/tier1'),
                   '-I', str(root / 'game/client'), '-I', str(out),
                   str(Path(__file__).with_name('hermite_fixup_tests.cpp')),
                   '-Wl,--wrap=_Znam', '-Wl,--wrap=_ZdaPv', '-Wl,--wrap=_ZdaPvm',
                   '-o', str(executable)]
        if args.debug:
            command.append('-DTEST_DEBUG_ASSERTS')
        if args.case in ['all', 'direct']:
            command.append('-DTEST_DIRECT_DERIVATIVE')
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                             '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable), args.case], check=True, timeout=60)


if __name__ == '__main__':
    main()
