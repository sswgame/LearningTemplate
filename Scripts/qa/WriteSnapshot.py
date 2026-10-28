#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
쓰기 위치 스냅샷 점검 — 명령(App) 한 번을 돌리기 전후로 쓰기 후보 폴더의 파일 목록(경로 · 크기 · 수정 시각)을 견주어
새로 생기거나 바뀌거나 사라진 경로를 찍고, `Test/AppTest/HostWriteLocations.json` 에 없는 위치가 있으면 진다.
(결정 기록 5-2 의 HostGpuIsolation H1 — 잠금을 풀기 전에 알려진 위치 표에 빠진 쓰기 위치를 찾는다.)

    py -3 -m Scripts write-snapshot --app build/Ninja-Shipping/Bin/App.exe -- -game=NileCity
    py -3 -m Scripts write-snapshot --app <App> --label editor.assetmanage -- -EnableEditor -scenario=engine/automation/editor/assetmanage.scenario.xml

`--` 뒤는 App 인자다. 작업 폴더는 App 이 있는 `Bin` 이다. 보는 곳: `Bin/Saved`, `Resource`, 프로젝트 루트 `Saved`,
`%LOCALAPPDATA%/SWEngine`, `%TEMP%` 안의 `sw_*` 폴더(다른 프로그램의 임시 파일이 섞이지 않게). 알려진 위치 표는 데이터 파일이고
이 코드는 표의 `glob` 으로 `<폴더>/**` 와 `**` 두 모양만 받는다(다른 모양은 오류로 멈춘다).
`--report <파일>` 이면 결과를 JSON 으로도 남긴다(시나리오마다 돌려 모을 때).

종료 코드: 0 = 알려진 위치만 씀, 1 = 표에 없는 쓰기가 있음 또는 App 이 실패함, 2 = 표 · 인자가 잘못됨.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common.AppRun import runApp  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.QualityPreset import SettingsIsolation  # noqa: E402

kLocationTableRelativePath = "Test/AppTest/HostWriteLocations.json"
kTempFolderPrefix = "sw_"

#: 한 파일의 모습: (크기, 수정 시각 ns). 경로는 폴더 이름과 `/` 로 이은 상대 경로(`resource:scratch/a/b.xml`)가 키다.
Snapshot = dict[str, tuple[int, int]]


@dataclass(frozen=True)
class KnownLocation:
    locationId: str
    root: str
    prefix: str  # 빈 문자열이면 폴더 안 전부
    what: str


class LocationTableError(Exception):
    """위치 표가 잘못됨 - 폴백 없이 멈춘다."""


def loadLocationTable(tablePath: Path) -> tuple[list[str], list[KnownLocation]]:
    """`(폴더 이름 목록, 알려진 위치 목록)`. 필수 키가 없거나 지원하지 않는 glob 모양이면 `LocationTableError`."""
    try:
        table = json.loads(tablePath.read_text(encoding="utf-8"))
        listRoot = list(table["roots"].keys())
        listLocation: list[KnownLocation] = []
        for entry in table["locations"]:
            glob = entry["glob"]
            if glob == "**":
                prefix = ""
            elif glob.endswith("/**") and "*" not in glob[:-3]:
                prefix = glob[:-3]
            else:
                raise LocationTableError(f"{entry['id']}: 지원하지 않는 glob '{glob}' ('<폴더>/**' 나 '**' 만 쓴다)")
            if entry["root"] not in listRoot:
                raise LocationTableError(f"{entry['id']}: 모르는 root '{entry['root']}'")
            listLocation.append(KnownLocation(entry["id"], entry["root"], prefix, entry["what"]))
    except (OSError, KeyError, json.JSONDecodeError) as error:
        raise LocationTableError(f"{tablePath}: {error}") from error
    return listRoot, listLocation


def resolveRootDirectories(binDirectory: Path, projectRoot: Path, environment: dict[str, str]) -> dict[str, Path]:
    """폴더 이름 -> 실제 경로. `LOCALAPPDATA` · `TEMP` 가 없으면(Windows 가 아니면) 그 폴더는 빠진다 - 없는 폴더는 비어 있는 것과 같다."""
    mapRoot = {"binSaved": binDirectory / "Saved", "resource": projectRoot / "Resource", "projectSaved": projectRoot / "Saved"}
    if environment.get("LOCALAPPDATA"):
        mapRoot["localAppData"] = Path(environment["LOCALAPPDATA"]) / "SWEngine"
    tempRoot = environment.get("TEMP") or environment.get("TMPDIR")
    if tempRoot:
        mapRoot["temp"] = Path(tempRoot)
    return mapRoot


def takeSnapshot(mapRoot: dict[str, Path]) -> Snapshot:
    """모든 폴더의 파일 목록. `temp` 는 `sw_*` 하위만 훑는다."""
    snapshot: Snapshot = {}
    for rootName, rootPath in mapRoot.items():
        if not rootPath.is_dir():
            continue
        for directory, listSubdirectory, listFile in os.walk(rootPath):
            relativeDirectory = Path(directory).relative_to(rootPath).as_posix()
            if rootName == "temp" and relativeDirectory == ".":
                listSubdirectory[:] = [name for name in listSubdirectory if name.startswith(kTempFolderPrefix)]
                continue
            for name in listFile:
                try:
                    stat = (Path(directory) / name).stat()
                except OSError:
                    continue  # 훑는 사이에 지워진 파일 - 다음 스냅샷이 사라짐으로 잡는다
                relativePath = name if relativeDirectory == "." else f"{relativeDirectory}/{name}"
                snapshot[f"{rootName}:{relativePath}"] = (stat.st_size, stat.st_mtime_ns)
    return snapshot


def diffSnapshots(before: Snapshot, after: Snapshot) -> dict[str, list[str]]:
    """`{"added": [...], "changed": [...], "removed": [...]}` - 키는 `폴더:상대경로`."""
    return {"added": sorted(set(after) - set(before)),
            "changed": sorted(key for key in set(before) & set(after) if before[key] != after[key]),
            "removed": sorted(set(before) - set(after))}


def findLocation(key: str, listLocation: list[KnownLocation]) -> KnownLocation | None:
    """`폴더:상대경로` 키가 속한 알려진 위치. 없으면 `None`(= 표에 없는 쓰기)."""
    rootName, _, relativePath = key.partition(":")
    for location in listLocation:
        if location.root != rootName:
            continue
        if location.prefix == "" or relativePath == location.prefix or relativePath.startswith(location.prefix + "/"):
            return location
    return None


def classifyChanges(mapDiff: dict[str, list[str]], listLocation: list[KnownLocation]) -> tuple[dict[str, list[str]], list[str]]:
    """`(위치 id -> 경로들, 표에 없는 경로들)`. 사라진 것도 쓰기(지우기)이므로 같이 본다."""
    mapKnown: dict[str, list[str]] = {}
    listUnknown: list[str] = []
    for kind, listKey in mapDiff.items():
        for key in listKey:
            location = findLocation(key, listLocation)
            if location is None:
                listUnknown.append(f"{kind} {key}")
            else:
                mapKnown.setdefault(location.locationId, []).append(f"{kind} {key}")
    return mapKnown, listUnknown


def parseArguments(listArgument: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="App 한 번의 쓰기 위치를 전후 스냅샷으로 점검한다.")
    parser.add_argument("--app", required=True, type=Path, help="App 실행 파일")
    parser.add_argument("--label", default="", help="보고에 붙일 이름(시나리오 이름 등)")
    parser.add_argument("--timeout", type=float, default=600.0, help="App 제한 시간(초)")
    parser.add_argument("--report", type=Path, default=None, help="결과를 JSON 으로 남길 파일")
    parser.add_argument("appArguments", nargs=argparse.REMAINDER, help="-- 뒤에 App 인자")
    arguments = parser.parse_args(listArgument)
    if arguments.appArguments and arguments.appArguments[0] == "--":
        arguments.appArguments = arguments.appArguments[1:]
    return arguments


def main(listArgument: list[str] | None = None) -> int:
    arguments = parseArguments(sys.argv[1:] if listArgument is None else listArgument)
    projectRoot = getProjectRoot()
    try:
        _, listLocation = loadLocationTable(projectRoot / kLocationTableRelativePath)
    except LocationTableError as error:
        print(f"[WriteSnapshot] 위치 표 오류: {error}", file=sys.stderr)
        return int(ExitCode.CheckInvalid)
    appPath = arguments.app.resolve()
    if not appPath.is_file():
        print(f"[WriteSnapshot] App 이 없다: {appPath}", file=sys.stderr)
        return int(ExitCode.CheckInvalid)

    mapRoot = resolveRootDirectories(appPath.parent, projectRoot, dict(os.environ))
    before = takeSnapshot(mapRoot)
    # 사용자 설정을 읽고 쓰는 위치(`%LOCALAPPDATA%/SWEngine`)까지 조사 대상이라 격리하면 안 된다. 결과는 그림이 아니라 쓰기 위치뿐이다.
    result = runApp(appPath, arguments.appArguments, settings=SettingsIsolation.NOT_APPLICABLE, timeoutSeconds=arguments.timeout)
    after = takeSnapshot(mapRoot)
    mapKnown, listUnknown = classifyChanges(diffSnapshots(before, after), listLocation)

    label = arguments.label or " ".join(arguments.appArguments)
    print(f"[WriteSnapshot] {label}: 종료 코드 {result.exitCode}, 알려진 위치 {', '.join(sorted(mapKnown)) or '없음'}")
    for line in listUnknown:
        print(f"[WriteSnapshot]   표에 없음: {line}")
    if arguments.report:
        arguments.report.parent.mkdir(parents=True, exist_ok=True)
        arguments.report.write_text(json.dumps({"label": label, "exitCode": result.exitCode, "known": mapKnown, "unknown": listUnknown},
                                               ensure_ascii=False, indent=2), encoding="utf-8")
    if not result.bLaunched or result.bTimedOut or result.exitCode != 0 or listUnknown:
        return int(ExitCode.Violation)
    return int(ExitCode.Passed)


if __name__ == "__main__":
    sys.exit(main())
