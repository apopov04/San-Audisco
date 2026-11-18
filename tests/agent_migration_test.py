#!/usr/bin/env python3
"""Upgrade real paired agents from v2 OS-indexed caches to v3 device identities.
Uses isolated synthetic agents and real protected credentials scoped to this test.
"""
from agent_test_support import *


def shutdown(instance):
    reply=instance.call('quit');assert reply['ok'],reply
    instance.process.wait(timeout=5)


def legacy_settings(**changes):
    result=dict(primary=1,mic_source=0,mute=0,deafen=0,balance=.2,master=.9,
                win_volume=.35,mac_volume=.8,win_mute=0,mac_mute=1,
                win_deafen=0,mac_deafen=0,win_silence=1,mac_silence=0)
    result.update(changes)
    return result


def downgrade(instance, settings):
    state=json.loads((instance.root/'state.json').read_text())
    # Model the old schema exactly: neither new coordination state nor automatic
    # name metadata existed in a v2 save. The protected room secret is untouched.
    for key in ('authority_epoch','settings_revision','transfer','recovery_required',
                'migration_pending','migration_source','name_automatic','kind_override'):
        state.pop(key,None)
    state['version']=2
    state['settings']=settings
    state['wanted_running']=False
    (instance.root/'state.json').write_text(json.dumps(state,indent=2)+'\n')
    return (instance.root/'state.json').read_bytes()


with tempfile.TemporaryDirectory(prefix='san-audisco-migration-') as temp:
    root=pathlib.Path(temp);instances=[]
    try:
        winroot=root/'windows';macroot=root/'mac';winroot.mkdir();macroot.mkdir()
        # Windows sorts after Mac: old OS index 0 becomes canonical slot 1.
        for directory,identity,name in [(winroot,'e'*32,'My Gaming Tower'),(macroot,'a'*32,'My Laptop')]:
            (directory/'state.json').write_text(json.dumps({'version':3,'device_id':identity,'name':name,'name_automatic':False}))
        wp,mp,wm,mm=port(),port(),port(),port()
        win=Instance(winroot,'windows',wp,wm,mp);instances.append(win)
        mac=Instance(macroot,'mac',mp,mm,wp);instances.append(mac)
        created=win.call('create');assert created['ok'],created
        joined=mac.call('join',code=created['state']['invite_code'],address=f'127.0.0.1:{wp}');assert joined['ok'],joined
        wait(lambda:all(x.state()['connected'] and x.state()['shared_ready'] for x in instances),'initial paired state')
        room=win.state()['room_id'];wid=win.state()['device_id'];mid=mac.state()['device_id']
        shutdown(win);shutdown(mac)
        windows_v2=downgrade(win,legacy_settings())
        mac_v2=downgrade(mac,legacy_settings(primary=0,mic_source=1,win_volume=.05,mac_volume=.1,balance=-.75,master=.2))
        # A transient protected-store refusal must not commit a migration or erase
        # pairing. Check the bytes, then retry normally using the original secret.
        failed=subprocess.run([str(agent),'serve',*win.args],capture_output=True,text=True,timeout=10,
                              env={**os.environ,'BRIDGE_FAKE_METRICS':'0','BRIDGE_TEST_CREDENTIAL_FAILURE':'1'})
        assert failed.returncode!=0,'Injected credential failure did not abort startup'
        assert (win.root/'state.json').read_bytes()==windows_v2,'Credential failure altered original v2 state'
        assert room in json.loads((win.root/'state.json').read_text())['room']['id'],'Credential failure erased remembered room'
        print('PASS credential failure preserves original v2 bytes and pairing for a retry',flush=True)
        # Start the old follower first to expose any accidental follower election.
        mac.start();win.start()
        wait(lambda:all(x.state()['connected'] and x.state()['shared_ready'] for x in instances),'v2 migration consensus',seconds=20)
        expected=dict(primary=0,mic_source=1,mute=1,deafen=0,balance=-.2,master=.9,
                      slot0_volume=.8,slot1_volume=.35,slot0_mute=1,slot1_mute=0,
                      slot0_deafen=0,slot1_deafen=0,slot0_silence=0,slot1_silence=1)
        def check_state():
            for x in instances:
                state=x.state()
                assert state['room_id']==room and state['paired'],'Migration lost room identity'
                assert state['hub_device_id']==mid and state['mic_device_id']==wid,'Roles followed OS index rather than stable device ID'
                assert all(abs(state['settings'].get(key,99)-value)<.0001 for key,value in expected.items()),(x.platform,state['settings'])
                assert {d['id']:d['slot'] for d in state['devices']}=={mid:0,wid:1},'Canonical slots differ between peers'
                assert not state['running'],'Migration unexpectedly started audio'
                saved=json.loads((x.root/'state.json').read_text())
                assert saved['version']==3 and not saved.get('migration_pending',False),'Migration not durably completed'
                assert saved['name_automatic'] is False,'Custom name changed to automatic mode'
            assert win.state()['name']=='My Gaming Tower' and mac.state()['name']=='My Laptop'
        check_state()
        assert (win.root/'state-v2.backup.json').exists() and (mac.root/'state-v2.backup.json').exists()
        assert json.loads((win.root/'state-v2.backup.json').read_text())['settings']==json.loads(windows_v2)['settings']
        assert json.loads((mac.root/'state-v2.backup.json').read_text())['settings']==json.loads(mac_v2)['settings']
        print('PASS reversed ID slots, role ownership, volumes, balance, per-device controls and custom names migrate from legacy Windows authority',flush=True)
        for iteration in range(2):
            shutdown(mac);shutdown(win)
            win.start();mac.start()
            wait(lambda:all(x.state()['connected'] and x.state()['shared_ready'] for x in instances),'idempotent restart',seconds=20)
            check_state()
        print('PASS repeated restarts neither rerun OS-slot conversion nor revive the divergent legacy follower cache',flush=True)
        # The migrated Output Hub is Mac, so its accepted controls must now own
        # both replicas even though Windows supplied the one-time migration seed.
        changed=win.call('control',device_id=wid,key='volume',value=.44);assert changed['ok'],changed
        wait(lambda:all(abs(next(d['mix']['volume'] for d in x.state()['devices'] if d['id']==wid)-.44)<.0001 for x in instances),'new Output Hub authority')
        assert all(x.state()['hub_device_id']==mid for x in instances)
        print('PASS legacy migration authority ends after upgrade; selected Mac Output Hub coordinates changes',flush=True)
        # Restore an older Output Hub cache while the follower retains genuine
        # later snapshots from that same authority. Reconciliation must recover
        # the latest accepted state, rather than hang or overwrite it with backup.
        older_owner=json.loads((mac.root/'state.json').read_text())
        for volume in (.45,.46,.47,.48):
            result=win.call('control',device_id=wid,key='volume',value=volume);assert result['ok'],result
            wait(lambda:all(abs(next(d['mix']['volume'] for d in x.state()['devices'] if d['id']==wid)-volume)<.0001 for x in instances),'new authoritative cache revision')
        shutdown(mac);shutdown(win)
        newer_follower=json.loads((win.root/'state.json').read_text())
        assert newer_follower['settings_revision']>older_owner['settings_revision']+1
        older_owner['wanted_running']=True;newer_follower['wanted_running']=True
        (mac.root/'state.json').write_text(json.dumps(older_owner))
        (win.root/'state.json').write_text(json.dumps(newer_follower))
        mac.start();win.start()
        wait(lambda:all(x.state()['shared_ready'] and x.state()['recovery_required'] and not x.state()['running'] for x in instances),'stale authority cache recovery',seconds=20)
        for x in instances:
            state=x.state()
            assert not state['wanted_running'],'Stale cache recovery kept automatic audio intent'
            assert state['settings']['mute']==1 and state['hub_device_id']==mid
            assert abs(next(d['mix']['volume'] for d in state['devices'] if d['id']==wid)-.48)<.0001,'Last accepted follower cache was discarded'
        assert win.state()['settings']==mac.state()['settings']
        assert win.state()['settings_revision']==mac.state()['settings_revision']
        started=win.call('start');assert started['ok'],started
        wait(lambda:all(x.state()['diagnostics'].get('synchronized') and not x.state()['recovery_required'] for x in instances),'explicit start after restored older authority cache',seconds=20)
        assert all(x.state()['diagnostics']['muted'] for x in instances)
        print('PASS older Output Hub cache recovers latest follower snapshot with explicit muted restart and no authority change',flush=True)
    except Exception:
        for x in instances:
            try:
                state=x.state();print(x.platform,{k:state.get(k) for k in ('status','shared_ready','hub_device_id','mic_device_id','settings','authority_epoch','settings_revision')},file=sys.stderr)
            except Exception:
                print(x.platform,(x.root/'server.log').read_text()[-3000:],file=sys.stderr)
        raise
    finally:
        for x in instances:x.close()
