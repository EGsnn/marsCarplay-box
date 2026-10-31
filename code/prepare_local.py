#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare ignored settings and pinned public USB dependencies. Never copies identities."""
import argparse,io,json,secrets,shutil,subprocess,tarfile,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--fetch-deps',action='store_true',help='Download fixed esp-usb/TinyUSB sources from GitHub')
a=p.parse_args();local=root/'local';local.mkdir(exist_ok=True)
settings=local/'settings.json'
if not settings.exists():settings.write_text(json.dumps({'debug_ap_ssid':'ATK-CarPlay','debug_ap_password':secrets.token_hex(6)},indent=2)+'\n')
pins=json.loads((root/'code/firmware/upstream-pins.json').read_text())
def extract(repo,revision,destination):
 url=f'https://codeload.github.com/{repo}/tar.gz/{revision}'
 data=subprocess.check_output(['curl','--fail','--location','--silent','--show-error','--max-time','120',url])
 with tarfile.open(fileobj=io.BytesIO(data),mode='r:gz') as archive:
  for item in archive.getmembers():
   parts=Path(item.name).parts
   if len(parts)<2:continue
   rel=Path(*parts[1:])
   if repo=='espressif/tinyusb' and rel.parts[0] not in {'src','LICENSE'} and rel.parts[:2]!=('lib','networking'):continue
   if rel.is_absolute() or '..' in rel.parts or not (item.isdir() or item.isfile()):
    raise RuntimeError('Unsafe archive entry: '+item.name)
   target=destination/rel
   if item.isdir():target.mkdir(parents=True,exist_ok=True)
   else:
    target.parent.mkdir(parents=True,exist_ok=True)
    with archive.extractfile(item) as src,target.open('wb') as out:shutil.copyfileobj(src,out)
    target.chmod(item.mode&0o777)
if a.fetch_deps:
 target=local/'deps/esp_tinyusb';metadata=local/'deps/pins.json'
 expected={key:pins[key] for key in ('esp_usb','tinyusb')}
 if target.exists():
  if not metadata.exists() or json.loads(metadata.read_text())!=expected:raise SystemExit('Existing USB dependency has no matching pin record; move it aside explicitly')
  print('Pinned USB dependency already prepared')
 else:
  (local/'deps').mkdir(exist_ok=True)
  with tempfile.TemporaryDirectory(prefix='mars-usb-',dir=local/'deps') as tmp:
   tmp=Path(tmp);extract('espressif/esp-usb',pins['esp_usb'],tmp/'esp-usb')
   component=tmp/'esp-usb/device/esp_tinyusb'
   if not (component/'CMakeLists.txt').exists():raise SystemExit('Unexpected esp-usb archive layout')
   extract('espressif/tinyusb',pins['tinyusb'],component/'tinyusb')
   shutil.copytree(component,target)
  metadata.write_text(json.dumps(expected,indent=2)+'\n')
  print('Pinned public USB dependencies prepared')
print('Local settings ready. No authentication identity was created or downloaded.')
