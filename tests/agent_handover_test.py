#!/usr/bin/env python3
"""Real two-agent crash/transfer journal recovery. Test fault hooks are fake-build only."""
from agent_test_support import *

def pair(root, drop=''):
    aroot=root/'a';broot=root/'b';aroot.mkdir();broot.mkdir()
    # Stable ordering exercises coordinator independent of the platform.
    for directory,identity in [(aroot,'1'*32),(broot,'2'*32)]:
        (directory/'state.json').write_text(json.dumps({'version':3,'device_id':identity,'name':'Custom '+directory.name,'name_automatic':False}))
    ap,bp,am,bm=port(),port(),port(),port()
    a=Instance(aroot,'mac',ap,am,bp,{'BRIDGE_TEST_HANDOVER_DROP':drop})
    b=Instance(broot,'mac',bp,bm,ap,{'BRIDGE_TEST_HANDOVER_DROP':drop})
    created=a.call('create');assert created['ok'],created
    assert b.call('join',code=created['state']['invite_code'],address=f'127.0.0.1:{ap}')['ok']
    wait(lambda:all(x.state()['connected'] and x.state()['shared_ready'] for x in (a,b)),'pair shared state')
    return a,b

def phase(x):return json.loads((x.root/'state.json').read_text()).get('transfer',{}).get('phase')

for drop in ['prepare','prepared','commit','committed','finalize']:
    with tempfile.TemporaryDirectory(prefix='san-handover-') as temp:
        instances=[]
        try:
            a,b=pair(pathlib.Path(temp),drop);instances=[a,b]
            assert a.call('start')['ok']
            wait(lambda:all(x.state()['diagnostics'].get('synchronized') for x in instances),'initial audio')
            assert b.call('set_role',role='output',device_id=b.state()['device_id'])['ok']
            expected={'prepare':(a,'prepare'),'prepared':(b,'prepared'),'commit':(a,'commit'),'committed':(b,'committed'),'finalize':(b,'committed')}
            owner,wanted_phase=expected[drop]
            wait(lambda:phase(owner)==wanted_phase,'fault boundary')
            # Old Output Hub has stopped before any transfer can be committed.
            assert not a.state()['diagnostics'].get('headphones_ready',False)
            crashed=a if drop in ('prepare','commit','finalize') else b
            survivor=b if crashed is a else a
            crashed.process.kill();crashed.process.wait(timeout=5)
            wait(lambda:not survivor.state()['connected'],'detect crash',seconds=20)
            crashed.extra_env={};survivor.extra_env={};crashed.start()
            wait(lambda:all(x.state()['connected'] and x.state()['shared_ready'] and not x.state()['handover_active'] for x in instances),'reconcile committed journal',seconds=20)
            assert all(not x.state()['running'] for x in instances),'Uncertain handover resumed without explicit Start'
            assert a.state()['hub_device_id']==b.state()['hub_device_id']
            assert a.call('start')['ok']
            wait(lambda:all(x.state()['diagnostics'].get('synchronized') for x in instances),'explicit recovery startup',seconds=20)
            assert all(x.state()['diagnostics']['muted'] for x in instances)
            assert sum(bool(x.state()['diagnostics'].get('headphones_ready')) for x in instances)==1
            print('PASS crash/reconnect at',drop,'preserves one Output Hub, paused recovery, explicit muted restart')
        except Exception:
            for x in instances:
                try:
                    st=x.state();print(x.root.name,{k:st.get(k) for k in ['status','authority_epoch','settings_revision','shared_ready','hub_device_id','handover_active','recovery_required','running']},'journal',phase(x),file=sys.stderr)
                except Exception:pass
            raise
        finally:
            for x in instances:x.close()
