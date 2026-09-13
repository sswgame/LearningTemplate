#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
`CheckCodeConventions` 가 아직 살아 있는지 검사합니다 (음성 테스트).

**린트는 조용히 죽는다.** 규칙 하나가 정규식 한 글자 때문에 아무것도 못 잡게 되어도 결과는 "위반 0건"
이라 통과처럼 보인다. 이 저장소는 실제로 그런 일을 겪었다 — 그래서 규칙마다 **일부러 어긴 조각**을 두고
그것이 잡히는지 본다. 잡히지 않으면 그 규칙은 죽은 것이다.

  python Scripts/lint/CheckCodeConventionsSelfTest.py [--root <repo>] [--verbose]

새 규칙을 `CheckCodeConventions.py` 에 넣었다면 여기에도 조각을 하나 넣는다. 넣지 않으면 이 검사가
"덮이지 않은 카테고리" 로 실패한다 — 규칙을 늘리는 일과 그것이 살아 있음을 증명하는 일을 같이 묶는다.
"""
from __future__ import annotations

import argparse
import re
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import CheckCodeConventions  # noqa: E402
from common import useUtf8Stdout  # noqa: E402

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
        "Naming/Constant",
        "Source/Probe/Constant.cpp",
        '#include "pch.h"\n\nstatic constexpr int32 MAX_COUNT = 4;\n',
    ),
    (
        "Naming/RawPointer",
        "Source/Probe/RawPointer.h",
        "#pragma once\n\nclass Probe\n{\nprivate:\n    int32* _value;\n};\n",
    ),
    (
        "Naming/DynamicContainer",
        "Source/Probe/DynamicContainer.h",
        "#pragma once\n\nclass Probe\n{\nprivate:\n    vector<int32> _items;\n};\n",
    ),
    (
        "Naming/FixedArray",
        "Source/Probe/FixedArray.h",
        "#pragma once\n\nclass Probe\n{\nprivate:\n    float32 _matrix[16];\n};\n",
    ),
    (
        "Naming/MapContainer",
        "Source/Probe/MapContainer.h",
        "#pragma once\n\nclass Probe\n{\nprivate:\n    map<int32, int32> _items;\n};\n",
    ),
    (
        "Naming/SetContainer",
        "Source/Probe/SetContainer.h",
        "#pragma once\n\nclass Probe\n{\nprivate:\n    set<int32> _items;\n};\n",
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
        "Naming/LoopVariable",
        "Source/Probe/LoopVariable.cpp",
        '#include "pch.h"\n\nvoid probe()\n{\n    for ( int32 i = 0; i < 4; ++i )\n    {\n    }\n}\n',
    ),
    (
        "Naming/TriplePointer",
        "Source/Probe/TriplePointer.cpp",
        '#include "pch.h"\n\nvoid probe( int32*** pppValue )\n{\n    (void)pppValue;\n}\n',
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
        "Naming/ContainerSingular",
        "Source/Probe/ContainerPlural.cpp",
        '#include "pch.h"\n\nvoid probe()\n{\n    vector<int32> listItems;\n    (void)listItems;\n}\n',
    ),
    (
        "Naming/OutParameter",
        "Source/Probe/OutParameter.cpp",
        '#include "pch.h"\n\nvoid probe( int32* outPValue )\n{\n    *outPValue = 1;\n}\n',
    ),
    (
        "Style/BasicTypeAlias",
        "Source/Probe/BasicType.cpp",
        '#include "pch.h"\n\nvoid probe()\n{\n    unsigned int count = 0u;\n    (void)count;\n}\n',
    ),
    (
        "Style/AutoUsage",
        "Source/Probe/AutoUsage.cpp",
        '#include "pch.h"\n\nvoid probe()\n{\n    auto name = "Probe";\n    (void)name;\n}\n',
    ),
    (
        "Style/ExplicitTrueCheck",
        "Source/Probe/ExplicitTrue.cpp",
        '#include "pch.h"\n\nvoid probe( bool bValid )\n{\n    if ( bValid == true )\n    {\n    }\n}\n',
    ),
    (
        "Style/ImplicitPointerNullCheck",
        "Source/Probe/ImplicitNull.cpp",
        '#include "pch.h"\n\nvoid probe()\n{\n    if ( getOwner() )\n    {\n    }\n}\n',
    ),
    (
        "Style/NegatedComparison",
        "Source/Probe/Negated.cpp",
        '#include "pch.h"\n\nvoid probe( int32* pActor )\n{\n    if ( !pActor )\n    {\n    }\n}\n',
    ),
    (
        "Style/ConstructorBraces",
        "Source/Probe/CtorBraces.cpp",
        '#include "pch.h"\n\nProbe::Probe()\n    : _count( 0 )\n{\n}\n',
    ),
    (
        "Style/LogFormatSpec",
        "Source/Probe/LogFormat.cpp",
        '#include "pch.h"\n\nvoid probe( int32 width, int32 count )\n{\n    SW_LOG_INFO( "count=%*d", width, count );\n}\n',
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


def knownCategoriesInternal(lintPath: Path) -> set[str]:
    """`CheckCodeConventions.py` 가 실제로 만들 수 있는 카테고리 전부."""
    text = lintPath.read_text(encoding="utf-8", errors="ignore")
    return set(re.findall(r'rule_category="([^"]+)"', text))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="CheckCodeConventions 음성 테스트")
    parser.add_argument("--root", default=str(Path(__file__).resolve().parents[2]))
    parser.add_argument("--verbose", action="store_true", help="조각마다 잡힌 카테고리를 모두 출력")
    args = parser.parse_args(argv)
    useUtf8Stdout()

    repoRoot = Path(args.root).resolve()
    errors: list[str] = []
    covered: set[str] = set()

    tempRoot = Path(tempfile.mkdtemp(prefix="swConventionsSelfTest"))
    try:
        # --- 파일 단위 규칙 ---
        for category, relPath, content in _kPerFileCases:
            caseRoot = tempRoot / category.replace("/", "_")
            path = writeFixtureInternal(caseRoot, relPath, content)
            resetPathMapCacheInternal()
            found = categoriesForFileInternal(caseRoot, path)

            if args.verbose:
                print(f"  [{category}] -> {sorted(found) if found else '(없음)'}")

            if category in found:
                covered.add(category)
            else:
                errors.append(f"{category}: 조각이 잡히지 않았습니다 — 규칙이 죽었거나 조각이 낡았습니다 "
                              f"(잡힌 것: {sorted(found) if found else '없음'})")

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
    known = knownCategoriesInternal(repoRoot / "Scripts" / "lint" / "CheckCodeConventions.py")
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
