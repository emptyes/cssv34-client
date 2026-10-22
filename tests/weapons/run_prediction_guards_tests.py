"""Preserve real weapon-source conditionals and check client/server maps.

Uses SDK's actual empty server BEGIN/END definitions with field markers.
This is a preprocessor check, not a complete game or MSVC build.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def scaffold(text):
    output, in_map = [], False
    for line in text.splitlines(keepends=True):
        if re.match(r'\s*BEGIN_PREDICTION_DATA\s*\(', line):
            assert not in_map
            in_map = True
        if in_map or re.match(r'\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b', line):
            output.append(line)
        if re.match(r'\s*END_PREDICTION_DATA\s*\(', line):
            assert in_map
            in_map = False
    assert not in_map
    return ''.join(output)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--debug', action='store_true')
    parser.add_argument('--sanitize', action='store_true', help='Preprocessor only; common runner interface')
    parser.add_argument('--expect-defect', action='store_true')
    args = parser.parse_args()
    root = args.source_root or Path(__file__).resolve().parents[2]
    sdk = (root / 'game/shared/predictable_entity.h').read_bytes().decode('latin1')
    server = sdk.split('// nothing, only client has a prediction system', 1)[1].split('#endif', 1)[0]
    server_macros = '\n'.join(line for line in server.splitlines()
                              if re.match(r'\s*#define\s+(BEGIN|END)_PREDICTION_DATA', line))
    assert len(server_macros.splitlines()) == 2
    assert re.search(r'#define\s+BEGIN_PREDICTION_DATA\(\s*className\s*\)\s*$', server_macros, re.M)
    markers = '''
#define DEFINE_PRED_FIELD(name,type,flags) PRED_FIELD_MARKER(#name)
#define DEFINE_PRED_FIELD_TOL(name,type,flags,tol) PRED_FIELD_MARKER(#name)
#define DEFINE_FIELD(name,type) PRED_FIELD_MARKER(#name)
'''
    defects, maps, checks = [], 0, 0
    with tempfile.TemporaryDirectory(prefix='cssv34-pred-guards-') as directory:
        path = Path(directory) / 'guards.cpp'
        for source in sorted((root / 'game/shared/cstrike').glob('weapon_*.cpp')):
            raw = scaffold(source.read_bytes().decode('latin1'))
            maps += len(re.findall(r'BEGIN_PREDICTION_DATA\s*\(', raw))
            expected = re.findall(r'DEFINE_(?:PRED_FIELD_TOL|PRED_FIELD|FIELD)\s*\(\s*([\w.]+)', raw)
            for client in (False, True):
                header = ('#define CLIENT_DLL\n#define BEGIN_PREDICTION_DATA(x)\n#define END_PREDICTION_DATA()\n'
                          if client else '#define GAME_DLL\n' + server_macros + '\n')
                path.write_text(header + markers + raw)
                command = [os.environ.get('CXX', 'g++'), '-E', '-P', '-x', 'c++', str(path)]
                if args.debug: command += ['-D_DEBUG', '-DDBGFLAG_ASSERT']
                result = subprocess.run(command, check=True, stdout=subprocess.PIPE, text=True)
                found = re.findall(r'PRED_FIELD_MARKER\("([^\"]+)"\)', result.stdout)
                if client: assert found == expected, (source.name, expected, found)
                elif found: defects.append((source.name, found))
                checks += 1
    print(f'Prediction guard checks={checks} maps={maps} server leaks={len(defects)}', flush=True)
    for source, fields in defects: print(f'{source}: {", ".join(fields)}', flush=True)
    if args.expect_defect:
        assert defects, 'Expected old server-map leaks'
        print('BASELINE SERVER PREDICTION GUARD DEFECT REPRODUCED')
    else:
        assert not defects, 'Prediction fields leaked into server translation units'
        print('PASS client entries preserved; server entries excluded')


if __name__ == '__main__':
    main()
