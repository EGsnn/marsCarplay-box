#!/usr/bin/env python3
"""Offline host checks; never used in the runtime video path."""
import argparse,json,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2];out=root/'build/host-checks';out.mkdir(parents=True,exist_ok=True)
p=argparse.ArgumentParser();p.add_argument('--sample',type=Path);a=p.parse_args()
main=root/'code/firmware/main';dec=root/'code/firmware/components/sub0_decoder';cpp_sdk=[]
if sys.platform=='darwin':
 sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
 cpp_sdk=['-isystem',sdk+'/usr/include/c++/v1']
flags=['-O2','-fsanitize=address,undefined','-I',str(main),'-I',str(dec/'include')]
subprocess.run([sys.executable,str(root/'code/tests/test_collect_summary.py')],check=True)
capture=root/'code/firmware/components/airplay_capture_core'
subprocess.run(['clang','-std=c11',*flags,'-I',str(root/'code/tests/simulation_stubs'),
 '-I',str(capture/'include'),str(root/'code/tests/test_live_config.c'),
 str(capture/'live_store.c'),'-o',str(out/'test-live-config')],check=True)
subprocess.run([str(out/'test-live-config')],check=True)
subprocess.run(['clang','-std=c11',*flags,'-I',str(capture/'include'),
 str(root/'code/tests/test_touch.c'),str(capture/'airplay_touch_report.c'),
 str(capture/'airplay_hid_descriptor.c'),'-o',str(out/'test-touch')],check=True)
subprocess.run([str(out/'test-touch')],check=True)
subprocess.run(['clang','-std=c11',*flags,str(root/'code/tests/test_perf.c'),str(main/'box_perf.c'),'-o',str(out/'test-perf')],check=True)
perf_output=subprocess.check_output([str(out/'test-perf')],text=True)
reports=[json.loads(line) for line in perf_output.splitlines() if line.startswith('{')]
assert len(reports)==2
assert reports[0]['decode_avg_ms']==300 and reports[0]['process_avg_ms']==325
assert reports[0]['convert_avg_ms']==7.5 and reports[0]['present_avg_ms']==17.5
assert reports[1]['decode_avg_ms']==0 and reports[1]['display_fps']==0
print(perf_output,end='')
subprocess.run(['clang++','-std=c++23',*cpp_sdk,*flags,'-I',str(dec),str(root/'code/tests/test_decoder_profile.cpp'),'-o',str(out/'test-decoder-profile')],check=True)
subprocess.run([str(out/'test-decoder-profile')],check=True)
subprocess.run(['clang','-std=c11',*flags,str(root/'code/tests/test_decode_profile.c'),str(main/'box_decode_profile.c'),'-o',str(out/'test-profile-totals')],check=True)
profile_output=subprocess.check_output([str(out/'test-profile-totals')],text=True)
profiles=[json.loads(line) for line in profile_output.splitlines()]
assert profiles[0]['samples']==2 and profiles[0]['decode_avg_ms']==31.5
assert profiles[0]['timer_reads']==400 and profiles[0]['stage_avg_ms']['motion_compensation']==6
assert sum(profiles[0]['stage_avg_ms'].values())==profiles[0]['decode_avg_ms']
assert profiles[1]['samples']==0 and profiles[1]['decode_avg_ms']==0
print(profile_output,end='')
subprocess.run(['clang','-std=c11',*flags,'-c',str(main/'box_pixels.c'),'-o',str(out/'pixels.o')],check=True)
subprocess.run(['clang','-std=c11',*flags,str(root/'code/tests/test_pixels.c'),str(out/'pixels.o'),'-o',str(out/'test-pixels')],check=True)
subprocess.run([str(out/'test-pixels')],check=True)
subprocess.run(['clang','-std=c11',*flags,str(root/'code/tests/test_pixels_equivalence.c'),str(out/'pixels.o'),'-o',str(out/'test-pixel-equivalence')],check=True)
subprocess.run([str(out/'test-pixel-equivalence')],check=True)
subprocess.run(['clang++','-std=c++23',*cpp_sdk,*flags,'-I',str(dec/'vendor/src'),str(root/'code/tests/test_bitstream.cpp'),'-o',str(out/'test-bitstream')],check=True)
subprocess.run([str(out/'test-bitstream')],check=True)
if a.sample:
 subprocess.run(['clang++','-std=c++23','-DNDEBUG',*cpp_sdk,*flags,'-I',str(dec/'vendor/src'),'-I',str(dec/'vendor/include'),str(root/'code/tests/test_decoder_adapter.cpp'),str(dec/'box_decoder.cpp'),str(out/'pixels.o'),'-o',str(out/'test-decoder')],check=True)
 subprocess.run([str(out/'test-decoder'),str(a.sample.resolve()),str(out/'last-frame.rgb565'),str(out/'all-frames.rgb565')],check=True)
