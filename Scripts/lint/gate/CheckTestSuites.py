#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
테스트 스위트 규칙 검사.

스위트 이름은 장식이 아니다. CI 가 못 돌리는 것은 **스위트 단위로** 선언되어 빠지고(`SW_TEST_REQUIRES_HOST`),
`--test_filter` 도 스위트 단위로 고른다. 그래서 이름이 흔들리면 그 둘이 흔들린다.

강제 규칙 열:

  1) 스위트 이름은 `XxxTest` — 대문자로 시작하고 `Test` 로 끝나며 밑줄이 없다.
     관례가 섞이면 필터가 흔들린다 — `Core` 라는 스위트가 있으면 `--test_filter=Core*` 가 `Core_String` 까지 끌어온다.
     계층 접두어(`Core_` · `Engine_`)는 **실행 파일 이름이 이미 말한다** — CoreTest.exe 안의 `Core_String` 은
     같은 말을 두 번 하고, 파일이 옮겨지면 접두어가 거짓말이 된다(`Engine_Event` 가 CoreTest 에 있는 식).

  2) 한 스위트는 **한 파일에만** 산다. 갈라져 있으면 "이 스위트를 고치려면 어디를 여나" 에 답이 둘이 된다.

  3) CI 가 못 돌리는 스위트는 자기 파일에서 `SW_TEST_REQUIRES_HOST( 스위트, "이유" );` 로 선언한다.
     **선언이 곧 분류다** — 테스트 실행 파일이 그 선언을 들고 있다가 `--host_suites=exclude|only` 로 가르고,
     `sw_addTestExecutable( ... HOST_SPLIT )` 가 그 둘을 `<타깃>_NoGPU` · `<타깃>_HostOnly` 로 등록한다.
     이 검사가 보는 것은 선언이 **실제로 효력이 있는가** 다:
       - 선언한 스위트가 그 파일에 있는가(이름을 바꾸고 선언을 놓치면 그 스위트가 CI 로 들어간다).
       - 그 폴더의 실행 파일이 `Test/TestPolicy.json` 에서 `hostSplit` 인가(아니면 선언을 아무도 읽지 않는다 — CI 가 그것을 돈다).
       - 거꾸로 `hostSplit` 인데 선언이 하나도 없으면 `_HostOnly` 는 빈 그물이다.
       - 주석 형태의 마커(`// SW_TEST_REQUIRES_HOST( X ): ...`)는 아무 효력이 없으므로 있으면 실패다.

  4) 마커가 붙은 스위트가 있는 파일에는 **다른 스위트를 두지 않는다.**
     호스트 스위트와 CI 스위트가 한 파일에 섞여 있으면 새 케이스를 옆 스위트에 붙이기가 너무 쉽다 —
     그 순간 GPU 가 필요한 케이스가 CI 로 들어간다.

  5) `EditorTest` 가 끌어오는 Editor 소스는 **존재해야 하고, ImGui 헤더를 include 하지 않아야 한다.**
     `EditorModule` 은 MODULE DLL 이라 링크할 수 없어서, 테스트가 필요한 `.cpp` 만 손으로 나열해 같이
     컴파일한다. 그 목록이 "Editor/Common 전부" 도 "ImGui 안 쓰는 것 전부" 도 아니라 **테스트가 실제로
     링크해야 하는 것** 이라 기계가 대신 고를 수 없다. 그래서 목록 자체는 손으로 두되, 썩는 두 방향만 막는다:
     파일이 옮겨져 경로가 죽는 것과, ImGui 를 타는 파일이 섞여 들어오는 것
     (EditorTest 는 ImGui 를 링크하지 않으므로 그 순간 빌드가 깨진다).

  6) `CoreTest` 는 **엔진 없이** Core 를 테스트한다 — `Test/CoreTest` 의 파일은 Engine · GameFramework · Editor · Games · App 헤더를
     include 하지 않고, 코드에서 `engine::` 를 부르지 않는다(주석은 보지 않는다). 엔진 타입이 필요한 테스트는 EngineTest 에 둔다.
     include 경로로는 막을 수 없다: 공용 `TestFramework` 가 Engine 을 PUBLIC 링크하고 `TestFramework.h` 가 `EngineMinimal.h` 를
     끌어온다. 그래서 이 규칙이 보는 것은 CoreTest 파일이 **직접** 쓰는 것이다.

  7) 태그를 단 테스트 doc 블록(`@brief [스위트] …`)은 **자기 `SW_TEST_CASE` 바로 위**에 있고, 태그는 그 케이스의 스위트 이름이다.
     블록과 케이스 사이에 빈 줄이 끼거나 블록 둘이 겹치면, 블록을 옮기거나 케이스를 지울 때 블록만 남아 다른 케이스를 설명하게 된다.

  8) 테스트 실행 파일과 등록 정책(`Test/TestPolicy.json`)이 한 짝이다 — 라벨 · 샤드 · 시간은 그 파일 한 곳에서만 정한다.
       - `sw_addTestExecutable(<이름>)` 은 `Test/<이름>/` 에 있고 정책에 같은 이름의 줄이 있다. 줄만 있고 실행 파일이 없어도 실패다.
       - 줄의 영역(`area`)과 스크립트 테스트의 `LABELS` 는 정책의 `labels` 표에 있다(모르는 라벨은 `ctest -L` 로 고를 수 없다).
       - 비용표(`caseCosts`)가 가리키는 케이스는 그 폴더에 있다(이름을 바꾸면 비용이 조용히 사라져 샤드가 기운다).
       - 레이스 묶음(`raceCases`, `Suite.Case` · `Suite.*`)이 가리키는 케이스 · 스위트도 그 폴더에 있다.

  9) 부하 스위트(`SW_TEST_LOAD_SUITE( 스위트, "이유" );`)는 자기 파일을 혼자 쓰고, 그 폴더의 정책 줄에 `loadSeconds` 가 있다
     (없으면 `<타깃>_Load` 항목이 없어 그 스위트는 어디서도 돌지 않는다).

 10) 활성이 아닌 게임 팩의 원본을 읽는 일반 케이스는 `SW_TEST_REQUIRES_FEATURE( SourceResources )` 를 선언한다.
     Shipping 은 활성 게임(`game/empty`)의 구운 팩만 두고 낱개 원본을 읽지 않는다. 케이스 본문이나 그 케이스가 쓰는 같은 파일의 상수에
     `game/<다른 게임>/` 리터럴이 있는데 선언이 없으면 CI Linux Shipping 에서 진다 — 사람이 기억하던 것을 이 검사가 기억한다.
     "다른 게임" 은 `Resource/game/` 에 실제로 있는 폴더에서 `_kPackLiteralAllow`(활성 게임 · 임시 폴더 픽스처)를 뺀 것이다 —
     디스크에 닿지 않는 순수 문자열 픽스처(`game/prefabs/…`, `game/a/…`)는 걸리지 않는다. 부하 스위트는 보지 않는다.

     shortcut: 문자열 리터럴만 본다 — 경로를 이어 붙이거나(`"game/" + name`) 헤더 · 다른 파일의 상수 · 도우미 함수 안에서 읽는 것은 못 본다.
     파일 안 상수는 선언부 이름(`이름 = "…game/x/…"`)을 케이스 본문의 단어와 맞춰 쓰는 케이스를 고르고, 이름을 못 찾는 리터럴은 그 파일의 모든 케이스로 번진다.

  python Scripts/lint/gate/CheckTestSuites.py [--root <repo>]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.CodeText import blankComments, blankCommentsAndLiterals  # noqa: E402
from common.Search import collectRepositoryFiles  # noqa: E402
from lint.LintGate import GateResult, LintGate  # noqa: E402

_kTestRoot = "Test"
_kEditorTestCMake = "Test/EditorTest/CMakeLists.txt"
_kPolicyFile = "Test/TestPolicy.json"

# 줄 맨 앞만 보면 안 된다 — `namespace sw::editor { ... }` 안에 들여쓴 케이스가 있으면 그 파일이 검사에서
# 통째로 빠진다.
_kCaseRe = re.compile(r"^[ 	]*SW_TEST_CASE\(\s*(\w+)\s*,\s*(\w+)\s*\)", re.M)
# 위 정규식이 놓치는 표기가 생기면 조용히 줄어들 뿐이라, 토큰을 따로 세어 대조한다.
_kCaseTokenRe = re.compile(r"\bSW_TEST_CASE\s*\(")
_kSuiteNameRe = re.compile(r"^[A-Z][A-Za-z0-9]*Test$")
_kMarkerRe = re.compile(r'^[ \t]*SW_TEST_REQUIRES_HOST\(\s*(\w+)\s*,\s*"([^"]*)"\s*\)\s*;', re.M)
# 선언도 케이스처럼 토큰을 따로 세어 대조한다 - 여러 줄로 쪼갠 표기를 놓치면 그 스위트가 조용히 CI 로 간다.
_kMarkerTokenRe = re.compile(r"^[ \t]*SW_TEST_REQUIRES_HOST\s*\(", re.M)
_kLegacyMarkerRe = re.compile(r"//\s*SW_TEST_REQUIRES_HOST\(\s*(\w+)\s*\)\s*:")
# 바깥 환경(DB 서버)이 있어야 도는 스위트 — 변수가 비면 그 스위트만 빠진다. 호스트 스위트처럼 파일을 혼자 쓴다.
_kEnvironmentMarkerRe = re.compile(r'^[ \t]*SW_TEST_REQUIRES_ENVIRONMENT\(\s*(\w+)\s*,\s*"(\w+)"\s*,\s*"([^"]*)"\s*\)\s*;', re.M)
_kEnvironmentMarkerTokenRe = re.compile(r"^[ \t]*SW_TEST_REQUIRES_ENVIRONMENT\s*\(", re.M)
_kLoadMarkerRe = re.compile(r'^[ \t]*SW_TEST_LOAD_SUITE\(\s*(\w+)\s*,\s*"([^"]*)"\s*\)\s*;', re.M)
_kLoadMarkerTokenRe = re.compile(r"^[ \t]*SW_TEST_LOAD_SUITE\s*\(", re.M)
_kAddExecutableRe = re.compile(r"^[ \t]*sw_addTestExecutable\(\s*(\w+)", re.M)
_kScriptLabelsRe = re.compile(r"\bLABELS\s+\"([^\"]*)\"")
_kEditorSourceRe = re.compile(r"\$\{CMAKE_SOURCE_DIR\}/(Source/Editor/[\w/]+\.cpp)")
_kImGuiIncludeRe = re.compile(r"^\s*#\s*include\s*[<\"][^>\"]*imgui[^>\"]*[>\"]", re.I | re.M)
_kCoreTestFolder = "Test/CoreTest"
_kCoreTestForbiddenIncludeRe = re.compile(r"^[ \t]*#[ \t]*include[ \t]*[<\"]((?:Engine|GameFramework|Editor|Games|App)/[^>\"]*)[>\"]", re.M)
_kEngineNamespaceRe = re.compile(r"\bengine::\w+")
_kDocBlockTagRe = re.compile(r"@brief\s+\[(\w+)\]")
_kPackRoot = "Resource/game"
_kSourceResourcesRe = re.compile(r"\bSW_TEST_REQUIRES_FEATURE\(\s*SourceResources\s*\)")
_kPackLiteralRe = re.compile(r"(?<![\w])game/(\w+)\b")
_kDeclaredNameRe = re.compile(r"(\w+)\s*(?:\[\s*\])?\s*[={]")

# 규칙 10 에서 `game/<이름>/` 리터럴을 봐도 되는 이름 — 나머지 실제 게임 팩은 `SourceResources` 선언이 있어야 읽는다.
_kPackLiteralAllow: dict[str, str] = {
    "empty": "활성 게임 — Shipping 도 구운 팩으로 갖고 있다",
    "test": "임시 폴더 픽스처 이름(디스크의 실제 팩이 아니다)",
    "x": "임시 폴더 픽스처 이름(디스크의 실제 팩이 아니다)",
    "demo": "임시 폴더 픽스처 이름(디스크의 실제 팩이 아니다)",
    "probe": "임시 폴더 픽스처 이름(디스크의 실제 팩이 아니다)",
}
# 리터럴은 있지만 **경로 문자열만** 다루고 디스크의 팩을 읽지 않는 케이스 — 리터럴만으로는 읽기와 구별되지 않아 케이스 단위로 선언한다.
# 낡은 줄(케이스가 사라졌거나 리터럴이 없어짐)은 위반이다. 선언을 더하면 Shipping 에서 그 케이스가 건너뛰어져 커버리지를 잃으므로 선언 대신 여기에 둔다.
_kPackLiteralPathOnlyCases: dict[str, str] = {
    "AssetHotReloadTest.AudioDataReloadsThroughTheEngineAudioCache": "에디터 에셋 종류 표가 경로 문자열로 다시 읽기 경로를 고른다",
    "EditorAssetTypeTest.ReloadRouteComesFromTheTable": "에디터 에셋 종류 표가 경로 문자열로 다시 읽기 경로를 고른다",
    "EditorAssetTypeTest.UIDocumentOpensTheUIPreviewPanel": "경로 문자열의 종류 판정",
    "EditorWindowTitleTest.SceneNameIsTheLastPathSegment": "창 제목을 경로 문자열에서 만든다",
    "GamePackDataTest.KindPathComesFromTheRegistration": "팩 경로 조립(문자열)",
    "GamePackDataTest.MissingRuleIsAnError": "팩 경로 조립(문자열)",
    "NavMeshBakeTest.CookedAssetRoundTripsTilesByteForByte": "쿠킹 경로 이름 규칙(문자열)",
    "ResourceDataKindTest.ModulesRegisterTheirCatalogKinds": "데이터 종류의 경로 일치 판정(문자열)",
    "ResourceHostTargetTest.DedicatedServerDoesNotReadExcludedKinds": "호스트 제외 판정(문자열) — 읽는 쪽은 활성 게임 경로",
    "ResourcePakTest.CollectResourcesTellsMissingFolderFromFailure": "없는 폴더를 묻는다 — 팩이 있든 없든 FolderMissing",
}


def listTestSourceInternal(rootDir: Path) -> list[Path]:
    """`Test/` 아래 .cpp 전부(빌드 산출물 · 생성 폴더로는 내려가지 않는다 — `collectRepositoryFiles`)."""
    return collectRepositoryFiles(rootDir, (_kTestRoot,), suffixes=(".cpp",))


def collectCases(rootDir: Path) -> tuple[list[tuple[str, str, str]], list[str]]:
    """(스위트, 케이스, 저장소 상대 경로) 전부와, 파싱이 놓친 자리."""
    out: list[tuple[str, str, str]] = []
    missed: list[str] = []
    for path in listTestSourceInternal(rootDir):
        relPath = path.relative_to(rootDir).as_posix()
        text = path.read_text(encoding="utf-8", errors="ignore")
        parsed = list(_kCaseRe.finditer(text))
        out += [(m.group(1), m.group(2), relPath) for m in parsed]
        tokenCount = len(_kCaseTokenRe.findall(text))
        if tokenCount != len(parsed):
            missed.append(f"{relPath}: SW_TEST_CASE 를 {tokenCount}개 썼는데 {len(parsed)}개만 읽혔습니다 "
                          f"— 이 검사가 그 파일의 스위트를 놓치고 있습니다 (표기를 맞추거나 파서를 고치세요)")
    return out, missed


def collectMarkers(rootDir: Path) -> tuple[dict[str, tuple[str, str]], list[str]]:
    """(스위트 -> (파일, 이유), 오류들). 선언은 그 스위트가 사는 파일에 둔다."""
    out: dict[str, tuple[str, str]] = {}
    errors: list[str] = []
    for path in listTestSourceInternal(rootDir):
        relPath = path.relative_to(rootDir).as_posix()
        text = path.read_text(encoding="utf-8", errors="ignore")
        parsed = list(_kMarkerRe.finditer(text))
        for match in parsed:
            out[match.group(1)] = (relPath, match.group(2).strip())
        tokenCount = len(_kMarkerTokenRe.findall(text))
        if tokenCount != len(parsed):
            errors.append(f"{relPath}: SW_TEST_REQUIRES_HOST 를 {tokenCount}개 썼는데 {len(parsed)}개만 읽혔습니다 "
                          f"— 한 줄에 `SW_TEST_REQUIRES_HOST( 스위트, \"이유\" );` 로 쓰세요")
        for legacy in _kLegacyMarkerRe.finditer(text):
            errors.append(f"{relPath}: 옛 주석 마커 `// SW_TEST_REQUIRES_HOST( {legacy.group(1)} ): ...` 는 아무 효력이 "
                          f"없습니다 — `SW_TEST_REQUIRES_HOST( {legacy.group(1)}, \"이유\" );` 로 선언하세요")
    return out, errors


def collectEnvironmentMarkers(rootDir: Path) -> tuple[dict[str, tuple[str, str]], list[str]]:
    """(스위트 -> (파일, 이유), 오류들) — `SW_TEST_REQUIRES_ENVIRONMENT` 선언."""
    out: dict[str, tuple[str, str]] = {}
    errors: list[str] = []
    for path in listTestSourceInternal(rootDir):
        relPath = path.relative_to(rootDir).as_posix()
        text = path.read_text(encoding="utf-8", errors="ignore")
        parsed = list(_kEnvironmentMarkerRe.finditer(text))
        for match in parsed:
            out[match.group(1)] = (relPath, match.group(3).strip())
        tokenCount = len(_kEnvironmentMarkerTokenRe.findall(text))
        if tokenCount != len(parsed):
            errors.append(f"{relPath}: SW_TEST_REQUIRES_ENVIRONMENT 를 {tokenCount}개 썼는데 {len(parsed)}개만 읽혔습니다 "
                          f"— 한 줄에 `SW_TEST_REQUIRES_ENVIRONMENT( 스위트, \"변수\", \"이유\" );` 로 쓰세요")
    return out, errors


def collectLoadMarkers(rootDir: Path) -> tuple[dict[str, tuple[str, str]], list[str]]:
    """(스위트 -> (파일, 이유), 오류들) — `SW_TEST_LOAD_SUITE` 선언."""
    out: dict[str, tuple[str, str]] = {}
    errors: list[str] = []
    for path in listTestSourceInternal(rootDir):
        relPath = path.relative_to(rootDir).as_posix()
        text = path.read_text(encoding="utf-8", errors="ignore")
        parsed = list(_kLoadMarkerRe.finditer(text))
        for match in parsed:
            out[match.group(1)] = (relPath, match.group(2).strip())
        tokenCount = len(_kLoadMarkerTokenRe.findall(text))
        if tokenCount != len(parsed):
            errors.append(f"{relPath}: SW_TEST_LOAD_SUITE 를 {tokenCount}개 썼는데 {len(parsed)}개만 읽혔습니다 "
                          f"— 한 줄에 `SW_TEST_LOAD_SUITE( 스위트, \"이유\" );` 로 쓰세요")
    return out, errors


def readPolicy(rootDir: Path) -> tuple[dict, list[str]]:
    """`Test/TestPolicy.json` 과 읽기 오류."""
    path = rootDir / _kPolicyFile
    if path.exists() is False:
        return {}, [f"{_kPolicyFile}: 파일이 없습니다 — 테스트 실행 파일의 라벨 · 샤드 · 시간을 정하는 곳입니다"]
    try:
        policy = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        return {}, [f"{_kPolicyFile}: JSON 이 아닙니다 ({error})"]
    # 필수 표 — 빠지면 위반으로 알리고 빈 정책으로 나머지를 본다(아래의 `.get(…, {})` 는 이미 알린 이 경우만 덮는다).
    listMissing = [key for key in ("labels", "executables") if key not in policy]
    if listMissing:
        return {}, [f"{_kPolicyFile}: 필수 표 {listMissing} 가 없습니다"]
    return policy, []


def collectHostSplitFolders(policy: dict) -> set[str]:
    """정책에서 `hostSplit` 인 실행 파일의 폴더(`Test/<이름>`)."""
    return {f"{_kTestRoot}/{name}" for name, row in policy.get("executables", {}).items() if row.get("hostSplit") is True}


def checkPolicy(rootDir: Path, policy: dict, cases: list[tuple[str, str, str]]) -> list[str]:
    """실행 파일 ↔ 정책 줄 · 라벨 · 비용표가 한 짝인지 봅니다(규칙 8)."""
    errors: list[str] = []
    labels = set(policy.get("labels", {}))
    rows: dict = policy.get("executables", {})
    declared: dict[str, str] = {}
    for cmakePath in collectRepositoryFiles(rootDir, (_kTestRoot,), fileNames=("CMakeLists.txt",)):
        relPath = cmakePath.relative_to(rootDir).as_posix()
        code = "\n".join(line.split("#", 1)[0] for line in cmakePath.read_text(encoding="utf-8", errors="ignore").splitlines())
        for match in _kAddExecutableRe.finditer(code):
            name = match.group(1)
            declared[name] = relPath
            if cmakePath.parent.name != name:
                errors.append(f"{relPath}: `sw_addTestExecutable({name})` — 폴더 이름(`{cmakePath.parent.name}`)과 실행 파일 이름이 같아야 합니다")
            if name not in rows:
                errors.append(f"{relPath}: `{name}` 의 줄이 {_kPolicyFile} 에 없습니다 — 영역(area)과 한 실행 시간(seconds)을 더하세요")
        for match in _kScriptLabelsRe.finditer(code):
            for label in filter(None, match.group(1).split(";")):
                if label not in labels:
                    errors.append(f"{relPath}: 라벨 `{label}` 이 {_kPolicyFile} 의 labels 표에 없습니다")
    casesByFolder: dict[str, set[str]] = defaultdict(set)
    for suite, caseName, relPath in cases:
        casesByFolder[testFolderOf(relPath)].add(f"{suite}.{caseName}")
    for name, row in sorted(rows.items()):
        if name not in declared:
            errors.append(f"{_kPolicyFile}: `{name}` 줄이 있는데 `Test/{name}/CMakeLists.txt` 의 `sw_addTestExecutable({name})` 이 없습니다")
        area = row.get("area")
        if area not in labels:
            errors.append(f"{_kPolicyFile}: `{name}` 의 영역 `{area}` 이 labels 표에 없습니다")
        for caseName in [*row.get("caseCosts", {}), *row.get("hostCaseCosts", {})]:
            if caseName not in casesByFolder.get(f"{_kTestRoot}/{name}", set()):
                errors.append(f"{_kPolicyFile}: `{name}` 의 비용표가 없는 케이스 `{caseName}` 을 가리킵니다 — 이름을 바꿨으면 비용표도 바꾸세요")
        folderCases = casesByFolder.get(f"{_kTestRoot}/{name}", set())
        folderSuites = {caseName.split(".", 1)[0] for caseName in folderCases}
        for bundleKey, bundleName in (("raceCases", "레이스 묶음"), ("softwareRenderCases", "소프트웨어 렌더러 묶음")):
            for pattern in row.get(bundleKey, []):
                bKnown = pattern[:-2] in folderSuites if pattern.endswith(".*") else pattern in folderCases
                if not bKnown:
                    errors.append(f"{_kPolicyFile}: `{name}` 의 {bundleName}({bundleKey})이 없는 케이스 `{pattern}` 을 가리킵니다 — 이름을 바꿨으면 묶음도 바꾸세요")
    return errors


def testFolderOf(relPath: str) -> str:
    """`Test/EngineTest/Graphics/TestRHIDevice.cpp` -> `Test/EngineTest`. 그 폴더의 CMakeLists 가 실행 파일 하나를 만든다."""
    return "/".join(relPath.split("/")[:2])


def checkEditorTestSources(rootDir: Path) -> list[str]:
    """EditorTest 가 나열한 Editor 소스가 살아 있고 ImGui 를 타지 않는지 봅니다."""
    path = rootDir / _kEditorTestCMake
    if path.exists() is False:
        return [f"{_kEditorTestCMake}: 파일이 없습니다"]
    listed = _kEditorSourceRe.findall(path.read_text(encoding="utf-8", errors="ignore"))
    if not listed:
        return [f"{_kEditorTestCMake}: Editor 소스 항목을 하나도 찾지 못했습니다 (검사가 헛돌고 있습니다)"]

    errors: list[str] = []
    for relPath in listed:
        sourcePath = rootDir / relPath
        if sourcePath.exists() is False:
            errors.append(f"{_kEditorTestCMake}: `{relPath}` 가 없습니다 — 파일이 옮겨졌으면 경로를 고치세요")
            continue
        for candidate in (sourcePath, sourcePath.with_suffix(".h")):
            if candidate.exists() is False:
                continue
            match = _kImGuiIncludeRe.search(candidate.read_text(encoding="utf-8", errors="ignore"))
            if match is not None:
                errors.append(
                    f"{candidate.relative_to(rootDir).as_posix()}: EditorTest 가 이 파일을 컴파일하는데 "
                    f"ImGui 를 include 합니다 (`{match.group(0).strip()}`) — EditorTest 는 ImGui 를 링크하지 "
                    f"않습니다. 테스트가 쓰는 부분을 ImGui 없는 TU 로 가르세요"
                )
    return errors


def checkCoreTestIsEngineFree(rootDir: Path) -> list[str]:
    """`Test/CoreTest` 의 파일이 엔진 계층 헤더를 include 하거나 `engine::` 를 부르지 않는지 봅니다."""
    folder = rootDir / _kCoreTestFolder
    if folder.is_dir() is False:
        return []
    errors: list[str] = []
    for path in collectRepositoryFiles(rootDir, (_kCoreTestFolder,), suffixes=(".cpp", ".h")):
        relPath = path.relative_to(rootDir).as_posix()
        code = blankComments(path.read_text(encoding="utf-8", errors="ignore"))
        for match in _kCoreTestForbiddenIncludeRe.finditer(code):
            line = code.count("\n", 0, match.start()) + 1
            errors.append(f"{relPath}:{line}: CoreTest 가 `{match.group(1)}` 를 include 합니다 — CoreTest 는 엔진 없이 Core 를 "
                          f"테스트합니다. 엔진 타입이 필요한 테스트는 EngineTest 로 옮기거나 지역 대역을 쓰세요")
        for match in _kEngineNamespaceRe.finditer(code):
            line = code.count("\n", 0, match.start()) + 1
            errors.append(f"{relPath}:{line}: CoreTest 가 `{match.group(0)}` 를 부릅니다 — 프로세스의 엔진 서비스 대신 "
                          f"지역 객체(예: 지역 GlobalVariableManager)를 쓰거나 EngineTest 로 옮기세요")
    return errors


def checkTestDocBlocks(rootDir: Path) -> list[str]:
    """태그(`@brief [스위트]`)를 단 doc 블록이 자기 `SW_TEST_CASE` 바로 위에 있고 태그가 그 스위트인지 봅니다."""
    errors: list[str] = []
    for path in listTestSourceInternal(rootDir):
        relPath = path.relative_to(rootDir).as_posix()
        lines = path.read_text(encoding="utf-8", errors="ignore").split("\n")
        lineIndex = 0
        while lineIndex < len(lines):
            stripped = lines[lineIndex].strip()
            if stripped.startswith("/**") is False or stripped.startswith("/**<"):
                lineIndex += 1
                continue
            beginIndex = lineIndex
            while lineIndex < len(lines) and "*/" not in lines[lineIndex]:
                lineIndex += 1
            tagMatch = _kDocBlockTagRe.search("\n".join(lines[beginIndex : lineIndex + 1]))
            lineIndex += 1
            if tagMatch is None:
                continue
            tag = tagMatch.group(1)
            caseMatch = _kCaseRe.match(lines[lineIndex]) if lineIndex < len(lines) else None
            if caseMatch is None:
                errors.append(f"{relPath}:{beginIndex + 1}: `[{tag}]` doc 블록이 자기 SW_TEST_CASE 바로 위에 있지 않습니다 "
                              f"(사이의 빈 줄 · 다른 블록을 지우거나, 설명할 케이스가 없으면 블록을 지우세요)")
            elif caseMatch.group(1) != tag:
                errors.append(f"{relPath}:{beginIndex + 1}: doc 블록 태그 `[{tag}]` 가 케이스의 스위트 `{caseMatch.group(1)}` 와 다릅니다")
    return errors


def listForeignPacks(rootDir: Path) -> set[str]:
    """`Resource/game/` 의 실제 게임 팩 폴더에서 허용 이름을 뺀 것 — Shipping 의 구운 팩에 없는 원본."""
    packRoot = rootDir / _kPackRoot
    if packRoot.is_dir() is False:
        return set()
    return {entry.name for entry in packRoot.iterdir() if entry.is_dir()} - set(_kPackLiteralAllow)


def findCaseBodies(code: str, maskedCode: str) -> list[tuple[re.Match, int, int]]:
    """(케이스 머리 매치, 본문 `{` 위치, 본문 끝 위치). 중괄호는 문자열 · 주석을 가린 글(`maskedCode`)로 센다."""
    out: list[tuple[re.Match, int, int]] = []
    for match in _kCaseRe.finditer(code):
        begin = maskedCode.find("{", match.end())
        if begin < 0:
            continue
        depth = 0
        for index in range(begin, len(maskedCode)):
            char = maskedCode[index]
            if char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    out.append((match, begin, index + 1))
                    break
    return out


def checkSourceResourcesDeclared(rootDir: Path, loadSuites: set[str], existingCases: set[str]) -> list[str]:
    """활성이 아닌 게임 팩의 원본을 읽는 일반 케이스가 `SourceResources` 를 선언했는지 봅니다(규칙 10)."""
    if (rootDir / _kPackRoot).is_dir() is False:
        return [f"{_kPackRoot}: 폴더가 없습니다 (규칙 10 이 헛돌고 있습니다)"]
    foreign = listForeignPacks(rootDir)
    errors: list[str] = []
    usedPathOnly: set[str] = set()
    for path in listTestSourceInternal(rootDir):
        relPath = path.relative_to(rootDir).as_posix()
        code = blankComments(path.read_text(encoding="utf-8", errors="ignore"))
        if "game/" not in code:
            continue
        bodies = [row for row in findCaseBodies(code, blankCommentsAndLiterals(code)) if row[0].group(1) not in loadSuites]
        if not bodies:
            continue
        # 케이스 밖(상수 · 도우미): 본문을 공백으로 지운 글에서 리터럴과 선언부 이름을 찾는다.
        outside = list(code)
        for _, begin, end in bodies:
            outside[begin:end] = [" " if char != "\n" else "\n" for char in code[begin:end]]
        outsideCode = "".join(outside)
        # 케이스 -> {팩: 줄}. 본문에서 직접 보인 것 먼저, 파일 상수는 이름이 본문에 나오는 케이스에 번진다.
        found: list[dict[str, int]] = [{} for _ in bodies]
        for index, (_, begin, end) in enumerate(bodies):
            for literal in _kPackLiteralRe.finditer(code, begin, end):
                if literal.group(1) in foreign:
                    found[index].setdefault(literal.group(1), code.count("\n", 0, literal.start()) + 1)
        for literal in _kPackLiteralRe.finditer(outsideCode):
            if literal.group(1) not in foreign:
                continue
            statementBegin = max(outsideCode.rfind(mark, 0, literal.start()) for mark in (";", "}")) + 1
            names = _kDeclaredNameRe.findall(outsideCode[statementBegin : literal.start()])
            line = outsideCode.count("\n", 0, literal.start()) + 1
            for index, (_, begin, end) in enumerate(bodies):
                if names and re.search(rf"\b{re.escape(names[-1])}\b", code[begin:end]) is None:
                    continue
                found[index].setdefault(literal.group(1), line)
        for (caseMatch, begin, end), packs in zip(bodies, found):
            caseKey = f"{caseMatch.group(1)}.{caseMatch.group(2)}"
            if packs and caseKey in _kPackLiteralPathOnlyCases:
                usedPathOnly.add(caseKey)
                continue
            if not packs or _kSourceResourcesRe.search(code, begin, end) is not None:
                continue
            for pack, line in sorted(packs.items()):
                errors.append(f"{relPath}:{line}: `{caseMatch.group(1)}.{caseMatch.group(2)}` 가 활성이 아닌 게임 팩 `game/{pack}/` 을 읽는데 "
                              f"`SW_TEST_REQUIRES_FEATURE( SourceResources )` 가 없습니다 — Shipping 은 활성 게임의 구운 팩만 둬서 "
                              f"CI Linux Shipping 에서 집니다(케이스 맨 앞에 선언하세요. 경로 문자열만 다루면 `_kPackLiteralPathOnlyCases` 에 사유와 함께 두세요)")
    # 케이스가 있는데 리터럴이 사라졌으면 낡은 줄이다(케이스가 없으면 아무 일도 하지 않는 줄이라 두고, 이름을 바꾼 케이스는 위에서 걸린다).
    for caseKey in sorted((set(_kPackLiteralPathOnlyCases) & existingCases) - usedPathOnly):
        errors.append(f"{_kTestRoot}: `_kPackLiteralPathOnlyCases` 의 `{caseKey}` 가 낡았습니다 — 활성이 아닌 팩 리터럴이 더는 없습니다 "
                      f"(Scripts/lint/gate/CheckTestSuites.py 에서 그 줄을 지우세요)")
    return errors


def check(rootDir: Path) -> tuple[list[str], int, int]:
    cases, missed = collectCases(rootDir)
    errors: list[str] = list(missed)
    if not cases:
        return [f"{_kTestRoot}: SW_TEST_CASE 를 하나도 찾지 못했습니다 (검사가 헛돌고 있습니다)"], 0, 0

    homes: dict[str, set[str]] = defaultdict(set)
    firstSite: dict[str, str] = {}
    seen: dict[tuple[str, str], str] = {}
    for suite, caseName, relPath in cases:
        homes[suite].add(relPath)
        firstSite.setdefault(suite, relPath)
        key = (suite, caseName)
        if key in seen:
            errors.append(f"{relPath}: `{suite}.{caseName}` 이 {seen[key]} 와 이름이 겹칩니다 "
                          f"(필터로 하나만 고를 수 없습니다)")
        else:
            seen[key] = relPath

    # 1) 이름 규칙
    for suite in sorted(homes):
        if _kSuiteNameRe.match(suite) is None:
            errors.append(f"{firstSite[suite]}: 스위트 `{suite}` — 이름은 `XxxTest` 여야 합니다 "
                          f"(대문자 시작, `Test` 로 끝, 밑줄 없음). 계층 접두어는 실행 파일 이름이 이미 말합니다")

    # 2) 한 스위트 한 파일
    for suite in sorted(homes):
        if len(homes[suite]) > 1:
            errors.append(f"스위트 `{suite}` 가 파일 {len(homes[suite])}개에 걸쳐 있습니다 "
                          f"({' · '.join(sorted(homes[suite]))}) — 한 파일로 모으거나 이름을 가르세요")

    # 3) 호스트 스위트 선언이 효력이 있는가
    markers, markerErrors = collectMarkers(rootDir)
    errors += markerErrors
    policy, policyErrors = readPolicy(rootDir)
    errors += policyErrors
    hostSplitFolders = collectHostSplitFolders(policy)
    for suite, (relPath, reason) in sorted(markers.items()):
        if suite not in homes:
            errors.append(f"{relPath}: `SW_TEST_REQUIRES_HOST( {suite} )` — 그런 스위트가 없습니다 "
                          f"(이름을 바꿨다면 선언도 바꾸세요 — 안 그러면 그 스위트가 CI 로 들어갑니다)")
            continue
        if relPath not in homes[suite]:
            errors.append(f"{relPath}: `SW_TEST_REQUIRES_HOST( {suite} )` — 그 스위트는 이 파일에 없습니다 "
                          f"({' · '.join(sorted(homes[suite]))} 에 있습니다)")
        if not reason:
            errors.append(f"{relPath}: `SW_TEST_REQUIRES_HOST( {suite} )` 에 이유가 없습니다")
        folder = testFolderOf(relPath)
        if folder not in hostSplitFolders:
            errors.append(f"{relPath}: `{suite}` 는 호스트가 필요하다고 선언했는데 {_kPolicyFile} 의 "
                          f"`{folder.split('/')[-1]}` 줄이 `hostSplit` 이 아닙니다 — **CI 가 이것을 돌립니다**")
    for folder in sorted(hostSplitFolders - {testFolderOf(relPath) for relPath, _ in markers.values()}):
        errors.append(f"{_kPolicyFile}: `{folder.split('/')[-1]}` 는 `hostSplit` 인데 그 폴더에 `SW_TEST_REQUIRES_HOST` 선언이 하나도 "
                      f"없습니다 — `_HostOnly` 가 아무것도 돌지 않습니다")

    # 4) 마커가 붙은 스위트는 자기 파일을 독차지한다
    suitesByFile: dict[str, set[str]] = defaultdict(set)
    for suite, _, relPath in cases:
        suitesByFile[relPath].add(suite)
    for suite, (relPath, _) in sorted(markers.items()):
        if suite not in homes:
            continue  # 위 3) 이 이미 "그런 스위트가 없다" 로 보고했다
        others = sorted(suitesByFile.get(relPath, set()) - {suite})
        if others:
            errors.append(f"{relPath}: `{suite}` 는 CI 가 못 돌리는데 같은 파일에 "
                          f"{' · '.join(others)} 가 있습니다 — 파일을 가르세요 "
                          f"(안 그러면 새 케이스가 CI 쪽 스위트에 붙어 CI 로 들어갑니다)")

    # 4-1) 환경 스위트 선언 — 그 스위트가 이 파일에 있고, 파일을 혼자 쓴다
    environmentMarkers, environmentErrors = collectEnvironmentMarkers(rootDir)
    errors += environmentErrors
    for suite, (relPath, reason) in sorted(environmentMarkers.items()):
        if suite not in homes:
            errors.append(f"{relPath}: `SW_TEST_REQUIRES_ENVIRONMENT( {suite} )` — 그런 스위트가 없습니다")
            continue
        if relPath not in homes[suite]:
            errors.append(f"{relPath}: `SW_TEST_REQUIRES_ENVIRONMENT( {suite} )` — 그 스위트는 이 파일에 없습니다 "
                          f"({' · '.join(sorted(homes[suite]))} 에 있습니다)")
        if not reason:
            errors.append(f"{relPath}: `SW_TEST_REQUIRES_ENVIRONMENT( {suite} )` 에 이유가 없습니다")
        others = sorted(suitesByFile.get(relPath, set()) - {suite})
        if others:
            errors.append(f"{relPath}: `{suite}` 는 바깥 환경이 있어야 도는데 같은 파일에 {' · '.join(others)} 가 있습니다 — 파일을 가르세요")

    # 5) EditorTest 가 손으로 나열한 Editor 소스
    errors += checkEditorTestSources(rootDir)

    # 6) CoreTest 는 엔진을 직접 쓰지 않는다
    errors += checkCoreTestIsEngineFree(rootDir)

    # 7) 테스트 doc 블록은 제 케이스 바로 위, 태그는 그 스위트
    errors += checkTestDocBlocks(rootDir)

    # 8) 실행 파일 ↔ 정책 줄 · 라벨 · 비용표
    errors += checkPolicy(rootDir, policy, cases)

    # 9) 부하 스위트 — 파일을 혼자 쓰고, 그 실행 파일에 `<타깃>_Load` 항목(`loadSeconds`)이 있다
    loadMarkers, loadErrors = collectLoadMarkers(rootDir)
    errors += loadErrors
    for suite, (relPath, reason) in sorted(loadMarkers.items()):
        if suite not in homes or relPath not in homes[suite]:
            errors.append(f"{relPath}: `SW_TEST_LOAD_SUITE( {suite} )` — 그 스위트가 이 파일에 없습니다")
            continue
        if not reason:
            errors.append(f"{relPath}: `SW_TEST_LOAD_SUITE( {suite} )` 에 이유가 없습니다")
        others = sorted(suitesByFile.get(relPath, set()) - {suite})
        if others:
            errors.append(f"{relPath}: 부하 스위트 `{suite}` 와 같은 파일에 {' · '.join(others)} 가 있습니다 — 파일을 가르세요")
        executable = testFolderOf(relPath).split("/")[-1]
        if "loadSeconds" not in policy.get("executables", {}).get(executable, {}):
            errors.append(f"{relPath}: 부하 스위트 `{suite}` 가 있는데 {_kPolicyFile} 의 `{executable}` 줄에 `loadSeconds` 가 없습니다 "
                          f"— `<타깃>_Load` 항목이 없어 어디서도 돌지 않습니다")

    # 10) 활성이 아닌 게임 팩의 원본을 읽는 일반 케이스는 SourceResources 를 선언한다
    errors += checkSourceResourcesDeclared(rootDir, set(loadMarkers), {f"{suite}.{caseName}" for suite, caseName, _ in cases})

    return errors, len(homes), len(cases)


_kCleanPolicy = (
    '{"labels": {"unit": "", "nogpu": "", "hostgpu": "", "load": "", "engine": "", "editor": ""},\n'
    ' "executables": {"EngineTest": {"area": "engine", "seconds": 1, "hostSplit": true}, "EditorTest": {"area": "editor", "seconds": 1}}}\n'
)

# 이 검사가 **통과하는** 가장 작은 저장소 — 호스트 스위트 하나(hostSplit 실행 파일) · 평범한 스위트 하나 · EditorTest 목록 · 정책.
_kCleanFixture: dict[str, str] = {
    "Test/TestPolicy.json": _kCleanPolicy,
    "Test/EngineTest/CMakeLists.txt": "sw_addTestExecutable(EngineTest)\n",
    "Test/EngineTest/TestHostProbe.cpp": (
        'SW_TEST_REQUIRES_HOST( HostProbeTest, "needs a GPU" );\nSW_TEST_CASE( HostProbeTest, One )\n{\n}\n'
    ),
    "Test/EngineTest/TestPlainProbe.cpp": "SW_TEST_CASE( PlainProbeTest, One )\n{\n}\n",
    "Test/EditorTest/CMakeLists.txt": (
        'sw_addTestExecutable(EditorTest\n\tSOURCES\n\t\t"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Probe.cpp"\n)\n'
    ),
    "Source/Editor/Common/Probe.cpp": "int probe() { return 0; }\n",
    "Resource/game/empty/Probe.xml": "<Probe/>\n",
    "Resource/game/farmprobe/Probe.xml": "<Probe/>\n",
}


class CheckTestSuitesGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있어 어긋날 수 없다."""

    description = "테스트 스위트 규칙 검사"
    buildComment = "Checking test suite naming, one-file-per-suite, test policy rows, and that every suite declaration takes effect..."
    timeoutSeconds = 15
    preCommitPattern = ("Test/*", "cmake/Engine/TestTargets.cmake")
    # 조각마다 **위반 하나만** 넣는다 — 나머지는 깨끗한 바탕(`_kCleanFixture`)이라, 이 검사가 그 바탕에서 통과하는
    # 한 실패는 넣은 위반 때문이다. 주의: 바탕부터 다른 규칙(예: EditorTest 소스 목록이 빈 것)에 걸리는 조각은
    # 넣은 규칙이 죽어도 실패하므로 증거가 아니다.
    selfTestCases = [
        {
            "name": "doc 블록과 케이스 사이에 빈 줄",
            "expectedViolation": "Test/EngineTest/TestProbe.cpp:1: `[ProbeTest]` doc 블록이 자기 SW_TEST_CASE 바로 위에 있지 않습니다 (사이의 빈 줄 · 다른 블록을 지우거나, 설명할 케이스가 없으면 블록을 지우세요)",
            "files": {**_kCleanFixture, "Test/EngineTest/TestProbe.cpp": "/**\n * @brief [ProbeTest] 설명\n */\n\nSW_TEST_CASE( ProbeTest, One )\n{\n}\n"},
        },
        {
            "name": "doc 블록 태그가 다른 스위트",
            "expectedViolation": "Test/EngineTest/TestProbe.cpp:1: doc 블록 태그 `[OtherTest]` 가 케이스의 스위트 `ProbeTest` 와 다릅니다",
            "files": {**_kCleanFixture, "Test/EngineTest/TestProbe.cpp": "/**\n * @brief [OtherTest] 설명\n */\nSW_TEST_CASE( ProbeTest, One )\n{\n}\n"},
        },
        {
            "name": "스위트 이름이 XxxTest 가 아님",
            "expectedViolation": "Test/EngineTest/TestProbe.cpp: 스위트 `Probe_Bad`",
            "files": {**_kCleanFixture, "Test/EngineTest/TestProbe.cpp": "SW_TEST_CASE( Probe_Bad, Something )\n{\n}\n"},
        },
        {
            "name": "한 스위트가 두 파일에",
            "expectedViolation": "스위트 `ProbeTest` 가 파일 2개에 걸쳐 있습니다 (Test/EngineTest/TestProbe.cpp · Test/EngineTest/TestProbeB.cpp)",
            "files": {
                **_kCleanFixture,
                "Test/EngineTest/TestProbe.cpp": "SW_TEST_CASE( ProbeTest, One )\n{\n}\n",
                "Test/EngineTest/TestProbeB.cpp": "SW_TEST_CASE( ProbeTest, Two )\n{\n}\n",
            },
        },
        {
            # 선언을 읽는 것은 정책에서 `hostSplit` 인 실행 파일뿐이다 — 아니면 그 스위트는 CI 에서 돈다.
            "name": "호스트 선언이 있는데 hostSplit 이 아님",
            "expectedViolation": "Test/GpuTest/TestGpuProbe.cpp: `GpuProbeTest` 는 호스트가 필요하다고 선언했는데 Test/TestPolicy.json 의 `GpuTest` 줄이 `hostSplit` 이 아닙니다",
            "files": {
                **_kCleanFixture,
                "Test/TestPolicy.json": _kCleanPolicy.replace('"EditorTest"', '"GpuTest": {"area": "engine", "seconds": 1}, "EditorTest"'),
                "Test/GpuTest/TestGpuProbe.cpp": (
                    'SW_TEST_REQUIRES_HOST( GpuProbeTest, "needs a GPU" );\nSW_TEST_CASE( GpuProbeTest, One )\n{\n}\n'
                ),
                "Test/GpuTest/CMakeLists.txt": "sw_addTestExecutable(GpuTest)\n",
            },
        },
        {
            # 이름을 바꾸고 선언을 놓치면 선언은 아무것도 빼지 않고, 바뀐 스위트가 CI 로 들어간다.
            "name": "호스트 선언이 없는 스위트를 가리킴",
            "expectedViolation": "Test/EngineTest/TestProbe.cpp: `SW_TEST_REQUIRES_HOST( OldNameTest )`",
            "files": {
                **_kCleanFixture,
                "Test/EngineTest/TestProbe.cpp": (
                    'SW_TEST_REQUIRES_HOST( OldNameTest, "needs a GPU" );\nSW_TEST_CASE( NewNameTest, One )\n{\n}\n'
                ),
            },
        },
        {
            "name": "호스트 스위트 파일에 다른 스위트가 섞임",
            "expectedViolation": "Test/EngineTest/TestProbe.cpp: `ProbeTest` 는 CI 가 못 돌리는데 같은 파일에 NeighbourTest 가 있습니다",
            "files": {
                **_kCleanFixture,
                "Test/EngineTest/TestProbe.cpp": (
                    'SW_TEST_REQUIRES_HOST( ProbeTest, "needs a GPU" );\nSW_TEST_CASE( ProbeTest, One )\n{\n}\n'
                    "SW_TEST_CASE( NeighbourTest, Two )\n{\n}\n"
                ),
            },
        },
        {
            "name": "CoreTest 가 Engine 헤더를 include",
            "expectedViolation": "Test/CoreTest/TestProbe.cpp:1: CoreTest 가 `Engine/Physics/Collision/CollisionLayers.h` 를 include 합니다",
            "files": {
                **_kCleanFixture,
                "Test/CoreTest/TestProbe.cpp": '#include "Engine/Physics/Collision/CollisionLayers.h"\nSW_TEST_CASE( ProbeTest, One )\n{\n}\n',
            },
        },
        {
            # 주석의 `engine::` 는 세지 않는다 — 걸리는 것은 본문의 호출이다.
            "name": "CoreTest 가 engine:: 서비스를 부름",
            "expectedViolation": "Test/CoreTest/TestProbe.cpp:4: CoreTest 가 `engine::getSubsystem` 를 부릅니다",
            "files": {
                **_kCleanFixture,
                "Test/CoreTest/TestProbe.cpp": (
                    "// ModuleImageUtil::releaseModuleCode 가 부르는 길\n"
                    "SW_TEST_CASE( ProbeTest, One )\n{\n    sw::engine::getSubsystem<GlobalVariableManager>();\n}\n"
                ),
            },
        },
        {
            "name": "환경 스위트 파일에 다른 스위트가 섞임",
            "expectedViolation": "Test/EngineTest/TestProbe.cpp: `ProbeTest` 는 바깥 환경이 있어야 도는데 같은 파일에 NeighbourTest 가 있습니다",
            "files": {
                **_kCleanFixture,
                "Test/EngineTest/TestProbe.cpp": (
                    'SW_TEST_REQUIRES_ENVIRONMENT( ProbeTest, "SW_TEST_PROBE_URL", "needs a server" );\nSW_TEST_CASE( ProbeTest, One )\n{\n}\n'
                    "SW_TEST_CASE( NeighbourTest, Two )\n{\n}\n"
                ),
            },
        },
        {
            "name": "정책에 줄이 없는 실행 파일",
            "expectedViolation": "Test/ToolTest/CMakeLists.txt: `ToolTest` 의 줄이 Test/TestPolicy.json 에 없습니다",
            "files": {
                **_kCleanFixture,
                "Test/ToolTest/CMakeLists.txt": "sw_addTestExecutable(ToolTest)\n",
                "Test/ToolTest/TestToolProbe.cpp": "SW_TEST_CASE( ToolProbeTest, One )\n{\n}\n",
            },
        },
        {
            "name": "폴더 이름과 다른 실행 파일",
            "expectedViolation": "폴더 이름(`EditorTest`)과 실행 파일 이름이 같아야 합니다",
            "files": {**_kCleanFixture, "Test/EditorTest/CMakeLists.txt": _kCleanFixture["Test/EditorTest/CMakeLists.txt"].replace("(EditorTest", "(EditorToolTest")},
        },
        {
            "name": "모르는 라벨",
            "expectedViolation": "Test/EngineTest/CMakeLists.txt: 라벨 `gpuu` 이 Test/TestPolicy.json 의 labels 표에 없습니다",
            "files": {**_kCleanFixture, "Test/EngineTest/CMakeLists.txt": 'sw_addTestExecutable(EngineTest)\nsw_registerScriptTest(Probe LABELS "nogpu;gpuu")\n'},
        },
        {
            # 이름을 바꾸면 비용이 조용히 사라져 샤드가 기운다.
            "name": "비용표가 없는 케이스를 가리킴",
            "expectedViolation": "Test/TestPolicy.json: `EngineTest` 의 비용표가 없는 케이스 `PlainProbeTest.Renamed` 을 가리킵니다",
            "files": {
                **_kCleanFixture,
                "Test/TestPolicy.json": _kCleanPolicy.replace('"hostSplit": true}', '"hostSplit": true, "caseCosts": {"PlainProbeTest.Renamed": 9}}'),
            },
        },
        {
            # 레이스 묶음도 이름을 바꾸면 그 케이스가 Debug 레이스 검사에서 조용히 빠진다.
            "name": "레이스 묶음이 없는 스위트를 가리킴",
            "expectedViolation": "Test/TestPolicy.json: `EngineTest` 의 레이스 묶음(raceCases)이 없는 케이스 `RenamedProbeTest.*` 을 가리킵니다",
            "files": {
                **_kCleanFixture,
                "Test/TestPolicy.json": _kCleanPolicy.replace('"hostSplit": true}', '"hostSplit": true, "raceCases": ["RenamedProbeTest.*"]}'),
            },
        },
        {
            "name": "부하 스위트인데 loadSeconds 가 없음",
            "expectedViolation": "Test/EngineTest/TestLoadProbe.cpp: 부하 스위트 `LoadProbeTest` 가 있는데 Test/TestPolicy.json 의 `EngineTest` 줄에 `loadSeconds` 가 없습니다",
            "files": {
                **_kCleanFixture,
                "Test/EngineTest/TestLoadProbe.cpp": 'SW_TEST_LOAD_SUITE( LoadProbeTest, "measures throughput" );\nSW_TEST_CASE( LoadProbeTest, One )\n{\n}\n',
            },
        },
        {
            # 활성 게임(empty) · 임시 폴더 픽스처(x) · 디스크에 없는 이름은 허용이라 걸리지 않고, 실제 다른 팩(farmprobe)만 걸린다.
            "name": "활성이 아닌 게임 팩을 본문에서 읽는데 SourceResources 선언이 없음",
            "expectedViolation": "Test/EngineTest/TestProbe.cpp:5: `ProbeTest.One` 가 활성이 아닌 게임 팩 `game/farmprobe/` 을 읽는데",
            "files": {
                **_kCleanFixture,
                "Test/EngineTest/TestProbe.cpp": (
                    'SW_TEST_CASE( ProbeTest, One )\n{\n    load( "game/empty/a.xml" );\n    load( "game/x/a.xml" );\n'
                    '    load( "game/farmprobe/a.xml" );\n}\n'
                ),
            },
        },
        {
            "name": "활성이 아닌 게임 팩을 파일 상수로 읽는데 SourceResources 선언이 없음",
            "expectedViolation": "Test/EngineTest/TestProbe.cpp:3: `ProbeTest.One` 가 활성이 아닌 게임 팩 `game/farmprobe/` 을 읽는데",
            "files": {
                **_kCleanFixture,
                "Test/EngineTest/TestProbe.cpp": (
                    'namespace\n{\n    constexpr const utf8* kPath = "game/farmprobe/a.xml";\n    constexpr const utf8* kOther = "game/empty/a.xml";\n}\n'
                    "SW_TEST_CASE( ProbeTest, One )\n{\n    load( kPath );\n}\n"
                ),
            },
        },
        {
            "name": "옛 주석 마커",
            "expectedViolation": "Test/EngineTest/TestProbe.cpp: 옛 주석 마커 `// SW_TEST_REQUIRES_HOST( ProbeTest ): ...` 는 아무 효력이 없습니다",
            "files": {
                **_kCleanFixture,
                "Test/EngineTest/TestProbe.cpp": (
                    "// SW_TEST_REQUIRES_HOST( ProbeTest ): GPU 가 필요합니다\nSW_TEST_CASE( ProbeTest, One )\n{\n}\n"
                ),
            },
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        errors, suiteCount, caseCount = check(repositoryRoot)
        return GateResult(listViolation=errors, summary=f"{suiteCount} suites, {caseCount} cases")


main = CheckTestSuitesGate.run


if __name__ == "__main__":
    sys.exit(main())
