"""Recover stale cable defaults with the real quit request path; fake OS only."""
import json,os,pathlib,subprocess,sys,tempfile
agent=pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='san-audisco-restore-') as directory:
 root=pathlib.Path(directory)
 for platform in ('mac','windows'):
  prefix='mac' if platform=='mac' else 'win'
  playback='mac-playback' if platform=='mac' else 'win-playback-render'
  mic='mac-mic-sink' if platform=='mac' else 'win-mic-capture'
  state={'wanted_running':True,'routes':{'headphones_output':prefix+'-headphones','physical_microphone':prefix+'-mic','playback_input':prefix+'-playback','microphone_output':prefix+'-mic-sink'}}
  (root/'state.json').write_text(json.dumps(state))
  defaults=root/'fake-defaults.json'
  defaults.write_text(json.dumps({'output':{'0':playback,'1':'manually-selected-speakers'},'input':{'0':mic}}))
  for name in ('input','output'):(root/(name+'-restore.json')).write_text('{"version":1,"entries":[]}')
  def quit():
   return subprocess.run([str(agent),'request','--data-dir',str(root),'--platform',platform],input='{"command":"quit"}\n',capture_output=True,text=True,env={**os.environ,'BRIDGE_FAKE_DEFAULTS_FILE':str(defaults)},timeout=10)
  result=quit();assert result.returncode==0,result.stdout
  recovered=json.loads(defaults.read_text());assert recovered=={'output':{'0':prefix+'-headphones','1':'manually-selected-speakers'},'input':{'0':prefix+'-mic'}},recovered
  assert not json.loads((root/'state.json').read_text())['wanted_running']
  assert all(json.loads((root/(name+'-restore.json')).read_text())['entries']==[] for name in ('input','output'))
  assert quit().returncode==0,'Repeated quit must be safe'
  # No credentials are accessed and no arbitrary replacement is selected when
  # the selected headphones are unavailable. The warning must remain visible without vetoing Quit.
  state['routes']['headphones_output']='disconnected-headphones';(root/'state.json').write_text(json.dumps(state))
  defaults.write_text(json.dumps({'output':{'0':playback},'input':{'0':prefix+'-mic'}}))
  result=quit();assert result.returncode==0 and 'physical Output' in json.loads(result.stdout)['state']['quit_warning'],result.stdout
  assert not json.loads((root/'state.json').read_text())['wanted_running']
  assert json.loads((root/'state.json').read_text())['last_quit_warning']
  assert json.loads(defaults.read_text())['output']['0']==playback
  print('PASS',platform,'legacy cable recovery, physical selections, manual override, idempotent quit, nonblocking unavailable-output warning')
