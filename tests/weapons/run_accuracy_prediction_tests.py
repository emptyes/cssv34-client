"""Exercise extracted shared accuracy methods with modeled prediction snapshots."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
from run_glock_prediction_tests import block


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--debug', action='store_true')
    parser.add_argument('--expect-defect', action='store_true')
    args = parser.parse_args()
    root = args.source_root or Path(__file__).resolve().parents[2]
    text = (root / 'game/shared/cstrike/weapon_csbase.cpp').read_bytes().decode('latin1')
    first = text.index('BEGIN_PREDICTION_DATA( CWeaponCSBase )')
    last = text.index('END_PREDICTION_DATA()', first)
    fields = text[first:last].split('\n', 1)[1]
    methods = '\n'.join(block(text, signature) for signature in [
        'void CWeaponCSBase::UpdateAccuracyPenalty()',
        'void CWeaponCSBase::OnJump(', 'void CWeaponCSBase::OnLand('])
    with tempfile.TemporaryDirectory(prefix='cssv34-accuracy-') as directory:
        out = Path(directory)
        (out / 'accuracy_fields.inc').write_text(fields)
        (out / 'accuracy_methods.inc').write_text(methods)
        executable = out / 'accuracy_prediction_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                   '-I', str(out), str(Path(__file__).with_name('accuracy_prediction_tests.cpp')), '-o', str(executable)]
        if args.debug: command += ['-O0', '-D_GLIBCXX_ASSERTIONS']
        if args.sanitize: command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable)] + (['defect'] if args.expect_defect else []), check=True, timeout=60)


if __name__ == '__main__':
    main()
