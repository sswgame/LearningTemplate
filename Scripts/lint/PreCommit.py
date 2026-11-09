#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/PreCommit.py

Git pre-commit 훅에서 호출되어 staged C++ · 셰이더 파일에 include 순서 검사(`IncludeOrder.py`) · C++ 모양 검사(`CppShape.py`) · 파일 읽기 모양 검사(`ByteReadShape.py`) · 카탈로그 루트 모양 검사(`CatalogRootShape.py`) · Core 티어 역류 검사(`CoreTiers.py`) · 에디터 위젯 이름표 검사(`EditorWidgetLabels.py`) · 숫자 리터럴 래칫(`MagicLiterals.py`) · 결과 형식 래칫(`ResultTypes.py`)과 clang-format 포맷 검사를, 저장소의 CMake 파일에 경로 대소문자 검사(`CMakePathCase.py`)를,
Content · 테스트 CMake 의 변경에 에셋 참조 검사(`AssetReferences.py` — 지우기 · 옮기기만 한 커밋도)를 돌립니다.
검사에 실패하면 커밋을 중단시킵니다. 고치는 것은 `py -3 -m Scripts format` · `format-includes` 다.

**통과한 것은 찍지 않는다.** 통과는 한 줄, 실패만 상세를 찍는다.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ProjectConstants import kShaderSourceExtensions  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.Git import getAllStagedFiles  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from lint.Format import getStagedCppFiles, runClangFormatBatch  # noqa: E402
from lint.AssetReferences import checkStagedAssetReferences  # noqa: E402
from lint.ByteReadShape import checkByteReadShape  # noqa: E402
from lint.CatalogRootShape import checkCatalogRootShape  # noqa: E402
from lint.CommonShapes import checkCommonShapes  # noqa: E402
from lint.ShaderKillOrder import checkShaderKillOrder  # noqa: E402
from lint.CMakePathCase import checkCMakePathCase  # noqa: E402
from lint.CoreTiers import checkCoreTiers  # noqa: E402
from lint.CppShape import checkCppShape  # noqa: E402
from lint.EditorViewIncludes import checkEditorViewIncludes  # noqa: E402
from lint.EditorWidgetLabels import checkEditorWidgetLabels  # noqa: E402
from lint.FeatureReferences import checkFeatureReferences  # noqa: E402
from lint.FrameIntern import checkFrameIntern  # noqa: E402
from lint.NamePathArguments import checkNamePathArguments  # noqa: E402
from lint.TickSceneWalk import checkTickSceneWalk  # noqa: E402
from lint.NamingWords import checkNamingWords  # noqa: E402
from lint.Acronyms import checkAcronyms  # noqa: E402
from lint.FolderLayout import checkFolderLayout  # noqa: E402
from lint.PlatformChains import checkPlatformChains  # noqa: E402
from lint.DevOnlyVariables import checkDevOnlyVariables  # noqa: E402
from lint.ServerUIIncludes import checkServerUIIncludes  # noqa: E402
from generate.ClientOnlyTypes import checkClientOnlyTypes  # noqa: E402
from lint.StateShape import checkStateShape  # noqa: E402
from lint.ThreadOwners import checkThreadOwners  # noqa: E402
from lint.MutableStatics import checkMutableStatics  # noqa: E402
from lint.IncludeOrder import checkIncludeOrder  # noqa: E402
from lint.MagicLiterals import checkMagicLiterals  # noqa: E402
from lint.ResultTypes import checkResultTypes  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    argparse.ArgumentParser(description="Git staged 파일에 커밋 전 검사(include 순서 · clang-format)를 돌린다 — 인자 없음").parse_args(argv)

    projectRoot = getProjectRoot()

    # --- 에셋 참조(경로가 유일한 참조라 끊긴 경로를 커밋 전에 잡는다 — AssetReferences.py 머리말) ---------------
    # 아래 목록(더하기 · 고치기만)보다 먼저 돈다 — 에셋을 지우거나 옮기기만 한 커밋이 남긴 역참조도 잡아야 한다.
    hasErrors = False
    listAssetProblem = checkStagedAssetReferences(projectRoot)
    if listAssetProblem:
        hasErrors = True
        print(f"[AssetReferences] FAIL — 끊어진 에셋 참조 {len(listAssetProblem)}건(옮기기는 `py -3 -m Scripts move-asset <옛> <새>` 가 참조를 함께 고친다)")
        for line in listAssetProblem:
            print(f"  - {line}")
    elif listAssetProblem is not None:
        print("[AssetReferences] OK")

    allStagedFiles = getAllStagedFiles(projectRoot)
    if not allStagedFiles:
        if hasErrors:
            print("\n[PreCommit] [FAIL] 검사에 실패했습니다. 오류를 수정한 후 다시 git add 하고 커밋해주세요.")
            return ExitCode.Violation
        return ExitCode.Passed

    print(f"[PreCommit] {len(allStagedFiles)}개의 Staged 파일에 대해 검사를 시작합니다.")

    stagedCppFiles = getStagedCppFiles(projectRoot)
    stagedShaderFiles = getStagedCppFiles(projectRoot, extensions=kShaderSourceExtensions)

    # --- include 순서 -------------------------------------------------------------
    if stagedCppFiles:
        listViolation, checked = checkIncludeOrder(projectRoot, [str(path) for path in stagedCppFiles])
        if listViolation:
            hasErrors = True
            print(f"[CheckIncludeOrder] FAIL — Include 순서 규칙 위반 {len(listViolation)}건")
            for line in listViolation:
                print(f"  - {line.lstrip('!')}")
        else:
            print(f"[CheckIncludeOrder] OK ({checked} files)")

    # --- C++ 모양(선언 순서 · 깃발 칸 · 익명 네임스페이스 · 주인 우선 · 중괄호 안 include — CppShape.py 머리말) ---------------
    if stagedCppFiles:
        listShapeViolation = checkCppShape(projectRoot, [str(path) for path in stagedCppFiles])
        if listShapeViolation:
            hasErrors = True
            print(f"[CppShape] FAIL — C++ 모양 규칙 위반 {len(listShapeViolation)}건")
            for line in listShapeViolation:
                print(f"  - {line}")

    # --- 상태를 손으로 짝지어 쓰고 읽는 모양(`Archive` 를 받는 함수를 새로 짓는 것을 형식으로 막지 못하는 자리 — StateShape.py 머리말) ---------------
    if stagedCppFiles:
        listStateViolation = checkStateShape(projectRoot, [str(path) for path in stagedCppFiles])
        if listStateViolation:
            hasErrors = True
            print(f"[StateShape] FAIL — 상태 직렬화 모양 위반 {len(listStateViolation)}건")
            for line in listStateViolation:
                print(f"  - {line}")

    # --- 일반 코드의 이름 없는 숫자 리터럴 래칫(리터럴이 매직인지는 형식이 가르지 못한다 — MagicLiterals.py 머리말) ---------------
    if stagedCppFiles:
        listLiteralProblem = checkMagicLiterals(projectRoot, stagedCppFiles)
        if listLiteralProblem:
            hasErrors = True
            print(f"[MagicLiterals] FAIL — 기준선(Config/Dev/MagicLiteralBaseline.json)과 다른 리터럴 수 {len(listLiteralProblem)}건")
            for line in listLiteralProblem:
                print(f"  - {line}")

    # --- 결과 형식 래칫(std::optional · string& outError 는 언어가 주는 모양이라 형식으로 막지 못한다 — ResultTypes.py 머리말) ---------------
    if stagedCppFiles:
        listResultProblem = checkResultTypes(projectRoot, stagedCppFiles)
        if listResultProblem:
            hasErrors = True
            print(f"[ResultTypes] FAIL — 결과 형식 규칙(Core 0 · 그 밖은 Config/Dev/ResultTypeBaseline.json)과 다른 수 {len(listResultProblem)}건")
            for line in listResultProblem:
                print(f"  - {line}")

    # --- 파일에서 읽은 개수를 믿고 할당하는 모양(`vector::resize` 를 형식으로 막지 못하는 자리 — ByteReadShape.py 머리말) ---------------
    if stagedCppFiles:
        listByteReadViolation = checkByteReadShape(projectRoot, [str(path) for path in stagedCppFiles])
        if listByteReadViolation:
            hasErrors = True
            print(f"[ByteReadShape] FAIL — 파일 읽기 모양 위반 {len(listByteReadViolation)}건")
            for line in listByteReadViolation:
                print(f"  - {line}")

    # --- 게임 데이터 루트의 원소를 손으로 고르는 모양(`findChild` 를 형식으로 막지 못하는 자리 — CatalogRootShape.py 머리말) ---------------
    if stagedCppFiles:
        listCatalogRootViolation = checkCatalogRootShape(projectRoot, [str(path) for path in stagedCppFiles])
        if listCatalogRootViolation:
            hasErrors = True
            print(f"[CatalogRootShape] FAIL — 카탈로그 루트 모양 위반 {len(listCatalogRootViolation)}건")
            for line in listCatalogRootViolation:
                print(f"  - {line}")

    # --- Core 티어 역류(Log 경계 밖은 빌드가 가르지 않는 자리 — CoreTiers.py 머리말) ---------------
    if stagedCppFiles:
        listTierViolation = checkCoreTiers(projectRoot, [str(path) for path in stagedCppFiles])
        if listTierViolation:
            hasErrors = True
            print(f"[CoreTiers] FAIL — Core 티어 역류 {len(listTierViolation)}건(Scripts/generate/rules/CoreLayers.toml)")
            for line in listTierViolation:
                print(f"  - {line}")

    # --- 에디터 ImGui 입력 위젯의 보이는 이름표(문자열 내용이라 형식이 가르지 못하는 자리 — EditorWidgetLabels.py 머리말) ---------------
    if stagedCppFiles:
        listLabelViolation = checkEditorWidgetLabels(projectRoot, [str(path) for path in stagedCppFiles])
        if listLabelViolation:
            hasErrors = True
            print(f"[EditorWidgetLabels] FAIL — 칸 오른쪽에 붙는 ImGui 이름표 {len(listLabelViolation)}건")
            for line in listLabelViolation:
                print(f"  - {line}")

    # --- CMake 경로의 대소문자(컴파일러가 보지 않는 자리 — CMakePathCase.py 머리말) ---------------
    listCaseViolation = checkCMakePathCase(projectRoot)
    if listCaseViolation:
        hasErrors = True
        print(f"[CMakePathCase] FAIL — 대소문자만 다른 CMake 경로 {len(listCaseViolation)}건(Linux 구성이 선다)")
        for line in listCaseViolation:
            print(f"  - {line}")

    # --- 표에 없는 함수 앞 단어 · 형식 끝 단어(업계 용어인지는 형식이 말하지 못한다)
    listNamingProblem = checkNamingWords(projectRoot)
    if listNamingProblem:
        hasErrors = True
        print(f"[NamingWords] FAIL — 표에 없는 이름 단어 {len(listNamingProblem)}건")
        for problem in listNamingProblem:
            print(f"  - {problem}")

    # --- 약어 표기(이름은 형식이 막지 못한다 — Acronyms.py 머리말)
    listAcronymProblem = checkAcronyms(projectRoot)
    if listAcronymProblem:
        hasErrors = True
        print(f"[Acronyms] FAIL — 약어 표기 어긋남 {len(listAcronymProblem)}건(Config/Dev/Acronyms.json)")
        for problem in listAcronymProblem:
            print(f"  - {problem}")

    # --- 파일 자리(잡동사니 폴더 · 엔진 쪽 Dev/ · Component 폴더 — FolderLayout.py 머리말)
    listFolderProblem = checkFolderLayout(projectRoot)
    if listFolderProblem:
        hasErrors = True
        print(f"[FolderLayout] FAIL — 파일 자리 어긋남 {len(listFolderProblem)}건(Config/Dev/FolderLayout.json)")
        for problem in listFolderProblem:
            print(f"  - {problem}")

    # --- 플랫폼 #if 사슬(전처리 갈래는 형식이 막지 못한다 — PlatformChains.py 머리말)
    listChainProblem = checkPlatformChains(projectRoot)
    if listChainProblem:
        hasErrors = True
        print(f"[PlatformChains] FAIL — 빠진 플랫폼을 컴파일 오류로 내지 않는 사슬 {len(listChainProblem)}건")
        for line in listChainProblem:
            print(f"  - {line}")
    # --- MVVM 으로 옮긴 에디터 View 의 모델 · 씬 include(옮기는 동안의 목록 — 다 옮기면 View 부분의 #error 껍데기로 바꾼다, EditorViewIncludes.py 머리말)
    listViewIncludeProblem = checkEditorViewIncludes(projectRoot)
    if listViewIncludeProblem:
        hasErrors = True
        print(f"[EditorViewIncludes] FAIL — MVVM View 의 모델 · 씬 include {len(listViewIncludeProblem)}건")
        for line in listViewIncludeProblem:
            print(f"  - {line}")
    # --- 런타임 코드의 테스트용 전역 변수(합법 C++ 라 형식으로 자리를 막지 못한다 — DevOnlyVariables.py 머리말)
    listDevVariableProblem = checkDevOnlyVariables(projectRoot)
    if listDevVariableProblem:
        hasErrors = True
        print(f"[DevOnlyVariables] FAIL — 런타임 코드의 테스트용 변수 {len(listDevVariableProblem)}건")
        for line in listDevVariableProblem:
            print(f"  - {line}")
    # --- 서버도 짓는 게임 코드의 UI include(한 DLL 안의 폴더라 타깃 게이트가 못 가른다 — ServerUIIncludes.py 머리말)
    listServerUIProblem = checkServerUIIncludes(projectRoot)
    if listServerUIProblem:
        hasErrors = True
        print(f"[ServerUIIncludes] FAIL — 서버 대상 게임 코드의 UI include {len(listServerUIProblem)}건")
        for line in listServerUIProblem:
            print(f"  - {line}")
    # --- 클라이언트 전용 형식 이름 표(서버가 씬의 HUD · 마커를 조용히 건너뛰는 근거 — 소스와 어긋나면 서버가 모르는 형식 경고를 낸다)
    listClientOnlyProblem = checkClientOnlyTypes(projectRoot)
    if listClientOnlyProblem:
        hasErrors = True
        print(f"[ClientOnlyTypes] FAIL — {listClientOnlyProblem[0]}")
    # --- 프레임 함수 안의 리터럴 이름(정상 코드라 형식으로 막지 못한다 — FrameIntern.py 머리말)
    listFrameIntern = checkFrameIntern(projectRoot)
    if listFrameIntern:
        hasErrors = True
        print(f"[FrameIntern] FAIL — 프레임 함수 안의 리터럴 이름 {len(listFrameIntern)}건")
        for problem in listFrameIntern:
            print(f"  - {problem}")
    # --- 경로 자리의 c_str() · view()(빈 이름이 "None" 파일을 찾는다 — string_view 는 무엇이든 받아 형식으로 막지 못한다, NamePathArguments.py 머리말)
    listNamePath = checkNamePathArguments(projectRoot)
    if listNamePath:
        hasErrors = True
        print(f"[NamePathArguments] FAIL — 경로 인자에 c_str() · view() 를 그대로 넘긴 곳 {len(listNamePath)}건")
        for problem in listNamePath:
            print(f"  - {problem}")
    # --- 틱에서 닿는 씬 전체 순회(순회는 틱 밖에서는 정상 코드라 형식으로 막지 못한다 — TickSceneWalk.py 머리말)
    listTickSceneWalk = checkTickSceneWalk(projectRoot)
    if listTickSceneWalk:
        hasErrors = True
        print(f"[TickSceneWalk] FAIL — 틱에서 씬 전체를 도는 곳 {len(listTickSceneWalk)}건")
        for problem in listTickSceneWalk:
            print(f"  - {problem}")

    # --- 작업 시스템 밖의 스레드(정상 형식이라 형식으로 막지 못한다 — ThreadOwners.py 머리말) ---------------
    listThreadProblem = checkThreadOwners(projectRoot)
    if listThreadProblem:
        hasErrors = True
        print(f"[ThreadOwners] FAIL — 허용 목록 밖의 자체 스레드 {len(listThreadProblem)}건")
        for line in listThreadProblem:
            print(f"  - {line}")

    # --- 가변 정적 저장소(합법 C++ 이라 형식으로 막지 못한다 — 감춘 싱글턴, MutableStatics.py 머리말) ---------------
    listMutableStaticProblem = checkMutableStatics(projectRoot)
    if listMutableStaticProblem:
        hasErrors = True
        print(f"[MutableStatics] FAIL — 주인 객체 밖의 가변 정적 {len(listMutableStaticProblem)}건")
        for line in listMutableStaticProblem:
            print(f"  - {line}")

    # --- 참조 0 인 컴포넌트 · 감시 값 · 개발 명령(쓰는 곳은 형식이 말하지 못한다 — FeatureReferences.py 머리말) ---------------
    listFeatureProblem = checkFeatureReferences(projectRoot)
    if listFeatureProblem:
        hasErrors = True
        print(f"[FeatureReferences] FAIL — 쓰는 곳이 없거나 낡은 예외 {len(listFeatureProblem)}건")
        for line in listFeatureProblem:
            print(f"  - {line}")

    # --- 공통 기반이 있는데 손으로 다시 적은 모양(합법 C++ 이라 형식이 가르지 못한다 — CommonShapes.py 머리말) ---------------
    listCommonProblem = checkCommonShapes(projectRoot)
    if listCommonProblem:
        hasErrors = True
        print(f"[CommonShapes] FAIL — 공통 기반을 두고 손으로 적은 모양 {len(listCommonProblem)}건")
        for line in listCommonProblem:
            print(f"  - {line}")

    # --- 픽셀 버리기 뒤의 미분 읽기(HLSL 에 미분을 쓰는 함수를 적을 형식이 없다 — ShaderKillOrder.py 머리말) ---------------
    listKillOrderProblem = checkShaderKillOrder(projectRoot)
    if listKillOrderProblem:
        hasErrors = True
        print(f"[ShaderKillOrder] FAIL — 픽셀을 버린 뒤 미분을 쓰는 읽기 {len(listKillOrderProblem)}건")
        for line in listKillOrderProblem:
            print(f"  - {line}")
    # --- clang-format(셰이더도 같은 설정, Content/.clang-format) -------------------
    if stagedCppFiles or stagedShaderFiles:
        if runClangFormatBatch(stagedCppFiles + stagedShaderFiles, checkOnly=True, cwd=projectRoot) != 0:
            hasErrors = True
            print("  [Error] 포맷팅 규칙에 어긋나는 파일이 있습니다. "
                  "'py -3 -m Scripts format' 으로 자동 수정한 뒤 다시 git add 하세요.")
        else:
            print(f"[포맷] C++ 파일 {len(stagedCppFiles)}개 · 셰이더 {len(stagedShaderFiles)}개 통과")

    if hasErrors:
        print("\n[PreCommit] [FAIL] 검사에 실패했습니다. 오류를 수정한 후 다시 git add 하고 커밋해주세요.")
        return ExitCode.Violation

    print("\n[PreCommit] [OK] 모든 검사를 통과했습니다.")
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
