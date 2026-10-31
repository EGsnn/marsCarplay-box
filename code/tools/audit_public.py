#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the published tree boundary and common credential markers (no network)."""
from pathlib import Path
import re,subprocess
root=Path(__file__).resolve().parents[2]
allowed_dirs={'code','versions','references','LICENSES','.github'}
allowed_root={'README.md','LICENSE','NOTICE.md','AGENTS.md','PUBLISH_SCOPE.md','.gitignore','requirements-dev.txt'}
forbidden_suffix={'.bin','.elf','.map','.pk8','.p7b','.der','.pem','.key','.h264','.mp4','.pcap','.pcapng','.zip','.html','.js','.ts','.vue'}
if (root/'.git').exists():
 raw=subprocess.check_output(['git','-C',str(root),'ls-files','-z'])
 paths=[Path(x.decode()) for x in raw.split(b'\0') if x]
else:
 paths=[p.relative_to(root) for p in root.rglob('*') if p.is_file() and not any(x in {'build','local','.git','__pycache__'} for x in p.relative_to(root).parts)]
patterns=[rb'-----BEGIN (?:[A-Z]+ )?PRIVATE KEY-----',rb'(?:ghp_|github_pat_)[A-Za-z0-9_]{20,}',rb'AKIA[0-9A-Z]{16}']
assert paths,'No source files selected'
for rel in paths:
 assert not rel.is_absolute() and '..' not in rel.parts,rel
 assert (len(rel.parts)==1 and rel.name in allowed_root) or (len(rel.parts)>1 and rel.parts[0] in allowed_dirs),rel
 assert not any(x in {'local','build','.git','__pycache__','assets','carplay_web','carplay-live-preview','carplay-headunit-test'} for x in rel.parts),rel
 assert rel.suffix.lower() not in forbidden_suffix,rel
 p=root/rel;assert p.is_file() and not p.is_symlink(),rel
 data=p.read_bytes();assert not any(re.search(pattern,data) for pattern in patterns),f'Credential marker in {rel}'
 if rel.suffix in {'.c','.cpp','.h','.hpp'}:
  assert b'_binary_identity_pk8' not in data,f'Embedded identity loader in {rel}'
print(f'Public boundary OK: {len(paths)} source/license/document files; no web application files or common credential markers. This is not an exhaustive rights/secret audit.')
