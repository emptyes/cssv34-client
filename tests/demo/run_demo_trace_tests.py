import argparse
import csv
import io
import os
from pathlib import Path
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
    return source[first:position]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    root = args.source_root or Path(__file__).resolve().parents[2]
    source = (root / 'game/client/cdll_client_int.cpp').read_text()
    methods = block(source, 'class CDemoPlaybackTrace') + ';\n'
    methods += 'static CDemoPlaybackTrace g_DemoPlaybackTrace;\n'
    methods += block(source, 'static void DemoPlaybackTraceCommand(') + '\n'
    hooks = block(source, 'void CHLClient::FrameStageNotify(')
    render = hooks[hooks.index('case FRAME_RENDER_START:'):hooks.index('case FRAME_RENDER_END:')]
    assert render.index('CDemoPlaybackTrace::RenderBefore') < render.index('OnRenderStart();')
    assert render.index('OnRenderStart();') < render.index('CDemoPlaybackTrace::RenderAfter')
    network = hooks[hooks.index('case FRAME_NET_UPDATE_END:'):hooks.index('case FRAME_NET_UPDATE_POSTDATAUPDATE_START:')]
    assert network.index('SetAbsQueriesValid( true )') < network.index('CDemoPlaybackTrace::NetworkUpdate')
    for signature in ('void CHLClient::LevelShutdown(', 'void CHLClient::Shutdown('):
        assert 'g_DemoPlaybackTrace.Stop(' in block(source, signature)
    command = source[source.index('static ConCommand cl_demo_trace('):]
    assert command[:command.index(';')].rstrip().endswith('FCVAR_DONTRECORD )')
    with tempfile.TemporaryDirectory(prefix='cssv34-demo-trace-') as directory:
        output = Path(directory)
        (output / 'trace_under_test.inc').write_text(methods)
        executable = output / 'demo_trace_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++17', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                   '-Wformat=2', '-I', str(output), str(Path(__file__).with_name('demo_trace_tests.cpp')),
                   '-o', str(executable)]
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        result = subprocess.run([str(executable)], check=True, text=True, capture_output=True, timeout=60)
        rows = list(csv.DictReader(io.StringIO(result.stdout)))
        assert len(rows) == 6
        assert len(rows[0]) == 45
        assert all(None not in row and all(value is not None for value in row.values()) for row in rows)
        assert {row['stage'] for row in rows} == {'network', 'render_before', 'render_after'}
        for row in rows:
            assert row['schema'] == '1'
            assert float(row['client_time']) == 1500.25
            assert float(row['server_time']) == 1500.0
            assert float(row['local_timebase']) == 1500.125
            assert float(row['local_predicted_time']) == 1500.375
            assert row['observer_mode'] == '5' and row['observer_target'] == '2'
            if row['entity'] == '2':
                assert float(row['network_x']) == 100.0
                assert float(row['origin_x']) == 90.0
                assert float(row['abs_x']) == 91.0
                assert float(row['render_x']) == 92.0
        print('Demo trace tests passed: lifecycle, sampling, limits, I/O failures, hooks and CSV schema.')


if __name__ == '__main__':
    main()
