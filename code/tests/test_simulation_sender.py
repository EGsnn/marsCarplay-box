import os
from pathlib import Path
import socket
import struct
import sys
import threading
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import simulate_phone as sim
from cryptography.exceptions import InvalidTag


class SenderTests(unittest.TestCase):
    def test_link_local_udp_retains_interface_scope(self):
        interfaces = socket.if_nameindex()
        loopback = next(((index, name) for index, name in interfaces
                         if name in ('lo', 'lo0')), None)
        if loopback is None: self.skipTest('No loopback interface')
        index, name = loopback
        address = sim.udp_address('fe80::2%' + name, 12345)
        self.assertEqual(address, ('fe80::2', 12345, 0, index))

    def test_tlv_split_and_truncation(self):
        value=os.urandom(600)
        self.assertEqual(sim.untlv(sim.tlv(t3=value,t6=b'\2')),{3:value,6:b'\2'})
        with self.assertRaises(ValueError):sim.untlv(b'\3\4a')

    def test_fragmented_encrypted_message_and_timeout(self):
        a,b=socket.socketpair()
        try:
            reader=sim.Channel(a);key=os.urandom(32);reader.keys(key,key)
            plain=b'HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc'
            header=struct.pack('<H',len(plain))
            packet=header+sim.ChaCha20Poly1305(key).encrypt(b'\0'*12,plain,header)
            a.settimeout(.01);b.sendall(packet[:5])
            with self.assertRaises(socket.timeout):reader.message()
            b.sendall(packet[5:]);line,headers,body=reader.message()
            self.assertEqual(body,b'abc');self.assertEqual(reader.rx_count,1)
        finally:a.close();b.close()

    def test_multiframe_message_and_bad_tag(self):
        a,b=socket.socketpair()
        try:
            key=os.urandom(32);writer=sim.Channel(a);reader=sim.Channel(b)
            writer.keys(key,key);reader.keys(key,key)
            plain=b'RTSP/1.0 200 OK\r\nContent-Length: 9000\r\n\r\n'+b'x'*9000
            thread=threading.Thread(target=writer.send,args=(plain,));thread.start()
            self.assertEqual(reader.message()[2],b'x'*9000);thread.join()
            self.assertEqual(reader.rx_count,3)
            packet=struct.pack('<H',1)+b'\0'*17;a.sendall(packet)
            with self.assertRaises(InvalidTag):reader.message()
        finally:a.close();b.close()


if __name__=='__main__':unittest.main()
