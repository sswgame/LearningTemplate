#!/usr/bin/env python3
"""
@file BackendSmoke.py
@brief 네 백엔드(DX12/Vulkan/DX11/GL)로 벤치 큐브 씬을 그려 SceneColor 를 PPM 으로 받아 비교하는 스모크.

사용법 (빌드 후):
    py -3 Scripts/dev/BackendSmoke.py                # 불투명 + 반투명 회차, 네 백엔드
    py -3 Scripts/dev/BackendSmoke.py --preset Ninja-Release --out C:/tmp/smoke
    py -3 Scripts/dev/BackendSmoke.py --backends dx12 vk               # 고른 백엔드만

판정: 각 실행이 exit 0 이고, 로그의 [Error] 수와 PPM 의 평균 RGB·"배경이 아닌 픽셀 수" 를 표로 낸다.
네 백엔드의 평균이 서로 1.0 이내이고 non-bg 픽셀 수가 0 이 아니면 정상이다.

백엔드는 쿠킹 표(Config/Engine/CookContract.json)의 줄마다 첫 별칭 플래그(-dx11 / -dx12 / -vk / -gl)로 고른다.
`-gv_rhiBackend=<이름|숫자>`(예: `Vulkan`)도 같은 RHIBackendUtil::findCommandLineBackend 를 지난다.
"""
from __future__ import annotations

import argparse
import re
import sys
from collections.abc import Sequence
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import BuildTree, CookContractSpec, addBuildTreeArguments, runProcess  # noqa: E402
from common.AppRun import kBackendSwitch  # noqa: E402

kArrBackgroundColor = (31, 38, 46)  # forwardpipeline.xml SceneColor clearColor 0.12,0.15,0.18


def readPpmStatsInternal(path: Path) -> tuple[tuple[float, ...], int] | None:
    """PPM(P6) 평균 RGB 와 배경이 아닌 픽셀 수(7 픽셀 간격 샘플)를 돌려준다."""
    data = path.read_bytes()
    match = re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s", data)
    if match is None:
        return None
    width, height = int(match.group(1)), int(match.group(2))
    pixels = data[match.end() : match.end() + width * height * 3]
    count = width * height
    mean = tuple(round(sum(pixels[c::3]) / count, 1) for c in range(3))
    nonBackgroundCount = 0
    for i in range(0, len(pixels), 3 * 7):
        if any(abs(pixels[i + c] - kArrBackgroundColor[c]) > 3 for c in range(3)):
            nonBackgroundCount += 1
    return mean, nonBackgroundCount


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="네 백엔드로 벤치 큐브 씬을 그려 PPM 을 견주는 스모크")
    addBuildTreeArguments(parser)
    parser.add_argument("--out", type=Path, default=None, help="PPM/log 출력 폴더 (기본: build/<preset>/smoke)")
    parser.add_argument("--meshes", type=int, default=16, help="벤치 메시 수(-gv_benchMeshes)")
    parser.add_argument("--backends", nargs="+", default=list(kBackendSwitch), choices=list(kBackendSwitch),
                        help="돌릴 백엔드들(기본: 쿠킹 표의 전부, 표 순서)")
    args = parser.parse_args(argv)

    tree = BuildTree.fromArguments(args)
    binDir = tree.binDir
    appPath = tree.appExecutable
    if appPath is None:
        print(f"App 실행 파일이 없습니다: {binDir}")
        return 2
    outDir = args.out or tree.path / "smoke"
    outDir.mkdir(parents=True, exist_ok=True)

    # (백엔드 이름, 스위치) — 쿠킹 표의 줄 순서, 스위치는 그 줄의 첫 별칭(`kBackendSwitch`).
    listBackend = [(backend.name, kBackendSwitch[backend.listAlias[0]]) for backend in CookContractSpec.load().listBackend
                   if backend.listAlias[0] in args.backends]

    bFailed = False
    listResult = []
    for kind, listExtra, frames in (("opaque", ["-gv_benchTransparent=0"], 30), ("transparent", ["-gv_benchTransparent=25"], 30)):
        for name, switch in listBackend:
            ppmPath = outDir / f"{kind}_{name}.ppm"
            logPath = outDir / f"{kind}_{name}.log"
            ppmPath.unlink(missing_ok=True)
            command = [str(appPath), switch, *listExtra, f"-gv_benchMeshes={args.meshes}", f"-gv_profileFrames={frames}", f"-gv_screenshot={ppmPath}"]
            result = runProcess(command, cwd=binDir, timeoutSeconds=180, stdoutPath=logPath)
            errorCount = logPath.read_bytes().count(b"[Error]") if logPath.is_file() else 0
            stats = readPpmStatsInternal(ppmPath) if ppmPath.is_file() else None
            bOk = result.bSucceeded and stats is not None and stats[1] > 0
            bFailed |= not bOk
            listResult.append((kind, name, result.returnCode, errorCount, stats))

    print("%-7s %-10s %5s %7s %-20s %s" % ("path", "backend", "exit", "errors", "mean RGB", "non-bg px"))
    for kind, name, returnCode, errorCount, stats in listResult:
        mean = f"{stats[0]}" if stats else "no ppm"
        nonBackgroundCount = stats[1] if stats else "-"
        print("%-7s %-10s %5d %7d %-20s %s" % (kind, name, returnCode, errorCount, mean, nonBackgroundCount))
    print(f"PPM/log: {outDir}")
    return 1 if bFailed else 0


if __name__ == "__main__":
    sys.exit(main())
