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
    def __init__(self, root, platform, roomport, media, discovery, extra_env=None):
        self.extra_env=extra_env or {};self.root=root;self.root.mkdir(exist_ok=True);self.platform=platform
        self.args=['--data-dir',str(root),'--engine',str(engine),'--platform',platform,'--bind','127.0.0.1','--room-port',str(roomport),'--media-port',str(media),'--discovery-port',str(discovery)]
        self.log=open(root/'server.log','w');self.start()
    def start(self):
        self.process=subprocess.Popen([str(agent),'serve',*self.args],stdout=self.log,stderr=self.log,env={**os.environ,'BRIDGE_FAKE_METRICS':'0',**self.extra_env})
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
