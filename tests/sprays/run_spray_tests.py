import argparse
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import tempfile


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    tokens = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
    for token in tokens.finditer(source, opening):
        if token.group() == '{':
            depth += 1
        elif token.group() == '}':
            depth -= 1
            if not depth:
                return source[start:token.end()] + '\n'
    raise ValueError(signature)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    production = args.source_root or root
    source = (production / 'common/imageutils.cpp').read_text()
    loader = importlib.util.spec_from_file_location('png_fixtures', root / 'tests/images/run_png_tests.py')
    fixtures = importlib.util.module_from_spec(loader)
    loader.loader.exec_module(fixtures)
    with tempfile.TemporaryDirectory(prefix='cssv34-spray-') as directory:
        output = Path(directory)
        fixtures.fixtures(output)
        for width, height in ((4, 4), (512, 128), (512, 1)):
            pixels = bytes(c for y in range(height) for x in range(width)
                           for c in (x % 256, y % 256, (x + y) % 256, 0 if x < width // 2 else 255))
            rows = b''.join(b'\0' + pixels[y * width * 4:(y + 1) * width * 4] for y in range(height))
            stem = f'rgba_{width}' if height != 1 else 'thin'
            (output / f'{stem}.png').write_bytes(fixtures.png(width, height, 8, 6, rows))
            (output / f'{stem}.rgba').write_bytes(pixels)
        signatures = (
            'static void ReadPNGData(',
            'unsigned char *ImgUtl_ReadPNGAsRGBAFromBuffer(',
            'unsigned char *ImgUtl_ReadPNGAsRGBA(',
            'static bool ImgUtl_ReadTGAHeader(', 'static bool ImgUtl_GetTGADataSize(', 'static void WriteTGAHeader(',
            'unsigned char * ImgUtl_ReadTGAAsRGBA(',
            'ConversionErrorType ImgUtl_StretchRGBAImage(', 'ConversionErrorType ImgUtl_PadRGBAImage(',
            'ConversionErrorType ImgUtl_ConvertTGA(',
            'ConversionErrorType ImgUtl_ConvertTGAToVTF(',
            'static bool DoCopyFile(', 'static void DoDeleteFile(',
            'static ConversionErrorType ImgUtl_ConvertPNGToTGA(',
            'ConversionErrorType ImgUtl_WriteGenericVMT(',
            'ConversionErrorType\tImgUtl_ConvertToVTFAndDumpVMT(',
        )
        (output / 'imageutils_under_test.inc').write_text('\n'.join(function(source, signature) for signature in signatures))
        header = (production / 'common/imageutils.h').read_text()
        (output / 'types.inc').write_text(header[header.index('enum ConversionErrorType'):header.index('enum ImageFileFormat')] +
                                         header[header.index('struct TGAHeader {'):header.index('ConversionErrorType ImgUtl_ConvertJPEGToTGA')])
        writer = (production / 'bitmap/tgawriter.cpp').read_text()
        conversion = (production / 'bitmap/colorconversion.cpp').read_text()
        (output / 'writer.inc').write_text(function(conversion, 'void RGBA8888ToBGRA8888( const uint8 *src, uint8 *dst, int numPixels )\n{') +
            '\nnamespace TGAWriter {\n#pragma pack(push, 1)\n' +
            writer[writer.index('struct TGAHeader_t'):writer.index('#pragma pack()')] + '\n#pragma pack(pop)\n' +
            function(writer, 'static void fputLittleShort(') + function(writer, 'bool WriteToBuffer(') + '}\n')
        ui = (production / 'gameui/OptionsSubMultiplayer.cpp').read_text()
        start = ui.index('#ifdef WIN32', ui.index('"ImportSprayImage"'))
        finish = ui.index('m_hImportSprayDialog->AddActionSignalTarget', start)
        (output / 'filters.inc').write_text(ui[start:finish])
        library = root / 'thirdparty/libpng'
        flags = ['-O2', '-g']
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-fno-pie']
        sources = 'png pngerror pngget pngmem pngpread pngread pngrio pngrtran pngrutil pngset pngtrans pngwio pngwrite pngwtran pngwutil'.split()
        subprocess.run([os.environ.get('CC', 'gcc'), *flags, '-I', str(library), '-I', str(root / 'thirdparty/zlib'), '-c',
                        *[str(library / (name + '.c')) for name in sources]], cwd=output, check=True)
        command = [os.environ.get('CXX', 'g++'), '-std=c++17', *flags, '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-parameter', '-Wno-write-strings', '-Wno-switch', '-I', str(output), '-I', str(library),
                   str(Path(__file__).with_name('spray_tests.cpp')),
                   *[str(output / (name + '.o')) for name in sources], '-lz', '-o', str(output / 'spray_tests')]
        if args.sanitize:
            command.append('-no-pie')
        subprocess.run(command, check=True)
        subprocess.run([str(output / 'spray_tests'), str(output)], check=True, timeout=90)


if __name__ == '__main__':
    main()
