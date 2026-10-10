#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckCodeConventions.py

# SW Engine C++ 코딩 컨벤션 및 정적 규칙 자동 검사 스크립트.

검사 항목:
  1) Explicit Comparison: bool, pointer, container 상태(empty, contains) 암시적 평가 및 ! 부정문 검출
  2) Loop Counter Naming: for 루프 내 단일 문자 카운터(i, j, k) 검출
  3) Member Variable Naming:
     - 고정 배열: _arr 접두어 누락 검출
     - 가변 배열(vector, list, deque): _list 접두어 누락 및 List 접미어 검출 (단, byte 단어가 포함된 바이트 벡터는 list 생략)
     - 연관 컨테이너(map, unordered_map): _map 접두어 누락 검출
     - 고유 집합(set, unordered_set): _unique 접두어 누락 검출
     - 원시 포인터 멤버: _p (단일), _pp (이중) 접두어 누락 검출
     - 삼중 포인터 이상(ppp, _ppp, ***) 금지 검출
     - 전역 정적 변수: s_, _s_ 및 포인터 s_p, _s_p 누락 검출
  4) Out-Parameter Naming:
     - 출력 매개변수 out 접두어(outList, outMap, outUnique, outArr, inout) 누락 검출
     - 포인터 출력 매개변수는 예외적으로 pOut, ppOut, pInOut, ppInOut 사용 강제 (outP, outPP 등 검출)
  5) Constructor Initialization Rules:
     - 중괄호 균일 초기화 ({}) 사용 여부 검출
     - 한 줄에 1개 변수 초기화 및 다음 줄 ',' 시작 포맷 검출
     - 생성자 멤버 초기화 순서가 클래스 멤버 선언 순서와 일치하는지 검출
  6) auto 사용 제한: 리터럴/원시 타입 직접 대입에 auto 사용 검출
  7) Include 규칙: .cpp 파일 첫 줄 #include "pch.h" 여부

사용법:
  py -3 Scripts/lint/gate/CheckCodeConventions.py [--root <repo>] [--json] [--files <files...>]
"""

from __future__ import annotations

import argparse
import functools
import json
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate · conventions

from common import (  # noqa: E402
    collectRepositoryFiles,
    flatMapInProcesses,
    getProjectRoot,
    kCppAllExtensions,
    kLintTargetRelDirs,
    kNotOurCodeDirNames,
    kNotOurDirNames,
)
from LintGate import GateResult, LintGate  # noqa: E402
from conventions.Model import ConventionViolation  # noqa: E402
from conventions.Patterns import getExactPathMapInternal  # noqa: E402
from conventions.FileScan import checkFileConventionsInternal, scopedTextCache  # noqa: E402
from conventions.CrossFile import (  # noqa: E402
    checkFileAndCollectFactsInternal,
    CrossFileFacts,
    reportBitfieldBooleanLiteralsInternal,
    reportConstructorInitializesEveryFieldInternal,
    reportDuplicateAnonymousConstantsInternal,
    reportDuplicateHelperNamesInternal,
)


def runConventionsCheck(rootDir: Path | None = None,
                        specificFiles: list[str] | None = None) -> list[ConventionViolation]:
    """
    지정된 소스 파일 또는 전체 소스 디렉터리를 순회하며 코딩 컨벤션 위반 항목을 검사합니다.
    """
    with scopedTextCache():
        return runConventionsCheckInternal(rootDir, specificFiles)


#: 검사하지 않는 폴더 이름(저장소 아래 경로의 폴더 하나와 같을 때).
_kExcludedDirNames: frozenset[str] = kNotOurCodeDirNames | frozenset({".vcpkg"})


def checkFileTargetInternal(target: tuple[Path, Path]) -> list[ConventionViolation]:
    """`--files` 항목 하나 — (파일, 상대 경로의 기준 폴더). 프로세스로 넘기려고 모듈 최상위에 둔다."""
    return checkFileConventionsInternal(target[0], target[1])


def runConventionsCheckInternal(rootDir: Path | None, specificFiles: list[str] | None) -> list[ConventionViolation]:
    projectRoot = rootDir or Path(getProjectRoot())
    getExactPathMapInternal(projectRoot)
    allViolations: list[ConventionViolation] = []

    if specificFiles:
        listTarget: list[tuple[Path, Path]] = []
        for fileString in specificFiles:
            filePath = Path(fileString).resolve()
            if not filePath.is_file():
                continue
            # 저장소 **아래** 경로의 폴더 이름으로 거른다 — 절대 경로의 부분 문자열로 보면 `.../bl-buildlint/` 같은 체크아웃에서 전부 빠진다.
            try:
                listPart = filePath.relative_to(projectRoot.resolve()).parts[:-1]
            except ValueError:
                listPart = filePath.parts[:-1]
            if _kExcludedDirNames.intersection(listPart):
                continue
            if filePath.suffix.lower() not in kCppAllExtensions:
                continue

            relRootDir = projectRoot
            if projectRoot not in filePath.parents:
                for parent in filePath.parents:
                    if (parent / "Source").is_dir() or (parent / "Test").is_dir() or (parent / "Tools").is_dir():
                        relRootDir = parent
                        break

            listTarget.append((filePath, relRootDir))
        # 커밋 훅이 파일을 수백 개 넘기는 경우(큰 커밋)도 전체 스캔과 같이 프로세스 덩어리로 나눈다 — 적으면 이 프로세스에서 돈다.
        allViolations.extend(flatMapInProcesses(checkFileTargetInternal, listTarget))
        return allViolations

    # 전체 스캔의 대상 고르기는 게이트 · 픽서와 같은 걷기(`collectRepositoryFiles` — 빌드 산출물 · 내려받은 도구로 내려가지 않는다).
    filesToScan = collectRepositoryFiles(projectRoot, kLintTargetRelDirs, suffixes=kCppAllExtensions,
                                         excludedDirNames=kNotOurDirNames | _kExcludedDirNames)
    # 파일별 검사는 파이썬 정규식이 대부분이라 스레드로는 한 코어다 — 프로세스 덩어리로 나눈다(`flatMapInProcesses`).
    # 교차 파일 검사의 파일별 몫(읽기 · 줄 훑기 · 클래스 스캔)도 같은 워커가 한 번에 한다 — 부모에는 합치기만 남는다.
    listFacts: list[CrossFileFacts] = []
    for fileViolations, facts in flatMapInProcesses(functools.partial(checkFileAndCollectFactsInternal, rootDir=projectRoot), filesToScan):
        allViolations.extend(fileViolations)
        listFacts.append(facts)

    # 파일 하나만 봐서는 알 수 없는 검사 — 전체 스캔일 때만 돈다(스테이지 파일 검사에는 상대편 파일이 없다). 이 순서가 보고 순서다.
    allViolations.extend(reportDuplicateHelperNamesInternal(listFacts))
    allViolations.extend(reportDuplicateAnonymousConstantsInternal(listFacts))
    allViolations.extend(violation for facts in listFacts for violation in facts.listHeaderInitializerViolation)
    allViolations.extend(reportBitfieldBooleanLiteralsInternal(listFacts))
    allViolations.extend(reportConstructorInitializesEveryFieldInternal(filesToScan, listFacts))

    return allViolations


@dataclass
class ConventionResult(GateResult):
    """
    카테고리별 보고서를 내려면 **원본 위반 객체**가 필요하다 — 문자열로 눌러 두면 다시 못 만든다.

    기반의 `report()` 대신 아래 `CheckCodeConventionsGate.report()` 가 이것을 읽는다.
    """

    listRaw: list[ConventionViolation] = field(default_factory=list)
    bJSON: bool = False
    repositoryRoot: Path | None = None


class CheckCodeConventionsGate(LintGate):
    """
    규칙이 서른 종이라 조각 하나로 "살아 있다" 를 증명할 수 없다.
    규칙마다 `ConventionRule.badSample` 을 들고, `CheckCodeConventionsSelfTest.py` 가 전수로 본다.
    """

    description = "SW Engine C++ 코딩 컨벤션 검사기"
    buildComment = "Checking C++ code conventions..."
    timeoutSeconds = 60
    preCommitPattern = ("*.cpp", "*.cc", "*.cxx", "*.c", "*.h", "*.hpp", "*.inl")
    preCommitFileArgument = "--files"
    selfTestSkipReason = "규칙별 음성 테스트는 CheckCodeConventionsSelfTest.py 가 담당한다"

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--category", type=str, default=None, help="특정 규칙 카테고리 필터링")
        parser.add_argument("--exclude-category", type=str, default=None, help="제외할 규칙 카테고리")
        parser.add_argument("--json", action="store_true", help="결과를 JSON 형식으로 출력")
        parser.add_argument("--files", nargs="*", help="전체 스캔 대신 검사할 개별 파일 목록")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> ConventionResult:
        violations = runConventionsCheck(repositoryRoot, args.files)
        if args.category:
            violations = [v for v in violations if args.category.lower() in v.rule_category.lower()]
        if args.exclude_category:
            violations = [v for v in violations if args.exclude_category.lower() not in v.rule_category.lower()]

        return ConventionResult(
            listViolation=[f"{v.file_path}:{v.line_number} -> {v.message}" for v in violations],
            listRaw=violations,
            bJSON=args.json,
            repositoryRoot=repositoryRoot,
        )

    def report(self, result: ConventionResult) -> int:
        """카테고리별로 묶어 찍는다 — 규칙이 서른 종이라 평평한 목록으로는 읽히지 않는다."""
        if result.bJSON:
            print(json.dumps([asdict(v) for v in result.listRaw], indent=2, ensure_ascii=False))
            return 0 if not result.listRaw else 1

        print("\n========================================================")
        print("  SW Engine C++ 코딩 컨벤션 검사 보고서")
        print(f"  저장소: {result.repositoryRoot}")
        print(f"  발견된 위반 항목: {len(result.listRaw)}건")
        print("========================================================\n")

        categoryGroups: dict[str, list[ConventionViolation]] = {}
        for violation in result.listRaw:
            categoryGroups.setdefault(violation.rule_category, []).append(violation)

        for category, items in sorted(categoryGroups.items()):
            print(f"[{category}] ({len(items)}건):")
            for item in items:
                print(f"  {item.file_path}:{item.line_number} -> {item.message}")
                print(f"      코드: {item.snippet}")
            print()

        print("--------------------------------------------------------")
        print("카테고리별 요약:")
        for category, items in sorted(categoryGroups.items()):
            print(f"  - {category:<25}: {len(items):>3}건")
        print("========================================================\n")

        return 0 if not result.listRaw else 1


main = CheckCodeConventionsGate.run


if __name__ == "__main__":
    sys.exit(main())
