#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
부하 검증 — 부하 시나리오 × 하드웨어 프로필을 Release 로 여러 번 돌려 p50 · p99 · max 를 예산(`Test/Load/Budgets.json`)과 이 기계의 기준선
(`Test/Load/Baselines.json`)에 견주고, 결과를 JSON 으로 남겨 추세를 본다. 실패하면 어디(CPU 게임 스레드 · CPU 렌더 스레드 · GPU · 메모리 · IO ·
서버)가 예산을 깼는지 적는다.

    py -3 -m Scripts load list                                         # 시나리오 · 프로필 · 묶음
    py -3 -m Scripts load run renderbench --profile Mid LowEnd --runs 3
    py -3 -m Scripts load run connect --profile Mid --record           # 이 기계의 기준선을 남긴다
    py -3 -m Scripts load suite baseline --runs 3 --record             # 묶음(Test/Load/Suites.json) 전체
    py -3 -m Scripts load scale renderbench --profile LowEnd           # 예산을 지키는 가장 높은 품질 단계를 찾는다
    py -3 -m Scripts load trend renderbench                            # 남긴 결과(LoadResults/trend.jsonl)의 추세

시나리오 형식: 같은 `<Load clients rampUpSeconds durationSeconds network meshes movers/>` 선언을 세 드라이버가 읽는다.
  - app     : `Resource/**/automation/load/*.scenario.xml`(엔진 자동화 시나리오). 엔진이 시작 프레임에 측정 창을 열고 끝에 프로파일 표를 낸다.
  - loadbot : `Test/Load/Scenarios/*.load.xml` 의 `<LoadScenario driver="loadbot" botScenario=…>` — OnlineLoadBot(--local-server) 를 돌린다.
  - cluster : `<LoadScenario driver="cluster" test=…>` — EngineTest 의 부하 스위트(루프백 클러스터)를 `SW_LOAD_*` 환경 변수로 돌린다.
하드웨어 프로필(`Test/Load/Profiles.json`)은 CPU 코어 · 메모리를 Job Object 로, VRAM · 디스크 · 회선 · 해상도 · 품질은 엔진 스위치로 건다.
소프트웨어 제한이라 결과에 `emulated` 를 적고 기준선 키를 실제 기기 측정(Native)과 가른다.

상용 엔진과 같은 자리: 언리얼 Gauntlet(밖에서 띄우고 판정) + Device Profiles/Scalability(프로필 → 품질 단계) + Insights/CSV 프로파일러(구간 표),
유니티 Performance Testing(측정 그룹 · 기준선 비교) + Quality Settings, 서버는 Nakama 부하 도구. 다른 점: 하드웨어 프로필을 기기 없이 흉내 내고,
예산 실패를 병목 갈래로 나눠 적으며, 같은 선언으로 클라이언트 · 서버 · 클러스터를 돈다.
"""

from __future__ import annotations

import argparse
import datetime
import json
import platform
import re
import statistics
import sys
import tempfile
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common.Paths import getProjectRoot  # noqa: E402
from common.Process import runProcess  # noqa: E402
from common.BuildTree import BuildTree  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.AppRun import (kBackendSwitch, kStartupRe, computeSlopePerMinute, loadGameTable, parseProfileTable, parseProfileWall,  # noqa: E402
                           runApp, splitRunSeconds)
from common.ProcessLimits import ProcessLimits  # noqa: E402
from common.ScenarioFile import readLoadSpec  # noqa: E402

kLoadFolder = "Test/Load"
kMegabyte = 1024.0 * 1024.0
#: 결과를 남길 때 들고 있는 구간 — 프레임 전체와 병목을 가를 때 보는 하위 구간(RT.BeginFrame = GPU 백프레셔, GT.Packet.submit = RT 를 기다림).
kTrackedScope: tuple[str, ...] = ("GT.Frame", "RT.Frame", "GPU.Frame", "GT.Scene.tick", "GT.Game.update", "GT.GPUScene.build", "RT.BeginFrame",
                                  "RT.ExecutePacket", "RT.Present", "GT.Packet.submit")
#: 기준선과 견줄 때의 허용 — 값 > 기준 × (1 + 허용) + 바닥 이면 회귀. max 는 한 번의 사건이라 기준선과 견주지 않고 예산으로만 본다.
kBaselineTolerance: dict[str, float] = {"p50_us": 0.15, "p99_us": 0.35, "_mb": 0.15, "per_frame": 0.25}
kBaselineFloor: dict[str, float] = {"_us": 150.0, "_mb": 32.0, "per_frame": 2.0}
#: 기다림 구간 — 다른 스레드 · GPU 를 기다린 시간이라 판마다 크게 흔들린다. 병목 갈래를 가를 때만 쓰고 기준선과 견주지 않는다.
kWaitScope: tuple[str, ...] = ("RT.BeginFrame", "RT.Present", "GT.Packet.submit")
#: 소크 기울기 — 앞 1/4 은 캐시 채우기라 뺀다(Scripts/qa/Soak.py 와 같다).
kWarmupFraction = 0.25

_kHitchRe = re.compile(r"\[Profile\] hitch\s+worst (\d+) (\d+) (\d+) (\d+) (\d+) us\s+over16ms (\d+)\s+over33ms (\d+)\s+over100ms (\d+)")
_kAllocRe = re.compile(r"\[Profile\] alloc/frame\s+(\d+)\.(\d)")
_kGpuMemoryRe = re.compile(r"\[Profile\] GPU memory by kind .*?\s(\d+)\.(\d+) MB in")
_kWorkerRe = re.compile(r"Initialized with (\d+) worker threads")
_kLoadMetricRe = re.compile(r"\[LoadMetric\] (\w+)=(-?[\d.]+)")


# ------------------------------------------------------------------------------
# 선언
# ------------------------------------------------------------------------------
@dataclass
class LoadDeclaration:
    """부하 시나리오 하나 — 어느 드라이버로 무엇을 돌리나와 `<Load>` 값."""

    name: str
    driver: str
    path: Path
    durationSeconds: float = 0.0
    rampUpSeconds: float = 0.0
    clientCount: int = 0
    meshCount: int = 0
    moverCount: int = 0
    network: str = ""
    botScenario: str = ""
    testFilter: str = ""
    resourceID: str = ""
    gameName: str = "Empty"


@dataclass
class HardwareProfile:
    name: str
    data: dict

    @property
    def bEmulated(self) -> bool:
        return bool(self.data["emulated"])

    def makeLimits(self) -> ProcessLimits:
        # 프로필 키는 모두 필수다(Profiles.json — 0 · 빈 값이 "제한 없음" 을 뜻한다). 빠지면 KeyError.
        return ProcessLimits(int(self.data["cpu_count"]), int(self.data["memory_mb"]))


@dataclass
class RunMeasurement:
    """한 판의 결과 — 지표 이름 → 값, 그 판의 실패(정확성 · 실행), 건너뛴 이유, 돌린 명령."""

    metrics: dict[str, float] = field(default_factory=dict)
    listFailure: list[str] = field(default_factory=list)
    skipReason: str = ""
    command: list[str] = field(default_factory=list)
    listLine: list[str] = field(default_factory=list)  #: 출력 전체 — `<빌드>/LoadResults/logs/` 에 남긴다


def readJsonInternal(path: Path) -> dict:
    """부하 표 하나 — 없거나 깨졌으면 멈춘다(빈 표로 돌면 시나리오 · 예산이 조용히 사라진다)."""
    return json.loads(path.read_text(encoding="utf-8"))


def parseDeclaration(repositoryRoot: Path, name: str, relativePath: str, mapGameByPack: dict[str, str]) -> LoadDeclaration:
    """시나리오 파일에서 `<Load>` 를 읽습니다. 모르는 속성 · 창 없음은 ValueError(엔진 `AutomationScenario::parseLoad` 와 같은 규칙)."""
    path = repositoryRoot / relativePath
    root = ElementTree.parse(path).getroot()
    if root.tag == "Scenario":
        declaration = LoadDeclaration(name, "app", path)
        parts = Path(relativePath).parts
        declaration.resourceID = Path(*parts[1:]).as_posix() if parts[0] == "Resource" else relativePath
        bGamePack = len(parts) > 2 and parts[1] == "game"
        if bGamePack and parts[2] not in mapGameByPack:
            raise ValueError(f"{relativePath}: no Config/Game preset has _packRoot game/{parts[2]}")
        declaration.gameName = mapGameByPack[parts[2]] if bGamePack else "Empty"
    elif root.tag == "LoadScenario":
        driver = root.get("driver")
        if driver not in ("loadbot", "cluster"):
            raise ValueError(f"{relativePath}: driver must be loadbot or cluster, got '{driver}'")
        # 드라이버마다 필수 속성이 다르다 — loadbot 은 botScenario, cluster 는 test.
        requiredAttribute = "botScenario" if driver == "loadbot" else "test"
        if root.get(requiredAttribute) is None:
            raise ValueError(f"{relativePath}: driver=\"{driver}\" needs the {requiredAttribute} attribute")
        if driver == "loadbot":
            declaration = LoadDeclaration(name, driver, path, botScenario=root.attrib["botScenario"])
        else:
            declaration = LoadDeclaration(name, driver, path, testFilter=root.attrib["test"])
    else:
        raise ValueError(f"{relativePath}: root must be <Scenario> or <LoadScenario>")
    # `<Load>` 의 읽기 규칙은 `common/ScenarioFile.readLoadSpec` 한 자리다(`scenario` 명령이 같은 선언을 본다).
    spec = readLoadSpec(relativePath, root)
    declaration.durationSeconds, declaration.rampUpSeconds, declaration.network = spec.durationSeconds, spec.rampUpSeconds, spec.network
    declaration.clientCount, declaration.meshCount, declaration.moverCount = spec.clientCount, spec.meshCount, spec.moverCount
    if declaration.driver == "loadbot" and declaration.network:
        raise ValueError(f"{relativePath}: the loadbot driver has no network emulation (TCP) - remove network")
    if declaration.driver == "cluster" and declaration.clientCount <= 0:
        raise ValueError(f"{relativePath}: the cluster driver needs <Load clients> > 0")
    return declaration


class LoadCatalog:
    """`Test/Load` 의 데이터 넷 — 시나리오 · 묶음, 프로필, 예산, 기준선."""

    def __init__(self, repositoryRoot: Path) -> None:
        self.repositoryRoot = repositoryRoot
        folder = repositoryRoot / kLoadFolder
        self.suites = readJsonInternal(folder / "Suites.json")
        self.profiles = readJsonInternal(folder / "Profiles.json")["profiles"]
        self.budgets = readJsonInternal(folder / "Budgets.json")
        self.baselinePath = folder / "Baselines.json"
        # 기준선은 처음 기록하기 전에는 없다 — 없음을 빈 기계 표로 적는다(깨진 파일은 멈춘다).
        self.baselines = readJsonInternal(self.baselinePath) if self.baselinePath.is_file() else {"machines": {}}
        # 팩 폴더 → 게임 이름의 정본은 게임 프리셋의 `_packRoot` 다(폴더 이름이 게임 이름과 다른 팩이 있다 — themepark).
        self.mapGameByPack = {
            str(json.loads(presetPath.read_text(encoding="utf-8"))["_packRoot"]).removeprefix("game/"): presetPath.stem
            for presetPath in sorted((repositoryRoot / "Config/Game").glob("*.json"))
        }

    def findDeclaration(self, name: str) -> LoadDeclaration:
        mapScenario = self.suites["scenarios"]
        if name not in mapScenario:
            raise KeyError(f"unknown load scenario '{name}' ({', '.join(sorted(mapScenario))})")
        return parseDeclaration(self.repositoryRoot, name, mapScenario[name], self.mapGameByPack)

    def findProfile(self, name: str) -> HardwareProfile:
        if name not in self.profiles:
            raise KeyError(f"unknown hardware profile '{name}' ({', '.join(sorted(self.profiles))})")
        return HardwareProfile(name, self.profiles[name])

    def findBudget(self, scenario: str, profile: str) -> dict[str, float]:
        entry = self.budgets["scenarios"].get(scenario, {})   # 예산이 없는 시나리오는 보고만 한다
        return {**entry.get("*", {}), **entry.get(profile, {})}


# ------------------------------------------------------------------------------
# 드라이버
# ------------------------------------------------------------------------------
def addResourceMetricsInternal(metrics: dict[str, float], listSample: list, seconds: float) -> None:
    """밖에서 뜬 표본에서 메모리 · 핸들의 최대와 기울기(워밍업 뒤 최소제곱, 분당)를 지표로 더합니다."""
    if not listSample:
        return
    metrics["private_mb.max"] = max(sample.privateBytes for sample in listSample) / kMegabyte
    metrics["handles.max"] = float(max(sample.handleCount for sample in listSample))
    listSteady = [sample for sample in listSample if sample.seconds >= seconds * kWarmupFraction]
    if len(listSteady) >= 3:
        metrics["private_mb_per_minute"] = computeSlopePerMinute([(sample.seconds, sample.privateBytes / kMegabyte) for sample in listSteady])
        metrics["handles_per_minute"] = computeSlopePerMinute([(sample.seconds, float(sample.handleCount)) for sample in listSteady])


def readAppMetricsInternal(listLine: list[str]) -> dict[str, float]:
    """App 출력의 측정 보고에서 지표를 읽습니다(구간 표 · 벽시계 · 히치 꼬리 · 할당 · GPU 메모리 · 기동 시간)."""
    metrics: dict[str, float] = {}
    for scope, row in parseProfileTable(listLine).items():
        if scope not in kTrackedScope:
            continue
        metrics[f"{scope}.p50_us"] = float(row.p50Micro)
        metrics[f"{scope}.p99_us"] = float(row.p99Micro)
        metrics[f"{scope}.max_us"] = float(row.maxMicro)
    wall = parseProfileWall(listLine)
    if wall is not None:
        metrics["wall.frames"] = float(wall[0])
        metrics["wall.us_per_frame"] = float(wall[2])
    for line in listLine:
        if (match := _kHitchRe.search(line)) is not None:
            metrics["hitch.worst_us"] = float(match.group(1))
            metrics["hitch.fifth_worst_us"] = float(match.group(5))
            metrics["hitch.over16ms"] = float(match.group(6))
            metrics["hitch.over33ms"] = float(match.group(7))
            metrics["hitch.over100ms"] = float(match.group(8))
        elif (match := _kAllocRe.search(line)) is not None:
            metrics["alloc.per_frame"] = float(f"{match.group(1)}.{match.group(2)}")
        elif (match := _kGpuMemoryRe.search(line)) is not None:
            metrics["gpu_memory_mb"] = float(f"{match.group(1)}.{match.group(2)}")
        elif (match := kStartupRe.search(line)) is not None:
            metrics["startup_ms"] = float(match.group(1))
            if match.group(2) is not None:
                metrics["startup_file_opens"] = float(match.group(2))  # 기동이 연 파일 수 — 느린 디스크에서 기동 시간을 정한다
        elif (match := _kWorkerRe.search(line)) is not None:
            metrics["workers"] = float(match.group(1))  # 하드웨어 흉내가 워커 풀까지 줄였는지(친화도 → 워커 수)
    return metrics


def addTimingMetricsInternal(metrics: dict[str, float], seconds: float) -> None:
    """판의 전체 시간과, 기동 보고(`startup_ms`)가 있으면 기동(로딩) · 측정 구간 시간을 지표로 더합니다 — 보고가 없으면 두 칸은 비운다."""
    metrics["run_seconds"] = seconds
    startupSeconds, measureSeconds = splitRunSeconds(seconds, metrics.get("startup_ms"))
    if startupSeconds is not None and measureSeconds is not None:
        metrics["startup_seconds"] = startupSeconds
        metrics["measure_seconds"] = measureSeconds


def readScalabilityPresetInternal(repositoryRoot: Path, quality: str) -> dict[str, str]:
    """엔진 설정 스키마(`Resource/engine/settings/engine.settings.xml` 의 <Scalability>)에서 품질 프리셋이 묶는 설정 값들 — 프리셋 표를 여기 다시 적지 않는다."""
    root = ElementTree.parse(repositoryRoot / "Resource/engine/settings/engine.settings.xml").getroot()
    for scalability in root.iter("Scalability"):
        for preset in scalability.findall("Preset"):
            if preset.get("name") == quality:
                mapValue = {value.attrib["setting"]: value.attrib["value"] for value in preset.findall("Value")}
                return {scalability.attrib["setting"]: quality, **mapValue}
    raise ValueError(f"quality '{quality}' is not a preset in engine.settings.xml")


def makeAppArgumentsInternal(declaration: LoadDeclaration, profile: HardwareProfile, backend: str, gameTable: dict, settingsPath: Path) -> list[str]:
    data = profile.data
    listArgument = [f"-game={declaration.gameName}", kBackendSwitch[backend], f"-scenario={declaration.resourceID}",
                    f"-gv_userSettingsFile={settingsPath.as_posix()}"]
    listArgument += [argument for argument in gameTable["games"][declaration.gameName]["arguments"]
                     if not argument.startswith("-gv_benchAnimate")]  # 부하는 움직여야 한다(정지한 장면은 빠른 길만 잰다)
    if declaration.meshCount > 0:
        listArgument = [argument for argument in listArgument if not argument.startswith("-gv_benchMeshes")]
        listArgument.append(f"-gv_benchMeshes={declaration.meshCount}")
    if declaration.moverCount > 0:
        listArgument.append(f"-gv_benchTickMovers={declaration.moverCount}")
    # 프로필 키는 필수다 — 0 · 빈 값이 "그 제한을 걸지 않는다" 이다(Profiles.json 머리말).
    if data["width"] and data["height"]:
        listArgument += [f"-W={data['width']}", f"-H={data['height']}"]
    if data["vram_mb"]:
        listArgument.append(f"-gv_emulatedGPUMemoryMb={data['vram_mb']}")
    if data["io_latency_ms"]:
        listArgument.append(f"-gv_ioEmuReadLatencyMs={data['io_latency_ms']}")
    if data["io_kilobytes_per_second"]:
        listArgument.append(f"-gv_ioEmuReadKilobytesPerSecond={data['io_kilobytes_per_second']}")
    network = declaration.network or data["network"]
    if network:
        listArgument.append(f"-gv_netEmuPreset={network}")
    return listArgument


def runAppDriver(declaration: LoadDeclaration, profile: HardwareProfile, tree: BuildTree, backend: str, gameTable: dict,
                 qualityOverride: str = "", listExtraArgument: list[str] | None = None) -> RunMeasurement:
    measurement = RunMeasurement()
    appPath = tree.appExecutable
    if appPath is None:
        measurement.skipReason = f"no App in {tree.binDir}"
        return measurement
    if qualityOverride:
        profile = HardwareProfile(profile.name, {**profile.data, "quality": qualityOverride})
    with tempfile.TemporaryDirectory(prefix="sw_load_") as temporaryFolder:
        # 사용자 폴더를 건드리지 않는 임시 설정 파일. 품질이 정해진 프로필은 파일에 적어 기동 적용(다시 시작이 필요한 텍스처 품질 포함)으로 넣고,
        # `auto` 는 파일을 두지 않아 엔진이 흉내 낸 사양으로 고른다(첫 실행 자동 선택).
        settingsPath = Path(temporaryFolder) / "usersettings.json"
        quality = qualityOverride or profile.data["quality"]
        if quality and quality != "auto":
            settingsPath.write_text(json.dumps({"version": 1, "values": readScalabilityPresetInternal(getProjectRoot(), quality)}) + "\n", encoding="utf-8")
        listArgument = makeAppArgumentsInternal(declaration, profile, backend, gameTable, settingsPath) + list(listExtraArgument or [])
        measurement.command = [str(appPath), *listArgument]
        timeoutSeconds = declaration.durationSeconds * 2.0 + 240.0
        result = runApp(appPath, listArgument, timeoutSeconds=timeoutSeconds, sampleIntervalSeconds=max(1.0, declaration.durationSeconds / 60.0),
                        limits=profile.makeLimits())
    if result.bBackendUnusable:
        measurement.skipReason = f"{backend} is not usable on this machine"
        return measurement
    if any("is not built in this build" in line for line in result.listLine):
        measurement.skipReason = f"game {declaration.gameName} is not built in {tree.name} - configure with -DSW_GAMES={declaration.gameName} (or ALL)"
        return measurement
    if not result.bClean:
        detail = result.listErrorLine[:3] if result.listErrorLine else [f"exit {result.exitCode}, timed out={result.bTimedOut}"]
        measurement.listFailure.append(f"App did not finish cleanly: {detail}")
    measurement.listLine = result.listLine
    measurement.metrics = readAppMetricsInternal(result.listLine)
    addResourceMetricsInternal(measurement.metrics, result.listSample, result.seconds)
    addTimingMetricsInternal(measurement.metrics, result.seconds)
    if "GT.Frame.p99_us" not in measurement.metrics:
        measurement.listFailure.append("no [Profile] table in the output (Shipping drops it - measure a Release build)")
    return measurement


def runLoadBotDriver(declaration: LoadDeclaration, profile: HardwareProfile, tree: BuildTree, repositoryRoot: Path) -> RunMeasurement:
    measurement = RunMeasurement()
    # Windows 는 키트 DLL 사본과 함께 ToolBin 에 있고(작업 폴더는 Bin), 리눅스는 Bin 에 있다(Tools/OnlineLoadBot/CMakeLists.txt).
    botPath = tree.path / "ToolBin" / "OnlineLoadBot.exe" if sys.platform == "win32" else tree.binDir / "OnlineLoadBot"
    if not botPath.is_file():
        measurement.skipReason = f"no {botPath} (the online kits are off in this build)"
        return measurement
    with tempfile.TemporaryDirectory(prefix="sw_load_") as temporaryFolder:
        reportPath = Path(temporaryFolder) / "report.json"
        listArgument = [f"--scenario={(repositoryRoot / declaration.botScenario).as_posix()}", "--local-server", "--light-hash",
                        f"--report={reportPath.as_posix()}", f"--duration-seconds={max(1, int(declaration.durationSeconds))}",
                        f"--ramp-up-seconds={int(declaration.rampUpSeconds)}", "--max-error-percent=100"]
        if declaration.clientCount > 0:
            listArgument.append(f"--bots={declaration.clientCount}")
        measurement.command = [str(botPath), *listArgument]
        result = runApp(botPath, listArgument, cwd=tree.binDir, timeoutSeconds=declaration.durationSeconds * 3.0 + 120.0,
                        sampleIntervalSeconds=1.0, limits=profile.makeLimits())
        # 봇이 보고서를 남기지 못한 것은 이 판의 실패다(아래에서 기록) — 필수 표용 readJsonInternal 은 없는 파일에서 멈춰 결과 JSON 이 통째로 사라진다.
        report = json.loads(reportPath.read_text(encoding="utf-8")) if reportPath.is_file() else None
    measurement.listLine = result.listLine
    if not report:
        measurement.listFailure.append(f"OnlineLoadBot left no report (exit {result.exitCode}): {result.listErrorLine[:3]}")
        return measurement
    completedCount = 0
    errorCount = 0
    for action in report.get("_listAction", []):
        name = action["_name"]
        completedCount += int(action["_count"])
        errorCount += int(action["_errorCount"])
        measurement.metrics[f"{name}.p50_us"] = float(action["_p50Us"])
        measurement.metrics[f"{name}.p99_us"] = float(action["_p99Us"])
        measurement.metrics[f"{name}.max_us"] = float(action["_maxUs"])
    measurement.metrics["actions"] = float(completedCount)
    measurement.metrics["error_percent"] = 100.0 * errorCount / completedCount if completedCount else 100.0
    addResourceMetricsInternal(measurement.metrics, result.listSample, result.seconds)
    addTimingMetricsInternal(measurement.metrics, result.seconds)
    return measurement


def runClusterDriver(declaration: LoadDeclaration, profile: HardwareProfile, tree: BuildTree) -> RunMeasurement:
    measurement = RunMeasurement()
    testPath = tree.testBinDir / ("EngineTest.exe" if sys.platform == "win32" else "EngineTest")
    if not testPath.is_file():
        measurement.skipReason = f"no EngineTest in {tree.testBinDir} (build --target AllTests)"
        return measurement
    # 클라이언트 수는 시나리오가 정한다(parseDeclaration 이 cluster 에 clients > 0 을 요구한다). 회선이 없으면 "off" 가 테스트 쪽 낱말이다.
    environment = {"SW_LOAD_CLIENTS": str(declaration.clientCount), "SW_LOAD_RAMP_UP_SECONDS": str(declaration.rampUpSeconds),
                   "SW_LOAD_DURATION_SECONDS": str(declaration.durationSeconds), "SW_LOAD_NETWORK": declaration.network or profile.data["network"] or "off"}
    listArgument = [f"--test_filter={declaration.testFilter}", "--load_suites=only", "--host_suites=all"]
    measurement.command = [f"{key}={value}" for key, value in environment.items()] + [str(testPath), *listArgument]
    result = runApp(testPath, listArgument, cwd=tree.binDir, timeoutSeconds=declaration.durationSeconds * 20.0 + 300.0, sampleIntervalSeconds=1.0,
                    limits=profile.makeLimits(), environment=environment)
    measurement.listLine = result.listLine
    for line in result.listLine:
        if (match := _kLoadMetricRe.search(line)) is not None:
            measurement.metrics[match.group(1)] = float(match.group(2))
    if result.exitCode != 0:
        listFailed = [line.strip() for line in result.listLine if "[  FAILED  ]" in line or "Failure" in line][:5]
        measurement.listFailure.append(f"{declaration.testFilter} failed (exit {result.exitCode}): {listFailed}")
    if not measurement.metrics:
        measurement.listFailure.append("no [LoadMetric] lines in the test output")
    addResourceMetricsInternal(measurement.metrics, result.listSample, result.seconds)
    addTimingMetricsInternal(measurement.metrics, result.seconds)
    return measurement


def runOnce(catalog: LoadCatalog, declaration: LoadDeclaration, profile: HardwareProfile, tree: BuildTree, backend: str,
            qualityOverride: str = "", listExtraArgument: list[str] | None = None) -> RunMeasurement:
    if declaration.driver == "app":
        return runAppDriver(declaration, profile, tree, backend, loadGameTable(catalog.repositoryRoot), qualityOverride, listExtraArgument or [])
    if declaration.driver == "loadbot":
        return runLoadBotDriver(declaration, profile, tree, catalog.repositoryRoot)
    return runClusterDriver(declaration, profile, tree)


# ------------------------------------------------------------------------------
# 판정
# ------------------------------------------------------------------------------
def summarizeRuns(listMeasurement: list[RunMeasurement]) -> dict[str, dict[str, float]]:
    """판들의 지표마다 가운데 값 · 최소 · 최대(분산을 같이 적는다)."""
    mapValues: dict[str, list[float]] = {}
    for measurement in listMeasurement:
        for name, value in measurement.metrics.items():
            mapValues.setdefault(name, []).append(value)
    return {name: {"median": statistics.median(values), "min": min(values), "max": max(values), "runs": float(len(values))}
            for name, values in sorted(mapValues.items())}


def classifyBottleneck(metricName: str, summary: dict[str, dict[str, float]]) -> str:
    """예산을 깬 지표 하나가 어느 자원의 문제인지 가릅니다 — 같은 판의 하위 구간 비율로 본다(GT 가 RT 를, RT 가 GPU 를 기다리는지)."""

    def medianInternal(name: str) -> float:
        # 이 판이 재지 않은 구간(서버 판의 GT · RT 등)은 0 — 아래 비교는 "0 보다 크면" 만 본다.
        return summary[name]["median"] if name in summary else 0.0

    stat = metricName.rsplit(".", 1)[-1] if "." in metricName else "p99_us"
    if metricName.startswith("GPU.") or metricName == "gpu_memory_mb":
        return "GPU" if metricName.startswith("GPU.") else "GPU memory"
    if metricName.startswith("RT."):
        rtFrame = medianInternal(f"RT.Frame.{stat}")
        if rtFrame > 0 and medianInternal(f"RT.BeginFrame.{stat}") > 0.5 * rtFrame:
            return "GPU (render thread waits on the GPU fence - RT.BeginFrame)"
        return "CPU render thread"
    if metricName.startswith("GT.") or metricName.startswith("wall.") or metricName.startswith("hitch."):
        gtFrame = medianInternal(f"GT.Frame.{stat}") or medianInternal("GT.Frame.p99_us")
        if gtFrame > 0 and medianInternal(f"GT.Packet.submit.{stat}") > 0.5 * gtFrame:
            return "render thread (game thread waits in GT.Packet.submit) - " + classifyBottleneck(f"RT.Frame.{stat}", summary)
        listChild = sorted(((medianInternal(f"{scope}.{stat}"), scope) for scope in kTrackedScope if scope.startswith("GT.") and scope != "GT.Frame"), reverse=True)
        heaviest = f" - heaviest {listChild[0][1]} {listChild[0][0]:.0f} us" if listChild and listChild[0][0] > 0 else ""
        return "CPU game thread" + heaviest
    if metricName.startswith("private_mb") or metricName.startswith("alloc"):
        return "memory"
    if metricName.startswith("handles"):
        return "OS handles (leak)"
    if metricName.startswith("startup_"):
        return "IO (startup reads)"
    if metricName.startswith("tick_") or metricName in ("converged", "hash_mismatch"):
        return "server tick (cluster)"
    return "server / network (load bot)"


def judgeBudget(budget: dict[str, float], summary: dict[str, dict[str, float]]) -> tuple[list[str], list[str]]:
    """예산 — 지표의 가운데 값이 상한을 넘으면 실패(병목 갈래를 붙인다). 측정되지 않은 예산은 알림."""
    listFailure: list[str] = []
    listNote: list[str] = []
    for metricName, limit in sorted(budget.items()):
        if metricName.startswith("_"):
            continue
        entry = summary.get(metricName)
        if entry is None:
            listNote.append(f"budget {metricName} was not measured")
            continue
        bMinimum = metricName in ("converged",)  # 아래로 지켜야 하는 지표(1 = 수렴)
        bBroken = entry["median"] < limit if bMinimum else entry["median"] > limit
        if bBroken:
            listFailure.append(f"{classifyBottleneck(metricName, summary)}: {metricName} {entry['median']:.1f} "
                               f"{'<' if bMinimum else '>'} budget {limit:g} (runs {entry['min']:.1f}..{entry['max']:.1f})")
    return listFailure, listNote


def makeToleranceInternal(metricName: str) -> tuple[float, float] | None:
    """(허용 비율, 바닥) — 기준선과 견주는 지표만. max · 횟수 · 기울기는 견주지 않는다."""
    if metricName.startswith(kWaitScope) or metricName.endswith(".max_us") or metricName.startswith("hitch.") or metricName.endswith("_per_minute") or metricName in ("wall.frames", "actions"):
        return None
    for suffix, tolerance in kBaselineTolerance.items():
        if metricName.endswith(suffix):
            floor = next((value for key, value in kBaselineFloor.items() if metricName.endswith(key)), 0.0)
            return tolerance, floor
    return None


def judgeBaseline(entry: dict | None, summary: dict[str, dict[str, float]]) -> tuple[list[str], list[str]]:
    if entry is None:
        return [], ["no baseline for this machine - record one with --record"]
    listRegression: list[str] = []
    listNote: list[str] = []
    for metricName, baseline in sorted(entry.get("metrics", {}).items()):
        tolerance = makeToleranceInternal(metricName)
        actual = summary.get(metricName, {}).get("median")
        if tolerance is None or actual is None:
            continue
        expected = baseline["median"]
        limit = expected * (1.0 + tolerance[0]) + tolerance[1]
        if actual > limit:
            listRegression.append(f"{classifyBottleneck(metricName, summary)}: {metricName} {actual:.1f} > {limit:.1f} (baseline {expected:.1f})")
        elif actual < expected * (1.0 - tolerance[0]) - tolerance[1]:
            listNote.append(f"{metricName} faster {actual:.1f} (baseline {expected:.1f}) - re-record to lock it in")
    return listRegression, listNote


def makeMachineKey() -> str:
    return f"{platform.node()}|{platform.processor() or platform.machine()}"


def readCommitInternal(repositoryRoot: Path) -> str:
    # runProcess 는 OSError 를 던지지 않고 종료 코드로 말한다 — 실패를 빈 커밋으로 적지 않는다.
    result = runProcess(["git", "rev-parse", "--short", "HEAD"], cwd=repositoryRoot)
    if not result.bSucceeded:
        raise RuntimeError(f"[Load] 커밋을 읽지 못했습니다: {result.stderr.strip()}")
    return result.stdout.strip()


# ------------------------------------------------------------------------------
# 한 조합(시나리오 × 프로필)
# ------------------------------------------------------------------------------
def runCombination(catalog: LoadCatalog, scenarioName: str, profileName: str, tree: BuildTree, args: argparse.Namespace,
                   qualityOverride: str = "") -> dict:
    """시나리오 × 프로필 하나를 N 판 돌려 판정한 결과(JSON 으로 남길 사전)."""
    declaration = catalog.findDeclaration(scenarioName)
    profile = catalog.findProfile(profileName)
    buildType = tree.readCacheValue("CMAKE_BUILD_TYPE") or ""
    bJudged = buildType == "Release" or args.allow_debug
    backend = args.backend if declaration.driver == "app" else "-"
    label = f"{scenarioName} x {profileName}{' (emulated)' if profile.bEmulated else ''}{f' quality={qualityOverride}' if qualityOverride else ''}"
    listMeasurement: list[RunMeasurement] = []
    for runIndex in range(max(1, args.runs)):
        measurement = runOnce(catalog, declaration, profile, tree, args.backend, qualityOverride, args.app_arg)
        if measurement.skipReason:
            print(f"[Load] SKIP {label}: {measurement.skipReason}", flush=True)
            return {"scenario": scenarioName, "profile": profileName, "verdict": "skip", "skip_reason": measurement.skipReason}
        listMeasurement.append(measurement)
        logFolder = tree.path / "LoadResults" / "logs"
        logFolder.mkdir(parents=True, exist_ok=True)
        (logFolder / f"{scenarioName}_{profileName}{f'_{qualityOverride}' if qualityOverride else ''}_{runIndex + 1}.log").write_text(
            "\n".join([" ".join(measurement.command), *measurement.listLine]) + "\n", encoding="utf-8")
        keyLine = ", ".join(f"{name} {measurement.metrics[name]:.0f}" for name in ("GT.Frame.p99_us", "RT.Frame.p99_us", "tick_p99_us", "error_percent")
                            if name in measurement.metrics)
        print(f"[Load] {label} run {runIndex + 1}/{args.runs}: {keyLine}{' FAIL ' + '; '.join(measurement.listFailure) if measurement.listFailure else ''}", flush=True)

    summary = summarizeRuns(listMeasurement)
    listFailure = [failure for measurement in listMeasurement for failure in measurement.listFailure]
    budget = catalog.findBudget(scenarioName, profileName)
    budgetFailure, budgetNote = judgeBudget(budget, summary)
    machineKey = makeMachineKey()
    baselineKey = f"{scenarioName}|{profileName}|{backend}{f'|{qualityOverride}' if qualityOverride else ''}"
    machineEntry = catalog.baselines["machines"].setdefault(machineKey, {})
    baselineFailure, baselineNote = judgeBaseline(machineEntry.get(baselineKey), summary)
    if not bJudged:
        budgetNote.append(f"{buildType} build - budgets and baselines are reported, not judged (Release only)")
    listVerdictFailure = listFailure + (budgetFailure + baselineFailure if bJudged else [])
    result = {
        "scenario": scenarioName, "profile": profileName, "emulated": profile.bEmulated, "driver": declaration.driver, "backend": backend,
        "quality": qualityOverride or profile.data["quality"], "build": tree.name, "build_type": buildType, "judged": bJudged,
        "machine": machineKey, "commit": readCommitInternal(catalog.repositoryRoot), "time": datetime.datetime.now().isoformat(timespec="seconds"),
        "runs": len(listMeasurement), "command": listMeasurement[0].command, "metrics": summary, "budget": budget,
        "failures": listFailure, "budget_failures": budgetFailure, "baseline_regressions": baselineFailure, "notes": budgetNote + baselineNote,
        "verdict": "fail" if listVerdictFailure else "pass",
    }
    if args.record and not listFailure:
        keep = {name: values for name, values in summary.items() if makeToleranceInternal(name) is not None or name in budget}
        machineEntry[baselineKey] = {"recorded": result["time"], "commit": result["commit"], "build": tree.name, "runs": len(listMeasurement),
                                     "emulated": profile.bEmulated, "metrics": keep}
    for line in budgetNote + baselineNote:
        print(f"[Load] note {label}: {line}", flush=True)
    for line in listVerdictFailure:
        print(f"[Load] FAIL {label}: {line}", flush=True)
    print(f"[Load] {'PASS' if not listVerdictFailure else 'FAIL'} {label}", flush=True)
    return result


def writeResultsInternal(tree: BuildTree, listResult: list[dict], outPath: Path | None) -> Path:
    """결과를 `<빌드>/LoadResults/<시각>.json` 에 쓰고 조합마다 한 줄을 `trend.jsonl` 에 더합니다."""
    folder = tree.path / "LoadResults"
    folder.mkdir(parents=True, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    path = outPath or folder / f"{stamp}.json"
    path.write_text(json.dumps({"results": listResult}, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    with (folder / "trend.jsonl").open("a", encoding="utf-8") as trend:
        for result in listResult:
            if result.get("verdict") == "skip":
                continue
            compact = {key: result[key] for key in ("time", "commit", "scenario", "profile", "emulated", "backend", "quality", "verdict")}
            compact["metrics"] = {name: values["median"] for name, values in result["metrics"].items()}
            trend.write(json.dumps(compact, ensure_ascii=False) + "\n")
    print(f"[Load] results {path}", flush=True)
    return path


def saveBaselinesInternal(catalog: LoadCatalog) -> None:
    catalog.baselinePath.write_text(json.dumps(catalog.baselines, indent=2, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"[Load] baselines -> {catalog.baselinePath.relative_to(catalog.repositoryRoot)}", flush=True)


def printTableInternal(listResult: list[dict]) -> None:
    """결과 표 — 가운데 값(최소..최대)."""
    listColumn = ("GT.Frame.p99_us", "RT.Frame.p99_us", "GPU.Frame.p99_us", "hitch.worst_us", "private_mb.max", "tick_p99_us", "error_percent")
    listSecondColumn = ("run_seconds", "startup_seconds", "measure_seconds")  # 시간 칸 — 초 단위라 소수 한 자리
    print("[Load] scenario x profile | verdict | " + " | ".join(listColumn + listSecondColumn), flush=True)
    for result in listResult:
        if result.get("verdict") == "skip":
            continue
        cells = []
        for column in listColumn:
            entry = result["metrics"].get(column)
            cells.append(f"{entry['median']:.0f} ({entry['min']:.0f}..{entry['max']:.0f})" if entry else "-")
        for column in listSecondColumn:
            entry = result["metrics"].get(column)
            cells.append(f"{entry['median']:.1f}" if entry else "-")
        print(f"[Load] {result['scenario']} x {result['profile']}{'*' if result['emulated'] else ''} | {result['verdict']} | " + " | ".join(cells), flush=True)


# ------------------------------------------------------------------------------
# 명령
# ------------------------------------------------------------------------------
def addRunArgumentsInternal(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--preset", default="Ninja-Release", help="빌드 프리셋(기본 Ninja-Release — 판정은 Release 만)")
    parser.add_argument("--backend", default="dx12", choices=list(kBackendSwitch), help="App 드라이버의 백엔드")
    parser.add_argument("--runs", type=int, default=3, help="조합마다 돌리는 횟수(가운데 값과 분산을 적는다)")
    parser.add_argument("--record", action="store_true", help="이 기계의 기준선을 남긴다(Test/Load/Baselines.json)")
    parser.add_argument("--allow-debug", action="store_true", help="Release 가 아닌 빌드도 판정한다(숫자는 의미가 적다)")
    parser.add_argument("--out", type=Path, default=None, help="결과 JSON 경로(기본 <빌드>/LoadResults/<시각>.json)")
    parser.add_argument("--app-arg", action="append", default=[], help="App 에 더 넘길 인자(진단용, 여러 번 — 예: --app-arg=-gv_ioLogReads=1)")


def commandList(catalog: LoadCatalog) -> int:
    for name, relativePath in catalog.suites["scenarios"].items():
        declaration = catalog.findDeclaration(name)
        print(f"scenario {name:<12} {declaration.driver:<8} {relativePath}  (duration {declaration.durationSeconds:g} s, clients {declaration.clientCount}, "
              f"meshes {declaration.meshCount}, network '{declaration.network}')")
    for name, data in catalog.profiles.items():
        print(f"profile  {name:<12} {'emulated' if data['emulated'] else 'native  '} {data['description']}")
    for name, listPair in catalog.suites["suites"].items():
        print(f"suite    {name:<12} {' '.join(f'{scenario}x{profile}' for scenario, profile in listPair)}")
    return ExitCode.Passed


def commandRun(catalog: LoadCatalog, listPair: list[tuple[str, str]], args: argparse.Namespace) -> int:
    tree = BuildTree.fromPreset(args.preset, catalog.repositoryRoot)
    listResult = [runCombination(catalog, scenario, profile, tree, args) for scenario, profile in listPair]
    printTableInternal(listResult)
    writeResultsInternal(tree, listResult, args.out)
    if args.record:
        saveBaselinesInternal(catalog)
    if all(result["verdict"] == "skip" for result in listResult):
        return ExitCode.Skipped
    return 1 if any(result["verdict"] == "fail" for result in listResult) else 0


def commandScale(catalog: LoadCatalog, scenarioName: str, profileName: str, args: argparse.Namespace) -> int:
    """품질 단계를 위에서부터 내려가며 예산을 지키는 첫 단계를 찾는다(언리얼 Scalability 자동 선택 · 유니티 Quality 를 측정으로 고르는 자리)."""
    tree = BuildTree.fromPreset(args.preset, catalog.repositoryRoot)
    args.record = False
    listResult = []
    chosen = ""
    for quality in ("ultra", "high", "medium", "low"):
        result = runCombination(catalog, scenarioName, profileName, tree, args, qualityOverride=quality)
        listResult.append(result)
        if result["verdict"] == "skip":
            break
        if not result["budget_failures"] and not result["failures"]:
            chosen = quality
            break
    printTableInternal(listResult)
    writeResultsInternal(tree, listResult, args.out)
    print(f"[Load] scale {scenarioName} x {profileName}: {'highest quality within budget = ' + chosen if chosen else 'no quality level meets the budget'}", flush=True)
    return 0 if chosen else 1


def commandTrend(tree: BuildTree, scenarioName: str | None, metricName: str) -> int:
    path = tree.path / "LoadResults" / "trend.jsonl"
    if not path.is_file():
        print(f"[Load] no {path}", flush=True)
        return ExitCode.Violation
    for line in path.read_text(encoding="utf-8").splitlines():
        entry = json.loads(line)
        if scenarioName and entry["scenario"] != scenarioName:
            continue
        value = entry["metrics"].get(metricName)
        print(f"{entry['time']} {entry['commit']:<10} {entry['scenario']} x {entry['profile']:<8} {entry['verdict']:<4} {metricName} "
              f"{'-' if value is None else f'{value:.1f}'}")
    return ExitCode.Passed


def main(listArgument: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="부하 시나리오 × 하드웨어 프로필을 Release 로 재고 예산 · 기준선과 견준다")
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("list", help="시나리오 · 프로필 · 묶음")
    runParser = subparsers.add_parser("run", help="시나리오 하나를 프로필들로")
    runParser.add_argument("scenario")
    runParser.add_argument("--profile", nargs="+", default=["Native"])
    addRunArgumentsInternal(runParser)
    suiteParser = subparsers.add_parser("suite", help="묶음(Test/Load/Suites.json) 전체")
    suiteParser.add_argument("suite")
    addRunArgumentsInternal(suiteParser)
    scaleParser = subparsers.add_parser("scale", help="예산을 지키는 가장 높은 품질 단계")
    scaleParser.add_argument("scenario")
    scaleParser.add_argument("--profile", required=True)
    addRunArgumentsInternal(scaleParser)
    trendParser = subparsers.add_parser("trend", help="남긴 결과의 추세")
    trendParser.add_argument("scenario", nargs="?")
    trendParser.add_argument("--metric", default="GT.Frame.p99_us")
    trendParser.add_argument("--preset", default="Ninja-Release")
    args = parser.parse_args(listArgument)

    catalog = LoadCatalog(getProjectRoot())
    try:
        if args.command == "list":
            return commandList(catalog)
        if args.command == "run":
            return commandRun(catalog, [(args.scenario, profile) for profile in args.profile], args)
        if args.command == "suite":
            listPair = catalog.suites["suites"].get(args.suite)
            if listPair is None:
                print(f"[Load] unknown suite '{args.suite}'", file=sys.stderr)
                return ExitCode.CheckInvalid
            return commandRun(catalog, [(scenario, profile) for scenario, profile in listPair], args)
        if args.command == "scale":
            return commandScale(catalog, args.scenario, args.profile, args)
        return commandTrend(BuildTree.fromPreset(args.preset, catalog.repositoryRoot), args.scenario, args.metric)
    except (KeyError, ValueError, ElementTree.ParseError) as error:
        print(f"[Load] {error}", file=sys.stderr)
        return ExitCode.CheckInvalid


if __name__ == "__main__":
    sys.exit(main())
