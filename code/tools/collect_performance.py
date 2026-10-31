#!/usr/bin/env python3
"""Read optional local diagnostics. Does not carry, decode or display video."""
import argparse
import datetime
import json
from pathlib import Path
import time
import urllib.request
import urllib.parse
from inspect_h264 import inspect as inspect_h264
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parents[2]


def summarize(records):
    duration = 0.0
    totals = dict(received=0, decoded=0, shown=0, gaps=0, errors=0,
                  decoder_resets=0, skipped_waiting_idr=0)
    resets = 0
    active_seconds = 0.0
    active_count = 0
    active_totals = dict.fromkeys(totals, 0)
    for a, b in zip(records, records[1:]):
        elapsed = b['elapsed_s'] - a['elapsed_s']
        old, new = a['status'], b['status']
        da, db = old['local_display'], new['local_display']
        before = dict(received=old['capture']['frames'],
                      **{k: da.get(k, 0) for k in totals if k != 'received'})
        after = dict(received=new['capture']['frames'],
                     **{k: db.get(k, 0) for k in totals if k != 'received'})
        if (elapsed <= 0 or old['firmware'] != new['firmware'] or
            any(after[k] < before[k] for k in totals) or
            db.get('performance', {}).get('sampled_uptime_ms', 0) <
                da.get('performance', {}).get('sampled_uptime_ms', 0)):
            resets += 1
            continue
        duration += elapsed
        delta = {k: after[k] - before[k] for k in totals}
        for k in totals:
            totals[k] += delta[k]
        if delta['received'] > 0:
            active_seconds += elapsed
            active_count += 1
            for k in totals:
                active_totals[k] += delta[k]
    last = records[-1]['status'] if records else {}
    display = last.get('local_display', {})
    return dict(samples=len(records), valid_interval_seconds=round(duration, 3),
                invalid_or_reset_intervals=resets, totals=totals,
                interval_fps={k: round(totals[v] / duration, 3) if duration else None
                              for k, v in [('received', 'received'), ('decoded', 'decoded'),
                                           ('displayed', 'shown')]},
                input_active_intervals=dict(
                    count=active_count, seconds=round(active_seconds, 3),
                    totals=active_totals,
                    fps={k: round(active_totals[v] / active_seconds, 3) if active_seconds else None
                         for k, v in [('received', 'received'), ('decoded', 'decoded'),
                                      ('displayed', 'shown')]},
                    definition='Valid adjacent status intervals with positive received-frame delta; excludes idle and drain-only intervals; not exact gesture duration or maximum throughput.'),
                firmware=last.get('firmware'),
                psram_config_mhz=display.get('psram_config_mhz'),
                last_boot_cumulative_timing=display.get('performance'),
                last_boot_decode_profile=display.get('decode_profile'),
                note='FPS uses counter deltas over observed intervals. Timing averages are boot-cumulative wall time; decode_profile is sampled and includes instrumentation/scheduling overhead; no end-to-end latency is measured.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://192.168.4.1/status')
    parser.add_argument('--seconds', type=float, default=30)
    parser.add_argument('--interval', type=float, default=1)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if not 5 <= args.seconds <= 300 or not 0.25 <= args.interval <= 5:
        parser.error('seconds must be 5..300, interval 0.25..5')
    stamp = datetime.datetime.now(ZoneInfo('Asia/Shanghai')).strftime('%Y%m%d-%H%M%S')
    output = args.output or ROOT / 'build/performance' / stamp
    output.mkdir(parents=True, exist_ok=True)
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    started = time.monotonic()
    records, failures = [], []
    print('Keep BOX USB connected to iPhone. Scroll CarPlay during the collection.')
    try:
        with (output / 'status.jsonl').open('a') as stream:
            while time.monotonic() - started <= args.seconds:
                tick = time.monotonic()
                try:
                    with opener.open(args.url, timeout=3) as response:
                        raw = response.read(16385)
                    if len(raw) > 16384:
                        raise ValueError('Status too large')
                    status = json.loads(raw)
                    if status.get('mode') != 'atk-box-direct':
                        raise ValueError('Not ATK BOX direct-display firmware')
                    record = dict(elapsed_s=time.monotonic() - started, status=status)
                    records.append(record)
                    stream.write(json.dumps(record) + '\n'); stream.flush()
                    d = status['local_display']; p = d.get('performance', {})
                    print(f"{record['elapsed_s']:5.1f}s RX {p.get('rx_fps', '?')} fps "
                          f"decode {p.get('decode_fps', '?')} show {p.get('display_fps', '?')} "
                          f"decode(last) {d.get('decode_ms')}ms gaps {d.get('gaps')} "
                          f"wait-IDR {d.get('waiting_idr')}", flush=True)
                except (OSError, ValueError, KeyError) as error:
                    failures.append(dict(elapsed_s=time.monotonic() - started, error=str(error)))
                    print('Read failed:', error, flush=True)
                time.sleep(max(0, args.interval - (time.monotonic() - tick)))
    except KeyboardInterrupt:
        print('Stopped; preserving collected samples.')
    summary = summarize(records)
    summary['read_failures'] = failures
    # Query after the run so it cannot be mistaken for proof of negotiation
    # before the iPhone has actually sent SPS/PPS. No full video is exported.
    url = urllib.parse.urlsplit(args.url)
    config_url = urllib.parse.urlunsplit((url.scheme, url.netloc, '/video-config.h264', '', ''))
    try:
        with opener.open(config_url, timeout=3) as response:
            data = response.read(4097)
        if not data or len(data) > 4096: raise ValueError('Invalid config length')
        analysis = inspect_h264(data)
        if not analysis['sps'] or not analysis['pps']: raise ValueError('Missing SPS/PPS')
        (output / 'stream-config-annex-b.h264').write_bytes(data)
        (output / 'stream-analysis.json').write_text(json.dumps(analysis, indent=2) + '\n')
        summary['video_config'] = analysis
        print('ACTUAL H.264:', json.dumps(analysis))
    except (OSError, ValueError) as error:
        summary['video_config_error'] = str(error)
        print('No verified H.264 configuration:', error)
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2)); print('Saved:', output)
    return 0 if len(records) >= 2 else 1


if __name__ == '__main__':
    raise SystemExit(main())
