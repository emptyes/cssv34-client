import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def block(source, signature):
    first = source.index(signature)
    opening = source.index('{', first)
    depth = 1
    position = opening + 1
    while depth:
        depth += (source[position] == '{') - (source[position] == '}')
        position += 1
    return source[first:position]


def demo_views(path):
    data = path.read_bytes()
    assert data[:8] == b'HL2DEMO\0'
    assert struct.unpack_from('<ii', data, 8) == (3, 7)
    offset = 1072
    views = []
    while offset < len(data):
        command, tick = struct.unpack_from('<Bi', data, offset)
        offset += 5
        if command in (1, 2):
            info = struct.unpack_from('<i18f', data, offset)
            offset += 84
            if command == 2:
                views.append((tick, *info))
        elif command == 5:
            offset += 4
        elif command == 3:
            continue
        elif command == 7:
            break
        elif command not in (4, 6, 8):
            raise ValueError(f'Unexpected demo command {command}')
        length, = struct.unpack_from('<i', data, offset)
        offset += 4
        assert 0 <= length <= len(data) - offset
        offset += length
    return views


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--demo', type=Path)
    args = parser.parse_args()
    root = args.source_root or Path(__file__).resolve().parents[2]
    source = (root / 'engine/cl_demo.cpp').read_text()
    header = (root / 'engine/cl_demo.h').read_text()
    read = block(source, 'netpacket_t *CDemoPlayer::ReadPacket(')
    assert read.index('ReadCmdInfo( info )') < read.index('StoreDemoCommandInfo( tick, info )')
    assert read.index('StoreDemoCommandInfo( tick, info )') < read.index('ReadSequenceInfo(') < read.index('ReadRawData(')
    assert 'm_bHasLastCmdInfoTick = false;' in block(source, 'CDemoPlayer::CDemoPlayer()')
    assert 'm_bHasLastCmdInfoTick = false;' in block(source, 'void CDemoPlayer::ResyncDemoClock()')
    for member in ('m_nLastCmdInfoTick', 'm_bHasLastCmdInfoTick', 'm_bLastCmdInfoTickHasFlags'):
        assert member in header
    methods = '\n'.join(block(source, name) for name in (
        'void CDemoPlayer::StoreDemoCommandInfo(',
        'void CDemoPlayer::InterpolateDemoCommand(',
        'void CDemoPlayer::InterpolateViewpoint(',
    ))
    methods = block((root / 'public/demofile/demoformat.h').read_text(), 'struct democmdinfo_t') + ';\nstruct DemoCommandQueue { int tick = 0; democmdinfo_t info; int filepos = 0; };\n' + methods
    with tempfile.TemporaryDirectory(prefix='cssv34-demo-camera-') as directory:
        output = Path(directory)
        (output / 'camera_under_test.inc').write_text(methods)
        executable = output / 'demo_camera_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++17', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                   '-Wno-deprecated-copy', '-I', str(output),
                   str(Path(__file__).with_name('demo_camera_tests.cpp')), '-o', str(executable)]
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True, timeout=60)
        if args.demo:
            fixture = output / 'views.txt'
            fixture.write_text('\n'.join(' '.join(str(value) for value in row) for row in demo_views(args.demo)))
            subprocess.run([str(executable), str(fixture)], check=True, timeout=60)


if __name__ == '__main__':
    main()
