from pathlib import Path
import hashlib
import json
import os
import secrets
import signal
import statistics
import subprocess
import sys
import tempfile
import time
import traceback
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'outputs/AudioBridge/tests'))
from runtime_test import Peer, free_port, wait_for, metric_check

class NativeMetrics:
    def __init__(self,path):self.log=path
    def metrics(self,sink,after):return Peer.metrics(self,sink,after)

def main():
    paths={'mac_cli':ROOT/'work/build-release13/audiobridge','fake_windows_cli':ROOT/'work/build-release13/runtime_fake','helper':HERE/'cable_helper','production_mac_backend':ROOT/'outputs/AudioBridge/src/audio_mac.cpp'}
    result={'scope':'Native Mac CoreAudio and BlackHole virtual cables, real CLI, encrypted localhost UDP with native macOS runtime_fake acting as Windows role. No physical input/output devices.','hashes':{k:hashlib.sha256(v.read_bytes()).hexdigest() for k,v in paths.items()},'checks':[],'success':False,'started':time.time()}
    peers=[];helper=None;helper_output=None;key=None
    metrics=NativeMetrics(HERE/'native.jsonl')
    for name in ('native.jsonl','win.jsonl','mac.jsonl'):(HERE/name).unlink(missing_ok=True)
    try:
        with tempfile.TemporaryDirectory(prefix='disposable-',dir=HERE) as temporary:
            tmp=Path(temporary);key=tmp/'pair.key';key.write_text(secrets.token_hex(32)+'\n');key.chmod(0o600)
            ports=[free_port(),free_port()]
            while ports[0]==ports[1]:ports[1]=free_port()
            configs=[tmp/'win.conf',tmp/'mac.conf']
            configs[0].write_text(f'device=windows\nkey_file={key}\npeer=127.0.0.1\nport={ports[0]}\npeer_port={ports[1]}\nplayback_input=win-playback\nphysical_microphone=win-mic\nheadphones_output=win-headphones\nmicrophone_output=win-mic-sink\n')
            configs[1].write_text(f'device=mac\nkey_file={key}\npeer=127.0.0.1\nport={ports[1]}\npeer_port={ports[0]}\nplayback_input=BlackHole2ch_UID\nmicrophone_output=BlackHole16ch_UID\n')
            result['configs_redacted']=[p.read_text().replace(str(key),'<disposable key, deleted>') for p in configs]
            helper_output=(HERE/'helper.log').open('w')
            helper=subprocess.Popen([str(paths['helper']),str(metrics.log),'120'],stdout=helper_output,stderr=subprocess.STDOUT)
            wait_for(lambda:metrics.log.exists() and metrics.log.stat().st_size>0,'Native helper did not capture virtual cable',timeout=10)
            peers=[Peer(str(paths['fake_windows_cli']),configs[0],HERE/'win.jsonl'),Peer(str(paths['mac_cli']),configs[1],HERE/'mac.jsonl')]
            win,mac=peers
            wait_for(lambda:all(p.latest().get('connected') and p.latest().get('synchronized') for p in peers),'Native/fake peers did not pair',timeout=12)
            def native(after,**kw):return all(metric_check(metrics,'mac-mic-'+channel,after,**kw) for channel in ('left','right'))
            def audio(sink,after,**kw):return metric_check(win,sink,after,**kw)
            def step(peer,command):
                now=time.time();peer.command(command);return now
            def verify(label,after,*conditions):
                start=time.monotonic()
                wait_for(lambda:helper.poll() is None and all(c() for c in conditions),label,timeout=10)
                snapshot={}
                for device,sink in ((win,'win-headphones'),(win,'win-mic-sink'),(metrics,'mac-mic-left'),(metrics,'mac-mic-right')):
                    rows=device.metrics(sink,after+.35)[-3:]
                    if rows:snapshot[sink]={'rms':statistics.median(r['rms'] for r in rows),'tones':[statistics.median(r['tones'][i] for r in rows) for i in range(4)]}
                result['checks'].append({'name':label,'status':'PASS','time':time.time(),'wait_seconds':time.monotonic()-start,'metrics':snapshot})
                print('PASS',label,flush=True)
            now=time.time()
            verify('Native 730Hz playback plus fake Windows410Hz mixed; both microphone destinations initially silent',now,
                lambda:audio('win-headphones',now,present=(0,1),absent=(2,3)),lambda:audio('win-mic-sink',now,silent=True),lambda:native(now,silent=True))
            mac.command('mic_source 0')
            wait_for(lambda:all(p.latest().get('mic_source')==0 for p in peers),'Windows microphone source selection not synchronized')
            now=step(mac,'mute 0')
            verify('Windows1090Hz voice reaches both apps; native virtual sink stereo excludes all playback',now,
                lambda:audio('win-headphones',now,present=(0,1),absent=(2,3)),lambda:audio('win-mic-sink',now,present=(2,),absent=(0,1,3)),lambda:native(now,present=(2,),absent=(0,1,3)))
            now=step(mac,'mac_mute 1')
            verify('Mac destination mute silences native virtual cable only',now,lambda:native(now,silent=True),lambda:audio('win-mic-sink',now,present=(2,)),lambda:audio('win-headphones',now,present=(0,1)))
            now=step(win,'mac_mute 0\nwin_mute 1')
            verify('Windows destination mute retains voice through native Mac cable',now,lambda:native(now,present=(2,),absent=(0,1,3)),lambda:audio('win-mic-sink',now,silent=True))
            now=step(mac,'win_mute 0\nmac_deafen 1')
            verify('Mac deafen removes native730Hz playback and native microphone destination',now,lambda:audio('win-headphones',now,present=(0,),absent=(1,2,3)),lambda:native(now,silent=True),lambda:audio('win-mic-sink',now,present=(2,)))
            now=step(win,'mac_deafen 0\nwin_deafen 1')
            verify('Windows deafen retains native730Hz playback and native microphone destination',now,lambda:audio('win-headphones',now,present=(1,),absent=(0,2,3)),lambda:native(now,present=(2,),absent=(0,1,3)),lambda:audio('win-mic-sink',now,silent=True))
            now=step(mac,'win_deafen 0\nmac_volume 0.25')
            verify('Native Mac playback volume quarter preserves voice',now,lambda:audio('win-headphones',now,levels={0:(.09,.19),1:(.022,.052)}),lambda:native(now,present=(2,),absent=(0,1,3)))
            now=step(win,'mac_volume 1\nmaster 0.5')
            verify('Master halves both listening sources without changing native microphone',now,lambda:audio('win-headphones',now,levels={0:(.05,.10),1:(.05,.10)}),lambda:native(now,levels={2:(.09,.19)},absent=(0,1,3)))
            now=step(mac,'master 1\nmac_silence 1')
            verify('Mac listening silence removes730Hz but preserves virtual microphone',now,lambda:audio('win-headphones',now,present=(0,),absent=(1,2,3)),lambda:native(now,present=(2,),absent=(0,1,3)))
            now=step(win,'mac_silence 0\nmute 1')
            verify('Global mute silences both microphone destinations while mixed playback remains',now,lambda:native(now,silent=True),lambda:audio('win-mic-sink',now,silent=True),lambda:audio('win-headphones',now,present=(0,1)))
            now=step(mac,'mute 0\ndeafen 1')
            verify('Global deafen silences playback and both microphone destinations',now,lambda:native(now,silent=True),lambda:audio('win-mic-sink',now,silent=True),lambda:audio('win-headphones',now,silent=True))
            now=step(win,'deafen 0')
            verify('Undeafen restores mixed playback and stereo native1090Hz voice without leakage',now,lambda:audio('win-headphones',now,present=(0,1),absent=(2,3)),lambda:audio('win-mic-sink',now,present=(2,),absent=(0,1,3)),lambda:native(now,present=(2,),absent=(0,1,3)))
            result['success']=True
            for p in peers:p.stop();assert p.process.returncode==0,p.errors
    except Exception:
        result['error']=traceback.format_exc();print(result['error'],file=sys.stderr)
    finally:
        for i,p in enumerate(peers):
            p.stop(crash=True);result.setdefault('peer_exits',[]).append(p.process.returncode)
            (HERE/(('win' if i==0 else 'mac')+'-states.json')).write_text(json.dumps(p.states,indent=2))
            result.setdefault('stderr',{})['win' if i==0 else 'mac']=p.errors
        if helper and helper.poll() is None:
            helper.terminate()
            try:helper.wait(timeout=5)
            except subprocess.TimeoutExpired:helper.kill();helper.wait()
        result['helper_exit']=helper.returncode if helper else None
        if helper_output:helper_output.close()
        result['disposable_key_removed']=key is None or not key.exists();result['finished']=time.time()
        if result['helper_exit']!=0:result['success']=False
        (HERE/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    return 0 if result['success'] else 1
if __name__=='__main__':raise SystemExit(main())
