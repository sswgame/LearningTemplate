#!/usr/bin/env python3
"""
@file BackendSmoke.py
@brief 네 백엔드(DX12/Vulkan/DX11/GL)로 벤치 큐브 씬을 그려 SceneColor 를 PPM 으로 받아 비교하는 스모크.

사용법 (빌드 후):
    py -3 Scripts/dev/BackendSmoke.py                # 불투명 + 반투명 회차, 네 백엔드
    py -3 Scripts/dev/BackendSmoke.py --preset Ninja-Release --out C:/tmp/smoke

판정: 각 실행이 exit 0 이고, 로그의 [Error] 수와 PPM 의 평균 RGB·"배경이 아닌 픽셀 수" 를 표로 낸다.
네 백엔드의 평균이 서로 1.0 이내이고 non-bg 픽셀 수가 0 이 아니면 정상이다.

백엔드는 쿠킹 표(Config/Engine/CookContract.json)의 줄마다 첫 별칭 플래그(-dx11 / -dx12 / -vk / -gl)로 고른다.
`-gv_rhiBackend=<이름|숫자>`(예: `Vulkan`)도 같은 RHIBackendUtil::findCommandLineBackend 를 지난다.
"""
import argparse
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import BuildTree, CookContractSpec, addBuildTreeArguments, runProcess

kArrBackgroundColor = (31, 38, 46)  # forwardpipeline.xml SceneColor clearColor 0.12,0.15,0.18


def readPpmStatsInternal(path):
    """PPM(P6) 평균 RGB 와 배경이 아닌 픽셀 수(7 픽셀 간격 샘플)를 돌려준다."""
    with open(path, "rb") as f:
        data = f.read()
    match = re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s", data)
    if match is None:
        return None
    width, height = int(match.group(1)), int(match.group(2))
    pixels = data[match.end() : match.end() + width * height * 3]
    count = width * height
    mean = tuple(round(sum(pixels[c::3]) / count, 1) for c in range(3))
    non_bg = 0
    for i in range(0, len(pixels), 3 * 7):
        if any(abs(pixels[i + c] - kArrBackgroundColor[c]) > 3 for c in range(3)):
            non_bg += 1
    return mean, non_bg


def main():
    parser = argparse.ArgumentParser(description="4-backend PPM smoke")
    addBuildTreeArguments(parser)
    parser.add_argument("--out", default=None, help="PPM/log 출력 폴더 (기본: build/<preset>/smoke)")
    parser.add_argument("--meshes", type=int, default=16)
    args = parser.parse_args()

    tree = BuildTree.fromArguments(args)
    bin_dir = str(tree.binDir)
    app = os.path.join(bin_dir, "App.exe")
    if os.path.exists(app) is False:
        print("App.exe 가 없습니다: " + app)
        return 2
    out_dir = args.out or str(tree.path / "smoke")
    os.makedirs(out_dir, exist_ok=True)

    # (백엔드 이름, 플래그) — 쿠킹 표의 줄 순서, 플래그는 그 줄의 첫 별칭.
    listBackend = [(backend.name, backend.listAlias[0]) for backend in CookContractSpec.load().listBackend]

    failed = False
    results = []
    for kind, extra, frames in (("opaque", ["-gv_benchTransparent=0"], 30), ("transparent", ["-gv_benchTransparent=25"], 30)):
        for name, flag in listBackend:
            ppm = os.path.join(out_dir, "%s_%s.ppm" % (kind, name))
            log = os.path.join(out_dir, "%s_%s.log" % (kind, name))
            if os.path.exists(ppm):
                os.remove(ppm)
            cmd = [app, "-" + flag] + extra + ["-gv_benchMeshes=%d" % args.meshes, "-gv_profileFrames=%d" % frames, "-gv_screenshot=" + ppm]
            proc = runProcess(cmd, cwd=Path(bin_dir), timeoutSeconds=180, stdoutPath=Path(log))
            with open(log, "rb") as log_file:
                errors = log_file.read().count(b"[Error]")
            stats = readPpmStatsInternal(ppm) if os.path.exists(ppm) else None
            ok = proc.bSucceeded and stats is not None and stats[1] > 0
            failed |= ok is False
            results.append((kind, name, proc.returnCode, errors, stats))

    print("%-7s %-10s %5s %7s %-20s %s" % ("path", "backend", "exit", "errors", "mean RGB", "non-bg px"))
    for kind, name, code, errors, stats in results:
        mean = "%s" % (stats[0],) if stats else "no ppm"
        non_bg = stats[1] if stats else "-"
        print("%-7s %-10s %5d %7d %-20s %s" % (kind, name, code, errors, mean, non_bg))
    print("PPM/log: " + out_dir)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
