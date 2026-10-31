#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the BOX application with a credential-free default. Never flashes hardware."""
import argparse,json,os,shutil,subprocess,sys,time
from pathlib import Path
root=Path(__file__).resolve().parents[1];out=root/'build/firmware';out.mkdir(parents=True,exist_ok=True)
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--idf-path',type=Path,help='ESP-IDF checkout; otherwise use IDF_PATH')
p.add_argument('--auth-provider',type=Path,help='Authorized external source; omitted = stub')
p.add_argument('--fresh-config',action='store_true')
a=p.parse_args();idf=a.idf_path or (Path(os.environ['IDF_PATH']) if os.environ.get('IDF_PATH') else None)
if not idf or not (idf/'tools/idf.py').is_file():raise SystemExit('Set IDF_PATH to the pinned ESP-IDF checkout or use --idf-path')
idf=idf.resolve()
if a.auth_provider and not a.auth_provider.is_file():raise SystemExit('External provider source does not exist')
if not (root/'local/deps/esp_tinyusb/CMakeLists.txt').exists():raise SystemExit('Run python3 code/prepare_local.py --fetch-deps first')
subprocess.run([sys.executable,str(root/'code/tools/build_capabilities.py')],check=True)
settings_path=root/'local/settings.json'
if not settings_path.exists():subprocess.run([sys.executable,str(root/'code/prepare_local.py')],check=True)
password=json.loads(settings_path.read_text())['debug_ap_password']
if len(password)!=12 or any(c not in '0123456789abcdef' for c in password):raise SystemExit('Invalid local AP setting')
fragment=out/'wifi.defaults';fragment.write_text(f'CONFIG_PROBE_AP_PASSWORD="{password}"\n')
config=out/'sdkconfig'
if a.fresh_config and config.exists():shutil.copy2(config,out/f'sdkconfig.before-reset-{time.time_ns()}');config.unlink()
env=dict(os.environ,IDF_PATH=str(idf),IDF_COMPONENT_MANAGER='0')
args=['bash',str(root/'code/idf_env.sh'),'python',str(idf/'tools/idf.py'),'-C',str(root/'code/firmware'),'-B',str(out),'-D',f'SDKCONFIG={config}','-D',f'SDKCONFIG_DEFAULTS={root/"code/firmware/sdkconfig.defaults"};{fragment}','-D','IDF_TARGET=esp32s3','-D',f'MARS_AUTH_PROVIDER_SOURCE={a.auth_provider.resolve() if a.auth_provider else ""}','build']
print('Authentication: external provider' if a.auth_provider else 'Authentication: stub (real iPhone authentication unavailable)',flush=True)
subprocess.run(args,env=env,check=True)
