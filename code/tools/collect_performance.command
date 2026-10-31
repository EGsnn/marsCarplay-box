#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 - <<'PY'
import json
from pathlib import Path
s=json.loads(Path('local/settings.json').read_text())
print('保持 BOX USB 接 iPhone；Mac 仅连接诊断热点采集数值，不转发视频。')
print('热点：',s['debug_ap_ssid'],'密码：',s['debug_ap_password'])
PY
read -r -p "Mac 连好诊断热点后按回车；接下来在 CarPlay 上连续滑动约 30 秒。"
python3 code/tools/collect_performance.py --seconds 30
read -r -p "采集已结束，可切回原 Wi-Fi。按回车关闭。"
