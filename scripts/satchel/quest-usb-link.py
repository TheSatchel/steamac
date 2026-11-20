#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Maintain one Quest's local ADB VR tunnel while steamac-vrhost is running."""
import argparse
import datetime
import fcntl
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import socket
import subprocess
import time

LABEL = 'com.thesatchel.quest-usb-link'
PACKAGE = 'org.meumeu.wivrn.github'
ACTIVITY = PACKAGE + '/org.meumeu.wivrn.MainActivity'
PORT = 'tcp:9757'


class Operations:
    def __init__(self, adb, control):
        self.adb = str(adb)
        self.control = Path(control)

    def host(self):
        try:
            stamp = self.control.stat()
            with socket.socket(socket.AF_UNIX) as s:
                s.settimeout(2)
                s.connect(str(self.control))
                s.sendall(b'status\n')
                data = b''
                while b'\n' not in data and len(data) < 131072:
                    chunk = s.recv(8192)
                    if not chunk:
                        return None
                    data += chunk
            reply = json.loads(data.split(b'\n', 1)[0])
            return str(stamp.st_ino) + ':' + str(stamp.st_ctime_ns), reply.get('state')
        except (OSError, ValueError):
            return None

    def run(self, args, device=None, timeout=6, allow_absent=False):
        cmd = [self.adb] + (['-s', device] if device else []) + args
        try:
            p = subprocess.run(cmd, env=dict(os.environ, ADB_LIBUSB='0'),
                               capture_output=True, text=True, timeout=timeout)
        except (OSError, subprocess.TimeoutExpired):
            raise RuntimeError('ADB command unavailable or timed out') from None
        if p.returncode == 1 and allow_absent:
            return ''
        if p.returncode:
            raise RuntimeError('ADB command failed')
        return p.stdout

    def device(self):
        candidates = []
        unauthorized = False
        for line in self.run(['devices', '-l']).splitlines()[1:]:
            fields = line.split()
            if len(fields) < 2:
                continue
            unauthorized |= fields[1] == 'unauthorized'
            if fields[1] == 'device' and 'model:Quest_3' in fields:
                candidates.append(fields[0])
        if len(candidates) == 1:
            return candidates[0], None
        return None, 'multiple_quests' if len(candidates) > 1 else 'usb_authorization_required' if unauthorized else 'waiting_usb'

    def tunnel(self, device):
        return any(line.split()[-2:] == [PORT, PORT]
                   for line in self.run(['reverse', '--list'], device).splitlines())

    def repair_tunnel(self, device):
        self.run(['reverse', PORT, PORT], device)
        if not self.tunnel(device):
            raise RuntimeError('USB tunnel verification failed')

    def awake(self, device):
        return 'mWakefulness=Awake' in self.run(['shell', 'dumpsys', 'power'], device)

    def client_running(self, device):
        # pidof returns 1 when absent; avoid treating that as a broken ADB link.
        return bool(self.run(['shell', 'pidof', PACKAGE], device, allow_absent=True).strip())

    def start_client(self, device, force=False):
        if force:
            self.run(['shell', 'am', 'force-stop', PACKAGE], device)
        result = self.run(['shell', 'am', 'start', '-W', '-n', ACTIVITY,
                           '-a', 'android.intent.action.VIEW',
                           '-d', 'wivrn+tcp://127.0.0.1:9757'], device, timeout=15)
        if 'Error:' in result or 'Exception' in result:
            raise RuntimeError('WiVRn activity did not start')


def reset_attempts(state):
    state.update(attempts=0, next_attempt=0, waiting_since=0)


def tick(ops, state, now):
    host = ops.host()
    if host is None:
        reset_attempts(state)
        state['generation'] = None
        return 'idle', None
    generation, status = host
    if generation != state.get('generation'):
        reset_attempts(state)
        state['generation'] = generation
    device, problem = ops.device()
    if device is None:
        reset_attempts(state)
        return problem, None
    repaired = not ops.tunnel(device)
    if repaired:
        ops.repair_tunnel(device)
        state['next_attempt'] = 0
    if status == 'connected':
        reset_attempts(state)
        return 'connected', 'restored USB tunnel' if repaired else None
    if not ops.awake(device):
        reset_attempts(state)
        return 'waiting_headset_awake', 'restored USB tunnel' if repaired else None
    if now < state.get('next_attempt', 0):
        return 'connecting', None
    if not state.get('waiting_since'):
        state['waiting_since'] = now
    running = ops.client_running(device)
    # Give an already-running client one poll to reconnect on its own.
    if running and not repaired and now-state['waiting_since'] < 5:
        return 'connecting', None
    force = running and (state.get('attempts', 0) > 0 or now-state['waiting_since'] >= 15)
    attempts = state.get('attempts', 0) + 1
    state.update(attempts=attempts, next_attempt=now+min(60, 15*(2**min(attempts-1, 2))))
    ops.start_client(device, force=force)
    return 'connecting', 'restarted stalled WiVRn client' if force else 'requested WiVRn USB connection'


def install(args):
    directory = args.state_dir
    directory.mkdir(parents=True, exist_ok=True)
    directory.chmod(0o700)
    target = 'gui/' + str(os.getuid())
    subprocess.run(['launchctl', 'bootout', target + '/' + LABEL], capture_output=True)
    # launchd must not depend on Downloads access or the user keeping an installer folder.
    tools = directory / 'platform-tools'
    tools.mkdir(exist_ok=True)
    bundled_adb = tools / 'adb'
    if args.adb.resolve() != bundled_adb.resolve():
        shutil.copy2(args.adb, bundled_adb)
    bundled_adb.chmod(0o700)
    for name in ['NOTICE.txt', 'source.properties']:
        source = args.adb.parent / name
        if source.is_file() and source.resolve() != (tools / name).resolve():
            shutil.copy2(source, tools / name)
    (tools / 'adb.sha256').write_text(hashlib.sha256(bundled_adb.read_bytes()).hexdigest() + '\n')
    script = directory / 'quest-usb-link.py'
    if Path(__file__).resolve() != script.resolve():
        shutil.copy2(__file__, script)
    script.chmod(0o700)
    agent = Path.home() / 'Library/LaunchAgents' / (LABEL + '.plist')
    agent.parent.mkdir(parents=True, exist_ok=True)
    config = {'Label': LABEL, 'ProgramArguments': ['/usr/bin/python3', str(script), '--once',
              '--adb', str(bundled_adb), '--control-socket', str(args.control_socket),
              '--state-dir', str(directory)], 'StartInterval': 5, 'ThrottleInterval': 5, 'RunAtLoad': True,
              'ProcessType': 'Background', 'StandardOutPath': str(directory / 'agent.log'),
              'StandardErrorPath': str(directory / 'agent.log')}
    agent.write_bytes(plistlib.dumps(config))
    subprocess.run(['launchctl', 'bootstrap', target, str(agent)], check=True)
    print('Installed ' + LABEL + '; checks every 5 seconds while the VR host is running')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', type=Path)
    parser.add_argument('--control-socket', type=Path, default=Path.home() / 'Library/Application Support/steamac/vrhost/control.sock')
    parser.add_argument('--state-dir', type=Path, default=Path.home() / 'Library/Application Support/steamac/quest-usb-link')
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--install', action='store_true')
    group.add_argument('--uninstall', action='store_true')
    group.add_argument('--status', action='store_true')
    group.add_argument('--once', action='store_true')
    args = parser.parse_args()
    if args.uninstall:
        subprocess.run(['launchctl', 'bootout', 'gui/' + str(os.getuid()) + '/' + LABEL], capture_output=True)
        (Path.home() / 'Library/LaunchAgents' / (LABEL + '.plist')).unlink(missing_ok=True)
        print('Stopped and uninstalled ' + LABEL)
        return
    if args.status:
        path = args.state_dir / 'state.json'
        print(path.read_text() if path.exists() else '{"status":"not_started"}')
        return
    if not args.adb or not args.adb.is_file():
        parser.error('--adb must point to the installed platform-tools adb')
    if args.install:
        install(args)
        return
    args.state_dir.mkdir(parents=True, exist_ok=True)
    with (args.state_dir / 'lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return
        path = args.state_dir / 'state.json'
        try:
            state = json.loads(path.read_text())
        except (OSError, ValueError):
            state = {}
        previous = state.get('status')
        try:
            status, event = tick(Operations(args.adb, args.control_socket), state, time.time())
            state.pop('error', None)
        except RuntimeError as e:
            status, event = 'retrying', None
            state['error'] = str(e)
        state.update(status=status, updated_at=datetime.datetime.now(datetime.timezone.utc).isoformat())
        temp = path.with_suffix('.tmp')
        temp.write_text(json.dumps(state, indent=2) + '\n')
        os.replace(temp, path)
        if status != previous or event:
            print(state['updated_at'] + ' ' + status + (': ' + event if event else ''), flush=True)


if __name__ == '__main__':
    main()
