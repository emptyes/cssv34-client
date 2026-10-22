"""Replay actual delayed knife methods with SDK trace/handle types.

Smack, ItemPostFrame and constructor are extracted from production. A separate
latched-hit double supplies traces (not actual SwingOrStab/collision). Map-driven
snapshots and entity lookup are test doubles, not CPredictionCopy or a live game.
"""
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
    parser.add_argument('--expect-defect', action='store_true')
    args = parser.parse_args()
    root = args.source_root or Path(__file__).resolve().parents[2]
    text = (root / 'game/shared/cstrike/weapon_knife.cpp').read_bytes().decode('latin1')
    first = text.index('BEGIN_PREDICTION_DATA( CKnife )')
    last = text.index('END_PREDICTION_DATA()', first)
    registration = text[first:last].split('\n', 1)[1]
    methods = '\n'.join(block(text, signature) for signature in [
        'CKnife::CKnife()', 'void CKnife::Smack( void )', 'void CKnife::ItemPostFrame( void )'])
    # Guard the handoff being modelled: all pending inputs must be latched on hit.
    hit = block(text, 'bool CKnife::SwingOrStab(')
    for assignment in ['m_trHit = tr;', 'm_pTraceHitEnt = tr.m_pEnt;', 'm_bStab = bStab;',
                       'm_flSmackTime = gpGlobals->curtime + (bStab?0.2f:0.1f);']:
        assert re.sub(r'\s+', '', assignment) in re.sub(r'\s+', '', hit), assignment
    with tempfile.TemporaryDirectory(prefix='cssv34-knife-prediction-') as directory:
        out = Path(directory)
        (out / 'knife_fields.inc').write_text(registration)
        (out / 'knife_methods.inc').write_text(methods)
        executable = out / 'knife_prediction_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++11', '-O2', '-g', '-Wall', '-Wextra',
                   '-Wno-unused-parameter', '-Wno-deprecated-copy', '-Wno-unused-local-typedefs',
                   '-DPOSIX', '-DLINUX', '-D_LINUX', '-DGNUC', '-DCOMPILER_GCC', '-DPLATFORM_64BITS',
                   '-DCLIENT_DLL', '-DNO_MALLOC_OVERRIDE', '-isystem', str(root / 'public'),
                   '-I', str(root / 'game/shared'), '-isystem', str(root / 'public/tier0'), '-isystem', str(root / 'public/tier1'), '-I', str(out),
                   str(Path(__file__).with_name('knife_prediction_tests.cpp')), '-o', str(executable)]
        if args.debug: command += ['-O0', '-D_GLIBCXX_ASSERTIONS']
        if args.sanitize: command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                                    '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable)] + (['defect'] if args.expect_defect else []), check=True, timeout=60)


if __name__ == '__main__':
    main()
