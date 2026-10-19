import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def block(text, signature):
    first = text.index(signature)
    opening = text.index('{', first)
    depth = 1
    position = opening + 1
    while depth:
        depth += (text[position] == '{') - (text[position] == '}')
        position += 1
    return text[first:position] + '\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--case', choices=['all', 'depth', 'front', 'water', 'particles'], default='all')
    args = parser.parse_args()
    root = args.source_root or Path(__file__).resolve().parents[2]
    view = (root / 'game/client/viewrender.cpp').read_text()
    water = (root / 'game/client/fx_water.cpp').read_text()
    particles = (root / 'game/client/particlemgr.cpp').read_text()
    order = '#define MAX_SMOKE_RENDERABLES 16\n#define MAX_FRONT_TRANSLUCENTS 256\n'
    order += block(view, 'class CSmokeTranslucentOrder')[:-1] + ';\n'
    for name in ['void CSmokeTranslucentOrder::Init(',
                 'bool CSmokeTranslucentOrder::QueueRenderable(',
                 'void CSmokeTranslucentOrder::DrawQueued(']:
        order += block(view, name)
    has_guard = 'void CSmokeTranslucentOrder::DrawRenderable(' in view
    if has_guard:
        order += block(view, 'void CSmokeTranslucentOrder::DrawRenderable(')
    emitter = ''
    if 'class CWaterRippleEmitter' in water:
        emitter = block(water, 'class CWaterRippleEmitter')[:-1] + ';\n'
    emitter += block(water, 'void FX_WaterRipple(')
    with tempfile.TemporaryDirectory(prefix='cssv34-smoke-water-tests-') as directory:
        output = Path(directory)
        (output / 'smoke_under_test.inc').write_text(order)
        (output / 'water_under_test.inc').write_text(emitter)
        (output / 'particles_under_test.inc').write_text(block(particles, 'int CParticleEffectBinding::DrawMaterialParticles('))
        executable = output / 'smoke_water_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++11', '-O2', '-g', '-Wall', '-Wextra',
                   '-Werror', '-Wno-unused-parameter', '-DHAS_DEPTH_GUARD=' + str(int(has_guard)),
                   '-I', str(output), str(Path(__file__).with_name('smoke_water_tests.cpp')),
                   '-o', str(executable)]
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable), args.case], check=True, timeout=60)


if __name__ == '__main__':
    main()
