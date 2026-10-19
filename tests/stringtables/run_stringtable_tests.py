import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def function(source, signature):
    first = source.index(signature)
    opening = source.index('{', first)
    depth = 1
    position = opening + 1
    while depth:
        if source[position] == '{':
            depth += 1
        elif source[position] == '}':
            depth -= 1
        position += 1
    return source[first:position] + '\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--case', default='all')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--allow-baseline-sign-compare', action='store_true')
    args = parser.parse_args()
    if args.allow_baseline_sign_compare and not args.source_root:
        parser.error('--allow-baseline-sign-compare requires --source-root')
    root = Path(__file__).resolve().parents[2]
    production = args.source_root or root
    header = (root / 'public/tier1/bitbuf.h').read_text()
    header = re.sub(r'^#include[^\n]*', '', header, flags=re.MULTILINE)
    bitbuf = (root / 'tier1/bitbuf.cpp').read_text()
    tables = (production / 'engine/networkstringtable.cpp').read_text()
    messages = (production / 'common/netmessages.cpp').read_text()
    client = (production / 'engine/baseclientstate.cpp').read_text()
    signatures = [
        'bf_write::bf_write()',
        'bf_read::bf_read()',
        'bf_read::bf_read( const void *pData, int nBytes, int nBits )',
        'bf_read::bf_read( const char *pDebugName, const void *pData, int nBytes, int nBits )',
        'void bf_read::StartReading(',
        'void bf_read::SetOverflowFlag()',
        'void bf_read::ReadBits(',
        'bool bf_read::ReadBytes(',
        'bool bf_read::ReadString(',
        'unsigned int bf_read::PeekUBitLong(',
    ]
    definitions = []
    for text, names in [
        ((root / 'engine/net.h').read_text(), ['NET_MAX_PAYLOAD_BITS_V23']),
        ((root / 'public/networkstringtabledefs.h').read_text(), ['MAX_TABLES']),
        (tables, ['SUBSTRING_BITS']),
    ]:
        for name in names:
            definitions.append(re.search(r'^#define\s+' + name + r'\s+[^\n]+', text, re.MULTILINE).group(0))
    first = tables.index('struct StringHistoryEntry\n')
    definitions.append(tables[first:tables.index('};', first) + 2])
    item = (root / 'engine/networkstringtableitem.h').read_text()
    first = item.index('\tenum\n')
    definitions.append('class CNetworkStringTableItem { public:\n' + item[first:item.index('};', first) + 2] + '\n};\n')
    table_code = ''
    if 'static void RejectStringTableUpdate(' in tables:
        table_code += function(tables, 'static void RejectStringTableUpdate(')
    table_code += function(tables, 'void CNetworkStringTable::ParseUpdate(')
    with tempfile.TemporaryDirectory(prefix='cssv34-stringtable-tests-') as directory:
        output = Path(directory)
        (output / 'bitbuf_under_test.h').write_text(header)
        (output / 'bitbuf_under_test.inc').write_text('\n'.join(function(bitbuf, s) for s in signatures))
        (output / 'stringtable_definitions.inc').write_text('\n'.join(definitions))
        (output / 'stringtable_under_test.inc').write_text(table_code)
        (output / 'messages_under_test.inc').write_text('\n'.join(function(messages, s) for s in [
            'bool SVC_CreateStringTable::ReadFromBuffer(',
            'bool SVC_UpdateStringTable::ReadFromBuffer(',
        ]))
        (output / 'client_under_test.inc').write_text('\n'.join(function(client, s) for s in [
            'bool CBaseClientState::ProcessCreateStringTable(',
            'bool CBaseClientState::ProcessUpdateStringTable(',
        ]))
        executable = output / 'stringtable_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++11', '-O1', '-g',
                   '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-DSWDS',
                   '-I', str(output), str(Path(__file__).with_name('stringtable_tests.cpp')),
                   '-o', str(executable)]
        if args.allow_baseline_sign_compare:
            command[1:1] = ['-Wno-error=sign-compare']
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable), args.case], check=True, timeout=45)


if __name__ == '__main__':
    main()
