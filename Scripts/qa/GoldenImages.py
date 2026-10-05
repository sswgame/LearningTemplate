#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
골든 이미지 회귀 — 시험 게임의 자동 플레이를 네 백엔드(dx12 · dx11 · vk · gl)로 그려 정해 둔 프레임을 캡처하고, 기준과 **배경을 뺀 지표**로 견준다.

    py -3 -m Scripts golden --app build/Ninja-Debug-NileCity/Bin/App.exe              # 그 빌드의 활성 게임, 네 백엔드
    py -3 -m Scripts golden --app <App> --backends dx12 vk                             # 백엔드를 골라
    py -3 -m Scripts golden --app <App> --record [--runs 3]                            # 기준을 새로 뜬다(같은 조건 여러 판 → 허용 오차)

기준: `Test/Qa/Golden/<게임>/<백엔드>.png`(160x90 축소본, 사람이 열어 보는 그림)와 `<백엔드>.json`(지표 · 지표별 허용 오차 · 캡처 조건 ·
뜬 장치 `device` — GPU 이름 · 드라이버 판). 다른 기계에서 지면 비교 메시지가 두 장치를 함께 찍는다 — 그 기계에서 `--record` 로 떠 드라이버 차이인지
회귀인지 가른다(기계별 기준 파일은 아직 두지 않는다).
판정은 픽셀이 아니라 지표다(`Scripts/common/ImageMetrics.py`) — 자동 플레이는 프레임 시간이 벽시계를 따라가 같은 프레임 번호에서도 장면이
조금씩 다르다. 허용 오차는 기록할 때 같은 조건 여러 판의 퍼짐에서 정한다. 언리얼 Automation Screenshot Comparison 의 "Tolerance"
(Low/Medium/High) 를 손으로 고르는 대신 잡음 바닥을 재서 정하는 것이 다르다.

종료 코드: 0 = 모두 맞음, 1 = 하나라도 어긋남 · App 이 실패함, 77 = 견줄 것이 없음(기준이 없거나 이 기계에서 도는 백엔드가 없음 — ctest 는 건너뜀).
진 캡처는 `--diff-dir` 에 PNG 로 남는다.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common import getProjectRoot  # noqa: E402
from common.AppRun import (kBackendSwitch, kSkipExitCode, findBuildDirOfApp, findUsableBackends, loadGameTable,  # noqa: E402
                           readCMakeCacheValue, runApp)
from common.ImageMetrics import (ImageMetrics, averageMetrics, compareMetrics, computeMetrics, deriveTolerance, downscale,  # noqa: E402
                                 readPng, readPpm, writePng)

#: 기준 폴더(저장소 루트 기준).
kGoldenRelativeDir = "Test/Qa/Golden"


def captureInternal(appPath: Path, game: dict, table: dict, backend: str, workDir: Path, runIndex: int) -> tuple[str, object]:
    """한 판 그려 축소본을 돌려줍니다. ("ok", RgbImage) · ("skip", 이유) · ("fail", 이유)."""
    screenshotPath = workDir / f"{backend}_{runIndex}.ppm"
    frame = int(game["screenshot_frame"])
    listArgument = [f"-W={table['capture_width']}", f"-H={table['capture_height']}", *game["arguments"],
                    f"-gv_profileFrames={max(30, frame)}", f"-gv_screenshotFrame={frame}", f"-gv_screenshot={screenshotPath}",
                    kBackendSwitch[backend], *game.get("extra_arguments", [])]
    result = runApp(appPath, listArgument, timeoutSeconds=300.0)
    if result.bBackendUnusable:
        return "skip", "backend unusable on this machine"
    if not result.bClean:
        detail = result.listErrorLine[0] if result.listErrorLine else f"exit {result.exitCode}, timed out={result.bTimedOut}"
        # 진 판의 App 출력 전체를 남긴다 — 간헐 실패는 다시 돌리면 안 나온다.
        logPath = workDir.parent / f"sw_golden_{backend}_{runIndex}_app.log"
        logPath.write_text("\n".join(result.listLine) + "\n", encoding="utf-8")
        return "fail", f"App did not finish cleanly ({logPath}): {detail}"
    if not screenshotPath.is_file():
        return "fail", "no screenshot was written"
    image = readPpm(screenshotPath)
    return "ok", downscale(image, table["reference_width"], table["reference_height"])


def describeGpuInternal() -> dict:
    """이 기계의 GPU 이름 · 드라이버 판입니다(Windows: 어댑터 메모리가 가장 큰 Win32_VideoController, 그 밖은 빈 값)."""
    if sys.platform != "win32":
        return {"gpu": "", "driver": ""}
    command = ("Get-CimInstance Win32_VideoController | Sort-Object AdapterRAM -Descending | Select-Object -First 1 Name,DriverVersion"
               " | ConvertTo-Json")
    try:
        output = subprocess.run(["powershell", "-NoProfile", "-Command", command], capture_output=True, text=True, timeout=30,
                                check=False).stdout
        record = json.loads(output) if output.strip() else {}
        return {"gpu": record.get("Name", "") or "", "driver": record.get("DriverVersion", "") or ""}
    except (OSError, ValueError, subprocess.TimeoutExpired):
        return {"gpu": "", "driver": ""}


def describeDeviceMismatchInternal(record: dict) -> str:
    """기준을 뜬 장치가 이 기계와 다르면 둘을 함께 적은 꼬리말입니다. 기준에 장치가 없거나 같으면 빈 글입니다."""
    reference = record.get("device")
    if not reference:
        return ""
    now = describeGpuInternal()
    if reference.get("gpu") == now["gpu"] and reference.get("driver") == now["driver"]:
        return ""
    return (f" (reference captured on {reference.get('gpu', '')} {reference.get('driver', '')}, this machine {now['gpu']} {now['driver']}"
            " - re-record here to tell a driver difference from a regression)")


def recordInternal(appPath: Path, gameName: str, game: dict, table: dict, backend: str, goldenDir: Path, runCount: int,
                   workDir: Path) -> tuple[str, str]:
    listImage = []
    for runIndex in range(runCount):
        status, payload = captureInternal(appPath, game, table, backend, workDir, runIndex)
        if status != "ok":
            return status, str(payload)
        listImage.append(payload)
    listMetrics = [computeMetrics(image) for image in listImage]
    tolerance = deriveTolerance(listMetrics)
    goldenDir.mkdir(parents=True, exist_ok=True)
    writePng(goldenDir / f"{backend}.png", listImage[0])
    record = {
        "game": gameName,
        "backend": backend,
        "runs": runCount,
        "capture": {"width": table["capture_width"], "height": table["capture_height"], "frame": game["screenshot_frame"],
                    "arguments": game["arguments"]},
        "metrics": averageMetrics(listMetrics).toJson(),
        "tolerance": tolerance,
        "device": describeGpuInternal(),
    }
    (goldenDir / f"{backend}.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    return "recorded", f"{runCount} run(s), tolerance {json.dumps(tolerance)}"


def compareInternal(appPath: Path, game: dict, table: dict, backend: str, goldenDir: Path, workDir: Path,
                    diffDir: Path | None) -> tuple[str, str]:
    referencePath = goldenDir / f"{backend}.json"
    if not referencePath.is_file():
        return "skip", f"no reference ({referencePath.relative_to(goldenDir.parents[2])}) - record it with --record"
    record = json.loads(referencePath.read_text(encoding="utf-8"))
    if record["capture"]["arguments"] != game["arguments"] or record["capture"]["frame"] != game["screenshot_frame"]:
        return "fail", "the reference was captured with other arguments/frame than Test/Qa/Games.json - re-record it"
    status, payload = captureInternal(appPath, game, table, backend, workDir, 0)
    if status != "ok":
        return status, str(payload)
    actual = computeMetrics(payload)
    listViolation = compareMetrics(ImageMetrics.fromJson(record["metrics"]), actual, record["tolerance"])
    if not listViolation:
        return "pass", f"foreground {actual.foregroundFraction:.3f}, edges {actual.edgeDensity:.3f}"
    if diffDir is not None:
        writePng(diffDir / f"{backend}_actual.png", payload)
        referencePng = goldenDir / f"{backend}.png"
        if referencePng.is_file():
            writePng(diffDir / f"{backend}_expected.png", readPng(referencePng))
    return "fail", "; ".join(violation.describe() for violation in listViolation) + describeDeviceMismatchInternal(record)


def main(listArgument: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Golden-image regression of the test games' autoplay on every RHI backend")
    parser.add_argument("--app", type=Path, required=True, help="the built App executable (its build decides the game)")
    parser.add_argument("--game", default=None, help="game name (default: SW_ACTIVE_GAME of the App's build)")
    parser.add_argument("--backends", nargs="+", default=list(kBackendSwitch), choices=list(kBackendSwitch))
    parser.add_argument("--record", action="store_true", help="capture new references instead of comparing")
    parser.add_argument("--runs", type=int, default=3, help="runs per backend when recording (the spread sets the tolerance)")
    parser.add_argument("--diff-dir", type=Path, default=None, help="where failing captures are kept")
    parser.add_argument("--extra-arg", action="append", default=[], help="extra App argument for this run only (not recorded) - e.g. -gv_viewMode=2 to see that a broken frame fails")
    args = parser.parse_args(listArgument)

    repositoryRoot = getProjectRoot()
    gameName = args.game or readCMakeCacheValue(findBuildDirOfApp(args.app), "SW_ACTIVE_GAME")
    table = loadGameTable(repositoryRoot)
    if not gameName or gameName not in table["games"]:
        print(f"[Golden] unknown game '{gameName}' - add it to Test/Qa/Games.json", file=sys.stderr)
        return 1
    if not args.app.is_file():
        print(f"[Golden] App not found: {args.app}", file=sys.stderr)
        return 1
    game = dict(table["games"][gameName], extra_arguments=list(args.extra_arg))
    goldenDir = repositoryRoot / kGoldenRelativeDir / gameName
    diffDir = (args.diff_dir / gameName) if args.diff_dir is not None else None

    # Shipping 은 백엔드 하나만 링크한다 — 다른 백엔드는 기동 오류라 건너뛴다(골든은 같은 기준과 견준다).
    listUsable = findUsableBackends(findBuildDirOfApp(args.app))
    listBackend = [backend for backend in args.backends if backend in listUsable]
    for backend in args.backends:
        if backend not in listUsable:
            print(f"[Golden] {gameName} {backend:5} SKIP     not linked into this build", flush=True)
    mapStatus: dict[str, str] = {}
    with tempfile.TemporaryDirectory(prefix="sw_golden_") as tempDir:
        for backend in listBackend:
            if args.record:
                status, message = recordInternal(args.app, gameName, game, table, backend, goldenDir, max(1, args.runs), Path(tempDir))
            else:
                status, message = compareInternal(args.app, game, table, backend, goldenDir, Path(tempDir), diffDir)
            mapStatus[backend] = status
            print(f"[Golden] {gameName} {backend:5} {status.upper():8} {message}", flush=True)

    if any(status == "fail" for status in mapStatus.values()):
        return 1
    if not any(status in ("pass", "recorded") for status in mapStatus.values()):
        print(f"[Golden] {gameName}: nothing was compared", flush=True)
        return kSkipExitCode
    return 0


if __name__ == "__main__":
    sys.exit(main())
