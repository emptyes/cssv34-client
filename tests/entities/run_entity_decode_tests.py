import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def block(text, signature):
    first = text.index(signature)
    opening = text.index('{', first)
    depth = 1
    position = opening + 1
    while depth:
        depth += (text[position] == '{') - (text[position] == '}')
        position += 1
    return text[first:position] + '\n'


def main():
    parser = argparse.ArgumentParser()
    source_group = parser.add_mutually_exclusive_group()
    source_group.add_argument('--source', type=Path)
    source_group.add_argument('--source-root', type=Path)
    parser.add_argument('--case', default='all')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--debug', action='store_true')
    parser.add_argument('--allow-baseline-sign-compare', action='store_true')
    parser.add_argument('--optimization', choices=['0', '1', '2', '3'], default='2')
    args = parser.parse_args()
    if args.allow_baseline_sign_compare and not args.source_root:
        parser.error('--allow-baseline-sign-compare requires --source-root')
    root = Path(__file__).resolve().parents[2]
    production = args.source_root or root
    source = (args.source or production / 'engine/dt_encode.cpp').read_text()
    header = (root / 'public/tier1/bitbuf.h').read_text()
    header = re.sub(r'^#include[^\n]*', '', header, flags=re.MULTILINE)
    bitbuf = (root / 'tier1/bitbuf.cpp').read_text()
    definitions = []
    for path, names in [
        ('public/dt_common.h', ['DT_MAX_STRING_BITS', 'DT_MAX_STRING_BUFFERSIZE', 'MAX_DATATABLE_PROPS',
                              'SPROP_UNSIGNED', 'SPROP_COORD', 'SPROP_NOSCALE', 'SPROP_NORMAL']),
        ('public/coordsize.h', ['COORD_INTEGER_BITS', 'COORD_FRACTIONAL_BITS',
                               'COORD_DENOMINATOR', 'COORD_RESOLUTION',
                               'NORMAL_FRACTIONAL_BITS', 'NORMAL_DENOMINATOR', 'NORMAL_RESOLUTION']),
    ]:
        text = (root / path).read_text()
        for name in names:
            definitions.append(re.search(r'^#define\s+' + name + r'\s+[^\n]+', text, re.MULTILINE).group(0))
    common = (root / 'public/dt_common.h').read_text()
    definitions.append(block(common, 'typedef enum')[:-1] + ' SendPropType;\n')
    recv = (root / 'public/dt_recv.h').read_text()
    classes = [block(recv, 'class CRecvProxyData\n')[:-1] + ';\n']
    decode_header = (root / 'engine/dt_encode.h').read_text()
    classes.append(block(decode_header, 'class DecodeInfo :')[:-1] + ';\n')
    delta = (production / 'engine/dt.h').read_text()
    classes.append(block(delta, 'class CDeltaBitsReader\n')[:-1] + ';\n')
    signatures = [
        'void DecodeInfo::CopyVars(', 'static inline bool DecodeSpecialFloat(',
        'static float DecodeFloat(', 'static inline void DecodeVector(',
        'void Int_Decode(', 'int Int_CompareDeltas(', 'bool Int_IsEncodedZero(', 'void Int_SkipProp(',
        'void Float_Decode(', 'void Vector_Decode(', 'void String_Decode(',
        'static inline int AreBitsDifferent(', 'int String_CompareDeltas(',
        'bool String_IsEncodedZero(', 'void String_SkipProp(',
        'void Array_Decode(', 'int Array_CompareDeltas(',
        'bool Array_IsEncodedZero(', 'void Array_SkipProp(',
    ]
    if 'static int ReadArrayLength(' in source:
        signatures.insert(signatures.index('void Array_Decode('), 'static int ReadArrayLength(')
    with tempfile.TemporaryDirectory(prefix='cssv34-entity-tests-') as directory:
        output = Path(directory)
        (output / 'bitbuf_under_test.h').write_text(header)
        (output / 'bitbuf_under_test.inc').write_text('\n'.join(block(bitbuf, s) for s in [
            'bf_write::bf_write()', 'bf_read::bf_read()',
            'bf_read::bf_read( const char *pDebugName, const void *pData, int nBytes, int nBits )',
            'void bf_read::StartReading(', 'void bf_read::SetOverflowFlag()',
            'void bf_read::SetAssertOnOverflow(', 'int bf_read::ReadSBitLong(',
            'void bf_read::ReadBits(', 'float bf_read::ReadBitCoord (', 'float bf_read::ReadBitNormal (',
        ]))
        (output / 'definitions_under_test.inc').write_text('\n'.join(definitions))
        (output / 'classes_under_test.inc').write_text('\n'.join(classes))
        (output / 'delta_under_test.inc').write_text('\n'.join(block(delta, name) for name in [
            'FORCEINLINE CDeltaBitsReader::CDeltaBitsReader(', 'FORCEINLINE CDeltaBitsReader::~CDeltaBitsReader()',
            'FORCEINLINE void CDeltaBitsReader::ForceFinished()',
            'FORCEINLINE unsigned int CDeltaBitsReader::ReadNextPropIndex()',
        ]))
        recv_source = (production / 'engine/dt_recv_eng.cpp').read_text()
        (output / 'recv_under_test.inc').write_text(block(recv_source, 'bool RecvTable_Decode( '))
        (output / 'decode_under_test.inc').write_text('\n'.join(block(source, s) for s in signatures))
        executable = output / 'entity_decode_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++11', '-O' + args.optimization, '-g',
                   '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                   '-I', str(output), str(Path(__file__).with_name('entity_decode_tests.cpp')),
                   '-o', str(executable)]
        if args.allow_baseline_sign_compare:
            command[1:1] = ['-Wno-error=sign-compare']
        if args.debug:
            command[1:1] = ['-D_DEBUG', '-DDBGFLAG_ASSERT']
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable), args.case], check=True, timeout=60)


if __name__ == '__main__':
    main()
