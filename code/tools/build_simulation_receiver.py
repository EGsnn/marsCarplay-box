#!/usr/bin/env python3
"""Build a native host receiver fixture; does not build/flash S3 firmware."""
import argparse
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
workspace = root.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--crypto-lib', type=Path, required=True)
args = parser.parse_args()
if not args.crypto_lib.is_file():
    raise SystemExit('Prepared native TF-PSA-Crypto library required: --crypto-lib PATH')
out = root/'build/simulation-host'; out.mkdir(parents=True, exist_ok=True)
c = root/'code/firmware/components'
if not os.environ.get('IDF_PATH'): raise SystemExit('Set IDF_PATH for the optional native fixture')
idf = Path(os.environ['IDF_PATH'])
crypto = idf/'components/mbedtls/mbedtls/tf-psa-crypto'
include = [root/'code/tests/simulation_stubs', root/'code/firmware/main',
           c/'sub0_decoder/include', crypto/'include', crypto/'drivers/builtin/include']
include += [c/name/'include' for name in ['airplay_pairing','airplay_capture_core',
            'airplay_rtsp','auth_port','probe_crypto']]
include += [c/'ed25519/upstream']
flags = ['-O2','-fPIC','-D_DARWIN_C_SOURCE','-DCONFIG_MBEDTLS_CHACHAPOLY_C=1',
         '-DMBEDTLS_DECLARE_PRIVATE_IDENTIFIERS']
for path in include: flags += ['-I',str(path)]
sources = [root/'code/tests/simulation_receiver.c',root/'code/tests/simulation_stubs/auth.c',
           root/'code/firmware/main/box_pixels.c',c/'probe_crypto/probe_crypto.c']
sources += [c/'airplay_pairing'/name for name in ['airplay_tlv8.c','airplay_srp6a.c',
             'airplay_pair_setup.c','airplay_pair_verify.c']]
sources += [c/'airplay_rtsp'/name for name in ['airplay_rtsp_codec.c','airplay_rtsp_pairing.c',
             'airplay_bplist.c','airplay_mfi_sap.c']]
sources += [c/'airplay_capture_core'/name for name in ['airplay_rtsp_session.c','airplay_commands.c',
             'airplay_screen_stream.c','airplay_screen_codec.c']]
sources += [c/'ed25519/upstream'/name for name in ['fe.c','ge.c','keypair.c','sc.c','sha512.c','sign.c','verify.c']]
objects = []
for i, source in enumerate(sources):
    obj=out/f'c-{i}.o';objects.append(obj)
    subprocess.run(['clang','-std=c11',*flags,'-c',str(source),'-o',str(obj)],check=True)
sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
obj=out/'decoder.o';objects.append(obj)
subprocess.run(['clang++','-std=c++23','-DNDEBUG','-isystem',sdk+'/usr/include/c++/v1',
                *flags,'-I',str(c/'sub0_decoder/vendor/src'),'-I',str(c/'sub0_decoder/vendor/include'),
                '-c',str(c/'sub0_decoder/box_decoder.cpp'),'-o',str(obj)],check=True)
library=out/'receiver.dylib'
subprocess.run(['clang++','-dynamiclib',*[str(o) for o in objects],str(args.crypto_lib),'-o',str(library)],check=True)
print('Built native host fixture:',library)
