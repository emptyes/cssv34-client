import argparse
import os
from pathlib import Path
import re
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
    return source[first:position] + '\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--fixture', type=Path)
    parser.add_argument('--packets', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (root / 'engine/cl_demo.cpp').read_text()
    tables = (root / 'engine/networkstringtable.cpp').read_text()
    bitbuf = (root / 'tier1/bitbuf.cpp').read_text()
    messages = (root / 'common/netmessages.cpp').read_text()
    recorder = block(source, 'void CDemoRecorder::RecordStringTables()')
    assert 'WriteStringTables(' not in recorder and 'm_DemoFile.WriteStringTables(' not in recorder
    startup = block(source, 'void CDemoRecorder::StartupDemoFile(')
    assert re.search(r'RecordStringTables\(\);\s*if \( !m_bRecording \)\s*return;', startup)
    signatures = [
        'bf_write::bf_write()',
        'bf_write::bf_write( void *pData, int nBytes, int nBits )',
        'void bf_write::StartWriting(', 'void bf_write::SeekToBit(', 'void bf_write::SetAssertOnOverflow(',
        'const char* bf_write::GetDebugName()', 'void bf_write::WriteSBitLong(',
        'void bf_write::WriteChar(', 'void bf_write::WriteWord(',
        'bool bf_write::WriteString(', 'bool bf_write::WriteBits(',
        'bf_read::bf_read()',
        'bf_read::bf_read( const void *pData, int nBytes, int nBits )',
        'void bf_read::StartReading(', 'void bf_read::SetOverflowFlag()',
        'void bf_read::ReadBits(', 'bool bf_read::ReadBytes(', 'bool bf_read::ReadString(',
    ]
    definitions = ''
    for text, names in [((root / 'public/networkstringtabledefs.h').read_text(), ['MAX_TABLES']),
                        (tables, ['SUBSTRING_BITS']), ((root / 'engine/net.h').read_text(), ['NETMSG_TYPE_BITS'])]:
        for name in names:
            match = re.search(r'^#define\s+' + name + r'\s+[^\n]+', text, re.MULTILINE)
            if not match and name == 'NETMSG_TYPE_BITS':
                text = (root / 'common/protocol.h').read_text()
                match = re.search(r'^#define\s+' + name + r'\s+[^\n]+', text, re.MULTILINE)
            definitions += match.group(0) + '\n'
    definitions += block(tables, 'struct StringHistoryEntry') + ';\n'
    item = (root / 'engine/networkstringtableitem.h').read_text()
    first = item.index('\tenum\n')
    definitions += 'class CNetworkStringTableItem { public:\n' + item[first:item.index('};', first) + 2] + '\n};\n'
    with tempfile.TemporaryDirectory(prefix='cssv34-demo-recording-') as directory:
        output = Path(directory)
        (output / 'bitbuf_under_test.h').write_text(re.sub(r'^#include[^\n]*', '', (root / 'public/tier1/bitbuf.h').read_text(), flags=re.MULTILINE))
        (output / 'bitbuf_under_test.inc').write_text('\n'.join(block(bitbuf, name) for name in signatures))
        (output / 'definitions.inc').write_text(definitions)
        (output / 'table_under_test.inc').write_text(block(tables, 'static void RejectStringTableUpdate(') + block(tables, 'void CNetworkStringTable::ParseUpdate('))
        (output / 'messages_under_test.inc').write_text(block(messages, 'bool SVC_UpdateStringTable::WriteToBuffer(') + block(messages, 'bool SVC_UpdateStringTable::ReadFromBuffer('))
        (output / 'recorder_under_test.inc').write_text(block(source, 'static bool WriteDemoStringTableEntry(') + recorder)
        executable = output / 'demo_recording_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++17', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-parameter', '-I', str(output), str(Path(__file__).with_name('demo_recording_tests.cpp')), '-o', str(executable)]
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        run = [str(executable)]
        if args.fixture:
            run += [str(args.fixture), str(args.packets or output / 'packets.bin')]
        subprocess.run(run, check=True, timeout=60)


if __name__ == '__main__':
    main()
