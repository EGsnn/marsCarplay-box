#!/usr/bin/env python3
"""Loopback test of the independent sender against production native C code.

Mocks: TCP lifecycle adapter, timing, heap and LCD. Real: SRP, Ed25519, X25519,
RTSP session, video AEAD, Annex-B conversion, H.264 decode and RGB conversion.
No iPhone/iAP2/MFi, FreeRTOS, BOX throughput or physical LCD acceptance claim.
"""
import argparse
import ctypes as ct
import json
from pathlib import Path
import socket
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import simulate_phone as sender


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sample',required=True,type=Path)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    out=root/'build/simulation-host/results';out.mkdir(parents=True,exist_ok=True)
    lib=ct.CDLL(str(root/'build/simulation-host/receiver.dylib'))
    lib.fixture_init.argtypes=[ct.c_uint]*3
    lib.fixture_handle.argtypes=[ct.c_char_p,ct.c_size_t,ct.c_void_p,ct.c_size_t,ct.POINTER(ct.c_int)]
    lib.fixture_video.argtypes=[ct.c_char_p,ct.c_char_p,ct.c_size_t]
    lock=threading.Lock();stop=threading.Event();failures=[];channels=[]
    def listener():
        s=socket.socket(socket.AF_INET6,socket.SOCK_STREAM);s.bind(('::1',0));s.listen(1);return s
    control,event,video=[listener() for _ in range(3)]
    keepalive=socket.socket(socket.AF_INET6,socket.SOCK_DGRAM);keepalive.bind(('::1',0))
    assert lib.fixture_init(event.getsockname()[1],video.getsockname()[1],keepalive.getsockname()[1])==0
    def stats():
        values=(ct.c_uint*3)()
        with lock:lib.fixture_stats(values)
        return dict(received=values[0],decoded=values[1],errors=values[2])
    def control_peer():
        try:
            sock,_=control.accept();channel=sender.Channel(sock);channels.append(sock)
            while not stop.is_set():
                line,headers,body=channel.message()
                raw=(line+'\r\n'+'\r\n'.join(k+': '+v for k,v in headers.items())+'\r\n\r\n').encode()+body
                response=ct.create_string_buffer(4096);enable=ct.c_int()
                with lock:n=lib.fixture_handle(raw,len(raw),response,len(response),ct.byref(enable))
                if n<0:raise RuntimeError('C request handler '+str(n))
                channel.send(response.raw[:n])
                if enable.value:
                    keys=ct.create_string_buffer(64)
                    with lock:assert lib.fixture_control_keys(keys)==0
                    channel.keys(keys.raw[32:64],keys.raw[:32])
        except (EOFError,OSError):pass
        except Exception as e:failures.append('control: '+repr(e))
    def event_peer():
        try:
            sock,_=event.accept();channels.append(sock);channel=sender.Channel(sock)
            keys=ct.create_string_buffer(64)
            with lock:lib.fixture_event_keys(keys)
            channel.keys(keys.raw[32:64],keys.raw[:32]);time.sleep(2)
            body=sender.plistlib.dumps({'type':'forceKeyFrame'},fmt=sender.plistlib.FMT_BINARY)
            channel.send((f'POST /command HTTP/1.1\r\nCSeq: 1\r\nContent-Length: {len(body)}\r\n\r\n').encode()+body)
            response,headers,body=channel.message()
            assert response.startswith('HTTP/1.1 200') and headers['cseq']=='1'
        except (EOFError,OSError):pass
        except Exception as e:failures.append('event: '+repr(e))
    def exact(sock,n):
        data=bytearray()
        while len(data)<n:
            part=sock.recv(n-len(data))
            if not part:raise EOFError()
            data.extend(part)
        return bytes(data)
    def video_peer():
        try:
            sock,_=video.accept();channels.append(sock)
            while not stop.is_set():
                header=exact(sock,128);n=int.from_bytes(header[:4],'little');body=exact(sock,n)
                with lock:result=lib.fixture_video(header,body,n)
                if result:raise RuntimeError('Production screen/decode '+str(result))
        except (EOFError,OSError):pass
        except Exception as e:failures.append('video: '+repr(e))
    class HTTP(ThreadingHTTPServer):address_family=socket.AF_INET6
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            values=stats();payload=json.dumps(dict(mode='host-production-fixture',firmware='host-native-test',
                local_display=values,capture=dict(frames=values['received']))).encode()
            self.send_response(200);self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload)
        def log_message(self,*args):pass
    # Sender's board diagnostic address is fixed to port 80. Host fixture instead
    # injects a local HTTPConnection adapter; all protocol/data ports stay real TCP.
    http=HTTP(('::1',0),Handler);original=sender.http.client.HTTPConnection
    class LocalHTTP(original):
        def __init__(self,host,port=80,**kw):super().__init__('::1',http.server_port,**kw)
    sender.http.client.HTTPConnection=LocalHTTP
    threads=[threading.Thread(target=f,daemon=True) for f in (control_peer,event_peer,video_peer,http.serve_forever)]
    for t in threads:t.start()
    previous=sys.argv
    try:
        sys.argv=['simulate_phone','--host','::1','--port',str(control.getsockname()[1]),
                  '--sample',str(args.sample.resolve()),'--fps','30','--seconds','10','--output',str(out)]
        sender.main()
        result=stats();report=json.loads((out/'report.json').read_text())
        assert report['pairing_verified'] and report['sent_frames']>100
        assert result['received']==report['sent_frames']==result['decoded'] and result['errors']==0,(report,result,failures)
        assert 'forceKeyFrame' in report['event_types'] and not failures,failures
        result.update(scope='native production-code fixture; NOT ESP32-S3 performance',pairing_verified=True,
                      force_keyframe_roundtrip=True,failures=failures)
        (out/'receiver-result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
    finally:
        sys.argv=previous;stop.set();sender.http.client.HTTPConnection=original
        http.shutdown()
        for sock in channels+[control,event,video,keepalive]:
            try:sock.shutdown(socket.SHUT_RDWR)
            except OSError:pass
            sock.close()
        for t in threads:t.join(timeout=2)
        with lock:lib.fixture_destroy()


if __name__=='__main__':main()
