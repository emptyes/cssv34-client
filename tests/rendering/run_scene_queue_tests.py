"""Extract actual scene queue methods and use real SDK containers.

Choreography/event dispatch is a double. Tests queue order, requeue, nested calls,
clear/dedup and temporary-container allocations, not visual or engine timing.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def block(text, signature):
    first = text.index(signature)
    opening = text.index('{', first)
    depth, pos = 1, opening + 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[first:pos]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--debug', action='store_true')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--case', choices=['behavior', 'measure', 'allocations', 'all'], default='all')
    args = parser.parse_args()
    root = args.source_root or Path(__file__).resolve().parents[2]
    source = (root / 'game/client/c_sceneentity.cpp').read_bytes().decode('latin1')
    header = (root / 'game/client/c_sceneentity.h').read_bytes().decode('latin1')
    methods = '\n'.join(block(source, signature) for signature in [
        'void C_SceneEntity::CheckQueuedEvents()', 'void C_SceneEntity::WipeQueuedEvents()',
        'void C_SceneEntity::QueueStartEvent('])
    entry = block(header, 'struct QueuedEvents_t') + ';'
    with tempfile.TemporaryDirectory(prefix='cssv34-scene-queue-') as directory:
        out = Path(directory)
        (out / 'scene_methods.inc').write_text(methods)
        (out / 'scene_entry.inc').write_text(entry)
        executable = out / 'scene_queue_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++11', '-O2', '-g', '-Wall', '-Wextra',
                   '-Wno-unused-parameter', '-Wno-unused-local-typedefs', '-Wno-deprecated-copy',
                   '-DPOSIX', '-DLINUX', '-D_LINUX', '-DGNUC', '-DCOMPILER_GCC', '-DPLATFORM_64BITS',
                   '-DNO_MALLOC_OVERRIDE', '-isystem', str(root / 'public'), '-I', str(out),
                   str(Path(__file__).with_name('scene_queue_tests.cpp')),
                   '-Wl,--wrap=malloc', '-Wl,--wrap=realloc', '-Wl,--wrap=free', '-o', str(executable)]
        if args.debug: command += ['-O0', '-D_GLIBCXX_ASSERTIONS']
        if args.sanitize: command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                                    '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable), args.case], check=True, timeout=90)


if __name__ == '__main__':
    main()
