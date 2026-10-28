#!/usr/bin/env python3
"""Probe an already running, disposable TCP QA session; never launches a game.

The caller supplies an isolated save/profile and mutes unattended audio.
Boundary placement bypasses travel prerequisites, then only native controller
input performs the crossing. Replay advances the disposable session twice.
"""
import argparse
import json
from pathlib import Path
import socket
import time

HEADS = (0,2,3,5,7,10,15,16,17,18,19,20,21,22,23,24,26,27,29,30,
         34,37,40,41,42,43,44,45,46,47,48,50,51,52,53,55,58,59,60,63)


def probe(args):
    result = {'started': time.time(), 'commands': [], 'passed': False}
    with socket.create_connection(('127.0.0.1', args.port), timeout=10) as sock:
        sock.settimeout(30)
        stream = sock.makefile('rb')

        def command(text):
            sock.sendall(text.encode('utf-8') + b'\n')
            reply = json.loads(stream.readline())
            if 'connected' in reply:
                reply = json.loads(stream.readline())
            result['commands'].append({'command': text, 'response': reply})
            if 'error' in reply:
                raise RuntimeError(f'{text}: {reply}')
            return reply

        try:
            world = command('game world')
            if not world['enabled'] or world['module'] != 9 or world['sub']:
                raise RuntimeError('Prepare an enabled overworld in a disposable profile first')
            isolation = command('game world_isolation')
            assert isolation['restore_exact'] and isolation['abort_exact']
            assert isolation['idle'] and not isolation['depth'] and not isolation['interrupt']
            if args.coverage:
                command('game world_view atlas')
                # Queued commands run after activation/ticking at the frame boundary.
                command('game world')
                world = command('game world')
                offset = world['area'] & 64
                coverage = []
                for head in HEADS:
                    area = command(f'game world_area {head + offset}')
                    assert area['authored_spawns'] == area['loaded_spawns'], area
                    pools = [area]
                    pools += [command(f'game world_area {head + offset} {p}')
                              for p in range(1, area['pools'])]
                    assert all(p['ticks'] > 0 for p in pools), pools
                    coverage.append(area)
                result['authored_spawns'] = sum(a['authored_spawns'] for a in coverage)
            if args.edge:
                command('game world_view follow')
                edge = command(f'game world_edge {args.edge[0]} {args.edge[1]}')
                assert edge['ok'], edge
            before = command('game world')
            command(f'game world_replay {args.frames} {args.mask:x}')
            deadline = time.monotonic() + max(90, args.frames * 2)
            while time.monotonic() < deadline:
                replay = command('game world_replay_status')
                if replay['phase'] in (4, 5):
                    break
                time.sleep(.2)
            else:
                raise TimeoutError('Replay did not finish')
            assert replay['phase'] == 4 and replay['first_mismatch_frame'] == -1 and replay['final_equal'], replay
            after = command('game world')
            assert not after['failures'], after
            if args.edge:
                assert after['area'] == edge['to'] and after['handoffs'] > before['handoffs'], after
            result['passed'] = True
        except Exception as error:
            result['error'] = str(error)
        finally:
            stream.close()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({'passed': result['passed'], 'output': str(args.output), 'error': result.get('error')}))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, type=int)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--coverage', action='store_true')
    parser.add_argument('--edge', nargs=2, metavar=('AREA', 'DIRECTION'))
    parser.add_argument('--frames', type=int, default=70)
    parser.add_argument('--mask', type=lambda s: int(s, 16), default=0)
    options = parser.parse_args()
    if options.output.exists():
        parser.error('Use a new output file to preserve earlier evidence')
    if not 1 <= options.frames <= 600 or not 0 <= options.mask <= 0xfff:
        parser.error('Need 1..600 frames and a 12-bit hexadecimal input mask')
    raise SystemExit(probe(options))
