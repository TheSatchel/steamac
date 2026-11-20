#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('quest_link', Path(__file__).with_name('quest-usb-link.py'))
link = importlib.util.module_from_spec(spec)
spec.loader.exec_module(link)


class Fake:
    def __init__(self, status='waiting', tunnel=True, awake=True, running=False):
        self.status, self.mapping, self.is_awake, self.running = status, tunnel, awake, running
        self.generation = 'one'
        self.problem = None
        self.actions = []
        self.fail_start = False

    def host(self):
        return None if self.status is None else (self.generation, self.status)

    def device(self):
        self.actions.append('devices')
        return (None, self.problem) if self.problem else ('test-device', None)

    def tunnel(self, device):
        self.actions.append('check_tunnel')
        return self.mapping

    def repair_tunnel(self, device):
        self.actions.append('repair_tunnel')
        self.mapping = True

    def awake(self, device):
        self.actions.append('awake')
        return self.is_awake

    def client_running(self, device):
        return self.running

    def start_client(self, device, force=False):
        self.actions.append(('start', force))
        if self.fail_start:
            raise RuntimeError('simulated activity failure')


class GuardTests(unittest.TestCase):
    def test_vr_off_does_not_touch_adb(self):
        ops = Fake(status=None)
        self.assertEqual(link.tick(ops, {}, 100)[0], 'idle')
        self.assertEqual(ops.actions, [])

    def test_healthy_stream_is_never_restarted(self):
        ops = Fake(status='connected', running=True)
        self.assertEqual(link.tick(ops, {}, 100)[0], 'connected')
        self.assertNotIn('awake', ops.actions)
        self.assertFalse(any(isinstance(a, tuple) for a in ops.actions))

    def test_missing_tunnel_is_repaired_without_restarting_a_live_stream(self):
        ops = Fake(status='connected', tunnel=False, running=True)
        self.assertEqual(link.tick(ops, {}, 100), ('connected', 'restored USB tunnel'))
        self.assertEqual(ops.actions.count('repair_tunnel'), 1)
        self.assertFalse(any(isinstance(a, tuple) for a in ops.actions))

    def test_sleeping_quest_gets_tunnel_but_no_app_restart(self):
        ops = Fake(tunnel=False, awake=False)
        self.assertEqual(link.tick(ops, {}, 100)[0], 'waiting_headset_awake')
        self.assertTrue(ops.mapping)
        self.assertFalse(any(isinstance(a, tuple) for a in ops.actions))

    def test_waking_quest_starts_usb_client_and_honors_backoff(self):
        ops, state = Fake(tunnel=False), {}
        self.assertEqual(link.tick(ops, state, 100)[0], 'connecting')
        self.assertIn(('start', False), ops.actions)
        ops.actions.clear()
        link.tick(ops, state, 105)
        self.assertFalse(any(isinstance(a, tuple) for a in ops.actions))
        ops.running = True
        link.tick(ops, state, 115)
        self.assertIn(('start', True), ops.actions)

    def test_existing_client_gets_time_before_forced_restart(self):
        ops, state = Fake(running=True), {}
        link.tick(ops, state, 100)
        self.assertFalse(any(isinstance(a, tuple) for a in ops.actions))
        link.tick(ops, state, 116)
        self.assertIn(('start', True), ops.actions)

    def test_new_host_does_not_inherit_old_retry_deadline(self):
        ops = Fake()
        state = {'generation': 'old', 'next_attempt': 10000, 'attempts': 4}
        link.tick(ops, state, 100)
        self.assertIn(('start', False), ops.actions)
        self.assertEqual(state['attempts'], 1)

    def test_authorization_or_multiple_devices_never_launches_client(self):
        for reason in ['waiting_usb', 'usb_authorization_required', 'multiple_quests']:
            ops = Fake()
            ops.problem = reason
            self.assertEqual(link.tick(ops, {}, 100)[0], reason)
            self.assertEqual(ops.actions, ['devices'])

    def test_failed_start_retains_retry_deadline(self):
        ops, state = Fake(), {}
        ops.fail_start = True
        with self.assertRaises(RuntimeError):
            link.tick(ops, state, 100)
        ops.actions.clear()
        link.tick(ops, state, 105)
        self.assertFalse(any(isinstance(a, tuple) for a in ops.actions))

    def test_reconnected_headset_clears_backoff(self):
        ops, state = Fake(), {}
        link.tick(ops, state, 100)
        ops.status = 'connected'
        link.tick(ops, state, 101)
        self.assertEqual(state['attempts'], 0)
        self.assertEqual(state['next_attempt'], 0)

    def test_native_adb_whitespace_and_exact_port_mapping(self):
        ops = link.Operations('/unused', '/unused')
        ops.run = lambda *args, **kwargs: 'List of devices attached\nexample device product:eureka model:Quest_3 device:eureka\n\n'
        self.assertEqual(ops.device(), ('example', None))
        ops.run = lambda *args, **kwargs: 'UsbFfs tcp:9757 tcp:9757\n'
        self.assertTrue(ops.tunnel('example'))
        ops.run = lambda *args, **kwargs: 'UsbFfs tcp:9757 tcp:1234\n'
        self.assertFalse(ops.tunnel('example'))


if __name__ == '__main__':
    unittest.main()
