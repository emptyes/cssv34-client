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
    parser.add_argument('--case', choices=['all', 'seek', 'short-block', 'predictor', 'overflow', 'loop'], default='all')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    production = args.source_root or root
    source = (production / 'engine/audio/snd_wave_mixer_adpcm.cpp').read_text()
    signatures = [
        'CAudioMixerWaveADPCM::CAudioMixerWaveADPCM(',
        'CAudioMixerWaveADPCM::~CAudioMixerWaveADPCM(',
        'int\\tCAudioMixerWaveADPCM::NumChannels(',
        'void CAudioMixerWaveADPCM::DecompressBlockMono(',
        'void CAudioMixerWaveADPCM::DecompressBlockStereo(',
        'bool CAudioMixerWaveADPCM::DecodeBlock(',
        'int CAudioMixerWaveADPCM::GetOutputData(',
        'void CAudioMixerWaveADPCM::SetSampleStart(',
    ]
    extracted = source[source.index('static int error_sign_lut[]'):source.index('//-----------------------------------------------------------------------------', source.index('static int error_sign_lut[]'))]
    extracted = ''
    for marker in ('static int error_sign_lut[]', 'static int error_coefficients_lut[]'):
        start = source.index(marker)
        extracted += source[start:source.index(';', start) + 2]
    for signature in signatures:
        extracted += function(source, signature.replace('\\t', '\t'))
    with tempfile.TemporaryDirectory(prefix='cssv34-adpcm-') as directory:
        out = Path(directory)
        (out / 'mixer_under_test.inc').write_text(extracted)
        executable = out / 'adpcm_tests'
        command = [
            os.environ.get('CXX', 'g++'), '-std=c++17', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
            '-Wno-unused-parameter', '-I', str(out), '-I', str(production / 'public'),
            str(Path(__file__).with_name('adpcm_tests.cpp')), '-o', str(executable),
        ]
        if args.sanitize:
            command[1:1] = [
                '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
            ]
        subprocess.run(command, check=True)
        subprocess.run([str(executable), args.case], check=True, timeout=30)


if __name__ == '__main__':
    main()