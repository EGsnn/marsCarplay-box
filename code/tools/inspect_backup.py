#!/usr/bin/env python3
"""Inspect a complete, local ATK flash backup. Never opens or writes a device.

Uses ESP-IDF's partition format and OTA sequence CRC. Refuses mismatched layouts,
ambiguous selections and pending verification; the result is a review plan, not
permission to bypass chip identity, security settings or post-write verification.
"""
import argparse
import binascii
import hashlib
import json
import struct
from pathlib import Path


def partitions(raw):
    entries = []
    digest = hashlib.md5()
    verified = False
    for at in range(0, len(raw), 32):
        entry = raw[at:at + 32]
        if entry == b'\xff' * 32:
            if not verified:
                raise ValueError('Partition MD5 missing')
            return entries
        if entry[:2] == b'\xeb\xeb':
            if entry[16:] != digest.digest():
                raise ValueError('Partition MD5 mismatch')
            verified = True
            continue
        magic, kind, subtype, offset, size, label, flags = struct.unpack('<HBBLL16sL', entry)
        if magic != 0x50aa or verified or not size:
            raise ValueError('Invalid partition entry')
        digest.update(entry)
        entries.append(dict(name=label.split(b'\0', 1)[0].decode('ascii'),
                            type=kind, subtype=subtype, offset=offset, size=size, flags=flags))
    raise ValueError('Partition terminator missing')


def inspect(flash, expected, candidate):
    if len(flash) != 0x1000000 or flash[0] != 0xe9 or candidate[0] != 0xe9:
        raise ValueError('Expected complete 16 MiB ESP32-S3 flash and application images')
    table = partitions(flash[0x8000:0x8c00])
    if table != partitions(expected):
        raise ValueError('Actual partition layout differs from candidate; no install plan')
    for p in table:
        if p['offset'] + p['size'] > len(flash):
            raise ValueError('Partition exceeds backup')
    ota = sorted((p for p in table if p['type'] == 0 and 0x10 <= p['subtype'] <= 0x1f),
                 key=lambda p: p['subtype'])
    if not ota or [p['subtype'] for p in ota] != list(range(0x10, 0x10 + len(ota))):
        raise ValueError('Noncontiguous or missing OTA application slots')
    metadata = next(p for p in table if p['type'] == 1 and p['subtype'] == 0)
    records = []
    for i in range(2):
        at = metadata['offset'] + i * 0x1000
        seq, = struct.unpack_from('<L', flash, at)
        state, crc = struct.unpack_from('<LL', flash, at + 24)
        valid = (seq not in (0, 0xffffffff) and state not in (3, 4) and
                 crc == binascii.crc32(struct.pack('<L', seq), 0xffffffff))
        records.append(dict(index=i, sequence=seq, state=state, valid=valid))
    if any(r['valid'] and r['state'] in (0, 1) for r in records):
        raise ValueError('New/pending OTA metadata; verify rollback behavior manually')
    valid = [r for r in records if r['valid']]
    if valid:
        selected = max(valid, key=lambda r: r['sequence'])
        target = ota[(selected['sequence'] - 1) % len(ota)]
        reason = 'highest valid OTA sequence (no pending verification)'
    elif all(flash[metadata['offset'] + i*0x1000:metadata['offset'] + i*0x1000 + 32]
             == b'\xff' * 32 for i in range(2)):
        factory = next((p for p in table if p['type'] == 0 and p['subtype'] == 0), None)
        target = factory or ota[0]
        reason = 'empty OTA metadata: factory if present, otherwise OTA 0'
    else:
        raise ValueError('Invalid nonempty OTA metadata; no automatic selection')
    if target['flags'] or target['offset'] % 0x1000 or len(candidate) > target['size']:
        raise ValueError('Encrypted/unaligned/undersized target partition')
    if flash[target['offset']] != 0xe9:
        raise ValueError('Selected partition has no readable application header')
    return dict(backup_bytes=len(flash), backup_sha256=hashlib.sha256(flash).hexdigest(),
                partitions=table, otadata=records, selected_partition=target,
                selection_reason=reason, candidate_bytes=len(candidate),
                candidate_sha256=hashlib.sha256(candidate).hexdigest(),
                application_erase_bytes=(len(candidate) + 4095) // 4096 * 4096,
                flashed=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('backup', type=Path)
    parser.add_argument('--expected-partitions', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    plan = inspect(args.backup.read_bytes(), args.expected_partitions.read_bytes(),
                   args.candidate.read_bytes())
    args.output.write_text(json.dumps(plan, indent=2) + '\n')
    print(json.dumps({k: plan[k] for k in ('backup_bytes', 'selected_partition',
                                          'selection_reason', 'candidate_bytes', 'flashed')}))


if __name__ == '__main__':
    main()
