#!/usr/bin/env python3
"""Isolated real-agent integration; synthetic hardware only."""
from agent_test_support import *
with tempfile.TemporaryDirectory(prefix='san-audisco-agent-test-') as temp:
    root=pathlib.Path(temp);wp,mp,wm,mm=port(),port(),port(),port();instances=[]
    try:
        platforms=os.environ.get('SAN_TEST_PAIR','windows,mac').split(',')
        win=Instance(root/'win',platforms[0],wp,wm,mp);instances.append(win)
        mac=Instance(root/'mac',platforms[1],mp,mm,wp);instances.append(mac)
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
        rejected=win.call('configure',device_id=wid,physical_microphone=('win-' if platforms[0]=='windows' else 'mac-')+'mic-sink')
        assert not rejected['ok']
        assert mac.call('autoconfigure')['ok']
        wait(lambda:mac.state()['pending_changes']==0,'room-wide Autoconfigure acknowledgement')
        wait(lambda:win.state()['shared_ready'] and mac.state()['shared_ready'],'shared authority state')
        for role,device_id in [('output',wid),('input',mid)]:
            r=win.call('set_role',role=role,device_id=device_id);assert r['ok'],r
            field='hub_device_id' if role=='output' else 'mic_device_id'
            wait(lambda:win.state()[field]==device_id and mac.state()[field]==device_id and win.state()['shared_ready'] and mac.state()['shared_ready'] and not win.state()['handover_active'] and not mac.state()['handover_active'],'initial role assignment')
        started=win.call('start');assert started['ok'],started
        wait(lambda:win.state()['diagnostics'].get('synchronized') and mac.state()['diagnostics'].get('synchronized'),'engine synchronized')
        wait(lambda:win.state()['diagnostics']['output_routing_active'] and mac.state()['diagnostics']['output_routing_active'],'automatic output takeover after both engines ready')
        wait(lambda:win.state()['diagnostics']['input_routing_active'] and mac.state()['diagnostics']['input_routing_active'],'Autoconfigure enables shared default microphone')
        for device,target in [(x,'win-mic-capture' if x.platform=='windows' else 'mac-mic-sink') for x in (win,mac)]:
            journal=json.loads((device.root/'input-restore.json').read_text())
            assert {r['target'] for r in journal['entries']}=={target}
        for device,target in [(x,'win-playback-render' if x.platform=='windows' else 'mac-playback') for x in (win,mac)]:
            journal=json.loads((device.root/'output-restore.json').read_text())
            assert {r['target'] for r in journal['entries']}=={target}
            assert {r['before'] for r in journal['entries']}=={'test-speakers','test-headphones','test-calls'}
        time.sleep(1.2)
        assert mac.call('control',key='mute',value=0)['ok']
        wait(lambda:not win.state()['diagnostics'].get('muted',True) and not mac.state()['diagnostics'].get('muted',True),'remote unmute')
        assert mac.call('control',device_id=wid,key='volume',value=.42)['ok']
        wait(lambda:abs(next(d['mix']['volume'] for d in win.state()['devices'] if d['id']==wid)-.42)<.001 and abs(next(d['mix']['volume'] for d in mac.state()['devices'] if d['id']==wid)-.42)<.001,'volume acknowledgement')
        assert win.call('control',key='deafen',value=1)['ok']
        wait(lambda:mac.state()['settings']['deafen']==1,'deafen sync')
        assert win.call('control',key='deafen',value=0)['ok']
        wait(lambda:mac.state()['diagnostics'].get('microphone_level',0)>.01 and win.state()['diagnostics'].get('microphone_delivery_level',0)>.01,'microphone signal meters')
        assert win.call('test_tone',device_id=mid)['ok']
        wait(lambda:win.state()['pending_changes']==0,'remote test sound acknowledgement')
        print('PASS remote setup, direction validation, room-wide start, mixer controls, signal meters and remote test-tone command')
        # Exercise all four role combinations while audio is running. IDs, not OS,
        # select the output and microphone owner, including same-platform pairs.
        for output in (wid,mid):
            for source in (wid,mid):
                for role,target in [('output',output),('input',source)]:
                    wait(lambda:win.state()['shared_ready'] and mac.state()['shared_ready'],'ready before handover')
                    result=mac.call('set_role',role=role,device_id=target);assert result['ok'],result
                    field='hub_device_id' if role=='output' else 'mic_device_id'
                    wait(lambda:all(x.state()[field]==target and not x.state()['handover_active'] for x in (win,mac)),'role handover')
                wait(lambda:all(x.state()['diagnostics'].get('synchronized') for x in (win,mac)),'engines resynchronized after handover')
                for x in (win,mac):
                    st=x.state();assert st['hub_device_id']==output and st['mic_device_id']==source
                    assert st['diagnostics'].get('headphones_ready')==(st['device_id']==output)
                    assert st['diagnostics'].get('microphone_ready')==(st['device_id']==source)
                assert mac.call('control',key='mute',value=0)['ok']
                wait(lambda:all(not x.state()['diagnostics'].get('muted',True) for x in (win,mac)),'unmute after handover')
                assert win.call('control',device_id=mid,key='volume',value=.63)['ok']
                wait(lambda:all(abs(next(d['mix']['volume'] for d in x.state()['devices'] if d['id']==mid)-.63)<.001 for x in (win,mac)),'controls follow new coordinator')
        print('PASS all four output/input role assignments, live handover, source routing and settings authority on either participant')
        # Explicit Quit stops the shared session and clears restart intent on both.
        assert mac.call('quit')['ok'];mac.process.wait(timeout=5)
        wait(lambda:not win.state()['running'] and not win.state()['wanted_running'],'Quit acknowledged peer stop')
        for device in (win,mac):assert not json.loads((device.root/'state.json').read_text())['wanted_running']
        assert mac.call('quit')['ok']  # Idempotent Quit when no daemon holds its lock.
        wait(lambda:not win.state()['connected'],'disconnect detected',seconds=20)
        for device in (win,mac):
            assert json.loads((device.root/'output-restore.json').read_text())['entries']==[], 'Disconnect/quit must restore outputs'
            assert json.loads((device.root/'input-restore.json').read_text())['entries']==[], 'Disconnect/quit must restore inputs'
        mac.start()
        wait(lambda:win.state()['connected'] and mac.state()['connected'],'saved room reconnect',seconds=20)
        time.sleep(1.5)
        assert all(not x.state()['running'] and not x.state()['wanted_running'] for x in (win,mac))
        wait(lambda:win.state()['shared_ready'] and mac.state()['shared_ready'],'room synchronized after reopen')
        assert win.call('start')['ok']
        wait(lambda:win.state()['diagnostics'].get('synchronized') and mac.state()['diagnostics'].get('synchronized'),'explicit audio restart',seconds=20)
        assert win.state()['diagnostics']['muted'] and mac.state()['diagnostics']['muted']
        assert win.state()['mute_reason']
        print('PASS acknowledged room-wide Quit, saved credential reconnect without automatic restart, explicit protective mute reason')
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
