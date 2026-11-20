#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import tempfile, os, subprocess, json
root=Path(__file__).resolve().parents[2]
wrapper=root/'guest/layer/usr/lib/steamac/vr-launch-bin/steam'
with tempfile.TemporaryDirectory() as d:
    d=Path(d);log=d/'args';mock=d/'mock'
    mock.write_text('#!/usr/bin/python3\nimport os,sys,json\nwith open(os.environ["PROBE_LOG"],"a") as f:f.write(json.dumps(sys.argv[1:])+"\\n")\n');mock.chmod(0o755)
    env=dict(os.environ,XDG_RUNTIME_DIR=str(d),SATCHEL_STEAM_BIN=str(mock),PROBE_LOG=str(log))
    subprocess.run([str(wrapper),'-ifrunning','steam://launch/438100/vr'],env=env,check=True)
    subprocess.run([str(wrapper),'-ifrunning','steam://launch/438100/VR'],env=env,check=True)
    subprocess.run([str(wrapper),'--argument with spaces','steam://rungameid/438100'],env=env,check=True)
    rows=[json.loads(x) for x in log.read_text().splitlines()]
    assert rows==[['-ifrunning','steam://launch/438100/VR'],['--argument with spaces','steam://rungameid/438100']]
print('PASS: VR case, startup debounce and argument preservation')
