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
    parser.add_argument('--case', choices=['all', 'valid', 'short-header', 'lod', 'size-overflow', 'source', 'tangent', 'fixup-source', 'fixup-target', 'allocation', 'dispatch', 'invalid', 'fuzz'], default='all')
    args = parser.parse_args()
    root = args.source_root or Path(__file__).resolve().parents[2]
    source = (root / 'datacache/mdlcache.cpp').read_text()
    builder = function(source, 'vertexFileHeader_t *CMDLCache::BuildAndCacheVertexData(')
    checked = 'int nDataSize' in builder.split('{', 1)[0]
    extracted = ''
    if checked:
        extracted = function(source, 'static bool ValidateVertexData(')
    extracted += builder
    process = function(source, 'bool CMDLCache::ProcessDataIntoCache(')
    branch = process[process.index('case MDLCACHE_VERTEXES:'):process.index('case MDLCACHE_STUDIOHWDATA:')]
    extracted += ('bool CMDLCache::ProcessVertexData(studiohdr_t *pStudioHdrCurrent, void *pData, int nDataSize, bool bDataValid) {\n'
                  'MDLHandle_t handle = 0; StudioData *pStudioDataCurrent = &entry;\n'
                  'switch (MDLCACHE_VERTEXES) {\n' + branch + '}\nreturn true;\n}\n')
    with tempfile.TemporaryDirectory(prefix='cssv34-vertex-') as directory:
        out = Path(directory)
        (out / 'vertex_under_test.inc').write_text(extracted)
        (out / 'tier1').mkdir()
        header = (root / 'public/tier1/utlbuffer.h').read_text()
        (out / 'tier1/utlbuffer.h').write_text(header.replace('template <> static const char *GetFmtStr', 'template <> const char *GetFmtStr'))
        executable = out / 'vertex_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++17', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                   '-Wno-cpp', '-Wno-class-memaccess', '-Wno-unused-parameter', '-Wno-unused-variable', '-Wno-invalid-offsetof', '-Wno-unknown-pragmas',
                   '-DPOSIX=1', '-D_LINUX=1', '-DGNUC=1', '-DVVD_HAS_SIZE=' + str(int(checked)),
                   '-I', str(out), '-isystem', str(root / 'public'), '-isystem', str(root / 'public/tier0'),
                   '-isystem', str(root / 'public/tier1'), '-isystem', str(root / 'common'),
                   str(Path(__file__).with_name('vertex_tests.cpp')), '-o', str(executable)]
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable), args.case], check=True, timeout=60)


if __name__ == '__main__':
    main()
