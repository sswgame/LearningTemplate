#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/PreCommit.py

린트 입구 하나 — 규칙은 `Rules.py` 의 표 한 곳에 있다.

  py -3 -m Scripts lint                 # 커밋 훅: 모든 규칙이 스테이지된 파일만 본다(LintScope) + clang-format
  py -3 -m Scripts lint --all           # 묶음 검증: 모든 규칙이 저장소 전체를 본다 + clang-format(VerifyBundle.sh 첫 단계)
  py -3 -m Scripts lint --list          # 규칙 표(갈래 · 막는 것 · 왜 린트인가 · 예외 파일)
  py -3 -m Scripts lint <id> [인자…]    # 규칙 하나의 단독 실행(`--all` · `--self-check` 등은 그 모듈의 인자)

커밋 훅은 저장소 크기에 비례하지 않는다: 규칙은 스테이지된 파일만 보고, 여러 파일을 보는 규칙은 내용 해시 색인(`FactCache`)을 바뀐 파일만
다시 계산해 쓴다. 규칙의 표 · 예외 · 모듈(그 규칙만 쓰는 도우미 포함)이 스테이지에 있으면 그 규칙만 저장소 전체를 보고(`Rule.listOwnInputs`),
여러 규칙이 함께 쓰는 도우미만 바뀌었으면 그것을 쓰는 규칙의 자체 검사를 돈다(`Rule.listInputs`). `--all` 은 모든 규칙의 자체 검사도 돈다.
이 모양은 시간이 아니라 연 파일 수로 지킨다(`checkReadBound` — 린트 코드가 바뀐 커밋과 `--all` 이 돈다): 파일 하나를 스테이지하면 모든 규칙을 통틀어
그 파일을 한 번만 연다. 규칙은 파일 글을 `LintScope.readText` · `readTexts` 나 `FactCache` 로만 얻는다.
**통과한 것은 찍지 않는다.** 통과는 한 줄, 실패만 상세를 찍는다. 고치는 것은 `py -3 -m Scripts format` · `format-includes` 다.
"""

from __future__ import annotations

import builtins
import contextlib
import importlib
import io
import os
import sys
import tempfile
from pathlib import Path
from typing import Callable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ProjectConstants import kCppAllExtensions, kLintTargetRelDirs, kNotOurDirNames, kShaderSourceExtensions  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.Git import runGit  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from lint.Format import runClangFormatBatch  # noqa: E402
from lint.LintScope import LintScope, makeStagedScope, makeWholeScope, readBytesInternal  # noqa: E402
from lint.ModuleImports import listImportedModuleFiles  # noqa: E402
from lint.Rules import Rule, formatTable, kRuleById, kRules  # noqa: E402

kCppSuffixes = tuple(sorted(kCppAllExtensions))
kShaderSuffixes = tuple(sorted(kShaderSourceExtensions))


def selectFormatFiles(scope: LintScope) -> list[str]:
    """clang-format 이 볼 파일 — `format --all` 과 같은 대상(C++ 은 `kLintTargetRelDirs`, 셰이더는 남의 코드 · 산출물 폴더 밖). 받아 온 남의 헤더는 고치지 않는다."""
    listCpp = scope.select(kLintTargetRelDirs, kCppSuffixes)
    listShader = [path for path in scope.select(suffixes=kShaderSuffixes) if not any(part in kNotOurDirNames for part in path.split("/")[:-1])]
    return listCpp + listShader


def runRule(rule: Rule, projectRoot: Path, scope: LintScope) -> list[str]:
    moduleName, _, functionName = rule.check.partition(":")
    return getattr(importlib.import_module(moduleName), functionName)(projectRoot, scope)


def selectRuleScope(rule: Rule, projectRoot: Path, scope: LintScope) -> LintScope:
    """
    규칙 자신의 표 · 예외 · 모듈(그 규칙만 쓰는 도우미 포함)이 바뀌었으면 그 규칙만 저장소 전체 — 바뀌지 않은 파일의 판정도 달라진다.

    여러 규칙이 함께 쓰는 도우미(LintScope · CppText · FunctionSpans · common …)만 바뀌었으면 전체로 돌리지 않는다 — 도우미 하나에 규칙 수십 개가
    저장소 전체를 돌았다. 그 도우미를 쓰는 규칙의 자체 검사가 보고(`runSelfCheck`), 실제 파일에서 판정이 달라졌는지는 묶음 검증 첫 단계 `lint --all` 이 본다.
    """
    if scope.bWholeRepo or not scope.touches(roots=rule.listOwnInputs()):
        return scope
    return makeWholeScope(projectRoot)


#: 읽기 한도를 볼 스테이지 파일의 갈래(뿌리, 접미사) — 갈래마다 저장소의 첫 파일 하나를 스테이지된 것처럼 넘긴다.
kProbeKinds = (("Source/Engine/", ".cpp"), ("Source/Engine/", ".h"), ("Source/Editor/", ".cpp"), ("Source/Editor/", ".h"),
               ("Source/", "CMakeLists.txt"), ("Content/", ".xml"), ("Content/", ".hlsl"), ("Scripts/", ".py"))


def listProbeFiles(projectRoot: Path) -> list[str]:
    listRepo = makeWholeScope(projectRoot).listFile
    return [next(path for path in listRepo if path.startswith(root) and path.endswith(suffix)) for root, suffix in kProbeKinds]


def checkReadBound(projectRoot: Path) -> list[str]:
    """
    커밋 훅이 저장소 크기에 비례하지 않는다는 것을 형식으로 지킨다(시간을 재지 않는다): 파일 하나를 스테이지한 범위로 **모든 규칙을 차례로** 돌 때
    연 파일이 그 파일 한 번뿐이어야 한다. 사실 캐시(`FactCache`)를 먼저 데우므로 캐시 미스는 0 이다 — 넘으면 어떤 규칙이 스테이지 밖 파일을 읽거나
    (색인 없이 저장소 · 폴더 · 짝을 읽는다) 같은 파일을 따로 읽은 것이다(`LintScope.readBytes` 를 거치지 않았다).

    세지 않는 것: 규칙의 표(`Config/Rules/`, `Rule.listTables`), 규칙 코드(Scripts 모듈 — 사실 캐시의 판), `Saved/`(캐시), `.git/`.
    shortcut: `io.open` · `open` 만 센다 — git 으로 읽는 블롭(`git cat-file`)은 세지 않는다, 그런 규칙이 늘면 git 호출도 센다.
    """
    setCode = {path for rule in kRules for path in listImportedModuleFiles(rule.getModuleName())}
    resolvedRoot = projectRoot.resolve()
    listProbe = listProbeFiles(projectRoot)
    for rule in kRules:
        runRule(rule, projectRoot, LintScope(tuple(listProbe), (), False))   # 사실 캐시 데우기
    listProblem: list[str] = []
    originalOpen = io.open
    for probe in listProbe:
        readBytesInternal.cache_clear()
        listOpened: list[tuple[str, str]] = []
        current = [""]

        def countingOpen(file, *args, **kwargs):  # noqa: ANN001, ANN002, ANN003 — io.open 과 같은 인자
            try:
                relative = Path(file).resolve().relative_to(resolvedRoot).as_posix() if isinstance(file, (str, Path)) else ""
            except ValueError:
                relative = ""
            if relative and relative not in setCode and not relative.startswith(("Config/Rules/", "Saved/", ".git/")):
                listOpened.append((current[0], relative))
            return originalOpen(file, *args, **kwargs)

        io.open = builtins.open = countingOpen   # Path.read_text · read_bytes 는 io.open, 맨 open 은 builtins.open 을 부른다
        try:
            for rule in kRules:
                current[0] = rule.id
                runRule(rule, projectRoot, LintScope((probe,), (), False))
        finally:
            io.open = builtins.open = originalOpen
        for index, (ruleID, relative) in enumerate(listOpened):
            rule = kRuleById[ruleID]
            if any(relative == table or relative.startswith(table.rstrip("/") + "/") for table in rule.listTables()):
                continue
            if relative != probe:
                listProblem.append(f"{ruleID}: 스테이지 '{probe}' 하나에 스테이지 밖 '{relative}' 를 읽는다 — 여러 파일을 보는 판정은 `FactCache` 색인으로")
            elif any(other == relative for _, other in listOpened[:index]):
                listProblem.append(f"{ruleID}: 스테이지 '{probe}' 를 다른 규칙과 따로 다시 읽는다 — `LintScope.readText` · `readTexts` 로")
    return listProblem


def checkMergeScope() -> list[str]:
    """병합 중 범위(`makeStagedScope`) — 임시 저장소에서 양쪽이 고친 파일 · 한쪽만 고친 파일 · 한쪽이 지운 파일 · 새 파일을 섞어 병합하고, 손으로 푼 충돌만 남는지 본다."""
    def git(root: Path, *listArgument: str) -> None:
        result = runGit(["-c", "user.name=lint", "-c", "user.email=lint@localhost", "-c", "core.autocrlf=false", *listArgument], cwd=root)
        if result.returnCode != 0 and listArgument[0] != "merge":   # 충돌 난 병합은 0 이 아니다
            raise RuntimeError(f"git {' '.join(listArgument)}: {result.stderr.strip()}")

    # 커밋 훅 안에서는 git 이 GIT_DIR · GIT_INDEX_FILE 을 넘겨 준다 — 그대로 두면 임시 저장소의 git 이 이 저장소를 건드린다(core.bare 를 켰다).
    mapSaved = {name: os.environ.pop(name) for name in list(os.environ) if name.startswith("GIT_")}
    try:
        return checkMergeScopeInternal(git)
    finally:
        os.environ.update(mapSaved)


def checkMergeScopeInternal(git: Callable[..., None]) -> list[str]:
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        git(root, "init", "-q", "-b", "main")
        for name in ("a", "b", "c"):
            (root / f"{name}.txt").write_text(f"{name}\n", encoding="utf-8")
        git(root, "add", ".")
        git(root, "commit", "-qm", "base")
        git(root, "checkout", "-qb", "side")
        (root / "b.txt").write_text("b side\n", encoding="utf-8")
        (root / "c.txt").write_text("c side\n", encoding="utf-8")
        (root / "n.txt").write_text("n\n", encoding="utf-8")
        git(root, "add", ".")
        git(root, "commit", "-qm", "side")
        git(root, "checkout", "-q", "main")
        (root / "c.txt").write_text("c main\n", encoding="utf-8")
        git(root, "rm", "-q", "a.txt")
        git(root, "commit", "-qam", "main")
        git(root, "merge", "-q", "side")
        (root / "c.txt").write_text("c merged\n", encoding="utf-8")
        git(root, "add", "c.txt")
        scope = makeStagedScope(root)
    return [] if (scope.listFile, scope.listRemoved) == (("c.txt",), ()) else [f"병합 범위가 손으로 푼 c.txt 하나가 아니다: {scope.listFile} · 지움 {scope.listRemoved}"]


def runSelfCheck(rule: Rule) -> str | None:
    """규칙 모듈의 자체 검사(`runSelfCheck` 가 있을 때) — 실패면 까닭, 통과면 None. 통과 줄은 찍지 않는다."""
    module = importlib.import_module(rule.getModuleName())
    if not hasattr(module, "runSelfCheck"):
        return None
    buffer = io.StringIO()
    try:
        with contextlib.redirect_stdout(buffer):
            result = module.runSelfCheck()
    except (Exception, SystemExit) as error:   # noqa: BLE001 — 자체 검사의 assert · 오류는 모두 실패다
        return f"{type(error).__name__}: {error} {buffer.getvalue().strip()}".strip()
    if result is False or (type(result) is int and result != 0):   # 모듈마다 None · 0 · True 가 통과다
        return buffer.getvalue().strip() or f"돌려준 값 {result}"
    return None


def runLint(projectRoot: Path, scope: LintScope) -> int:
    if not scope.listFile and not scope.listRemoved:
        return ExitCode.Passed
    hasErrors = False
    listWhole: list[str] = []
    for rule in kRules:
        ruleScope = selectRuleScope(rule, projectRoot, scope)
        if ruleScope.bWholeRepo and not scope.bWholeRepo:
            listWhole.append(rule.id)
        listProblem = runRule(rule, projectRoot, ruleScope)
        if scope.bWholeRepo or scope.touches(roots=rule.listInputs()):
            failure = runSelfCheck(rule)
            listProblem = listProblem + ([f"자체 검사 실패 — {failure}"] if failure else [])
        if listProblem:
            hasErrors = True
            where = f" — 예외 · 기준선: {rule.exceptions}" if rule.exceptions else ""
            print(f"[{rule.id}] FAIL — {rule.blocks} {len(listProblem)}건{where}")
            for line in listProblem:
                print(f"  - {line}")

    # 린트 코드(규칙 · 도우미 · 이 파일)가 바뀌면 커밋 훅의 모양(스테이지 비례 읽기 · 병합 범위)을 다시 본다 — 시간을 재지 않고 연 파일을 센다.
    if scope.bWholeRepo or scope.touches(roots=("Scripts/lint/", *sorted({path for rule in kRules for path in rule.listInputs() if path.startswith("Scripts/")}))):
        listShape = checkReadBound(projectRoot) + checkMergeScope()
        if listShape:
            hasErrors = True
            print(f"[lint-shape] FAIL — 커밋 훅이 스테이지 수에 비례하지 않는다 {len(listShape)}건")
            for line in listShape:
                print(f"  - {line}")

    listFormat =[projectRoot / path for path in selectFormatFiles(scope)]
    if listFormat and runClangFormatBatch(listFormat, checkOnly=True, cwd=projectRoot) != 0:
        hasErrors = True
        print("[format] FAIL — 'py -3 -m Scripts format' 으로 고친 뒤 다시 git add 한다.")

    if hasErrors:
        print("\n[lint] FAIL — 고친 뒤 다시 git add 하고 커밋한다.")
        return ExitCode.Violation
    what = "저장소 전체" if scope.bWholeRepo else f"스테이지 {len(scope.listFile) + len(scope.listRemoved)}개"
    whole = f"(표가 바뀌어 전체: {', '.join(listWhole)})" if listWhole else ""
    print(f"[lint] OK — {what}, 규칙 {len(kRules)}개{whole} · 포맷")
    return ExitCode.Passed


def main(argv: Sequence[str] | None = None) -> int:
    listArgument = list(sys.argv[1:] if argv is None else argv)
    if not listArgument:
        return runLint(getProjectRoot(), makeStagedScope(getProjectRoot()))
    if listArgument[0] == "--all":
        return runLint(getProjectRoot(), makeWholeScope(getProjectRoot()))
    if listArgument[0] == "--list":
        print(formatTable())
        return ExitCode.Passed
    rule = kRuleById.get(listArgument[0])
    if rule is None:
        print(f"[lint] 모르는 규칙 '{listArgument[0]}' — `py -3 -m Scripts lint --list`", file=sys.stderr)
        return ExitCode.CheckInvalid
    module = importlib.import_module(rule.getModuleName())
    if hasattr(module, "main") is False:
        print(f"[lint] {rule.id} 는 커밋 훅으로만 돈다(단독 실행 없음)", file=sys.stderr)
        return ExitCode.CheckInvalid
    return module.main(listArgument[1:])


if __name__ == "__main__":
    sys.exit(main())
