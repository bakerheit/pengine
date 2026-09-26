import io
import json
import unittest
from unittest.mock import patch

import playtest as p


def snapshot(**updates):
    state = dict(position=[0, 0, 0], car_position=[2, 0, 0], on_foot=True,
                 transition=False, distance_walked_m=0, speed_mps=0, can_enter=True)
    state.update(updates)
    return state


class ObjectiveTests(unittest.TestCase):
    def test_moving_car_without_walking_and_entry_does_not_pass(self):
        obj = p.Objective(snapshot())
        obj.update(snapshot(car_position=[99, 0, 0], on_foot=False))
        self.assertEqual(obj.stage, 'walk')
        self.assertFalse(obj.entered)

    def test_sequence_requires_completed_entry_and_exit(self):
        obj = p.Objective(snapshot())
        self.assertEqual(obj.update(snapshot(distance_walked_m=1, position=[0, 0, -1])), 'return')
        self.assertEqual(obj.update(snapshot(distance_walked_m=2)), 'enter')
        self.assertEqual(obj.update(snapshot(on_foot=False, transition=True)), 'enter')
        self.assertEqual(obj.update(snapshot(on_foot=False)), 'drive')
        self.assertEqual(obj.update(snapshot(on_foot=False, car_position=[2, 0, 4], speed_mps=3)), 'stop')
        self.assertEqual(obj.update(snapshot(on_foot=False, speed_mps=.1)), 'exit')
        self.assertEqual(obj.update(snapshot(transition=True)), 'exit')
        self.assertEqual(obj.update(snapshot()), 'complete')


class TransportTests(unittest.TestCase):
    def test_stale_network_response_cannot_apply_throttle(self):
        state = dict(on_foot=False, transition=False, playing=True, alive=True)
        self.assertEqual(p.current_action('forward', state, state, 1500), 'brake')
        self.assertEqual(p.current_action('forward', state, dict(state, on_foot=True), 100), 'wait')

    def test_invalid_controls_rejected_before_any_io(self):
        game = p.Game.__new__(p.Game)
        for action, duration in [('shell', 400), ('forward', 999999), ('forward', True), ('forward', 1.5)]:
            with self.assertRaises(ValueError):
                game.act(action, duration)

    def test_mcp_discovery_and_missing_session(self):
        requests = [dict(jsonrpc='2.0', id=1, method='initialize', params={'protocolVersion': '2024-11-05'}),
                    dict(jsonrpc='2.0', method='notifications/initialized'),
                    dict(jsonrpc='2.0', id=2, method='tools/list'),
                    dict(jsonrpc='2.0', id=3, method='tools/call', params={'name': 'probable_cause_act', 'arguments': {'action': 'forward'}})]
        output = io.StringIO()
        with patch('sys.stdin', io.StringIO('\n'.join(map(json.dumps, requests)))), patch('sys.stdout', output):
            p.serve()
        rows = list(map(json.loads, output.getvalue().splitlines()))
        self.assertEqual(len(rows), 3)
        self.assertEqual(len(rows[1]['result']['tools']), 6)
        self.assertTrue(rows[2]['result']['isError'])


class FakeConnection:
    status = 200
    def __init__(self, answer):
        self.answer = answer
    def request(self, *args):
        self.request_args = args
    def getresponse(self):
        return self
    def read(self):
        return json.dumps({'answers': {'action': self.answer}}).encode()


class JevTests(unittest.TestCase):
    def client(self, answer):
        client = p.Jev.__new__(p.Jev)
        client.key = 'test-only'
        client.connection = FakeConnection(answer)
        return client

    def test_low_confidence_releases_controls(self):
        client = self.client({'choice': 'forward', 'confidence': .2})
        action, record = client.decide({}, 'walk', [])
        self.assertEqual(action, 'wait')
        self.assertNotIn('test-only', json.dumps(record))

    def test_unlisted_action_cannot_execute(self):
        client = self.client({'choice': 'shell', 'confidence': .99})
        with self.assertRaises(ValueError):
            client.decide({}, 'walk', [])

    def test_nonfinite_confidence_cannot_execute(self):
        client = self.client({'choice': 'forward', 'confidence': float('nan')})
        with self.assertRaises(ValueError):
            client.decide({}, 'walk', [])

    def test_http_error_does_not_echo_server_body(self):
        client = self.client({'secret': 'never echo this'})
        client.connection.status = 401
        with self.assertRaisesRegex(RuntimeError, '^TypeSafe HTTP 401$'):
            client.decide({}, 'walk', [])


if __name__ == '__main__':
    unittest.main()
