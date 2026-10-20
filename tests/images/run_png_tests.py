import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))


def png(width, height, depth, color, data, extra=b'', interlace=0):
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, depth, color, 0, 0, interlace))
            + extra + chunk(b'IDAT', zlib.compress(data)) + chunk(b'IEND', b''))


def fixtures(output):
    cases = []
    width, height = 9, 7
    for color in (0, 2, 3, 4, 6):
        for depth in ((1, 2, 4, 8, 16) if color == 0 else (1, 2, 4, 8) if color == 3 else (8, 16)):
            for transparency in ([False, True] if color in (0, 2, 3) else [False]):
                channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
                rows, expected = bytearray(), bytearray()
                palette = bytes((0, 20, 40, 50, 80, 110))
                extra = chunk(b'PLTE', palette) if color == 3 else b''
                if transparency:
                    extra += chunk(b'tRNS', bytes((0, 255)) if color == 3 else struct.pack('>H', 0) * (3 if color == 2 else 1))
                for y in range(height):
                    rows.append(0)
                    samples = []
                    for x in range(width):
                        value = (x + y) % 2
                        maximum = (1 << depth) - 1
                        if color == 3:
                            samples.append(value)
                            expected += palette[value * 3:value * 3 + 3] + bytes((0 if transparency and value == 0 else 255,))
                        elif color in (0, 4):
                            samples.append(value * maximum)
                            if color == 4:
                                samples.append((1 - value) * maximum)
                            expected += bytes((value * 255,) * 3 + ((1 - value) * 255 if color == 4 else 0 if transparency and value == 0 else 255,))
                        else:
                            rgb = (value * maximum,) * 3
                            samples.extend(rgb)
                            if channels == 4:
                                samples.append((1 - value) * maximum)
                            expected += bytes((value * 255,) * 3 + ((1 - value) * 255 if channels == 4 else 0 if transparency and value == 0 else 255,))
                    if depth == 16:
                        rows += b''.join(struct.pack('>H', sample) for sample in samples)
                    elif depth == 8:
                        rows += bytes(samples)
                    else:
                        for i in range(0, len(samples), 8 // depth):
                            group = samples[i:i + 8 // depth]
                            rows.append(sum(value << (8 - depth * (j + 1)) for j, value in enumerate(group)))
                name = 'c' + str(color) + '_d' + str(depth) + ('_trns' if transparency else '')
                cases.append((name, png(width, height, depth, color, rows, extra), expected))
    expected = bytes((x * 17 % 256, y * 23 % 256, (x + y) * 19 % 256, (x * y) * 13 % 256)[c]
                     for y in range(height) for x in range(width) for c in range(4))
    rows = bytearray()
    for x0, y0, dx, dy in ((0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)):
        for y in range(y0, height, dy):
            if x0 >= width:
                continue
            rows.append(0)
            for x in range(x0, width, dx):
                rows += expected[(y * width + x) * 4:(y * width + x + 1) * 4]
    cases.append(('interlaced', png(width, height, 8, 6, rows, interlace=1), expected))
    for name, data, expected in cases:
        (output / (name + '.png')).write_bytes(data)
        (output / (name + '.rgba')).write_bytes(expected)
    (output / 'oversized.png').write_bytes(png(100000, 100000, 8, 6, b''))
    (output / 'cases.txt').write_text('\n'.join(name for name, _, _ in cases))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--case', choices=['all', 'signature', 'reader', 'valid', 'truncated', 'limits'], default='all')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    production = args.source_root or root
    library = root / 'thirdparty/libpng'
    with tempfile.TemporaryDirectory(prefix='cssv34-png-') as directory:
        output = Path(directory)
        (output / 'libpng').mkdir()
        (output / 'tier0').mkdir()
        (output / 'tier0/dbg.h').write_text('')
        (output / 'libpng/png.h').write_text('#include "' + str(library / 'png.h') + '"\n')
        source = (production / 'common/pngloader.cpp').read_text().replace('#include "pngloader.h"', '')
        (output / 'png_under_test.inc').write_text(source)
        fixtures(output)
        flags = ['-O2', '-g']
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-fno-pie']
        sources = 'png pngerror pngget pngmem pngpread pngread pngrio pngrtran pngrutil pngset pngtrans pngwio pngwrite pngwtran pngwutil'.split()
        subprocess.run([os.environ.get('CC', 'gcc'), *flags, '-I', str(library), '-I', str(root / 'thirdparty/zlib'), '-c',
                        *[str(library / (name + '.c')) for name in sources]], cwd=output, check=True)
        executable = output / 'png_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++17', *flags, '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                   '-I', str(output), str(Path(__file__).with_name('png_tests.cpp')), *[str(output / (name + '.o')) for name in sources],
                   '-lz', '-o', str(executable)]
        if args.sanitize:
            command.append('-no-pie')
        subprocess.run(command, check=True)
        subprocess.run([str(executable), str(output), args.case], check=True, timeout=60)


if __name__ == '__main__':
    main()
