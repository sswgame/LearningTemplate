#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
게이트 린트들이 아직 무언가를 잡는지 검사합니다 (음성 테스트).

**린트는 조용히 죽는다.** 정규식 한 글자, 경로 한 조각이 어긋나면 아무것도 못 잡게 되는데 결과는
"위반 0건" 이라 통과처럼 보인다 — 예: 죽은 사본 검사가 자기 독스트링을 참조로 세어 통과하거나,
소유 검사가 나열한 구조체 이름만 보고 새 구조체를 놓친다.

**증거는 린트가 직접 든다.** 각 게이트 클래스가 `selfTestCases` 에 "이건 반드시 잡아야 한다" 는 조각을
적어 두고, 이 장치가 임시 트리에 그것을 써서 린트를 돌린다. 종료 1(위반)이 나오고 **출력에 조각의 `expectedViolation` 문구가 있어야**
통과다 — 종료 2(입력을 못 읽음 · 게이트 내부 오류)는 조각과 무관하게 나오므로 실패로 센다. 종료 코드만 보면 노린 위반이 빠져도 같은 조각의
다른 위반으로 통과한다(`expectedViolation` 이 없는 조각은 `LintGate` 가 클래스를 만들 때 막는다). 조각에는 게이트가 읽는 입력 파일까지 갖춘다.
게이트마다 하위 프로세스 하나(`--cases-of <게이트>`)가 그 게이트의 조각을 차례로 진입점(`XxxGate.run`)으로 돈다 — 조각마다 프로세스를 띄우면
기동 · import 가 조각 수(수백)만큼 든다. 예외 기록은 `run` 이 조각마다 비우고, 게이트끼리는 프로세스가 갈려 모듈 상태가 섞이지 않는다.
조각 표를 여기 모아 두지 않는 이유는 하나다 — **표가 둘이면 언제든 어긋난다.**

게이트가 `selfTestCases` 를 선언하지 않으면 이 검사가 "덮이지 않은 린트" 로 실패한다.
정말 조각을 만들 수 없는 린트는 `selfTestSkipReason` 에 이유를 적는다(이유 없는 예외는 없다).

예외 표 · 규칙 데이터(`Scripts/lint/rules/<게이트>.toml`)를 읽지 못한 게이트는 조각마다 종료 2 를 내 "살아 있다" 로 보인다 — 그래서 조각을 돌리기 전에
게이트 클래스의 읽기 오류를 먼저 본다.

**대상 목록도 두지 않는다** — `Scripts/lint/gate/` 에 있는 것이 게이트다. 목록을 적어 두면 새 게이트를
거기 넣는 걸 잊는 순간 그 게이트는 아무에게도 검사받지 않는다. 자리가 규칙이다.
폴더를 훑는 일은 `Scripts/lint/LintCatalog.py` 가 한다 — CMake 등록 파일을 만드는 쪽과 **같은 훑기**다.

  python Scripts/lint/selftest/CheckLintsAreAlive.py [--root <repo>] [--verbose]
"""
from __future__ import annotations

import argparse
import contextlib
import importlib
import io
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common import getProcessWorkerCount, mapConcurrent, runProcess  # noqa: E402
from lint.LintCatalog import LintScript, discoverLintScripts  # noqa: E402
from lint.LintGate import GateError, GateResult, LintGate, findGateClass  # noqa: E402

#: `--cases-of` 하위 프로세스가 결과 JSON 앞에 찍는 표식 — 게이트가 찍는 줄과 가른다.
_kResultMarker = "\n@@CheckLintsAreAlive-result@@"

#: 조각의 `repoFiles` 를 복사해 오는 저장소 — 이 스크립트가 든 체크아웃이다.
_kRepositoryRoot = Path(__file__).resolve().parents[3]


def runCaseInternal(gateClass: type[LintGate], case: dict) -> tuple[int, str]:
    """
    조각 하나를 임시 저장소에 풀어 두고 게이트를 **실제 진입점으로**(`run` — 인자 해석 · 예외 표 확인 · 낡은 예외까지) 이 프로세스에서 돌립니다.
    예외 기록은 `run` 이 조각마다 비운다(`LintGate.beginRun`). 출력은 모아 돌려준다. 게이트가 예외로 죽으면 잡지 않는다 —
    하위 프로세스가 결과 없이 끝나 이 검사가 종료 2 로 그 게이트를 알린다(죽은 게이트를 "0 이 아니니 살아 있다" 로 세지 않는다).
    """
    tempRoot = Path(tempfile.mkdtemp(prefix="swLintAlive"))
    output = io.StringIO()
    try:
        if case.get("git"):
            # 저장소를 묻는 게이트(`git check-ignore` …)용. 실패하면 조각을 돌리지 않고 멈춘다.
            initResult = runProcess(["git", "init", "-q", str(tempRoot)])
            if initResult.returnCode != 0:
                raise GateError(f"{case['name']}: 임시 트리에서 git init 이 실패했습니다: {initResult.output.strip()}")
        for relPath in case.get("repoFiles", []):
            # 저장소의 표 · 설정을 그대로 복사한다 — 없으면 예외로 죽어 이 검사가 알린다.
            target = tempRoot / relPath
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(_kRepositoryRoot / relPath, target)
        for relPath, content in case.get("files", {}).items():
            target = tempRoot / relPath
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            try:
                code = gateClass.run(["--root", str(tempRoot), *case.get("args", [])])
            except SystemExit as exitRequest:
                code = exitRequest.code if isinstance(exitRequest.code, int) else 1
        return code, output.getvalue()
    finally:
        shutil.rmtree(tempRoot, ignore_errors=True)


def runGateCasesInternal(script: LintScript) -> list[tuple[int, str]]:
    """
    게이트 하나의 조각 전부를 **하위 프로세스 하나**에서 돌립니다(`--cases-of`). 조각마다 프로세스를 띄우면 기동 · import 가 조각 수(수백)만큼 든다.
    게이트끼리는 프로세스를 나눈다 — 한 게이트의 모듈 상태가 다른 게이트로 새지 않게.
    """
    environment = dict(os.environ, PYTHONIOENCODING="utf-8")
    result = runProcess([sys.executable, Path(__file__).resolve(), "--cases-of", script.name], env=environment)
    marker = result.stdout.rfind(_kResultMarker)
    if marker < 0:
        raise GateError(f"{script.name}: 조각을 돌리는 하위 프로세스가 결과 없이 끝났습니다(종료 {result.returnCode}): {result.output.strip()[-500:]}")
    return [tuple(item) for item in json.loads(result.stdout[marker + len(_kResultMarker):])]


def runCasesOfInternal(gateName: str) -> int:
    """`--cases-of` 하위 프로세스 — 게이트 하나의 조각을 차례로 돌려 (종료 코드, 출력) 목록을 표식 뒤 JSON 으로 찍는다."""
    # 그 게이트 모듈 하나만 import 한다 — 폴더 전부(`discoverLintScripts`)를 import 하면 하위 프로세스마다 게이트 수만큼 import 한다.
    gateClass = None
    if (Path(__file__).resolve().parents[1] / "gate" / f"{gateName}.py").is_file():
        gateClass = findGateClass(importlib.import_module(f"lint.gate.{gateName}"))
    if gateClass is None:
        print(f"[CheckLintsAreAlive] gate/ 에 게이트 '{gateName}' 가 없습니다", file=sys.stderr)
        return 2
    listResult = [runCaseInternal(gateClass, case) for case in gateClass.selfTestCases]
    sys.stdout.write(_kResultMarker + json.dumps(listResult, ensure_ascii=False))
    return 0


class CheckLintsAreAliveGate(LintGate):
    description = "게이트 린트 음성 테스트 — 위반 조각을 넣으면 게이트가 지는지"
    buildComment = "Checking that every gate lint still fails on a deliberately broken fixture..."
    timeoutSeconds = 120
    selfTestSkipReason = "린트를 보는 린트 — 조각을 들지 않는다(대상이 린트 폴더 자체다)"
    violationHeader = "문제"

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--verbose", action="store_true", help="조각마다 종료 코드를 모두 출력")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        errors: list[str] = []
        listJob: list[tuple[LintScript, dict]] = []

        listGateScript = discoverLintScripts("gate")
        if not listGateScript:
            raise GateError("Scripts/lint/gate/ 에 게이트가 하나도 없습니다 — 이 검사가 헛돌고 있습니다")

        for script in listGateScript:
            moduleName = script.name
            gateClass = script.gateClass
            if gateClass is None:
                errors.append(f"{moduleName}: `LintGate` 를 상속한 게이트 클래스가 없습니다 — "
                              f"`gate/` 에 있는 것은 게이트여야 합니다 (Scripts/lint/LintGate.py 참고)")
                continue

            if gateClass._ruleDataError:
                # 규칙 데이터를 못 읽은 게이트는 모든 조각에서 종료 2 를 낸다 — 0 이 아니라 "살아 있다" 로 보이므로 여기서 따로 잡는다.
                errors.append(f"{moduleName}: 규칙 데이터를 읽지 못합니다 — {gateClass._ruleDataError}")
                continue

            cases = gateClass.selfTestCases
            if not cases:
                if gateClass.selfTestSkipReason:
                    if args.verbose:
                        print(f"  [{moduleName}] 건너뜀 — {gateClass.selfTestSkipReason}")
                    continue
                errors.append(f"{moduleName}: `selfTestCases` 가 없습니다 — 이 린트가 죽어도 아무도 모릅니다 "
                              f"(정말 못 만들면 `selfTestSkipReason` 에 이유를 적으세요)")
                continue

            listJob += [(script, case) for case in cases]

        # 게이트마다 하위 프로세스 하나가 그 게이트의 조각을 전부 돈다. 기다리는 일이라 스레드로 동시에 돌리고(`getProcessWorkerCount`),
        # 보고는 조각 순서대로 한다.
        listScriptWithCase = list(dict.fromkeys(script for script, _ in listJob))
        mapCaseResult = dict(mapConcurrent(lambda script: (script.name, runGateCasesInternal(script)), listScriptWithCase,
                                           workerCount=getProcessWorkerCount(len(listScriptWithCase))))
        listResult: list[tuple[int, int, str]] = []
        mapNextCase: dict[str, int] = {}
        for index, (script, _) in enumerate(listJob):
            caseIndex = mapNextCase.get(script.name, 0)
            mapNextCase[script.name] = caseIndex + 1
            listResult.append((index, *mapCaseResult[script.name][caseIndex]))
        for index, code, output in listResult:
            script, case = listJob[index]
            caseName = case["name"]
            if args.verbose:
                print(f"  [{script.name}/{caseName}] exit={code}")
            lastLine = output.strip().splitlines()[-1] if output.strip() else '(없음)'
            if code != 1:
                # 종료 1(위반)만 "잡았다" 로 센다. 종료 2 는 게이트가 입력을 못 읽거나 예외로 죽은 것이라 조각이 무엇이든 같은 결과다 —
                # 0 이 아니라는 이유로 살아 있다고 세면 죽은 게이트가 숨는다.
                verdict = "위반을 넣었는데 통과했습니다" if code == 0 else f"위반 대신 종료 {code}(게이트 내부 오류)로 끝났습니다"
                errors.append(f"{script.name}/{caseName}: {verdict} — 이 검사는 죽었습니다\n      출력: {lastLine}")
            elif case["expectedViolation"] not in output:
                # 종료 1 이어도 노린 위반이 아닐 수 있다 — 같은 조각이 낸 다른 위반(입력 부족 · 낡은 예외)이 노린 규칙의 죽음을 가린다.
                errors.append(f"{script.name}/{caseName}: 종료 1 이지만 노린 위반 '{case['expectedViolation']}' 이 출력에 없습니다"
                              f" — 이 규칙은 죽었습니다\n      출력: {lastLine}")
        checkedCases = len(listJob)

        return GateResult(listViolation=errors, summary=f"{checkedCases} cases across {len(listGateScript)} gates")


def main(argv: list[str] | None = None) -> int:
    listArgument = list(sys.argv[1:] if argv is None else argv)
    if len(listArgument) == 2 and listArgument[0] == "--cases-of":
        return runCasesOfInternal(listArgument[1])
    return CheckLintsAreAliveGate.run(listArgument)


if __name__ == "__main__":
    sys.exit(main())
