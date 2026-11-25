#!/bin/bash
cd "$(dirname "$0")/../.." && exec py -3 -m Scripts preflight "$@"   # 단계 · 끝 줄 · 인자의 정본은 Scripts/dev/preflight.py(`--help`)
