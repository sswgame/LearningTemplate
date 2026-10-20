#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/PreCommitLint.py

Git pre-commit 훅에서 호출되어 staged 파일에 게이트·픽서·포맷 검사를 돌립니다.
검사에 실패하면 커밋을 중단시킵니다.

**게이트 목록을 들지 않는다.** `gate/` 폴더를 훑고, 무엇이 staged 되었을 때 도는지는 게이트가
`LintGate.preCommitPattern` 으로 스스로 답한다. 게이트를 이름으로 import 하면 새 게이트가 빠지고,
C++ 파일이 staged 됐을 때만 돌리면 **`.cmake` 나 `.py` 만 커밋할 때 아무 게이트도 돌지 않는다.**

**병합 커밋은 새 내용만 파일 단위로 본다.** 병합 중(`MERGE_HEAD` 가 있다)이면 병합으로 바뀐 파일이 전부 staged 로 잡히는데,
그 대부분은 한쪽 부모와 바이트가 같다 — 그 부모 커밋을 만들 때 이 훅이 같은 내용을 이미 검사했다. 그래서 파일 단위 검사
(`--files` · 위치 인자를 받는 게이트, 픽서, clang-format)에는 **staged 내용이 어느 부모의 같은 경로 blob 과도 다른 파일**만
넘긴다(`StagedScope.selectFileScopedStaged`) — 충돌 해결 · 자동 병합으로 내용이 새로 생긴 파일이다. 파일 인자 없이 트리 전체를 보는
게이트와 셰이더 쿠킹 검증은 staged 전체를 기준으로 그대로 돈다. 두 부모에서 따로 온 파일끼리의 관계(헤더는 한쪽, 짝 `.cpp` 는
다른 쪽에서 온 생성자 초기화 순서 같은 것)는 파일 단위 검사가 원래 못 보는 것이라, 병합 뒤 `ctest -L lint`(CI 도 같다)가 트리
전체로 다시 본다.

**트리 전체 게이트는 하위 프로세스로 먼저 띄운다**(`runGatesInternal`). staged 파일 수와 상관없이 저장소를 훑는 게이트가 차례로 돌면
C++ 파일 하나짜리 커밋도 그 합만큼 걸린다. 파일 단위 게이트는 이 프로세스에서 차례로 돈다(기동이 게이트보다 비싸다).
찍히는 순서는 게이트 순서 그대로다.

**통과한 것은 찍지 않는다.** 에이전트와 세션이 훅 출력을 매번 입력으로 다시 읽는다. 기본 출력은 통과 · 건너뜀 게이트의 줄을 모두 접어 한 줄
요약(`formatGateSummary`)으로 보이고, 실패한 게이트는 머리말과 상세를 그대로 찍는다. 이 프로세스에서 도는 게이트도 출력을 붙잡아 두었다가
실패일 때만 찍는다. 전부 보려면 `--verbose`(인자 — 환경 변수 같은 숨은 스위치는 없다). 훅이 부르는 `Scripts lint` 는 요약이 기본이다.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import contextlib
import io
import os
import re
import sys
import time
from dataclasses import dataclass, field
from fnmatch import fnmatch
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from lint.LintCatalog import LintScript, discoverLintScripts
from lint.StagedScope import selectFileScopedStaged
from lint.fixer import FormatBranchBraces
from lint.fixer import FormatForwardDeclarations
from common import (
    findAppExecutable,
    findShaderCompileFailures,
    getAllStagedFiles,
    getProjectRoot,
    getCpuCount,
    getStagedCppFiles,
    runClangFormatBatch,
    runGit,
    runProcess,
    runShaderCook,
)


def listMatchingStagedInternal(listStaged: list[Path], projectRoot: Path,
                               listPattern: tuple[str, ...]) -> list[Path]:
    """패턴에 맞는 staged 파일만 고릅니다. 패턴이 비어 있으면 전부 (= 항상 도는 게이트)."""
    if not listPattern:
        return list(listStaged)

    listMatched: list[Path] = []
    for path in listStaged:
        try:
            relPath = path.relative_to(projectRoot).as_posix()
        except ValueError:
            relPath = path.as_posix()

        if any(fnmatch(relPath, pattern) for pattern in listPattern):
            listMatched.append(path)

    return listMatched


def _relativeInternal(path: Path, projectRoot: Path) -> str:
    try:
        return path.relative_to(projectRoot).as_posix()
    except ValueError:
        return path.as_posix()


class StagedContentProbe:
    """
    staged 파일의 **내용**을 묻는다 — `LintGate.preCommitContentPattern`(HEAD · staged 파일 전체) · `preCommitChangedLinePattern`(바뀐 줄).
    git 이 찾는다: 파일 전체는 `git grep -P` 를 HEAD 와 인덱스에 한 번씩, 바뀐 줄은 `git diff --cached -U0` 한 번(파일 수와 상관없다).
    git 이 답하지 못하면 "맞는다" 고 답한다 — 좁히는 쪽이 아니라 도는 쪽으로 틀린다.
    """

    kChunkSize = 200

    def __init__(self, projectRoot: Path) -> None:
        self._root = projectRoot
        self._mapChangedLine: dict[str, str] | None = None

    def hasContentMatch(self, listRelative: list[str], pattern: str) -> bool:
        """`listRelative` 중 HEAD 내용이나 staged 내용이 `pattern`(PCRE) 에 맞는 파일이 있는가."""
        for start in range(0, len(listRelative), self.kChunkSize):
            listChunk = listRelative[start:start + self.kChunkSize]
            for listRevision in ([("HEAD",)], [("--cached",)]):
                result = runGit(["--literal-pathspecs", "grep", "-l", "-P", "-e", pattern, *listRevision[0], "--", *listChunk], cwd=self._root)
                if result.returnCode == 0 or result.returnCode > 1:
                    return True      # 맞는다 — 또는 git 이 답하지 못했다(조건 없이 돈다)
        return False

    def hasChangedLineMatch(self, listRelative: list[str], pattern: str) -> bool:
        """`listRelative` 중 바뀐 줄(+ · -)이 `pattern` 에 맞는 파일이 있는가."""
        if self._mapChangedLine is None:
            result = runGit(["diff", "--cached", "-U0", "--no-color", "--no-ext-diff", "--no-renames"], cwd=self._root)
            if result.returnCode != 0:
                return True
            mapLine: dict[str, list[str]] = {}
            current = ""
            for line in result.stdout.splitlines():
                if line.startswith("diff --git "):
                    current = line.rsplit(" b/", 1)[-1]
                    mapLine[current] = []
                elif line.startswith(("+", "-")) and not line.startswith(("+++", "---")) and current:
                    mapLine[current].append(line[1:])
            self._mapChangedLine = {relative: "\n".join(listLine) for relative, listLine in mapLine.items()}
        regex = re.compile(pattern)
        return any(regex.search(self._mapChangedLine.get(relative, "")) is not None for relative in listRelative)

    def isTriggered(self, listRelative: list[str], listContent: tuple[tuple[str, str], ...], listChanged: tuple[tuple[str, str], ...]) -> bool:
        """어느 staged 파일이든 내용 규칙에 맞으면 True."""
        for listRule, bChangedOnly in ((listContent, False), (listChanged, True)):
            for glob, pattern in listRule:
                listTarget = [relative for relative in listRelative if fnmatch(relative, glob)]
                if listTarget and (self.hasChangedLineMatch if bChangedOnly else self.hasContentMatch)(listTarget, pattern):
                    return True
        return False


@dataclass(frozen=True)
class GateRunPlan:
    """
    훅이 게이트 하나를 어떻게 다룰지.

    - `script`     : 게이트 파일(`LintCatalog`)
    - `skipReason` : 비어 있지 않으면 돌리지 않는다(그 이유를 찍는다)
    - `listArgument`: 돌릴 때 넘길 인자(`--root` · 파일 인자 게이트면 `--files …`)
    """

    script: LintScript
    skipReason: str = ""
    listArgument: list[str] = field(default_factory=list)

    @property
    def bBackground(self) -> bool:
        """
        하위 프로세스로 먼저 띄우는가 — 돌리는 트리 전체 게이트(`preCommitFileArgument == ""`)다. staged 파일 수와 상관없이 저장소를
        훑으므로 다른 게이트와 겹친다. 파일 단위 게이트는 대개 수십 ms 라 프로세스 기동(0.1~0.3 s)이 더 비싸 이 프로세스에서 돈다.
        """
        return not self.skipReason and self.script.gateClass.preCommitFileArgument == ""


def selectGatesForStaged(projectRoot: Path, listStaged: list[Path], listFileScoped: list[Path],
                         listScript: list[LintScript] | None = None, probe: StagedContentProbe | None = None) -> list[GateRunPlan]:
    """
    staged 파일 목록 → 게이트마다의 계획(건너뜀 이유 또는 넘길 인자). 아무것도 돌리거나 찍지 않는다 — 판정만(테스트가 이것을 본다).

    무엇이 staged 되었을 때 도는지, staged 부분집합을 어떻게 받는지는 게이트가 스스로 선언한다
    (`LintGate.preCommitPattern` · `preCommitFileArgument`). 여기에는 게이트 이름이 없다.

    파일을 인자로 받는 게이트는 `listFileScoped`(병합 커밋이면 새 내용인 파일만)에서, 트리 전체 게이트는 `listStaged` 에서 고른다.
    파일 인자 게이트에 넘길 파일이 하나도 없으면 돌리지 않는다 — 빈 `--files` 는 "전체를 훑어라" 로 읽힌다.
    게이트 클래스가 없는 파일은 계획에 넣지 않는다.
    내용 규칙(`preCommitContentPattern` · `preCommitChangedLinePattern`)은 `probe` 가 답한다 — `probe` 가 없으면(테스트) 글롭에 맞는 파일이 있는 것으로 본다.
    """
    listPlan: list[GateRunPlan] = []
    for script in discoverLintScripts("gate") if listScript is None else listScript:
        gateClass = script.gateClass
        if gateClass is None:
            continue

        if gateClass.preCommitSkipReason:
            listPlan.append(GateRunPlan(script, skipReason=gateClass.preCommitSkipReason))
            continue

        bContentRule = bool(gateClass.preCommitContentPattern or gateClass.preCommitChangedLinePattern)
        listMatched = listMatchingStagedInternal(listStaged, projectRoot, gateClass.preCommitPattern) if gateClass.preCommitPattern or not bContentRule else []
        if bContentRule and not listMatched:
            listRelative = [_relativeInternal(path, projectRoot) for path in listStaged]
            if probe is None:
                bTriggered = any(fnmatch(relative, glob) for relative in listRelative
                                 for glob, _ in (*gateClass.preCommitContentPattern, *gateClass.preCommitChangedLinePattern))
            else:
                bTriggered = probe.isTriggered(listRelative, gateClass.preCommitContentPattern, gateClass.preCommitChangedLinePattern)
            if not bTriggered:
                listPlan.append(GateRunPlan(script, skipReason="해당 파일 변경 없음 (내용 규칙에 맞는 변경 없음)"))
                continue
        elif gateClass.preCommitPattern and not listMatched:
            listPlan.append(GateRunPlan(script, skipReason="해당 파일 변경 없음"))
            continue

        if gateClass.preCommitFileArgument:
            listMatched = listMatchingStagedInternal(listFileScoped, projectRoot, gateClass.preCommitPattern)
            if not listMatched:
                listPlan.append(GateRunPlan(script, skipReason="병합 커밋: 부모와 내용이 다른 해당 파일 없음"))
                continue

        listArgument = ["--root", str(projectRoot)]
        if gateClass.preCommitFileArgument == "--files":
            listArgument += ["--files", *(str(path) for path in listMatched)]
        listPlan.append(GateRunPlan(script, listArgument=listArgument))
    return listPlan


kGatePassed = "passed"
kGateSkipped = "skipped"
kGateFailed = "failed"


@dataclass(frozen=True)
class GateOutcome:
    """게이트 하나의 결과 — 요약 한 줄(`formatGateSummary`)의 재료."""

    name: str
    status: str
    seconds: float = 0.0


def formatGateSummary(listOutcome: list[GateOutcome]) -> str:
    """게이트 결과들 → 한 줄 요약. 통과 · 건너뜀 · 실패 수와 가장 오래 걸린 게이트(건너뛴 것은 뺀다)."""
    passedCount = sum(1 for outcome in listOutcome if outcome.status == kGatePassed)
    skippedCount = sum(1 for outcome in listOutcome if outcome.status == kGateSkipped)
    failedCount = sum(1 for outcome in listOutcome if outcome.status == kGateFailed)
    listRan = [outcome for outcome in listOutcome if outcome.status != kGateSkipped]
    slowest = max(listRan, key=lambda outcome: outcome.seconds) if listRan else None
    tail = f" (가장 긴 것 {slowest.name} {slowest.seconds:.2f} s)" if slowest else ""
    return f"[PreCommitLint] 게이트 {len(listOutcome)}개: 통과 {passedCount} · 건너뜀 {skippedCount} · 실패 {failedCount}{tail}"


def runGateInProcessInternal(plan: GateRunPlan, bVerbose: bool) -> tuple[int, str, float]:
    """
    게이트를 이 프로세스에서 돌려 (종료 코드, 붙잡은 출력, 걸린 초). `bVerbose` 면 출력을 붙잡지 않고 그대로 흘린다(출력은 빈 문자열).
    붙잡는 동안 예외가 나면 그때까지의 출력을 먼저 찍고 예외를 다시 던진다.
    """
    start = time.perf_counter()
    if bVerbose:
        returnCode = plan.script.gateClass.run(plan.listArgument)
        return returnCode, "", time.perf_counter() - start
    buffer = io.StringIO()
    try:
        with contextlib.redirect_stdout(buffer), contextlib.redirect_stderr(buffer):
            returnCode = plan.script.gateClass.run(plan.listArgument)
    except BaseException:
        sys.stdout.write(buffer.getvalue())
        raise
    return returnCode, buffer.getvalue(), time.perf_counter() - start


def runGateProcessInternal(plan: GateRunPlan, projectRoot: Path) -> tuple[int, str, float]:
    """
    게이트를 하위 프로세스로 돌려 (종료 코드, 출력, 걸린 초)를 돌려준다. 출력은 UTF-8, stderr 를 stdout 에 섞고 버퍼 없이 써서 — 위반 머리말(stderr)과
    목록(stdout)이 이 프로세스에서 돌 때와 같은 순서로 남는다.
    """
    environment = dict(os.environ, PYTHONIOENCODING="utf-8", PYTHONUNBUFFERED="1")
    start = time.perf_counter()
    result = runProcess([sys.executable, plan.script.scriptPath, *plan.listArgument], cwd=projectRoot, env=environment, bMergeStderr=True)
    return result.returnCode, result.stdout, time.perf_counter() - start


def runGatesInternal(projectRoot: Path, listStaged: list[Path], listFileScoped: list[Path], bVerbose: bool = False) -> bool:
    """
    `gate/` 에 있는 게이트를 **전부** 훑어 계획대로 돌립니다 — 목록이 아니라 자리가 규칙이다(`selectGatesForStaged`).

    **트리 전체 게이트(`GateRunPlan.bBackground`)는 하위 프로세스로 먼저 띄운다.** 파일 단위 게이트는 이 프로세스에서 차례로 돌고,
    하위 프로세스의 출력은 그 게이트의 차례에 붙인다 — 찍히는 순서는 게이트 순서 그대로다.
    """
    listScript = discoverLintScripts("gate")
    mapIndex = {script.name: index for index, script in enumerate(listScript, start=1)}
    listPlan = selectGatesForStaged(projectRoot, listStaged, listFileScoped, listScript, StagedContentProbe(projectRoot))
    listBackground = [plan for plan in listPlan if plan.bBackground]
    bFailed = False
    # 게이트마다의 결과 — 끝에 한 줄 요약으로 접는다(가장 긴 게이트가 훅의 바닥이다).
    listOutcome: list[GateOutcome] = []

    # 하위 프로세스를 기다리는 일이라 스레드로 띄운다(기다리는 동안 GIL 을 놓는다). 코어 수보다 많이 띄우면 경합만 는다.
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(len(listBackground), getCpuCount()))) as executor:
        mapFuture = {plan.script.name: executor.submit(runGateProcessInternal, plan, projectRoot) for plan in listBackground}
        for plan in listPlan:
            head = f"[{mapIndex[plan.script.name]}/{len(listScript)}] {plan.script.name}"
            if plan.skipReason:
                listOutcome.append(GateOutcome(plan.script.name, kGateSkipped))
                if bVerbose:
                    print(f"\n{head} ... 건너뜀 ({plan.skipReason})")
                continue
            future = mapFuture.get(plan.script.name)
            if future is None:
                if bVerbose:
                    print(f"\n{head} ...", flush=True)
                returnCode, output, seconds = runGateInProcessInternal(plan, bVerbose)
            else:
                returnCode, output, seconds = future.result()
            listOutcome.append(GateOutcome(plan.script.name, kGatePassed if returnCode == 0 else kGateFailed, seconds))
            if bVerbose or returnCode != 0:
                # 실패한 게이트는 머리말과 상세를 그대로 보인다. 이 프로세스에서 돈 게이트의 verbose 출력은 이미 흘렀다(output 이 비어 있다).
                if not bVerbose:
                    print(f"\n{head} ... 실패")
                sys.stdout.write(output)
            sys.stdout.flush()
            sys.stderr.flush()
            if returnCode != 0:
                bFailed = True
    if listOutcome:
        print(("\n" if bVerbose else "") + formatGateSummary(listOutcome))
    return bFailed


def checkStagedShadersInternal(projectRoot: Path, stagedFiles: list[Path], bVerbose: bool = False) -> bool:
    """Staged HLSL/HLSLI 파일이 있을 경우 전 RHI 백엔드(쿠킹 표의 넷: DX11 · DX12 · Vulkan · OpenGL) 컴파일 검증 및 바이너리 갱신을 확인합니다."""
    stagedShaders = [f for f in stagedFiles if f.suffix.lower() in (".hlsl", ".hlsli")]
    if not stagedShaders:
        if bVerbose:
            print("  - Staged 셰이더 없음 (HLSL 검증 생략)")
        return True

    if bVerbose:
        print(f"  - {len(stagedShaders)}개의 Staged 셰이더 소스 발견. 전 RHI 백엔드(쿠킹 표의 넷: DX11 · DX12 · Vulkan · OpenGL) 컴파일 검증 진행...")
        for s in stagedShaders:
            try:
                rel = s.relative_to(projectRoot)
            except ValueError:
                rel = s
            print(f"    * {rel}")

    # 후보 목록은 `common.AppBinary` 한 곳이다 — 여기서 따로 들면 프리셋 하나를 빼먹어(예: Ninja-Shipping)
    # 그 프리셋만 빌드해 둔 사람은 이 검증을 통째로 건너뛴다.
    appExe = findAppExecutable(projectRoot)
    if appExe is None:
        print("  [Warning] App 을 찾을 수 없어 셰이더를 굽지 못했습니다. 낡은 바이너리는 CheckShaderStamps 게이트가 스탬프로 막습니다(빌드 후 --cook-shaders).")
        return True

    res = runShaderCook(appExe, cwd=projectRoot, bCapture=True)

    if res.returncode != 0:
        print(f"  [Error] App.exe --cook-shaders 실행 실패 (종료 코드: {res.returncode})")
        if res.stdout:
            print(res.stdout)
        if res.stderr:
            print(res.stderr)
        return False

    listCompileError = findShaderCompileFailures(res.stdout or "")
    for line in listCompileError:
        print(f"  [Error] {line}")

    if listCompileError:
        print("  [Error] 하나 이상의 RHI 백엔드에서 셰이더 컴파일 실패가 발생했습니다. HLSL 문법을 수정하세요.")
        return False

    print(f"  - 모든 RHI 백엔드 컴파일 검증 통과 (셰이더 {len(stagedShaders)}개)." if not bVerbose else "  - 모든 RHI 백엔드 컴파일 검증 통과.")

    gitCmd = runGit(["status", "--porcelain", "Resource/engine/shaders/bin/", "Resource/common/shaders/bin/"], cwd=projectRoot)
    if gitCmd.returnCode == 0 and gitCmd.stdout.strip():
        added = runGit(["add", "Resource/engine/shaders/bin/", "Resource/common/shaders/bin/"], cwd=projectRoot)
        if not added.bSucceeded:
            print(f"  [Error] 갱신된 셰이더 바이너리를 stage 하지 못했습니다: {added.stderr.strip()}")
            return False
        print("  - 갱신된 RHI별 바이너리(.dxil · .spv · .dxbc)를 자동으로 Git Stage에 추가했습니다.")

    return True


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Git staged 파일에 커밋 전 검사(게이트 · 픽서 · clang-format · 셰이더)를 돌린다")
    parser.add_argument("--verbose", dest="bVerbose", action="store_true",
                        help="통과 · 건너뜀 게이트의 줄까지 전부 찍는다(기본: 실패한 게이트만 상세, 나머지는 한 줄 요약)")
    bVerbose = parser.parse_args(argv).bVerbose

    projectRoot = getProjectRoot()
    allStagedFiles = getAllStagedFiles(projectRoot)

    if not allStagedFiles:
        return 0

    print(f"[PreCommitLint] {len(allStagedFiles)}개의 Staged 파일에 대해 검사를 시작합니다.")
    hasErrors = False

    # --- 게이트 — `gate/` 폴더가 목록이다 --------------------------------------
    #
    # 폴더를 훑고, 무엇이 staged 되었을 때 도는지는 게이트가 스스로 답한다(모듈 독스트링 참고).
    listFileScoped = selectFileScopedStaged(projectRoot, allStagedFiles)
    if runGatesInternal(projectRoot, allStagedFiles, listFileScoped, bVerbose):
        hasErrors = True

    # --- staged HLSL 이 있으면 전 백엔드 컴파일 검증 ----------------------------
    if bVerbose:
        print("\n[셰이더] Staged 셰이더 전 RHI 백엔드(쿠킹 표의 넷: DX11 · DX12 · Vulkan · OpenGL) 컴파일 검증...")
    if not checkStagedShadersInternal(projectRoot, allStagedFiles, bVerbose):
        hasErrors = True

    # --- 픽서 — 게이트가 아니라 고쳐 주는 쪽이라 `--check` 로만 부른다 -----------
    setFileScoped = set(listFileScoped)
    stagedCppFiles = [path for path in getStagedCppFiles(projectRoot) if path in setFileScoped]
    if stagedCppFiles:
        if bVerbose:
            print("\n[픽서] 전방 선언 순서 · 분기 중괄호 검사...")
        listFixer = (
            (FormatForwardDeclarations.FormatForwardDeclarationsFixer(), "전방 선언"),
            (FormatBranchBraces.FormatBranchBracesFixer(), "분기 중괄호"),
        )
        for filePath in stagedCppFiles:
            for fixer, label in listFixer:
                try:
                    listViolation = fixer.processFile(filePath, checkOnly=True)
                except Exception as exception:
                    # 검사기가 깨져 이 파일을 못 봤다 — 경고로 넘기면 검사 누락이 통과로 보이므로 커밋을 막는다.
                    hasErrors = True
                    print(f"  [Error] {filePath.relative_to(projectRoot)} {label} 검사 중 오류: {exception}")
                    continue

                if listViolation:
                    hasErrors = True
                    for violation in listViolation:
                        print(f"    - {violation}")

        if bVerbose:
            print("\n[포맷] clang-format 포맷팅 검사...")
        if runClangFormatBatch(stagedCppFiles, checkOnly=True, cwd=projectRoot) != 0:
            hasErrors = True
            print(
                "  [Error] 포맷팅 규칙에 어긋나는 파일이 있습니다. "
                "'python Scripts/lint/fixer/FormatModified.py'를 실행하여 자동 수정한 뒤 다시 git add 하세요."
            )
        elif bVerbose:
            print("  - 포맷팅 OK")
        else:
            print(f"[픽서·포맷] C++ 파일 {len(stagedCppFiles)}개 통과")
    elif bVerbose:
        print("\n  - 검사 대상 C++ 파일 없음 (Resource/데이터 파일만 변경됨)")

    if hasErrors:
        print("\n[PreCommitLint] [FAIL] 검사에 실패했습니다. 오류를 수정한 후 다시 git add 하고 커밋해주세요.")
        return 1

    print("\n[PreCommitLint] [OK] 모든 검사를 통과했습니다.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
