"""Quit/reopen/Quit with missing local or peer output; synthetic OS and audio only."""
from agent_test_support import *
import agent_test_support as support
legacy=pathlib.Path(sys.argv[3]).resolve() if len(sys.argv)>3 else None
with tempfile.TemporaryDirectory(prefix='san-audisco-quit-') as directory:
 root=pathlib.Path(directory);instances=[]
 try:
  pcroot=root/'pc';pcroot.mkdir()
  routes={'playback_input':'win-playback','microphone_output':'win-mic-sink','headphones_output':'disconnected-headphones','physical_microphone':'win-mic'}
  (pcroot/'state.json').write_text(json.dumps({'version':3,'routes':routes,'wanted_running':False}))
  defaults=root/'defaults.json';defaults.write_text(json.dumps({'output':{'0':'win-playback-render'},'input':{'0':'win-mic'}}))
  current=support.agent
  if legacy:support.agent=legacy
  try:pc=Instance(pcroot,'windows',port(),port(),port(),{'BRIDGE_FAKE_DEFAULTS_FILE':str(defaults)});instances.append(pc)
  finally:support.agent=current
  mac=Instance(root/'mac','mac',port(),port(),port());instances.append(mac)
  invitation=mac.call('create')['state'];assert pc.call('join',code=invitation['invite_code'],address=f"127.0.0.1:{invitation['room_port']}")['ok']
  wait(lambda:pc.state()['connected'] and mac.state()['connected'],'paired')
  room=mac.state()['room_id']
  for attempt in range(2):
   wait(lambda:pc.state()['connected'] and mac.state()['connected'] and mac.state()['shared_ready'],'reopened room')
   assert not pc.state()['running'] and not mac.state()['running']
   result=mac.call('quit');assert result['ok'],result
   assert 'physical Output' in result['state']['quit_warning'],result
   assert not result['state']['running'] and not result['state']['wanted_running']
   mac.process.wait(timeout=5)
   assert pc.process.poll() is None,'Quit closed the peer app'
   assert not pc.state()['running'] and not pc.state()['wanted_running']
   assert json.loads(defaults.read_text())['output']['0']=='win-playback-render','Missing headphones caused an arbitrary output choice'
   assert json.loads((mac.root/'state.json').read_text())['last_quit_warning']
   if attempt==0:
    mac.start();wait(lambda:mac.state()['room_id']==room and mac.state()['connected'],'remembered room')
  # The same warning must not block Quit on the device where restoration failed.
  if not legacy:
   result=pc.call('quit');assert result['ok'] and result['state']['quit_warning'],result
   pc.process.wait(timeout=5)
  assert json.loads((pcroot/'state.json').read_text())['routes']==routes
  print('PASS legacy peer compatibility' if legacy else 'PASS updated peer including local missing-output Quit', 'Mac Quit/reopen/Quit while peer output unavailable; stopped intent, preserved pairing/routes, no arbitrary fallback')
 finally:
  for instance in reversed(instances):instance.close()
