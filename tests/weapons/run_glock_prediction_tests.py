"""Replay extracted Glock methods with map-driven local-state snapshots.

Engine services and base-class/player snapshots are doubles. This is not an
in-game client/server comparison, a complete CPredictionCopy test, or a test of
burst scheduling, ballistic RNG, collision, or server hit registration.
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
    return text[first:pos] + '\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--debug', action='store_true')
    parser.add_argument('--expect-defect', action='store_true')
    args = parser.parse_args()
    root = args.source_root or Path(__file__).resolve().parents[2]
    text = (root / 'game/shared/cstrike/weapon_glock.cpp').read_bytes().decode('latin1')
    first = text.index('BEGIN_PREDICTION_DATA( CWeaponGlock )')
    last = text.index('END_PREDICTION_DATA()', first)
    registration = text[first:last].split('\n', 1)[1]
    methods = '\n'.join(block(text, signature) for signature in [
        'void CWeaponGlock::SecondaryAttack()',
        'void CWeaponGlock::PrimaryAttack()',
        'float CWeaponGlock::GetInaccuracy() const',
        'void CWeaponGlock::FireRemaining(',
        'void CWeaponGlock::ItemPostFrame()',
        'void CWeaponGlock::GlockFire('])
    with tempfile.TemporaryDirectory(prefix='cssv34-glock-prediction-') as directory:
        out = Path(directory)
        (out / 'glock_fields.inc').write_text(registration)
        (out / 'glock_methods.inc').write_text(methods)
        executable = out / 'glock_prediction_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++11', '-O2', '-g',
                   '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                   '-I', str(out), str(Path(__file__).with_name('glock_prediction_tests.cpp')),
                   '-o', str(executable)]
        if args.debug:
            command += ['-O0', '-D_GLIBCXX_ASSERTIONS']
        if args.sanitize:
            command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable)] + (['defect'] if args.expect_defect else []),
                       check=True, timeout=60)


if __name__ == '__main__':
    main()
