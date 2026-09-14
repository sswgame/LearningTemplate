#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
`CheckCodeConventions` 가 아직 살아 있는지 검사합니다 (음성 테스트).

**린트는 조용히 죽는다.** 규칙 하나가 정규식 한 글자 때문에 아무것도 못 잡게 되어도 결과는 "위반 0건"
이라 통과처럼 보인다. 이 저장소는 실제로 그런 일을 겪었다 — 그래서 규칙마다 **일부러 어긴 조각**을 두고
그것이 잡히는지 본다. 잡히지 않으면 그 규칙은 죽은 것이다.

  python Scripts/lint/selftest/CheckCodeConventionsSelfTest.py [--root <repo>] [--verbose]

**조각은 되도록 규칙이 직접 든다.** `ConventionRule` 을 상속한 규칙은 `badSample` 에 자기 위반 조각을
적어 두고, 이 검사가 그것을 읽어 온다 — 규칙과 증거가 붙어 있으면 둘이 어긋날 수가 없다.
`badSample` 이 비어 있으면 그 자체로 실패한다.

아래 `_kPerFileCases` · `_kWholeScanCases` 는 **아직 클래스가 아닌** 검사들(매개변수·지역변수 규칙,
생성자 상태 기계, 파일 짝이 필요한 교차 검사)을 위한 나머지다. 그쪽도 클래스가 되면 표는 비어야 한다.
"""
from __future__ import annotations

import argparse
import re
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — 사촌 린트 패키지
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common

from gate import CheckCodeConventions  # noqa: E402
from common import useUtf8Stdout  # noqa: E402

#: CMake 등록 정보 — 게이트는 `LintGate` 클래스가 들고, 클래스가 없는 이쪽은 모듈이 든다
#: (`Scripts/lint/LintCatalog.py`). 영어인 이유는 ninja 가 찍는 줄이기 때문이다.
kLintBuildComment = "Checking that every CheckCodeConventions rule still catches a deliberately broken snippet..."
kLintTimeoutSeconds = 60

# ------------------------------------------------------------------------------
# 1) 파일 하나로 잡히는 규칙 — (카테고리, 파일 이름, 내용)
#
# 내용은 **그 규칙만** 건드리도록 최소로 쓴다. 다른 규칙이 같이 걸려도 검사는 통과하지만,
# 조각이 커질수록 "이 조각이 왜 이 규칙을 트리거하는가" 가 흐려진다.
# ------------------------------------------------------------------------------
_kPerFileCases: list[tuple[str, str, str]] = [
    (
        "Include/PCH",
        "Source/Probe/NoPch.cpp",
        '#include "Engine/EngineMinimal.h"\n\nvoid probe() {}\n',
    ),
    (
        "Naming/LocalNoUnderscore",
        "Source/Probe/LocalUnderscore.cpp",
        '#include "pch.h"\n\nvoid probe()\n{\n    int32 _count = 0;\n    (void)_count;\n}\n',
    ),
    (
        "Naming/LocalPointer",
        "Source/Probe/LocalPointer.cpp",
        '#include "pch.h"\n\nvoid probe()\n{\n    int32* value = nullptr;\n    (void)value;\n}\n',
    ),
    (
        "Naming/LocalContainer",
        "Source/Probe/LocalContainer.cpp",
        '#include "pch.h"\n\nvoid probe()\n{\n    vector<int32> items;\n    (void)items;\n}\n',
    ),
    (
        "Naming/ParameterNoUnderscore",
        "Source/Probe/ParamUnderscore.cpp",
        '#include "pch.h"\n\nvoid probe( int32 _count )\n{\n    (void)_count;\n}\n',
    ),
    (
        "Naming/ParameterPointer",
        "Source/Probe/ParamPointer.cpp",
        '#include "pch.h"\n\nvoid probe( int32* value )\n{\n    (void)value;\n}\n',
    ),
    (
        "Naming/ParameterContainer",
        "Source/Probe/ParamContainer.cpp",
        '#include "pch.h"\n\nvoid probe( const vector<int32>& items )\n{\n    (void)items;\n}\n',
    ),
    (
        "Style/ConstructorBraces",
        "Source/Probe/CtorBraces.cpp",
        '#include "pch.h"\n\nProbe::Probe()\n    : _count( 0 )\n{\n}\n',
    ),
    (
        "Style/ConstructorOnePerLine",
        "Source/Probe/CtorOnePerLine.cpp",
        '#include "pch.h"\n\nProbe::Probe()\n    : _count{ 0 }, _other{ 1 }\n{\n}\n',
    ),
]

# ------------------------------------------------------------------------------
# 2) 파일 하나로는 알 수 없는 규칙 — 트리를 통째로 스캔할 때만 돈다
# ------------------------------------------------------------------------------
_kWholeScanCases: list[tuple[str, dict[str, str]]] = [
    (
        "Style/HeaderMemberInitializer",
        {
            "Source/Probe/HeaderInit.h": "#pragma once\n\nclass HeaderInit\n{\npublic:\n    HeaderInit();\n\nprivate:\n    int32 _count{ 0 };\n};\n",
            "Source/Probe/HeaderInit.cpp": '#include "pch.h"\n\nHeaderInit::HeaderInit()\n    : _count{ 0 }\n{\n}\n',
        },
    ),
    (
        "Style/ConstructorOrder",
        {
            "Source/Probe/CtorOrder.h": "#pragma once\n\nclass CtorOrder\n{\npublic:\n    CtorOrder();\n\nprivate:\n    int32 _first{ 0 };\n    int32 _second{ 0 };\n};\n",
            "Source/Probe/CtorOrder.cpp": '#include "pch.h"\n\nCtorOrder::CtorOrder()\n    : _second{ 1 }\n    , _first{ 2 }\n{\n}\n',
        },
    ),
    (
        "Style/BitfieldBoolean",
        {
            "Source/Probe/Bitfield.h": "#pragma once\n\nclass Bitfield\n{\nprivate:\n    uint8 _bReady : 1;\n};\n",
            "Source/Probe/Bitfield.cpp": '#include "pch.h"\n\nvoid probe( Bitfield& target )\n{\n    target._bReady = true;\n}\n',
        },
    ),
    (
        "Include/PathCasing",
        {
            "Source/Probe/CaseTarget.h": "#pragma once\n",
            "Source/Probe/CaseUser.cpp": '#include "pch.h"\n\n#include "Probe/casetarget.h"\n',
        },
    ),
    (
        "Naming/DuplicateInternalHelper",
        {
            "Source/Probe/AlphaThing.cpp": '#include "pch.h"\n\nnamespace\n{\n    struct SharedInternal\n    {\n        int32 _value{ 0 };\n    };\n} // namespace\n',
            "Source/Probe/BetaThing.cpp": '#include "pch.h"\n\nnamespace\n{\n    struct SharedInternal\n    {\n        int32 _value{ 0 };\n    };\n} // namespace\n',
        },
    ),
]

# ------------------------------------------------------------------------------
# 3) 주체 × 어휘 교차표 — **드리프트를 막는 자리다**
#
# 위의 카테고리 검사는 "이 카테고리가 한 번은 잡히는가"만 본다. 그래서 같은 규칙이 주체마다
# 다르게 적혀 있어도 **하나만 살아 있으면 통과했다**. 실제로 그 상태로 오래 있었다:
#
#   * `inoutListActors` 는 매개변수면 잡히고 지역변수면 통과했다
#   * `vector<uint8> listBuffer` 는 매개변수에선 "`list` 를 빼라", 멤버 `_listBuffer` 는 통과 —
#     같은 이름에 **정반대 판정**이 나왔다
#
# 지금은 판정이 `kMapContainerVocabulary` 한 곳에 있으니 그런 일이 나올 수 없다. 이 표는 그게
# **계속** 그런지 본다: 어휘 넷을 주체 셋에 각각 물어, 하나라도 조용하면 실패한다.
# 표는 손으로 들지 않는다 — 두 목록의 곱이라 어휘나 주체가 늘면 칸도 같이 는다.
# ------------------------------------------------------------------------------

#: 어휘별로 (선언을 쓰는 법, 일부러 어긴 이름). 주체 셋에 같은 위반을 넣어 본다.
_kMapVocabularyProbe: dict[str, tuple[str, str]] = {
    "list": ("vector<int32> {name};", "item"),
    "map": ("unordered_map<int32, int32> {name};", "table"),
    "unique": ("set<int32> {name};", "id"),
    "arr": ("int32 {name}[8];", "slot"),
}


def buildSubjectProbeInternal(subjectKey: str, declaration: str, badName: str) -> tuple[str, str]:
    """주체 하나에 위반 하나를 심은 파일 (경로, 내용) 을 만듭니다."""
    if subjectKey == "member":
        body = declaration.format(name=f"_{badName}")
        return (
            f"Source/Probe/Member{badName.capitalize()}.h",
            f"#pragma once\n\nclass Probe\n{{\nprivate:\n    {body}\n}};\n",
        )

    if subjectKey == "local":
        body = declaration.format(name=badName)
        return (
            f"Source/Probe/Local{badName.capitalize()}.cpp",
            f'#include "pch.h"\n\nvoid probe()\n{{\n    {body}\n}}\n',
        )

    # 매개변수 — 선언에서 `;` 를 떼고 시그니처에 넣는다. 고정 배열은 매개변수 문법이 다르다.
    paramDecl = declaration.format(name=badName).rstrip(";")
    return (
        f"Source/Probe/Param{badName.capitalize()}.cpp",
        f'#include "pch.h"\n\nvoid probe( {paramDecl} )\n{{\n}}\n',
    )


def checkSubjectMatrixInternal(tempRoot: Path, bVerbose: bool) -> list[str]:
    """
    주체 셋 × 어휘 넷 — 열두 칸이 전부 무언가를 잡아야 합니다.

    어느 칸이 조용하면 그 주체가 그 어휘를 안 보고 있다는 뜻이다. 예전의 드리프트가 정확히
    그 모양이었다.
    """
    listError: list[str] = []

    for subjectKey in CheckCodeConventions.kMapNamingSubject:
        for vocabularyKey, (declaration, badName) in _kMapVocabularyProbe.items():
            relPath, content = buildSubjectProbeInternal(subjectKey, declaration, badName)
            caseRoot = tempRoot / f"matrix_{subjectKey}_{vocabularyKey}"
            path = writeFixtureInternal(caseRoot, relPath, content)
            resetPathMapCacheInternal()
            found = categoriesForFileInternal(caseRoot, path)

            if bVerbose:
                print(f"  [{subjectKey} × {vocabularyKey}] -> {sorted(found) if found else '(없음)'}")

            if not found:
                listError.append(
                    f"{subjectKey} × {vocabularyKey}: '{badName}' 를 아무도 잡지 않았습니다 — "
                    f"이 주체가 그 어휘를 보고 있지 않습니다 ({relPath})"
                )

    return listError


# 아무 규칙도 건드리면 안 되는 조각. 오탐이 생기면 여기서 잡힌다.
_kCleanCase: tuple[str, str] = (
    "Source/Probe/Clean.cpp",
    '#include "pch.h"\n\nnamespace\n{\n    constexpr int32 kProbeLimit = 4;\n} // namespace\n\n'
    "void probe( int32 count )\n{\n    (void)count;\n}\n",
)


def resetPathMapCacheInternal() -> None:
    """
    `CheckCodeConventions` 의 경로 맵 캐시를 비웁니다.

    그 맵은 **처음 한 번만** 채워지는 모듈 전역이다(정상 실행에서는 루트가 하나라 맞는 설계다).
    여기서는 조각마다 임시 루트가 다르므로, 비우지 않으면 앞 조각의 맵으로 판정해
    `Include/PathCasing` 같은 규칙이 조용히 안 걸린다 — 실제로 한 번 그랬다.
    """
    CheckCodeConventions._s_exactPathMap = {}


def writeFixtureInternal(root: Path, relPath: str, content: str) -> Path:
    path = root / relPath
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    return path


def categoriesForFileInternal(root: Path, path: Path) -> set[str]:
    return {v.rule_category for v in CheckCodeConventions.runConventionsCheck(root, [str(path)])}


def categoriesForTreeInternal(root: Path) -> set[str]:
    return {v.rule_category for v in CheckCodeConventions.runConventionsCheck(root)}


def knownCategoriesInternal() -> set[str]:
    """`CheckCodeConventions.py` 가 실제로 만들 수 있는 카테고리 전부."""
    text = Path(CheckCodeConventions.__file__).read_text(encoding="utf-8", errors="ignore")
    return set(re.findall(r'rule_category="([^"]+)"', text))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="CheckCodeConventions 음성 테스트")
    parser.add_argument("--root", default=str(Path(__file__).resolve().parents[3]))
    parser.add_argument("--verbose", action="store_true", help="조각마다 잡힌 카테고리를 모두 출력")
    args = parser.parse_args(argv)
    useUtf8Stdout()

    repoRoot = Path(args.root).resolve()
    errors: list[str] = []
    covered: set[str] = set()

    tempRoot = Path(tempfile.mkdtemp(prefix="swConventionsSelfTest"))
    try:
        # --- 규칙이 스스로 드는 조각 (표가 아니라 규칙에서 온다) ---
        ruleOwnedCases: list[tuple[str, str, str]] = []
        for scopeRules in CheckCodeConventions._kRulesByScope.values():
            for rule in scopeRules:
                # 조각이 없으면 아래 "조각이 없는 카테고리" 검사가 잡는다 (파일 짝이 필요한 규칙은
                # `_kWholeScanCases` 가 대신 덮는다 — 그쪽도 같은 검사에 걸린다).
                # 규칙이 카테고리를 여럿 내면 조각 하나가 그중 하나만 증명한다 — 전부를 요구하지 않는다.
                # (나머지는 `extraSamples` 가 덮고, 끝의 "조각이 없는 카테고리" 검사가 빠진 것을 잡는다.)
                if rule.badSample:
                    ruleOwnedCases.append((rule.allCategories(), rule.badSampleFile, rule.badSample))
                for extraFile, extraBody in rule.extraSamples:
                    ruleOwnedCases.append((None, extraFile, extraBody))

        # --- 파일 단위 규칙 ---
        for category, relPath, content in ruleOwnedCases + _kPerFileCases:
            expected = (category,) if isinstance(category, str) else category
            caseName = Path(relPath).stem if expected is None else expected[0].replace("/", "_")
            caseRoot = tempRoot / caseName
            path = writeFixtureInternal(caseRoot, relPath, content)
            resetPathMapCacheInternal()
            found = categoriesForFileInternal(caseRoot, path)

            if args.verbose:
                print(f"  [{category}] -> {sorted(found) if found else '(없음)'}")

            if expected is None:
                # 카테고리를 여럿 내는 규칙의 보조 조각 — 무엇이 잡히든 덮인 것으로 친다.
                covered.update(found)
                continue

            if found & set(expected):
                covered.update(found)
            else:
                errors.append(f"{' / '.join(expected)}: 조각이 잡히지 않았습니다 — 규칙이 죽었거나 "
                              f"조각이 낡았습니다 (잡힌 것: {sorted(found) if found else '없음'})")

        # --- 트리 전체를 봐야 아는 규칙 ---
        for category, files in _kWholeScanCases:
            caseRoot = tempRoot / category.replace("/", "_")
            for relPath, content in files.items():
                writeFixtureInternal(caseRoot, relPath, content)
            resetPathMapCacheInternal()
            found = categoriesForTreeInternal(caseRoot)

            if args.verbose:
                print(f"  [{category}] -> {sorted(found) if found else '(없음)'}")

            if category in found:
                covered.add(category)
            else:
                errors.append(f"{category}: 조각이 잡히지 않았습니다 (잡힌 것: {sorted(found) if found else '없음'})")

        # --- 주체 × 어휘 교차표 ---
        errors.extend(checkSubjectMatrixInternal(tempRoot, args.verbose))

        # --- 오탐 확인 ---
        cleanRoot = tempRoot / "clean"
        cleanPath = writeFixtureInternal(cleanRoot, _kCleanCase[0], _kCleanCase[1])
        resetPathMapCacheInternal()
        cleanFound = categoriesForFileInternal(cleanRoot, cleanPath)

        if cleanFound:
            errors.append(f"깨끗해야 할 조각에서 위반이 나왔습니다 (오탐): {sorted(cleanFound)}")
    finally:
        shutil.rmtree(tempRoot, ignore_errors=True)

    # --- 덮이지 않은 카테고리 ---
    known = knownCategoriesInternal()
    uncovered = sorted(known - covered)

    if uncovered:
        errors.append("조각이 없는 카테고리 " + str(len(uncovered)) + "종: " + ", ".join(uncovered))

    if errors:
        print(f"[CheckCodeConventionsSelfTest] 문제 {len(errors)}건", file=sys.stderr)
        for error in errors:
            print(f"  {error}")
        return 1

    print(f"[CheckCodeConventionsSelfTest] OK ({len(covered)} categories covered)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
