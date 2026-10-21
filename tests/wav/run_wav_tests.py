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
    parser.add_argument('--case', choices=['all', 'pcm-zero-bits', 'stereo-duration', 'adpcm-zero-channels', 'tier2-adpcm'], default='all')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    production = args.source_root or root
    with tempfile.TemporaryDirectory(prefix='cssv34-wav-') as directory:
        out = Path(directory)
        (out / 'tier0').mkdir()
        (out / 'tier0/dbg.h').write_text('#include <cassert>\n#define Assert(x) assert(x)\n')
        (out / 'tier0/memdbgon.h').write_text('')
        (out / 'commonmacros.h').write_text('#include <cstdint>\nusing intp = intptr_t;\n#define MAKEID(a,b,c,d) ((a) | ((b)<<8) | ((c)<<16) | ((d)<<24))\ninline int LittleLong(int v) { return v; }\ninline int LittleWord(int v) { return v; }\ninline int LittleDWord(int v) { return v; }\n')
        source = (production / 'engine/audio/snd_wave_source.cpp').read_text()
        (out / 'wave_under_test.inc').write_text(function(source, 'int ADPCMSampleCount(') +
            function(source, 'bool CAudioSourceWave::GetStartupData(') + function(source, 'static float Audio_GetWaveDuration('))
        utility = (production / 'tier2/soundutils.cpp').read_text()
        (out / 'utility.inc').write_text(function(utility, 'float GetWavSoundDuration('))
        posix = (production / 'engine/audio/posix_stubs.h').read_text()
        (out / 'formats.inc').write_text(posix[posix.index('#pragma pack(push, 1)'):posix.index('#pragma pack(pop)')] + '#pragma pack(pop)\n')
        (out / 'riff_under_test.inc').write_text('#include "' + str(production / 'tier2/riff.cpp') + '"\n')
        executable = out / 'wav_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++17', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-parameter', '-Wno-unused-variable', '-Wno-unused-but-set-variable', '-Wno-unknown-pragmas',
                   '-I', str(out), '-I', str(production / 'public/tier2'), '-I', str(production / 'public'),
                   str(Path(__file__).with_name('wav_tests.cpp')), '-o', str(executable)]
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable), args.case], check=True, timeout=60)


if __name__ == '__main__':
    main()
