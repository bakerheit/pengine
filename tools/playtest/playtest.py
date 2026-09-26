#!/usr/bin/env python3
"""Probable Cause local playtest tool: CLI and dependency-free MCP stdio server."""
import argparse
import base64
import http.client
import json
import math
import os
from pathlib import Path
import shutil
import signal
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
KEY_FILE = Path.home() / '.config/probable-cause/jev.key'
ACTIONS = {
    'wait': 'Release controls and observe.',
    'stop': 'Release all controls; this does not brake a moving vehicle.',
    'forward': 'Walk forward or accelerate the vehicle.',
    'backward': 'Walk backward; in a vehicle brake then reverse.',
    'left': 'Strafe left on foot or steer left in a vehicle.',
    'right': 'Strafe right on foot or steer right in a vehicle.',
    'forward_left': 'Move forward while steering/strafe left.',
    'forward_right': 'Move forward while steering/strafe right.',
    'sprint': 'Sprint forward on foot.',
    'jump': 'Jump on foot; handbrake in a vehicle.',
    'interact': 'Press E once to enter or exit a nearby vehicle.',
    'brake': 'Hold the handbrake to stop the vehicle without engaging reverse; on foot this is jump.',
    'camera': 'Cycle camera distance.',
    'map': 'Toggle the map.',
    'pause': 'Toggle pause.',
}


def distance(a, b):
    return math.hypot(a[0] - b[0], a[2] - b[2])


def current_action(action, before, fresh, latency_ms):
    changed = any(before[k] != fresh[k] for k in ('on_foot', 'transition', 'playing'))
    if changed or latency_ms > 1000 or not fresh['alive']:
        return 'brake' if fresh['playing'] and not fresh['on_foot'] and not fresh['transition'] else 'wait'
    return action


class Game:
    def __init__(self, output, seed=42):
        self.output = Path(output).resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.session = Path(tempfile.mkdtemp(prefix='pc-playtest-'))
        self.sequence = 0
        self.log = (self.output / 'game.log').open('w')
        env = dict(os.environ, APRICOT_PLAYTEST_DIR=str(self.session))
        # Credentials belong only to this Python process, never the game.
        env.pop('TYPESAFE_API_KEY', None)
        self.process = subprocess.Popen([
            str(ROOT / 'build/bin/apricot'), '--frames', '1000000',
            '--save-file', str(self.session / 'checkpoint.bin'),
            '--daylight', '--clear', '--seed', str(seed),
            '--start-at', '27.3', '-14.39', '--start-heading', '186', '--road-start',
        ], cwd=ROOT, env=env, stdout=self.log, stderr=subprocess.STDOUT)
        try:
            self.initial = self.observe(timeout=60)
        except BaseException:
            self.close()
            raise

    def observe(self, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(f'Game exited ({self.process.returncode}); see {self.output / "game.log"}')
            try:
                path = self.session / 'state.json'
                if time.time() - path.stat().st_mtime > 3:
                    raise RuntimeError('Game state is stale; controls will expire automatically.')
                return json.loads(path.read_text())
            except (FileNotFoundError, json.JSONDecodeError):
                time.sleep(.03)
        raise TimeoutError('Game did not publish state in time.')

    def act(self, action, milliseconds=400):
        if action not in ACTIONS:
            raise ValueError('Unknown action')
        if isinstance(milliseconds, bool) or not isinstance(milliseconds, int) or not 30 <= milliseconds <= 2000:
            raise ValueError('milliseconds must be an integer in 30..2000')
        before = self.observe()
        self.sequence = max(self.sequence, before['accepted']) + 1
        temporary = self.session / 'command.tmp'
        temporary.write_text(f'{self.sequence} {action} {milliseconds}\n')
        temporary.replace(self.session / 'command.txt')
        deadline = time.monotonic() + milliseconds / 1000 + 8
        while time.monotonic() < deadline:
            state = self.observe()
            if state['completed'] == self.sequence:
                capture = self.output / f'{self.sequence:04d}-{action}.png'
                if state['capture_id'] == self.sequence:
                    shutil.copyfile(self.session / 'snapshot.png', capture)
                    state['screenshot'] = str(capture)
                with (self.output / 'actions.jsonl').open('a') as stream:
                    stream.write(json.dumps({'action': action, 'milliseconds': milliseconds,
                                             'before': before, 'after': state}) + '\n')
                return state
            time.sleep(.02)
        raise TimeoutError('Game did not acknowledge action completion.')

    def close(self):
        if self.process.poll() is None:
            temporary = self.session / 'command.tmp'
            temporary.write_text(f'{self.sequence + 1000000} quit 30\n')
            temporary.replace(self.session / 'command.txt')
            try:
                self.process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                try:
                    self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
        self.log.close()
        shutil.rmtree(self.session)


class Jev:
    def __init__(self):
        self.key = os.environ.get('TYPESAFE_API_KEY') or KEY_FILE.read_text().strip()
        self.connection = http.client.HTTPSConnection('api.typesafe.ai', timeout=10)

    def decide(self, state, goal, history, allowed=None):
        choices = {a: ACTIONS[a] for a in (allowed or ACTIONS)}
        payload = {'model': 'jev-1.13.0',
                   'state': {'goal': goal, 'observation': {k: v for k, v in state.items()
                       if k not in ('screenshot', 'fps', 'capture_id', 'accepted', 'completed')},
                       'recent_actions': history[-5:]},
                   'questions': {'action': {'type': 'choice',
                       'instructions': 'Choose the next short controller action toward the goal. '
                           'Use wait during a vehicle transition. Interaction toggles enter/exit; '
                           'do not repeat it during a transition. Use only observed facts.',
                       'criteria': choices}}}
        started = time.monotonic()
        self.connection.request('POST', '/v1/systemone', json.dumps(payload), {
            'Authorization': 'Bearer ' + self.key, 'Content-Type': 'application/json'})
        response = self.connection.getresponse()
        body = response.read()
        if response.status != 200:
            # Never echo arbitrary response bodies, headers, or credentials.
            raise RuntimeError(f'TypeSafe HTTP {response.status}')
        result = json.loads(body)
        answer = result['answers']['action']
        action = answer['choice']
        confidence = answer.get('confidence', 0)
        if action not in choices or not isinstance(confidence, (int, float)) or not math.isfinite(confidence):
            raise ValueError('Invalid Jev action/confidence')
        if confidence < .55:
            action = 'wait'
        return action, {'request': payload, 'response': result,
                        'latency_ms': (time.monotonic() - started) * 1000,
                        'executed_action': action}

    def close(self):
        self.connection.close()


class Objective:
    """Success comes from measured game changes, never a model's claim."""
    def __init__(self, initial):
        self.initial = initial
        self.stage = 'walk'
        self.entered = False
        self.driven = False
        self.exited = False
        self.walked = False
        self.car_start = initial['car_position']

    def update(self, state):
        if self.stage == 'walk' and state['distance_walked_m'] - self.initial['distance_walked_m'] >= .8:
            self.walked = True
            self.stage = 'return'
        if self.stage == 'return' and distance(state['position'], self.initial['position']) < .35:
            self.stage = 'enter'
        if self.stage == 'enter' and not state['on_foot'] and not state['transition']:
            self.entered = True
            self.car_start = state['car_position']
            self.stage = 'drive'
        if self.stage == 'drive' and distance(state['car_position'], self.car_start) >= 3:
            self.driven = True
            self.stage = 'stop'
        if self.stage == 'stop' and state['speed_mps'] < .3:
            self.stage = 'exit'
        if self.stage == 'exit' and state['on_foot'] and not state['transition']:
            self.exited = True
            self.stage = 'complete'
        return self.stage

    def goal(self):
        return {
            'walk': 'Walk forward about one metre, then stop.',
            'return': 'Walk backward to the original position beside the car.',
            'enter': 'Enter the nearby car using interact when can_enter is true; wait during transition.',
            'drive': 'Drive straight forward at least three metres.',
            'stop': 'Brake the moving car until it is stopped.',
            'exit': 'Exit the stopped car with interact; wait during transition.',
            'complete': 'Stop. The measured test is complete.',
        }[self.stage]


def rules(state, stage):
    if state['transition']:
        return 'wait'
    return {'walk': 'forward', 'return': 'backward',
            'enter': 'interact' if state['can_enter'] else 'wait',
            'drive': 'forward', 'stop': 'brake', 'exit': 'interact', 'complete': 'wait'}[stage]


def run_trial(policy, output, max_steps=50, seed=42):
    game = Game(output, seed)
    jev = None
    started = time.monotonic()
    records, error = [], None
    max_damage = 0.0
    objective = Objective(game.initial)
    state = game.initial
    # Fixed open-loop schedule: no adaptive waits or recovery.
    script = [('forward', 500), ('backward', 500), ('interact', 100),
              ('wait', 2000), ('wait', 1500), ('forward', 1200),
              ('brake', 1800), ('interact', 100), ('wait', 2000), ('wait', 1500)]
    try:
        if policy == 'jev':
            jev = Jev()
        for index in range(max_steps):
            stage = objective.update(state)
            if stage == 'complete':
                break
            if not state['alive'] or state['gl_errors'] or max_damage > 1:
                raise RuntimeError('Player died, vehicle took damage, or renderer reported an error')
            if time.monotonic() - started > 100:
                raise TimeoutError('Trial exceeded 100 seconds')
            record = {'stage': stage}
            if policy == 'scripted':
                if index >= len(script):
                    break
                action, duration = script[index]
            elif policy == 'rules':
                action, duration = rules(state, stage), 400
            elif policy == 'jev':
                action, decision = jev.decide(state, objective.goal(),
                    [r['action'] for r in records],
                    ['wait', 'forward', 'backward', 'interact', 'brake'])
                record['jev'] = decision
                # Recheck state after network I/O; do not apply an old choice
                # if its high-level preconditions changed while waiting.
                fresh = game.observe()
                validated = current_action(action, state, fresh, decision['latency_ms'])
                if validated != action:
                    action = validated
                    record['stale_choice_discarded'] = True
                duration = 400
            else:
                raise ValueError('Unknown policy')
            if action == 'interact':
                duration = 100
            state = game.act(action, duration)
            max_damage = max(max_damage, game.initial['car_health'] - state['car_health'])
            record.update(action=action, milliseconds=duration, state=state)
            records.append(record)
            with (game.output / 'decisions.jsonl').open('a') as stream:
                stream.write(json.dumps(record) + '\n')
        objective.update(state)
    except Exception as exc:
        error = str(exc)
    finally:
        if jev:
            jev.close()
        game.close()
    latency = [r['jev']['latency_ms'] for r in records if 'jev' in r]
    result = {'policy': policy, 'seed': seed,
              'success': (objective.stage == 'complete' and error is None and max_damage <= 1
                          and state['alive'] and state['gl_errors'] == 0 and game.process.returncode == 0),
              'walked': objective.walked, 'max_car_damage': max_damage,
              'stage': objective.stage, 'entered': objective.entered, 'driven': objective.driven,
              'exited': objective.exited, 'steps': len(records),
              'elapsed_seconds': time.monotonic() - started, 'error': error,
              'median_api_ms': statistics.median(latency) if latency else None,
              'max_api_ms': max(latency) if latency else None,
              'input_tokens': sum(r.get('jev', {}).get('response', {}).get('usage', {}).get('input_tokens', 0) for r in records),
              'initial': game.initial, 'final': state, 'output': str(game.output),
              'game_exit_code': game.process.returncode}
    (game.output / 'result.json').write_text(json.dumps(result, indent=2))
    return result


def schema(properties=None, required=None):
    return {'type': 'object', 'properties': properties or {}, 'required': required or [], 'additionalProperties': False}


TOOLS = [
    {'name': 'probable_cause_run_test', 'description': 'Run the complete walk/enter/drive/stop/exit experiment locally in one call. Returns measured results and a screenshot. Jev policy uses the saved TypeSafe key. Requires no other active tool session.', 'inputSchema': schema({'policy': {'type': 'string', 'enum': ['jev', 'rules', 'scripted']}}, ['policy'])},
    {'name': 'probable_cause_start', 'description': 'Start an isolated unfocused QA game. Auto-exits after 3 minutes.', 'inputSchema': schema()},
    {'name': 'probable_cause_observe', 'description': 'Read game telemetry and capture a fresh screenshot.', 'inputSchema': schema()},
    {'name': 'probable_cause_act', 'description': 'Apply bounded ordinary game controls and return resulting state and screenshot.', 'inputSchema': schema({'action': {'type': 'string', 'enum': list(ACTIONS)}, 'milliseconds': {'type': 'integer', 'minimum': 30, 'maximum': 2000}}, ['action'])},
    {'name': 'probable_cause_jev_step', 'description': 'Ask Jev for one short action toward a goal, execute it, return evidence.', 'inputSchema': schema({'goal': {'type': 'string', 'maxLength': 2000}}, ['goal'])},
    {'name': 'probable_cause_stop', 'description': 'Close only the game session started by this tool.', 'inputSchema': schema()},
]


def serve():
    game, jev = None, None
    try:
        for line in sys.stdin:
            request = None
            try:
                request = json.loads(line)
                if 'id' not in request:
                    continue
                method = request.get('method')
                if method == 'initialize':
                    result = {'protocolVersion': '2024-11-05', 'capabilities': {'tools': {}},
                              'serverInfo': {'name': 'probable-cause-playtest', 'version': '0.1.0'}}
                elif method == 'ping':
                    result = {}
                elif method == 'tools/list':
                    result = {'tools': TOOLS}
                elif method == 'tools/call':
                    params = request['params']
                    name, args = params['name'], params.get('arguments', {})
                    try:
                        decision = None
                        if name == 'probable_cause_run_test':
                            if game:
                                raise ValueError('Stop the interactive test session before running a separate experiment.')
                            if args['policy'] not in ('jev', 'rules', 'scripted'):
                                raise ValueError('Unknown policy')
                            trial = run_trial(args['policy'], ROOT / 'build/playtests' / str(time.time_ns()))
                            state = dict(trial['final'], experiment=trial)
                        elif name == 'probable_cause_start':
                            if game:
                                raise ValueError('A session is already running; stop it first.')
                            game = Game(ROOT / 'build/playtests' / str(time.time_ns()))
                            state = game.initial
                        elif name == 'probable_cause_stop':
                            if game:
                                game.close()
                                game = None
                            state = {'stopped': True}
                        elif not game:
                            raise ValueError('Call probable_cause_start first.')
                        elif name == 'probable_cause_observe':
                            state = game.act('wait', 30)
                        elif name == 'probable_cause_act':
                            state = game.act(args['action'], args.get('milliseconds', 400))
                        elif name == 'probable_cause_jev_step':
                            goal = args['goal']
                            if not isinstance(goal, str) or not 1 <= len(goal) <= 2000:
                                raise ValueError('goal must have 1..2000 characters')
                            if jev is None:
                                jev = Jev()
                            before = game.observe()
                            action, decision = jev.decide(before, goal, [])
                            fresh = game.observe()
                            action = current_action(action, before, fresh, decision['latency_ms'])
                            state = game.act(action, 100 if action == 'interact' else 400)
                        else:
                            raise ValueError('Unknown tool')
                        result = {'content': [{'type': 'text', 'text': json.dumps({'state': state, 'decision': decision})}]}
                        if state.get('screenshot'):
                            result['content'].append({'type': 'image', 'mimeType': 'image/png',
                                'data': base64.b64encode(Path(state['screenshot']).read_bytes()).decode()})
                    except Exception as exc:
                        result = {'isError': True, 'content': [{'type': 'text', 'text': str(exc)}]}
                else:
                    raise ValueError('Unsupported method')
                response = {'jsonrpc': '2.0', 'id': request['id'], 'result': result}
            except Exception as exc:
                response = {'jsonrpc': '2.0', 'id': request.get('id') if isinstance(request, dict) else None,
                            'error': {'code': -32600, 'message': str(exc)}}
            print(json.dumps(response), flush=True)
    finally:
        if game:
            game.close()
        if jev:
            jev.close()


def main():
    def terminate(_signum, _frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, terminate)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['mcp', 'run', 'compare'])
    parser.add_argument('--policy', choices=['jev', 'rules', 'scripted'], default='jev')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/playtests' / str(time.time_ns()))
    parser.add_argument('--repeats', type=int, choices=range(1, 4), default=1)
    args = parser.parse_args()
    if args.command == 'mcp':
        serve()
        return
    results = []
    for repeat in range(args.repeats):
        for policy in (['scripted', 'rules', 'jev'] if args.command == 'compare' else [args.policy]):
            result = run_trial(policy, args.output / f'{repeat + 1}-{policy}', seed=42 + repeat)
            results.append(result)
            print(json.dumps(result), flush=True)
            (args.output / 'comparison.json').write_text(json.dumps(results, indent=2))
    if any(not r['success'] for r in results):
        sys.exit(1)


if __name__ == '__main__':
    main()
