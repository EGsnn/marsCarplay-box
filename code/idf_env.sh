#!/bin/bash
set -euo pipefail
: "${IDF_PATH:?Set IDF_PATH to your ESP-IDF checkout}"
export IDF_COMPONENT_MANAGER=0
source "$IDF_PATH/export.sh" >/dev/null
for tool in cmake ninja; do
    command -v "$tool" >/dev/null || { echo "Install $tool and add it to PATH" >&2; exit 1; }
done
exec "$@"
