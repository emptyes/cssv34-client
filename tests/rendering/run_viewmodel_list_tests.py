"""Extract production collation and complete view-model render path; use SDK containers."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def block(text, signature):
    first = text.index(signature)
    opening = text.index('{', first)
    depth, position = 1, opening + 1
    while depth:
        depth += (text[position] == '{') - (text[position] == '}')
        position += 1
    return text[first:position] + '\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--case', choices=['behavior', 'measure', 'allocations', 'all'], default='all')
    args = parser.parse_args()
    root = args.source_root or Path(__file__).resolve().parents[2]
    view = (root / 'game/client/viewrender.cpp').read_text()
    leaf = (root / 'game/client/clientleafsystem.cpp').read_text()
    signatures = ['bool CViewRender::UpdateRefractIfNeededByList(',
                  'void CViewRender::DrawRenderablesInList(',
                  'void CViewRender::DrawViewModels(']
    methods = [block(view, sig) for sig in signatures]
    collate = block(leaf, 'void CClientLeafSystem::CollateViewModelRenderables(')
    with tempfile.TemporaryDirectory(prefix='cssv34-viewmodel-') as directory:
        out = Path(directory)
        header = (root / 'game/client/viewrender.h').read_text()
        decls = [re.search(r'(?:bool|void)\s+' + re.escape(sig.split('::')[1]) + r'[^;]+;', header).group() for sig in signatures]
        (out / 'view_declarations.inc').write_text('\n'.join(decls))
        (out / 'leaf_declaration.inc').write_text(collate[:collate.index('{')].strip().replace('CClientLeafSystem::', '') + ';')
        (out / 'view_methods.inc').write_text(collate + ''.join(methods))
        executable = out / 'viewmodel_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++17', '-O2', '-g', '-Wall', '-Wextra',
                   '-Wno-unused-parameter', '-Wno-unused-local-typedefs', '-Wno-deprecated-copy',
                   '-DPOSIX', '-DLINUX', '-D_LINUX', '-DGNUC', '-DCOMPILER_GCC', '-DPLATFORM_64BITS',
                   '-DNO_MALLOC_OVERRIDE', '-isystem', str(root / 'public'), '-I', str(out),
                   str(Path(__file__).with_name('viewmodel_list_tests.cpp')),
                   '-Wl,--wrap=malloc', '-Wl,--wrap=realloc', '-Wl,--wrap=free', '-o', str(executable)]
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                             '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable), args.case], check=True, timeout=60)


if __name__ == '__main__':
    main()
