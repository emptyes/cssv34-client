import argparse
import ast
from pathlib import Path
import subprocess
import sys


def supports_debug(path):
    tree = ast.parse(path.read_text())
    return any(isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)
               and node.func.attr == 'add_argument'
               and any(isinstance(arg, ast.Constant) and arg.value == '--debug' for arg in node.args)
               for node in ast.walk(tree))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    runners = sorted(tests.glob('*/run_*tests.py'))
    if not runners:
        parser.error('No regression test runners found')
    total = 0
    for runner in runners:
        for debug in ([False, True] if supports_debug(runner) else [False]):
            command = [sys.executable, str(runner)]
            if args.sanitize:
                command.append('--sanitize')
            if debug:
                command.append('--debug')
            print('RUN ' + runner.relative_to(tests).as_posix()
                  + (' --sanitize' if args.sanitize else '') + (' --debug' if debug else ''), flush=True)
            if not args.list:
                result = subprocess.run(command, cwd=tests.parent)
                if result.returncode:
                    return result.returncode if result.returncode > 0 else 1
            total += 1
    print(('Listed' if args.list else 'Passed') + ' regression runs: ' + str(total), flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
