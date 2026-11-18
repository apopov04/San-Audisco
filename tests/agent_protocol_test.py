#!/usr/bin/env python3
"""Reject invalid authenticated peer state without poisoning the native agent."""
from agent_test_support import *

def peers(root, legacy=False):
    dirs=[root/'a',root/'b']
    for i,d in enumerate(dirs):
        d.mkdir();(d/'state.json').write_text(json.dumps({'version':3,'device_id':str(i+1)*32}))
    ap,bp,am,bm=port(),port(),port(),port()
    a=Instance(dirs[0],'windows',ap,am,bp,{'BRIDGE_TEST_PROTOCOL_VERSION':'2'} if legacy else {})
    b=Instance(dirs[1],'mac',bp,bm,ap)
    result=a.call('create');assert result['ok']
    assert b.call('join',code=result['state']['invite_code'],address=f'127.0.0.1:{ap}')['ok']
    wait(lambda:a.state()['connected'] and b.state()['connected'],'paired')
    return a,b

with tempfile.TemporaryDirectory(prefix='san-concurrent-') as temp:
    a,b=peers(pathlib.Path(temp))
    try:
        from concurrent.futures import ThreadPoolExecutor
        wait(lambda:a.state()['shared_ready'] and b.state()['shared_ready'],'ready for concurrent controls')
        before=a.state()['settings_revision']
        def controls(instance,key):
            for i in range(20):
                assert instance.call('control',key=key,value=(i+1)/25)['ok']
        with ThreadPoolExecutor(max_workers=2) as pool:
            tasks=[pool.submit(controls,a,'master'),pool.submit(controls,b,'balance')]
            for task in tasks:task.result()
        wait(lambda:all(x.state()['settings_revision']==before+40 for x in (a,b)),
             'every concurrent control receives one revision')
        wait(lambda:a.state()['settings']==b.state()['settings'],'concurrent settings converge')
        state=a.state()
        assert abs(state['settings']['master']-.8)<.001 and abs(state['settings']['balance']-.8)<.001

        # An authenticated duplicate request must not gain a new revision or
        # replace a later value. Use an isolated room so this synthetic sequence
        # cannot interfere with subsequent real UI requests.
        message={'type':'request','sequence':10000,'body':{'command':'control',
                 'authority_epoch':state['authority_epoch'],'key':'master','value':.37}}
        assert b.call('test_peer_message',message=message)['ok']
        wait(lambda:all(x.state()['settings_revision']==before+41 for x in (a,b)),
             'first uniquely sequenced request')
        message['body']['value']=.99
        assert b.call('test_peer_message',message=message)['ok']
        message['sequence']=9999
        assert b.call('test_peer_message',message=message)['ok']
        message['sequence']=10001;message['body']['authority_epoch']=state['authority_epoch']-1
        assert b.call('test_peer_message',message=message)['ok']
        time.sleep(.3)
        for instance in (a,b):
            current=instance.state()
            assert current['settings_revision']==before+41,'Duplicate or stale request gained a revision'
            assert abs(current['settings']['master']-.37)<.001,'Duplicate or stale request changed settings'
        print('PASS concurrent controls converge; duplicate, reordered and stale-authority requests cannot revert settings')
    finally:a.close();b.close()

with tempfile.TemporaryDirectory(prefix='san-protocol-') as temp:
    a,b=peers(pathlib.Path(temp))
    try:
        wait(lambda:a.state()['shared_ready'] and b.state()['shared_ready'],'ready')
        original=b.state()['settings']
        source=a.state()
        base={'type':'snapshot','protocol_version':3,'device':next(d for d in source['devices'] if d['id']==source['device_id']),
              'diagnostics':{},'running':False,'shared':{'schema':3,'epoch':source['authority_epoch'],'revision':source['settings_revision'],
              'slots':sorted(d['id'] for d in source['devices']),'settings':original}}
        mutations=[lambda j:j.update(diagnostics='not an object'),
                   lambda j:j['device'].update(headphones_output={'bad':1}),
                   lambda j:j['device'].update(name=[]),
                   lambda j:j['device'].update(audio_devices='bad'),
                   lambda j:j['diagnostics'].update(playback_ready='true')]
        for key in ('epoch','revision'):
            for bad in (-1,0,1.5,2**64-1):
                mutations.append(lambda j,k=key,v=bad:j['shared'].update({k:v}))
        for mutate in mutations:
            j=json.loads(json.dumps(base));mutate(j)
            assert a.call('test_peer_message',message=j)['ok']
            time.sleep(.1)
            assert b.process.poll() is None,'Malformed peer state crashed agent'
            state=b.state();assert state['settings']==original and not state['running'],'Malformed update changed room state'
        assert b.call('control',key='master',value=.72)['ok']
        wait(lambda:abs(a.state()['settings']['master']-.72)<.001,'valid control after invalid frames')
        print('PASS malformed diagnostics, route metadata and counter ranges rejected without poisoning state')
    finally:a.close();b.close()

with tempfile.TemporaryDirectory(prefix='san-old-peer-') as temp:
    a,b=peers(pathlib.Path(temp),legacy=True)
    try:
        wait(lambda:not b.state()['compatible'] and len(b.state()['devices'])==2,'legacy capability recognized')
        for command in ('start','control'):
            reply=b.call(command,**({'key':'mute','value':0} if command=='control' else {}))
            assert not reply['ok'],reply
        time.sleep(.3)
        assert not a.state()['running'] and not b.state()['running'],'New follower started incompatible coordinator'
        assert a.state()['paired'] and b.state()['paired'],'Capability mismatch discarded pairing'
        print('PASS incompatible peer retains pairing but cannot start engines or accept controls')
    finally:a.close();b.close()

# A replacement UI must never silently reuse an older local coordinator.
import threading
with tempfile.TemporaryDirectory(prefix='san-local-version-') as temp:
    root=pathlib.Path(temp)
    with socket.socket() as server:
        server.bind(('127.0.0.1',0));server.listen(1)
        endpoint={'port':server.getsockname()[1],'token':'a'*64,'version':2}
        (root/'endpoint.json').write_text(json.dumps(endpoint))
        def old_service():
            connection,_=server.accept()
            with connection:
                connection.recv(8192)
                connection.sendall(b'{"ok":true,"state":{"running":true}}\n')
        worker=threading.Thread(target=old_service,daemon=True);worker.start()
        result=subprocess.run([str(agent),'ensure','--data-dir',str(root)],capture_output=True,text=True,timeout=8)
        worker.join(timeout=2)
        reply=json.loads(result.stdout)
        assert not reply['ok'] and 'older local' in reply['error'],reply
        assert not (root/'state.json').exists(),'ensure launched a competing coordinator'
        print('PASS older local agent is rejected explicitly without replacement or audio interruption')

for bad in (-1,1.5,2**64-1):
    with tempfile.TemporaryDirectory(prefix='san-bad-save-') as temp:
        root=pathlib.Path(temp);state={'version':3,'device_id':'a'*32,'authority_epoch':bad,'settings_revision':1}
        original=json.dumps(state).encode();(root/'state.json').write_bytes(original)
        result=subprocess.run([str(agent),'serve','--data-dir',str(root)],capture_output=True,text=True,timeout=5)
        assert result.returncode!=0
        assert (root/'state.json').read_bytes()==original,'Invalid saved counter was silently rewritten'
print('PASS invalid saved counters fail before committing any migration or state rewrite')
