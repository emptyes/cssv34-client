import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def region(text, start, end):
    first = text.index(start)
    return text[first:text.index(end, first)]


def function(text, signature):
    first = text.index(signature)
    opening = text.index('{', first)
    depth = 1
    pos = opening + 1
    while depth:
        if text[pos] == '{':
            depth += 1
        elif text[pos] == '}':
            depth -= 1
        pos += 1
    return text[first:pos] + '\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path)
    parser.add_argument('--case', default='all')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (args.source or root / 'engine/net_ws.cpp').read_text()
    net_header = (root / 'engine/net.h').read_text()
    definitions = []
    for text, names in (
        (net_header, ['NET_MAX_PAYLOAD', 'HEADER_BYTES', 'NET_MAX_MESSAGE', 'NET_MIN_MESSAGE']),
        (source, ['NET_COMPRESSION_STACKBUF_SIZE', 'MIN_USER_MAXROUTABLE_SIZE', 'MIN_SPLIT_SIZE', 'MAX_ROUTABLE_PACKET',
                  'SPLIT_SIZE', 'MAX_SPLITPACKET_SPLITS', 'SPLIT_PACKET_STALE_TIME',
                  'SPLIT_PACKET_TRACKING_MAX']),
    ):
        for name in names:
            match = re.search(r'^#define\s+' + name + r'\s+[^\n]+', text, re.MULTILINE)
            if not match:
                raise ValueError(f'Missing definition: {name}')
            definitions.append(match.group(0))
    structures = region(source, 'typedef struct\n{\n\tint\t\tcurrentSequence;', '#define MIN_USER_MAXROUTABLE_SIZE')
    entries = region(source, 'class CSplitPacketEntry\n', '//-----------------------------------------------------------------------------\n// Purpose:',)
    functions = '\n'.join(function(source, name) for name in (
        'static void NET_RemoveSplitPacketEntry(',
        'static void NET_ClearSplitPackets(',
        'void NET_DiscardStaleSplitpackets(',
        'CSplitPacketEntry *NET_FindOrCreateSplitPacketEntry(',
        'bool NET_GetLong(',
        'static bool NET_DecompressPacket(',
    ))
    generated = '\n'.join(definitions) + '\n' + structures + entries + functions
    with tempfile.TemporaryDirectory(prefix='cssv34-net-tests-') as directory:
        output = Path(directory)
        (output / 'network_under_test.inc').write_text(generated)
        executable = output / 'split_packet_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++11', '-O1', '-g',
                   '-Wall', '-Wextra', '-Werror', '-Wframe-larger-than=16384',
                   '-I', str(output), str(Path(__file__).with_name('split_packet_tests.cpp')),
                   '-lbz2', '-o', str(executable)]
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable), args.case], check=True, timeout=90)


if __name__ == '__main__':
    main()
