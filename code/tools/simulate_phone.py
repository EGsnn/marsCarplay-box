#!/usr/bin/env python3
"""Owned-device test sender: SRP pairing, Pair-Verify, encrypted RTSP/events/video.

This is a Mac test peer, not an iPhone emulator. It does not test iAP2/MFi or
Apple's negotiation policy. An explicit local H.264 sample is required; no
private sample or long-term test key is bundled or saved. Normal CarPlay still
runs directly on BOX. Protocol constants follow the pinned receiver sources.
"""
import argparse
import hashlib
import http.client
import json
import os
from pathlib import Path
import plistlib
import re
import socket
import struct
import threading
import time
import uuid

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ed25519, x25519
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from cryptography.hazmat.primitives.kdf.hkdf import HKDF

ROOT = Path(__file__).resolve().parents[2]


def hkdf(secret, salt, info):
    return HKDF(algorithm=hashes.SHA512(), length=32, salt=salt.encode(),
                info=info.encode()).derive(secret)


def sha(*parts):
    return hashlib.sha512(b''.join(parts)).digest()


def tlv(**items):
    out = bytearray()
    for name, value in items.items():
        kind = int(name.removeprefix('t'))
        for at in range(0, len(value), 255):
            part = value[at:at+255]
            out.extend(bytes([kind, len(part)]) + part)
    return bytes(out)


def untlv(data):
    out = {}; at = 0
    while at < len(data):
        if at+2 > len(data) or at+2+data[at+1] > len(data):
            raise ValueError('Truncated TLV')
        kind, size = data[at:at+2]; at += 2
        out[kind] = out.get(kind, b'') + data[at:at+size]; at += size
    if 7 in out:
        raise ValueError('Pairing rejected with error ' + str(out[7][0]))
    return out


def labeled_nonce(label):
    return b'\0'*4 + label.encode()


def public(key):
    return key.public_key().public_bytes(serialization.Encoding.Raw,
                                         serialization.PublicFormat.Raw)


class Channel:
    def __init__(self, sock):
        self.sock = sock; self.tx = self.rx = None
        self.tx_count = self.rx_count = 0
        self.wire = bytearray(); self.plain = bytearray()

    def keys(self, send, receive):
        self.tx = ChaCha20Poly1305(send); self.rx = ChaCha20Poly1305(receive)

    def send(self, data):
        if self.tx is None:
            self.sock.sendall(data); return
        for at in range(0, len(data), 4096):
            part = data[at:at+4096]; header = struct.pack('<H', len(part))
            nonce = b'\0'*4 + struct.pack('<Q', self.tx_count)
            self.sock.sendall(header + self.tx.encrypt(nonce, part, header))
            self.tx_count += 1

    def message(self):
        while True:
            pos = self.plain.find(b'\r\n\r\n')
            if pos >= 0:
                lines = bytes(self.plain[:pos]).decode('ascii').split('\r\n')
                headers = dict(line.lower().split(':', 1) for line in lines[1:])
                headers = {k: v.strip() for k, v in headers.items()}
                size = int(headers.get('content-length', '0'))
                if size > 65536: raise ValueError('Oversize control body')
                if len(self.plain) >= pos+4+size:
                    body = bytes(self.plain[pos+4:pos+4+size])
                    del self.plain[:pos+4+size]
                    return lines[0], headers, body
            if self.rx is not None and len(self.wire) >= 2:
                size = struct.unpack('<H', self.wire[:2])[0]
                if size > 4096: raise ValueError('Oversize AEAD frame')
                if len(self.wire) >= size+18:
                    nonce = b'\0'*4 + struct.pack('<Q', self.rx_count)
                    self.plain.extend(self.rx.decrypt(nonce, bytes(self.wire[2:size+18]),
                                                     bytes(self.wire[:2])))
                    del self.wire[:size+18]; self.rx_count += 1
                    continue
            data = self.sock.recv(16384)
            if not data: raise EOFError('Peer closed channel')
            (self.wire if self.rx is not None else self.plain).extend(data)


class Sender:
    def __init__(self, host, port, pin, report):
        self.host, self.port, self.pin = host, port, pin
        self.channel = Channel(self.connect(port)); self.cseq = 0
        self.report = report; self.keyframe = threading.Event()
        self.stop = threading.Event(); self.event = None
        self.identity = ('atk-simulation-' + str(uuid.uuid4())).encode()
        self.signer = ed25519.Ed25519PrivateKey.generate()

    def connect(self, port):
        sock = socket.create_connection((self.host, port), timeout=15)
        sock.settimeout(15); return sock

    def request(self, method, path, body=b'', content_type='application/octet-stream'):
        self.cseq += 1
        packet = (f'{method} {path} RTSP/1.0\r\nCSeq: {self.cseq}\r\n'
                  f'Content-Type: {content_type}\r\nContent-Length: {len(body)}\r\n\r\n').encode()+body
        self.channel.send(packet)
        status, headers, data = self.channel.message()
        code = int(status.split()[1])
        self.report['requests'].append(dict(method=method, path=path, code=code))
        if code != 200: raise RuntimeError(f'{method} {path}: {status}')
        return data

    def plist(self, method, path, value):
        data = self.request(method, path, plistlib.dumps(value, fmt=plistlib.FMT_BINARY),
                            'application/x-apple-binary-plist')
        return plistlib.loads(data) if data else {}

    def pair(self):
        source = (ROOT/'code/firmware/components/airplay_pairing/airplay_srp6a.c').read_text()
        block = re.search(r'SRP_N_HEX\s*=\s*(.*?);', source, re.S)[1]
        n = int(''.join(re.findall(r'"([0-9A-F]+)"', block)), 16)
        pad = lambda v: v.to_bytes(384, 'big')
        response = untlv(self.request('POST', '/pair-setup', tlv(t6=b'\1', t0=b'\0')))
        assert response[6] == b'\2'
        salt, B = response[2], int.from_bytes(response[3], 'big')
        if not B % n: raise ValueError('Invalid SRP public B')
        private = int.from_bytes(os.urandom(32), 'big'); A = pow(5, private, n)
        x = int.from_bytes(sha(salt, sha(b'Pair-Setup:', self.pin.encode())), 'big')
        k = int.from_bytes(sha(pad(n), pad(5)), 'big')
        u = int.from_bytes(sha(pad(A), pad(B)), 'big')
        shared = pow((B-k*pow(5, x, n)) % n, private+u*x, n)
        secret = sha(shared.to_bytes(max(1, (shared.bit_length()+7)//8), 'big'))
        xor = bytes(a^b for a, b in zip(sha(pad(n)), sha(b'\5')))
        proof = sha(xor, sha(b'Pair-Setup'), salt, pad(A), pad(B), secret)
        response = untlv(self.request('POST', '/pair-setup', tlv(t6=b'\3', t3=pad(A), t4=proof)))
        assert response[6] == b'\4' and response[4] == sha(pad(A), proof, secret)
        encrypt = hkdf(secret, 'Pair-Setup-Encrypt-Salt', 'Pair-Setup-Encrypt-Info')
        signing = hkdf(secret, 'Pair-Setup-Controller-Sign-Salt', 'Pair-Setup-Controller-Sign-Info')
        pk = public(self.signer)
        sub = tlv(t1=self.identity, t3=pk, t10=self.signer.sign(signing+self.identity+pk))
        response = untlv(self.request('POST', '/pair-setup', tlv(t6=b'\5', t5=
            ChaCha20Poly1305(encrypt).encrypt(labeled_nonce('PS-Msg05'), sub, None))))
        assert response[6] == b'\6'
        peer = untlv(ChaCha20Poly1305(encrypt).decrypt(labeled_nonce('PS-Msg06'), response[5], None))
        signing = hkdf(secret, 'Pair-Setup-Accessory-Sign-Salt', 'Pair-Setup-Accessory-Sign-Info')
        ed25519.Ed25519PublicKey.from_public_bytes(peer[3]).verify(peer[10], signing+peer[1]+peer[3])
        ephemeral = x25519.X25519PrivateKey.generate(); epk = public(ephemeral)
        response = untlv(self.request('POST', '/pair-verify', tlv(t6=b'\1', t3=epk)))
        self.shared = ephemeral.exchange(x25519.X25519PublicKey.from_public_bytes(response[3]))
        encrypt = hkdf(self.shared, 'Pair-Verify-Encrypt-Salt', 'Pair-Verify-Encrypt-Info')
        verified = untlv(ChaCha20Poly1305(encrypt).decrypt(labeled_nonce('PV-Msg02'), response[5], None))
        assert verified[1] == peer[1]
        ed25519.Ed25519PublicKey.from_public_bytes(peer[3]).verify(verified[10], response[3]+peer[1]+epk)
        sub = tlv(t1=self.identity, t10=self.signer.sign(epk+self.identity+response[3]))
        response = untlv(self.request('POST', '/pair-verify', tlv(t6=b'\3', t5=
            ChaCha20Poly1305(encrypt).encrypt(labeled_nonce('PV-Msg03'), sub, None))))
        assert response[6] == b'\4'
        self.channel.keys(hkdf(self.shared, 'Control-Salt', 'Control-Write-Encryption-Key'),
                          hkdf(self.shared, 'Control-Salt', 'Control-Read-Encryption-Key'))
        self.report['pairing_verified'] = True

    def events(self):
        while not self.stop.is_set():
            try:
                status, headers, body = self.event.message()
                value = plistlib.loads(body) if body else {}
                kind = value.get('type', 'unknown')
                self.report['event_types'].append(kind)
                if kind == 'forceKeyFrame': self.keyframe.set()
                payload = plistlib.dumps({'status': 0}, fmt=plistlib.FMT_BINARY)
                self.event.send((f'HTTP/1.1 200 OK\r\nCSeq: {headers.get("cseq", "0")}\r\n'
                                 f'Content-Length: {len(payload)}\r\n\r\n').encode()+payload)
            except socket.timeout: continue
            except (EOFError, OSError, ValueError) as error:
                if not self.stop.is_set(): self.report['event_error'] = str(error)
                return

    def setup(self):
        initial = self.plist('SETUP', '/stream', {'timingPort': 0, 'name': 'ATK simulation'})
        self.event = Channel(self.connect(initial['eventPort']))
        self.event.sock.settimeout(1)
        self.event.keys(hkdf(self.shared, 'Events-Salt', 'Events-Read-Encryption-Key'),
                        hkdf(self.shared, 'Events-Salt', 'Events-Write-Encryption-Key'))
        self.thread = threading.Thread(target=self.events, daemon=True); self.thread.start()
        self.keepalive = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
        self.keepalive.connect(udp_address(self.host, initial['keepAlivePort']))
        self.keepalive.send(b'\0')
        self.request('RECORD', '/stream')
        self.plist('POST', '/command', {'type': 'modesChanged', 'params':
                   {'resources': [{'resourceID': 1, 'entity': 1}], 'appStates': []}})
        connection = int.from_bytes(os.urandom(6), 'big')
        video = self.plist('SETUP', '/stream', {'streams':
                           [{'type': 110, 'streamConnectionID': connection}]})
        self.video = self.connect(video['streams'][0]['dataPort'])
        self.video.settimeout(10)
        self.video_cipher = ChaCha20Poly1305(hkdf(self.shared, 'DataStream-Salt'+str(connection),
                                                'DataStream-Output-Encryption-Key'))
        self.video_counter = 0
        self.report['setup'] = {'event': True, 'keepalive': True, 'video': True}

    def send_video(self, opcode, body):
        header = bytearray(128)
        struct.pack_into('<I', header, 0, len(body)+(16 if opcode == 0 else 0)); header[4] = opcode
        struct.pack_into('<Q', header, 8, int(time.monotonic()*(1<<32)))
        if opcode == 0:
            nonce = b'\0'*4+struct.pack('<Q', self.video_counter)
            body = self.video_cipher.encrypt(nonce, body, bytes(header)); self.video_counter += 1
        self.video.sendall(bytes(header)+body)

    def close(self):
        self.stop.set()
        try: self.request('TEARDOWN', '/stream')
        except (OSError, ValueError, EOFError, RuntimeError): pass
        for name in ('event', 'channel', 'video', 'keepalive'):
            value = getattr(self, name, None)
            sock = value.sock if isinstance(value, Channel) else value
            if sock is not None:
                try: sock.shutdown(socket.SHUT_RDWR)
                except OSError: pass
                sock.close()


def sample(path):
    data = path.read_bytes()
    nals = [n for n in re.split(b'\x00\x00(?:\x00)?\x01', data) if n]
    sps = next(n for n in nals if n[0]&31 == 7); pps = next(n for n in nals if n[0]&31 == 8)
    frames = [n for n in nals if n[0]&31 in (1, 5)]
    if not frames or frames[0][0]&31 != 5: raise ValueError('Sample must start with IDR')
    # The validated CarPlay sample has exactly one slice/NAL per frame.
    for n in frames:
        bits = ''.join(f'{b:08b}' for b in n[1:3])
        if not bits.startswith('1'): raise ValueError('Only first_mb_in_slice=0 supported')
    avcc = bytes([1, sps[1], sps[2], sps[3], 255, 225])+struct.pack('>H', len(sps))+sps
    avcc += b'\1'+struct.pack('>H', len(pps))+pps
    return data, avcc, frames


def udp_address(host, port):
    # Darwin's raw socket.connect does not resolve a %interface suffix for UDP.
    # getaddrinfo supplies the scope_id in the fourth sockaddr tuple member.
    return socket.getaddrinfo(host, port, socket.AF_INET6, socket.SOCK_DGRAM)[0][4]


def read_status(host):
    connection = http.client.HTTPConnection(host, 80, timeout=5)
    try:
        connection.request('GET', '/status')
        response = connection.getresponse()
        if response.status != 200: raise RuntimeError('Diagnostic HTTP ' + str(response.status))
        return json.loads(response.read(16384))
    finally: connection.close()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', required=True, help='USB IPv6 address including %%interface')
    p.add_argument('--port', type=int, default=5000); p.add_argument('--pin', default='3939')
    p.add_argument('--sample', type=Path, required=True); p.add_argument('--fps', type=float, default=30)
    p.add_argument('--seconds', type=float, default=30); p.add_argument('--output', type=Path, required=True)
    p.add_argument('--drain-seconds', type=float, default=3,
                   help='Keep the session alive after sending so the decoder can finish (0..10)')
    p.add_argument('--repeat-config-frames', type=int, default=0,
                   help='Repeat identical SPS/PPS every N frames to test receiver deduplication (0=off)')
    a = p.parse_args()
    if not 0.1 <= a.fps <= 30 or not 5 <= a.seconds <= 120: p.error('fps 0.1..30, seconds 5..120')
    if not 0 <= a.drain_seconds <= 10: p.error('drain-seconds 0..10')
    if not 0 <= a.repeat_config_frames <= 300: p.error('repeat-config-frames 0..300')
    a.output.mkdir(parents=True, exist_ok=True)
    data, config, frames = sample(a.sample)
    report = dict(test_peer='Mac simulator, not iPhone/iAP2/MFi', fps_requested=a.fps,
                  seconds_requested=a.seconds, sample_sha256=hashlib.sha256(data).hexdigest(),
                  requests=[], event_types=[], sent_frames=0, sent_configs=0,
                  repeat_config_frames=a.repeat_config_frames)
    sender = None; records = []; collector_stop = threading.Event(); collector = None
    try:
        sender = Sender(a.host, a.port, a.pin, report); sender.pair(); sender.setup()
        sender.send_video(1, config)
        report['sent_configs'] += 1
        records.append(dict(elapsed_s=0, status=read_status(a.host), phase='before_send'))
        start = time.monotonic(); due = start; cursor = 0
        def collect():
            while not collector_stop.is_set():
                try:
                    sender.keepalive.send(b'\0')
                    status = read_status(a.host)
                    elapsed = time.monotonic()-start
                    records.append(dict(elapsed_s=elapsed, status=status, phase='periodic'))
                    print(json.dumps(dict(seconds=round(elapsed, 1), sent=report['sent_frames'],
                                          display=status.get('local_display', {}))), flush=True)
                except (OSError, ValueError, RuntimeError) as error:
                    report.setdefault('sampling_errors', []).append(str(error))
                collector_stop.wait(1)
        # HTTP on the S3 can take hundreds of ms. It must not stall video pacing.
        collector = threading.Thread(target=collect, daemon=True); collector.start()
        while time.monotonic()-start < a.seconds:
            now = time.monotonic()
            if sender.keyframe.is_set(): sender.keyframe.clear(); cursor = 0
            if now >= due:
                if (a.repeat_config_frames and report['sent_frames'] and
                        report['sent_frames'] % a.repeat_config_frames == 0):
                    sender.send_video(1, config); report['sent_configs'] += 1
                nal = frames[cursor]; sender.send_video(0, struct.pack('>I', len(nal))+nal)
                cursor = (cursor+1) % len(frames); report['sent_frames'] += 1
                due += 1/a.fps
                if due < now-1/a.fps: due = now+1/a.fps
            time.sleep(max(0, min(0.01, due-time.monotonic())))
        report['elapsed_seconds'] = time.monotonic()-start
        report['sent_fps'] = report['sent_frames']/report['elapsed_seconds']
        report['drain_seconds'] = a.drain_seconds
        collector_stop.wait(a.drain_seconds)
        collector_stop.set(); collector.join(timeout=6)
        if collector.is_alive(): raise RuntimeError('Diagnostic collector did not stop')
        records.append(dict(elapsed_s=time.monotonic()-start, status=read_status(a.host),
                            phase='after_drain'))
    except Exception as error:
        report['error'] = f'{type(error).__name__}: {error}'
        raise
    finally:
        collector_stop.set()
        if collector is not None: collector.join(timeout=6)
        if sender is not None: sender.close()
        (a.output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
        (a.output/'status.json').write_text(json.dumps(records, indent=2)+'\n')


if __name__ == '__main__':
    main()
