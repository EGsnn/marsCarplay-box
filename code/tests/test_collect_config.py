"""Local HTTP integration check using SPS/PPS from a private, optional sample."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import subprocess
import sys
import threading

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sample',required=True,type=Path)
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'code/tools'))
from inspect_h264 import inspect
config=b''.join(b'\0\0\0\1'+n for n in re.split(b'\x00\x00(?:\x00)?\x01',args.sample.read_bytes())
                if n and n[0]&31 in (7,8))
expected=inspect(config)
assert expected['sps'] and expected['pps']
class Handler(BaseHTTPRequestHandler):
    provide_config=True
    def do_GET(self):
        if self.path=='/video-config.h264':
            if not self.provide_config:self.send_error(404);return
            data=config
        elif self.path=='/status':
            data=json.dumps(dict(mode='atk-box-direct',firmware='HTTP-test-fixture',
                capture=dict(frames=0),local_display=dict(decoded=0,shown=0,
                requested_width=320,requested_height=192,performance={}))).encode()
        else:self.send_error(404);return
        self.send_response(200);self.send_header('Content-Length',str(len(data)))
        self.end_headers();self.wfile.write(data)
    def log_message(self,*args):pass
server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
try:
    for available in (True,False):
        Handler.provide_config=available
        out=root/'build/collect-config-test'/('present' if available else 'missing')
        subprocess.run([sys.executable,str(root/'code/tools/collect_performance.py'),
            '--url',f'http://127.0.0.1:{server.server_port}/status','--seconds','5',
            '--interval','0.25','--output',str(out)],check=True,stdout=subprocess.DEVNULL)
        result=json.loads((out/'summary.json').read_text())
        if available:
            assert result['video_config']==expected
            assert (out/'stream-config-annex-b.h264').read_bytes()==config
        else:assert 'video_config_error' in result and 'video_config' not in result
    print('Actual SPS/PPS export/analysis and missing-config reporting passed; declared size never replaces measured size')
finally:
    server.shutdown();server.server_close();thread.join(timeout=2)
