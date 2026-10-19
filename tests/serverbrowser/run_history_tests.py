import argparse
import os
from pathlib import Path
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
    parser.add_argument('--source', type=Path)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (args.source or root / 'serverbrowser/ServerBrowserDialog.cpp').read_text()
    address = (root / 'tier1/NetAdr.cpp').read_text()
    declarations = (root / 'public/tier1/netadr.h').read_text()
    declarations = declarations[declarations.index('typedef enum'):declarations.index('/// Helper')]
    address_functions = '\n'.join(function(address, name) for name in (
        'void netadr_t::Clear()', 'void netadr_t::SetIP(uint unIP)',
        'void netadr_t::SetType(', 'netadrtype_t netadr_t::GetType()',
        'void netadr_t::SetPort(',
        'bool netadr_t::IsValid()', 'bool netadr_t::IsLocalhost()',
        'unsigned short netadr_t::GetPort()',
        'unsigned int netadr_t::GetIPNetworkByteOrder()',
        'unsigned int netadr_t::GetIPHostByteOrder()',
        'const char * netadr_t::ToString(', 'void netadr_t::ToString('))
    handlers = '\n'.join(function(source, name) for name in (
        'void CServerBrowserDialog::OnConnectToGame(',
        'void CServerBrowserDialog::OnDisconnectFromGame(',
        'void CServerBrowserDialog::AddServerToHistory('))
    with tempfile.TemporaryDirectory(prefix='cssv34-history-tests-') as directory:
        output = Path(directory)
        (output / 'address_under_test.inc').write_text(declarations + address_functions)
        (output / 'history_under_test.inc').write_text(handlers)
        executable = output / 'history_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++11', '-O2', '-g',
                   '-Wall', '-Wextra', '-Werror', '-I', str(output),
                   str(Path(__file__).with_name('history_tests.cpp')), '-o', str(executable)]
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True, timeout=30)


if __name__ == '__main__':
    main()
