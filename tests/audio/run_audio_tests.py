import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def function(source, signature):
    first = source.index(signature)
    opening = source.index('{', first)
    depth = 1
    position = opening + 1
    while depth:
        depth += (source[position] == '{') - (source[position] == '}')
        position += 1
    return source[first:position] + '\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    production = args.source_root or root
    with tempfile.TemporaryDirectory(prefix='cssv34-audio-tests-') as directory:
        out = Path(directory)
        (out / 'tier0').mkdir()
        (out / 'interface.h').write_text('#define EXPOSE_INTERFACE(a,b,c)\n')
        (out / 'tier0/memdbgon.h').write_text('')
        codec = production / 'engine/voice_codecs/minimp3/mp3codecs.cpp'
        (out / 'codec_under_test.inc').write_text('#include "' + str(codec) + '"\n')
        source = (production / 'engine/audio/snd_mp3_source.cpp').read_text(encoding='latin-1')
        probe = function(source, 'static int MP3_ReadSampleRateFromHeader(')
        if 'CON_COMMAND( snd_probe_mp3,' in source:
            probe += function(source, 'CON_COMMAND( snd_probe_mp3,')
        (out / 'probe_under_test.inc').write_text(probe)
        for name in ('decoder', 'probe'):
            executable = out / (name + '_tests')
            command = [os.environ.get('CXX', 'g++'), '-std=c++11', '-O2', '-g',
                       '-Wall', '-Wextra', '-Werror', '-I', str(out),
                       '-I', str(root / 'public'), '-I', str(root / 'thirdparty/minimp3'),
                       str(Path(__file__).with_name(name + '_tests.cpp')), '-o', str(executable)]
            if args.sanitize:
                command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                                '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
            subprocess.run(command, check=True)
            subprocess.run([str(executable), str(root / 'tests/audio/data/tone.mp3')],
                           check=True, timeout=60)


if __name__ == '__main__':
    main()
