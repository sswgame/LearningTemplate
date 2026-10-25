#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file RunIntegrationGate.py
@brief 통합이 main 으로 올리기 전의 증거 — `-k 0` 전체 빌드와 `ctest -L "nogpu|lint"` 의 통과 · 실패 요약을 JSON 하나로 남기고, 그 증거를 요구합니다.

    py -3 -m Scripts integ-gate run --owner <이름>                       # build/Ninja-Debug 를 `-k 0` 로 짓고 nogpu · lint 를 돌려 증거를 쓴다
    py -3 -m Scripts integ-gate run --owner <이름> --below-normal -j 4   # 도우미 — 낮은 우선순위
    py -3 -m Scripts integ-gate check [--rev HEAD]                       # 그 커밋의 증거가 있고 통과인가(아니면 이유와 종료 1)
    py -3 -m Scripts integ-gate check-push < <pre-push 입력>             # main 으로 가는 커밋마다 `check` — 훅이 아니라 통합이 push 앞에 부른다

**왜 `-k 0` 인가.** 기본 빌드는 첫 오류에서 서서 병합 한 번에 컴파일 오류가 하나씩만 보인다(통합 한 바퀴마다 오류를 하나 고치고 다시 짓는다).
`-k 0` 는 지을 수 있는 것을 끝까지 지어 오류를 한 번에 모은다. `full-build`(`all AllTests`, 빌드 증거)와 같은 타깃 · 같은 빌드 칸 잠금이다.

**왜 nogpu | lint 인가.** GPU 항목은 장치를 독점해 통합을 막는다. 그것은 정해진 시각에 `verify-batch` 로 몰아 돌린다([작업 방식](../../docs/11_Workflow.md) 3절).
이 증거는 GPU 없이 돌 수 있는 것 — 빌드, `nogpu`, `lint` — 이 이 입력에서 통과했다는 것만 말한다. 항목이 하나도 안 돌았으면(`--no-tests=error`) 통과가 아니다.

**증거.** `<공통 .git>/sw-integration-evidence/<빌드 입력 해시>.json` 하나다. 키는 `full-build` 와 같은 빌드 입력 해시(`common/BuildEvidence.py`)라서
마크다운 · `.claude/` 만 바뀐 커밋은 직전 증거가 그대로 맞는다(그 파일은 커밋 훅이 이미 린트한다). 실패한 실행도 증거를 남겨 `check` 가 이유를 말하게 한다.
빌드가 서면 ctest 는 돌지 않는다(옛 실행 파일로 도는 통과를 막는다). 커밋하지 않은 변경이 있거나 도는 동안 트리가 바뀌면 증거를 쓰지 않는다.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
import xml.etree.ElementTree as ElementTree
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.BuildEvidence import (  # noqa: E402
    getEvidenceDirectory, getEvidencePath, listPushedTreesNeedingEvidence, listUncommittedFiles, readBuildInputHash, readTreeHash, writeEvidence)
from common.BuildTree import BuildTree, kDefaultPreset, kTestBuildTargets  # noqa: E402
from common.Host import runGit  # noqa: E402
from common.LogDigest import getLogPath, printDigest, runDigested  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.SharedLock import getSharedSlotCount, kDefaultWaitSeconds, runHolding  # noqa: E402
from common.Worktree import findMainRoot  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402

#: 통합 증거 폴더 — 공통 .git 아래(`full-build` 의 `sw-build-evidence` 와 다른 폴더다: 한쪽이 있다고 다른 쪽이 있는 것이 아니다).
kIntegrationEvidenceDirName = "sw-integration-evidence"
#: 증거가 말해야 하는 ctest 라벨 식 — 이보다 좁게 돈 증거는 통과로 받지 않는다. GPU 가 필요한 `hostgpu` 는 `verify-batch` 의 일이다.
kRequiredLabels = "nogpu|lint"
#: 검증 빌드가 잡는 잠금 자원(`full-build` 와 같은 공유 칸).
kBuildLockResource = "build"
#: 실패한 테스트 이름을 증거에 담는 상한(수천 개여도 JSON 이 부풀지 않게 — 총 실패 수는 따로 센다).
kMaxFailedNames = 50


def parseJunitSummary(xmlText: str) -> dict:
    """
    ctest `--output-junit` 의 XML → `{total, passed, failed, skipped, failedNames}`. 실패는 `failure` · `error` 자식이 있는 케이스,
    건너뜀은 `skipped` 자식이 있거나 `status="notrun"` 인 케이스다. XML 이 깨졌으면 `ElementTree.ParseError` 로 멈춘다(빈 요약으로 읽지 않는다).
    """
    root = ElementTree.fromstring(xmlText)
    listFailed: list[str] = []
    passed = skipped = total = 0
    for case in root.iter("testcase"):
        total += 1
        childTags = {child.tag for child in case}
        if "failure" in childTags or "error" in childTags:
            listFailed.append(case.get("name", "?"))
        elif "skipped" in childTags or case.get("status") == "notrun":
            skipped += 1
        else:
            passed += 1
    return {"total": total, "passed": passed, "failed": len(listFailed), "skipped": skipped, "failedNames": listFailed[:kMaxFailedNames]}


def evaluateEvidence(record: dict | None, inputHash: str) -> list[str]:
    """증거가 이 빌드 입력의 통과 증거인가. 받지 않는 이유 목록 — 비어 있으면 통과다."""
    if record is None:
        return [f"빌드 입력 {inputHash[:12]} 의 통합 증거가 없습니다 — `py -3 -m Scripts integ-gate run --owner <이름>` 을 그 커밋에서 돌립니다"]
    listReason: list[str] = []
    if record.get("inputs") != inputHash:
        listReason.append("증거의 빌드 입력 해시가 이 커밋과 다릅니다")
    if record.get("labels") != kRequiredLabels:
        listReason.append(f"ctest 라벨이 `{record.get('labels')}` 입니다 — `{kRequiredLabels}` 로 돈 증거만 받습니다")
    build = record.get("build") or {}
    if build.get("bKeepGoing") is not True:
        listReason.append("`-k 0` 로 지은 빌드가 아닙니다")
    if build.get("bPassed") is not True:
        listReason.append(f"빌드가 실패했습니다(종료 {build.get('returnCode')})")
    ctest = record.get("ctest") or {}
    if ctest.get("bRan") is not True:
        listReason.append("ctest 가 돌지 않았습니다")
    else:
        if ctest.get("total", 0) == 0:
            listReason.append("ctest 가 돌린 항목이 0 개입니다")
        if ctest.get("failed", 0) > 0 or ctest.get("returnCode") != 0:
            listReason.append(f"ctest 실패 {ctest.get('failed', 0)} 건(종료 {ctest.get('returnCode')}): {', '.join(ctest.get('failedNames', [])[:10])}")
    if not listReason and record.get("bPassed") is not True:
        listReason.append("증거의 bPassed 가 참이 아닙니다")
    return listReason


def readEvidenceRecord(evidenceDirectory: Path, inputHash: str) -> dict | None:
    """증거 JSON 을 읽는다. 없으면 None, 깨졌으면 `ValueError`(없는 것으로 읽지 않는다)."""
    path = getEvidencePath(evidenceDirectory, inputHash)
    if not path.is_file():
        return None
    try:
        record = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise ValueError(f"{path} 가 JSON 이 아닙니다: {error}") from error
    if not isinstance(record, dict):
        raise ValueError(f"{path} 의 최상위가 객체가 아닙니다")
    return record


def checkRevision(repositoryRoot: Path, revision: str) -> list[str]:
    """커밋의 통합 증거를 평가한다. 받지 않는 이유 목록(비면 통과)."""
    inputHash = readBuildInputHash(repositoryRoot, revision)
    return evaluateEvidence(readEvidenceRecord(getEvidenceDirectory(repositoryRoot, kIntegrationEvidenceDirName), inputHash), inputHash)


def checkPush(repositoryRoot: Path, pushInput: str) -> int:
    """main 으로 가는 커밋마다 통합 증거를 요구한다. 하나라도 받지 못하면 1."""
    bFailed = False
    for ref, inputHash in listPushedTreesNeedingEvidence(pushInput, lambda commit: readBuildInputHash(repositoryRoot, commit)):
        record = readEvidenceRecord(getEvidenceDirectory(repositoryRoot, kIntegrationEvidenceDirName), inputHash)
        for reason in evaluateEvidence(record, inputHash):
            print(f"[IntegGate] {ref}: {reason}", file=sys.stderr)
            bFailed = True
    return 1 if bFailed else 0


def runGate(repositoryRoot: Path, args: argparse.Namespace) -> int:
    """`-k 0` 로 짓고, 빌드가 서지 않았으면 nogpu · lint 를 돌려 증거를 쓴다. 통과면 0, 아니면 1(환경이 틀리면 2)."""
    listDirty = listUncommittedFiles(repositoryRoot)
    if listDirty:
        print(f"[IntegGate] 커밋하지 않은 변경이 있어 돌리지 않습니다 — 증거는 커밋 트리에 붙는다: {', '.join(listDirty[:10])}", file=sys.stderr)
        return ExitCode.CheckInvalid
    treeHash = readTreeHash(repositoryRoot, "HEAD")
    inputHash = readBuildInputHash(repositoryRoot, "HEAD")
    tree = BuildTree.fromPreset(args.preset, repositoryRoot)
    if not tree.bConfigured:
        print(f"[IntegGate] 구성되지 않은 빌드 폴더입니다: {tree.path} — `cmake --preset {args.preset}` 를 먼저(vcpkg 잠금을 잡고) 돌립니다", file=sys.stderr)
        return ExitCode.CheckInvalid
    startTime = time.time()

    # 1) `-k 0` 빌드 — `cmake --build` 의 `--` 뒤는 ninja 로 간다. 빌드 칸 잠금은 `full-build` 와 같다.
    lockArgs = argparse.Namespace(resource=kBuildLockResource, owner=args.owner, shared=True, slots=getSharedSlotCount(kBuildLockResource),
                                  purpose=f"integ-gate {args.preset} {inputHash[:12]}", wait=args.wait, belowNormal=args.belowNormal)
    buildCommand = [*tree.buildCommand(kTestBuildTargets, args.jobs), "--", "-k", "0"]
    logPath = None if args.bVerbose else getLogPath(repositoryRoot, f"integ-build-{args.preset}")
    buildStart = time.time()
    buildCode = runHolding(findMainRoot(repositoryRoot), lockArgs, buildCommand, logPath)
    if logPath is not None and logPath.is_file():
        printDigest(logPath, buildCode, time.time() - buildStart)
    record: dict = {"inputs": inputHash, "tree": treeHash, "commit": runGit(["rev-parse", "HEAD"], cwd=repositoryRoot).stdout.strip(),
                    "preset": args.preset, "labels": args.labels, "owner": args.owner, "worktree": str(repositoryRoot),
                    "build": {"bKeepGoing": True, "bPassed": buildCode == 0, "returnCode": buildCode, "seconds": round(time.time() - buildStart, 1)},
                    "ctest": {"bRan": False}}

    # 2) nogpu · lint — 빌드가 섰으면 돌리지 않는다(옛 실행 파일로 도는 통과를 만들지 않는다).
    evidenceDirectory = getEvidenceDirectory(repositoryRoot, kIntegrationEvidenceDirName)
    if buildCode == 0:
        evidenceDirectory.mkdir(parents=True, exist_ok=True)
        junitPath = evidenceDirectory / f"{inputHash}.junit.xml"
        junitPath.unlink(missing_ok=True)
        ctestCommand = ["ctest", "--test-dir", str(tree.path), "-L", args.labels, "-j", "--output-on-failure", "--no-tests=error",
                        "--output-junit", str(junitPath)]
        ctestStart = time.time()
        ctestResult = runDigested(ctestCommand, repositoryRoot, f"integ-ctest-{args.preset}", bVerbose=args.bVerbose, bBelowNormalPriority=args.belowNormal)
        ctest: dict = {"bRan": ctestResult.bLaunched, "returnCode": ctestResult.returnCode, "seconds": round(time.time() - ctestStart, 1)}
        if junitPath.is_file():
            ctest.update(parseJunitSummary(junitPath.read_text(encoding="utf-8")))
        else:
            ctest.update({"total": 0, "passed": 0, "failed": 0, "skipped": 0, "failedNames": []})
        record["ctest"] = ctest

    if listUncommittedFiles(repositoryRoot) or readTreeHash(repositoryRoot, "HEAD") != treeHash:
        print("[IntegGate] 도는 동안 트리가 바뀌었습니다 — 지은 것이 지금 커밋과 같다고 말할 수 없어 증거를 남기지 않습니다.", file=sys.stderr)
        return ExitCode.CheckInvalid
    record["startTime"], record["endTime"] = startTime, time.time()
    if buildCode == 0:   # `-k 0` 가 0 으로 끝났다면 `all AllTests` 전체가 지어진 것 — `full-build` 증거(pre-push 훅이 보는 것)도 같은 근거로 쓴다
        writeEvidence(getEvidenceDirectory(repositoryRoot), inputHash,
                      {"inputs": inputHash, "tree": treeHash, "commit": record["commit"], "preset": args.preset, "targets": list(kTestBuildTargets),
                       "worktree": str(repositoryRoot), "owner": args.owner, "startTime": startTime, "endTime": record["endTime"], "via": "integ-gate"})
    listReason = evaluateEvidence({**record, "bPassed": True}, inputHash)
    record["bPassed"] = not listReason
    record["listReason"] = listReason
    path = writeEvidence(evidenceDirectory, inputHash, record)
    if listReason:
        for reason in listReason:
            print(f"[IntegGate] 실패: {reason}", file=sys.stderr)
        print(f"[IntegGate] 실패 증거를 남겼습니다(`check` 는 받지 않습니다): {path}", file=sys.stderr)
        return ExitCode.Violation
    ctestSummary = record["ctest"]
    print(f"[IntegGate] 통과 — `-k 0` 빌드 + ctest `{args.labels}` {ctestSummary['passed']}/{ctestSummary['total']}(건너뜀 {ctestSummary['skipped']}) — 증거 {path}")
    return ExitCode.Passed


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="통합 증거 — `-k 0` 빌드와 ctest nogpu · lint 의 통과 · 실패 요약을 남기고, main push 앞에 그 증거를 요구한다")
    subparsers = parser.add_subparsers(dest="command", required=True)
    runParser = subparsers.add_parser("run", help="`-k 0` 로 짓고 nogpu · lint 를 돌려 증거를 쓴다")
    runParser.add_argument("--owner", required=True, help="소유자 이름(에이전트 · 워크트리 이름) — 빌드 칸 잠금에 적힌다")
    runParser.add_argument("--preset", default=kDefaultPreset, help=f"빌드 폴더 build/<프리셋>(기본 {kDefaultPreset})")
    runParser.add_argument("--labels", default=kRequiredLabels, help=f"ctest 라벨 식(기본 `{kRequiredLabels}` — 다른 값으로 돈 증거는 `check` 가 받지 않는다)")
    runParser.add_argument("-j", "--jobs", type=int, default=None, help="빌드 동시 작업 수(기본: 빌드 도구가 정한다)")
    runParser.add_argument("--below-normal", dest="belowNormal", action="store_true", help="낮은 우선순위로 짓고 돌린다")
    runParser.add_argument("--wait", type=int, default=kDefaultWaitSeconds, help=f"빌드 칸을 기다릴 최대 초(기본 {kDefaultWaitSeconds})")
    runParser.add_argument("--verbose", dest="bVerbose", action="store_true", help="출력을 요약하지 않고 전부 흘린다(기본: 로그 파일 + 요약)")
    checkParser = subparsers.add_parser("check", help="그 커밋의 통합 증거가 있고 통과인지 본다")
    checkParser.add_argument("--rev", default="HEAD", help="검사할 커밋(기본 HEAD)")
    subparsers.add_parser("check-push", help="표준 입력의 pre-push 줄마다 main 으로 가는 커밋의 증거를 요구한다")
    args = parser.parse_args(argv)
    repositoryRoot = getProjectRoot()
    try:
        if args.command == "run":
            return runGate(repositoryRoot, args)
        if args.command == "check-push":
            return checkPush(repositoryRoot, sys.stdin.read())
        listReason = checkRevision(repositoryRoot, args.rev)
        for reason in listReason:
            print(f"[IntegGate] {reason}", file=sys.stderr)
        if not listReason:
            print(f"[IntegGate] {args.rev} 의 통합 증거가 통과입니다")
        return ExitCode.Violation if listReason else ExitCode.Passed
    except (RuntimeError, ValueError, ElementTree.ParseError) as error:
        print(f"[IntegGate] {error}", file=sys.stderr)
        return ExitCode.CheckInvalid


if __name__ == "__main__":
    sys.exit(main())
