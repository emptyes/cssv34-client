import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def block(source, signature):
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
    parser.add_argument('--case', default='all')
    args = parser.parse_args()
    root = args.source_root or Path(__file__).resolve().parents[2]
    source = (root / 'materialsystem/texturemanager.cpp').read_text()
    structure = block(source, 'struct AsyncReadJob_t\n' if 'struct AsyncReadJob_t\n' in source else 'struct AsyncReadJob_t \n') + ';\n'
    reader = block(source, 'class AsyncReader\n{')
    update = block(reader, 'void ThreadMain_Update()').replace('void ThreadMain_Update()', 'void AsyncReader::ThreadMain_Update()', 1)
    methods = update + block(source, 'void CTextureManager::CompleteAsyncRead(') + block(source, 'void CTextureManager::AsyncReadTexture(')
    with tempfile.TemporaryDirectory(prefix='cssv34-readback-') as directory:
        out = Path(directory)
        (out / 'job.inc').write_text(structure)
        (out / 'methods.inc').write_text(methods)
        (out / 'cancel.inc').write_text(block(source, 'bool IsJobCancelled( AsyncReadJob_t*'))
        executable = out / 'readback_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++17', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                   '-I', str(out), str(Path(__file__).with_name('readback_tests.cpp')), '-o', str(executable)]
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable), args.case], check=True, timeout=60)


if __name__ == '__main__':
    main()
