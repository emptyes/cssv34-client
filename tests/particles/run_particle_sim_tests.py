import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def block(source, signature):
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
    parser.add_argument('--case', default='all')
    args = parser.parse_args()
    root = args.source_root or Path(__file__).resolve().parents[2]
    source = (root / 'game/client/particlemgr.cpp').read_text()
    header = (root / 'game/client/particlemgr.h').read_text()
    typedef = re.search(r'typedef CUtlVectorFixedGrowable<[^;]+ParticleSimList_t;', header)
    declaration = re.search(r'void BuildParticleSimList\([^;]+;', header).group()
    methods = ''.join(block(source, signature) for signature in [
        'void CParticleMgr::BuildParticleSimList(',
        'static int CountParticleSystemActiveParticles(',
        'void CParticleMgr::UpdateNewEffects('])
    with tempfile.TemporaryDirectory(prefix='cssv34-particle-sim-') as directory:
        out = Path(directory)
        (out / 'sim_declaration.inc').write_text((typedef.group() if typedef else '') + '\n' + declaration)
        (out / 'sim_methods.inc').write_text(methods)
        executable = out / 'particle_sim_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++17', '-O2', '-g', '-Wall', '-Wextra',
                   '-Wno-unused-parameter', '-Wno-unused-local-typedefs', '-Wno-deprecated-copy', '-DPOSIX', '-DLINUX', '-D_LINUX', '-DGNUC',
                   '-DCOMPILER_GCC', '-DPLATFORM_64BITS', '-DNO_MALLOC_OVERRIDE', '-pthread',
                   '-isystem', str(root / 'public'), '-I', str(out),
                   str(Path(__file__).with_name('particle_sim_tests.cpp')),
                   '-Wl,--wrap=malloc', '-Wl,--wrap=realloc', '-Wl,--wrap=free', '-o', str(executable)]
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                             '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable), args.case], check=True, timeout=60)


if __name__ == '__main__':
    main()
