#!/usr/bin/env python3
"""Two isolated agents + production engine using synthetic audio; never touches live audio routes."""
import json, os, pathlib, socket, subprocess, sys, tempfile, time
agent=pathlib.Path(sys.argv[1]).resolve(); engine=pathlib.Path(sys.argv[2]).resolve()
def port():
    with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
def wait(fn, label, seconds=15):
    end=time.monotonic()+seconds;last=None
    while time.monotonic()<end:
        try:
            last=fn()
            if last:return last
        except (OSError,ValueError,AssertionError,subprocess.TimeoutExpired):pass
        time.sleep(.12)
    raise AssertionError(f'{label}: timed out, last={last}')
class Instance:
    def __init__(self, root, platform, roomport, media, discovery):
        self.root=root;self.root.mkdir();self.platform=platform
        self.args=['--data-dir',str(root),'--engine',str(engine),'--platform',platform,'--bind','127.0.0.1','--room-port',str(roomport),'--media-port',str(media),'--discovery-port',str(discovery)]
        self.log=open(root/'server.log','w');self.start()
    def start(self):
        self.process=subprocess.Popen([str(agent),'serve',*self.args],stdout=self.log,stderr=self.log,env={**os.environ,'BRIDGE_FAKE_METRICS':'0'})
        wait(lambda:self.call('status')['ok'],'agent startup')
    def call(self,command,**kw):
        p=subprocess.run([str(agent),'request',*self.args],input=json.dumps({'command':command,**kw})+'\n',text=True,capture_output=True,timeout=8)
        return json.loads(p.stdout)
    def state(self):return self.call('status')['state']
    def close(self):
        try:self.call('forget')
        except Exception:pass
        try:self.call('quit')
        except Exception:pass
        try:self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:self.process.kill();self.process.wait()
        self.log.close()
with tempfile.TemporaryDirectory(prefix='san-audisco-agent-test-') as temp:
    root=pathlib.Path(temp);wp,mp,wm,mm=port(),port(),port(),port();instances=[]
    try:
        win=Instance(root/'win','windows',wp,wm,mp);instances.append(win)
        mac=Instance(root/'mac','mac',mp,mm,wp);instances.append(mac)
        assert not win.state()['running'] and not mac.state()['running']
        assert not win.state()['diagnostics']['output_routing_active']
        assert not mac.state()['diagnostics']['output_routing_active']
        assert win.state()['devices'][0]['setup_status']=='Ready'
        duplicate=subprocess.run([str(agent),'ensure',*win.args],capture_output=True,text=True,timeout=10)
        assert json.loads(duplicate.stdout)['ok']
        # Same endpoint remains in use: ensure did not spawn another engine/agent.
        created=win.call('create');assert created['ok'],created
        code=created['state']['invite_code'];assert len(code.replace('-',''))==8
        joined=mac.call('join',code=code,address=f'127.0.0.1:{wp}');assert joined['ok'],joined
        wait(lambda:win.state()['connected'] and mac.state()['connected'],'pairing')
        wait(lambda:len(win.state()['devices'])==2 and len(mac.state()['devices'])==2,'inventory')
        assert win.state()['invite_code']==''
        print('PASS private agent startup, discovery/manual address, pairing, inventory, singleton ensure')
        mid=mac.state()['device_id'];wid=win.state()['device_id']
        assert win.call('configure',device_id=mid,name='Laptop from Windows')['ok']
        wait(lambda:mac.state()['name']=='Laptop from Windows','remote rename')
        rejected=win.call('configure',device_id=wid,physical_microphone='win-mic-sink')
        assert not rejected['ok']
        assert mac.call('autoconfigure')['ok']
        wait(lambda:mac.state()['pending_changes']==0,'room-wide Autoconfigure acknowledgement')
        assert win.call('start')['ok']
        wait(lambda:win.state()['diagnostics'].get('synchronized') and mac.state()['diagnostics'].get('synchronized'),'engine synchronized')
        wait(lambda:win.state()['diagnostics']['output_routing_active'] and mac.state()['diagnostics']['output_routing_active'],'automatic output takeover after both engines ready')
        wait(lambda:win.state()['diagnostics']['input_routing_active'] and mac.state()['diagnostics']['input_routing_active'],'Autoconfigure enables shared default microphone')
        for device,target in [(win,'win-mic-capture'),(mac,'mac-mic-sink')]:
            journal=json.loads((device.root/'input-restore.json').read_text())
            assert {r['target'] for r in journal['entries']}=={target}
        for device,target in [(win,'win-playback-render'),(mac,'mac-playback')]:
            journal=json.loads((device.root/'output-restore.json').read_text())
            assert {r['target'] for r in journal['entries']}=={target}
            assert {r['before'] for r in journal['entries']}=={'test-speakers','test-headphones','test-calls'}
        time.sleep(1.2)
        assert mac.call('control',key='mute',value=0)['ok']
        wait(lambda:not win.state()['diagnostics'].get('muted',True) and not mac.state()['diagnostics'].get('muted',True),'remote unmute')
        assert mac.call('control',key='win_volume',value=.42)['ok']
        wait(lambda:abs(win.state()['settings']['win_volume']-.42)<.001 and abs(mac.state()['settings']['win_volume']-.42)<.001,'volume acknowledgement')
        assert win.call('control',key='deafen',value=1)['ok']
        wait(lambda:mac.state()['settings']['deafen']==1,'deafen sync')
        assert win.call('control',key='deafen',value=0)['ok']
        wait(lambda:mac.state()['diagnostics'].get('microphone_level',0)>.01 and win.state()['diagnostics'].get('microphone_delivery_level',0)>.01,'microphone signal meters')
        assert win.call('test_tone',device_id=mid)['ok']
        wait(lambda:win.state()['pending_changes']==0,'remote test sound acknowledgement')
        print('PASS remote setup, direction validation, room-wide start, mixer controls, signal meters and remote test-tone command')
        # Restart an agent with saved state, preserving pairing and explicit start intent.
        mac.call('quit');mac.process.wait(timeout=5)
        assert mac.call('quit')['ok']  # Idempotent Quit when no daemon holds its lock.
        wait(lambda:not win.state()['connected'],'disconnect detected',seconds=20)
        for device in (win,mac):
            assert json.loads((device.root/'output-restore.json').read_text())['entries']==[], 'Disconnect/quit must restore outputs'
            assert json.loads((device.root/'input-restore.json').read_text())['entries']==[], 'Disconnect/quit must restore inputs'
        mac.start()
        wait(lambda:win.state()['connected'] and mac.state()['connected'],'saved room reconnect',seconds=20)
        wait(lambda:win.state()['diagnostics'].get('synchronized') and mac.state()['diagnostics'].get('synchronized'),'audio resumes',seconds=20)
        assert win.state()['diagnostics']['muted'] and mac.state()['diagnostics']['muted']
        assert win.state()['mute_reason']
        print('PASS saved credential reconnect, remembered start intent, explicit protective mute reason')
        assert mac.call('stop')['ok']
        wait(lambda:not win.state()['running'] and not mac.state()['running'],'room-wide stop')
        assert 'Audio bridge active' not in win.state()['status'], 'Remote Stop left stale active status'
        for device in (win,mac):
            assert not device.state()['diagnostics']['output_routing_active']
            assert json.loads((device.root/'output-restore.json').read_text())['entries']==[]
            assert json.loads((device.root/'input-restore.json').read_text())['entries']==[]
        print('PASS automatic render takeover, saved original defaults, disconnect/quit and room-wide stop restoration')
        # Plaintext state must not contain invitation or cryptographic secrets.
        for device in (win,mac):
            state=json.loads((device.root/'state.json').read_text())
            assert 'secret' not in state.get('room',{}) and code not in json.dumps(state)
            cfg=(device.root/'engine.conf').read_text();assert 'key_stdin=1' in cfg and 'key_file' not in cfg
        # Unknown local caller cannot issue commands without the per-user token.
        endpoint=json.loads((win.root/'endpoint.json').read_text())
        with socket.create_connection(('127.0.0.1',endpoint['port'])) as s:
            s.sendall(b'{"token":"wrong","request":{"command":"quit"}}\n')
            assert not json.loads(s.recv(8192))['ok']
        assert win.state()['paired']
        print('PASS room stop, credential-free configuration, local IPC authentication')
    except Exception:
        for instance in instances:
            print(instance.platform,(instance.root/'server.log').read_text(),file=sys.stderr)
            try:print(json.dumps(instance.state(),indent=2),file=sys.stderr)
            except Exception:pass
        raise
    finally:
        for instance in instances:instance.close()
