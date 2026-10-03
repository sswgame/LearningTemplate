#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
`CheckCodeConventions` 가 아직 살아 있는지 검사합니다 (음성 테스트).

**린트는 조용히 죽는다.** 규칙 하나가 정규식 한 글자 때문에 아무것도 못 잡게 되어도 결과는 "위반 0건"
이라 통과처럼 보인다. 그래서 규칙마다 **일부러 어긴 조각**을 두고
그것이 잡히는지 본다. 잡히지 않으면 그 규칙은 죽은 것이다.

  python Scripts/lint/selftest/CheckCodeConventionsSelfTest.py [--root <repo>] [--verbose]

**조각은 되도록 규칙이 직접 든다.** `ConventionRule` 을 상속한 규칙은 `badSample` 에 자기 위반 조각을
적어 두고, 이 검사가 그것을 읽어 온다 — 규칙과 증거가 붙어 있으면 둘이 어긋날 수가 없다.
`badSample` 이 비어 있으면 그 자체로 실패한다.

아래 `_kWholeScanCases` 는 파일 짝이 있어야 성립하는 교차 검사(헤더+소스, 같은 이름의 다른 파일)의 조각이다 — 파일 하나짜리
조각으로는 적을 수 없어 규칙이 들지 못한다.
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
# 1) 파일 하나로는 알 수 없는 규칙 — 트리를 통째로 스캔할 때만 돈다(파일 짝이 있어야 성립해 규칙이 조각을 들 수 없다)
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
        # 초기화 목록을 가진 생성자가 기본값 없는 포인터 · 비트필드를 빠뜨린다.
        "Style/ConstructorInitializesEveryField",
        {
            "Source/Probe/CtorMissing.h": ("#pragma once\n\nclass CtorMissing\n{\npublic:\n    CtorMissing();\n\nprivate:\n"
                                           "    Widget* _pWidget;\n    int32 _count;\n    uint8 _bReady : 1;\n};\n"),
            "Source/Probe/CtorMissing.cpp": '#include "pch.h"\n\nCtorMissing::CtorMissing()\n    : _count{ 0 }\n{\n}\n',
        },
    ),
    (
        # `= default` 기본 생성자인데 스칼라 필드에 헤더 기본값이 없다.
        "Style/ConstructorInitializesEveryField",
        {
            "Source/Probe/CtorDefaulted.h": ("#pragma once\n\nenum class ProbeMode : uint8\n{\n    Off,\n};\n\nstruct CtorDefaulted\n{\n"
                                             "    CtorDefaulted() = default;\n\n    ProbeMode _mode;\n    float32 _scale{ 1.0f };\n};\n"),
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
        "Naming/DuplicateAnonymousConstant",
        {
            "Source/Probe/AlphaLimit.cpp": '#include "pch.h"\n\nnamespace sw\n{\n    namespace\n    {\n        constexpr int32 kProbeLimit = 4;\n    } // namespace\n} // namespace sw\n',
            "Source/Probe/BetaLimit.cpp": '#include "pch.h"\n\nnamespace\n{\n    constexpr int32 kProbeLimit = 8;\n} // namespace\n',
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
# 2) 주체 × 어휘 교차표 — **드리프트를 막는 자리다**
#
# 위의 카테고리 검사는 "이 카테고리가 한 번은 잡히는가"만 본다. 그래서 같은 규칙이 주체마다
# 다르게 적혀 있어도 **하나만 살아 있으면 통과한다** — `inoutListActors` 가 매개변수면 잡히고 지역변수면
# 통과하거나, `vector<uint8> listBuffer` / `_listBuffer` 에 정반대 판정이 나와도 모른다.
#
# 판정은 `kMapContainerVocabulary` 한 곳에 있다. 이 표는 그게
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

    어느 칸이 조용하면 그 주체가 그 어휘를 안 보고 있다는 뜻이다 — 주체마다 표가 갈라지는 드리프트가 그 모양이다.
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


def checkCheckoutPathWithExcludedWordInternal(tempRoot: Path) -> list[str]:
    """
    저장소가 `.../wt-buildlint/` 처럼 제외 폴더 이름(`build`)을 이름에 품은 폴더에 있어도 전체 스캔 · `--files` 가 파일을 본다.

    제외를 절대 경로의 부분 문자열로 보면 그런 체크아웃에서 **모든 파일이 빠져** 위반 0 으로 통과한다(조용히 죽은 게이트).
    """
    caseRoot = tempRoot / "wt-buildlint"
    path = writeFixtureInternal(caseRoot, "Source/Probe/ParamInBuildCheckout.cpp",
                                '#include "pch.h"\n\nvoid probe( int32 _count )\n{\n    (void)_count;\n}\n')
    listError: list[str] = []
    resetPathMapCacheInternal()
    if "Naming/ParameterNoUnderscore" not in categoriesForTreeInternal(caseRoot):
        listError.append("경로에 'build' 가 든 체크아웃: 전체 스캔이 파일을 보지 않습니다 (제외를 절대 경로 부분 문자열로 보고 있습니까?)")
    resetPathMapCacheInternal()
    if "Naming/ParameterNoUnderscore" not in categoriesForFileInternal(caseRoot, path):
        listError.append("경로에 'build' 가 든 체크아웃: --files 가 파일을 거릅니다 (제외를 절대 경로 부분 문자열로 보고 있습니까?)")
    return listError


# 아무 규칙도 건드리면 안 되는 조각. 오탐이 생기면 여기서 잡힌다.
# 반복자 쌍 생성자는 소괄호가 맞다(`Style/ConstructorBraces` 의 예외) — 중괄호는 initializer_list 로 빠진다.
_kCleanCase: tuple[str, str] = (
    "Source/Probe/Clean.cpp",
    '#include "pch.h"\n\nnamespace\n{\n    constexpr int32 kProbeLimit = 4;\n} // namespace\n\n'
    "void probe( int32 count )\n{\n    (void)count;\n}\n\n"
    "Probe::Probe( std::initializer_list<int32> listValue, const int32* pBegin, const int32* pEnd )\n"
    "    : _listValue( listValue.begin(), listValue.end() )\n"
    "    , _listCopy( pBegin, pEnd )\n"
    "    , _listOther( std::begin( listValue ), std::end( listValue ) )\n"
    "{\n}\n",
)

# 트리 전체를 봐야 아는 규칙의 **오탐**을 잡는 조각. 위 `_kCleanCase` 는 파일 하나만 넘기므로
# 전체 스캔 전용 규칙(`Style/BitfieldBoolean` · `Naming/DuplicateInternalHelper` ·
# `Style/HeaderMemberInitializer`)이 아예 돌지 않는다 — **그 규칙들의 오탐은 아무도 보고 있지 않았다.**
#
# `_buttonMask` 가 여기 있는 이유: `Style/BitfieldBoolean` 이 이름을 `_b` + 아무 글자로 보면
# **`_b` 다음이 소문자인 평범한 이름**(비트마스크·바이트 버퍼)까지 "uint8 불리언" 으로 읽는다.
# 게다가 이름 집합이 트리 전역이라, 한 파일의 `uint8 _buttonMask` 가 **다른 파일의 `uint64
# _buttonMask` 까지** 불리언으로 만든다. 이 조각이 그 오탐을 막는다.
_kWholeScanCleanCase: dict[str, str] = {
    # 같은 상수 이름이라도 `XxxInternal` 구조체 안 · 함수 지역이면 그 구조체 · 함수가 가린다 — `Naming/DuplicateAnonymousConstant` 가 아니다.
    "Source/Probe/GammaLimit.cpp": (
        '#include "pch.h"\n\nnamespace\n{\n    struct GammaLimitInternal\n    {\n'
        "        static constexpr int32 kProbeLimit = 4;\n    };\n} // namespace\n"
    ),
    "Source/Probe/DeltaLimit.cpp": (
        '#include "pch.h"\n\nnamespace\n{\n    int32 computeDeltaLimit()\n    {\n'
        "        constexpr int32 kProbeLimit = 8;\n        return kProbeLimit;\n    }\n} // namespace\n"
    ),
    "Source/Probe/EpsilonLimit.cpp": (
        '#include "pch.h"\n\nnamespace\n{\n    struct EpsilonLimitInternal\n    {\n'
        "        static constexpr int32 kProbeLimit = 16;\n    };\n} // namespace\n"
    ),
    "Source/Probe/MaskDevice.h": (
        "#pragma once\n\nclass MaskDevice\n{\nprivate:\n"
        "    uint8 _buttonMask{ 0 };   ///< 8비트 비트마스크 — 불리언이 아니다\n};\n"
    ),
    "Source/Probe/MaskSnapshot.h": (
        "#pragma once\n\nstruct MaskSnapshot\n{\n"
        "    uint64 _buttonMask{ 0 };  ///< 64비트 비트마스크 — 같은 이름, 다른 폭\n};\n"
    ),
    "Source/Probe/MaskUse.cpp": (
        '#include "pch.h"\n\nvoid probeMask( MaskSnapshot& target )\n{\n'
        "    target._buttonMask = 0;\n}\n"
    ),
    # 같은 이름이 어디에도 더 넓게 선언돼 있지 않은 경우 — **이름 모양만으로** 걸러야 한다.
    "Source/Probe/ByteCounter.h": (
        "#pragma once\n\nstruct ByteCounter\n{\n"
        "    uint8 _bytesWritten{ 0 };  ///< 쓴 바이트 수 — 불리언이 아니다\n};\n"
    ),
    "Source/Probe/ByteCounterUse.cpp": (
        '#include "pch.h"\n\nvoid probeCounter( ByteCounter& target )\n{\n'
        "    target._bytesWritten = 0;\n}\n"
    ),
    # 같은 이름이 두 폭으로 선언돼 있으면 **어느 쪽을 쓴 것인지 단정할 수 없다** — 건너뛴다.
    "Source/Probe/WidthClash.h": (
        "#pragma once\n\nstruct NarrowFlags\n{\n    uint8 _bReady : 1;\n};\n\n"
        "struct WideFlags\n{\n    uint32 _bReady{ 0 };  ///< 같은 이름, 다른 폭\n};\n"
    ),
    "Source/Probe/WidthClashUse.cpp": (
        '#include "pch.h"\n\nvoid probeWide( WideFlags& target )\n{\n'
        "    target._bReady = 0;\n}\n"
    ),
}


def resetPathMapCacheInternal() -> None:
    """
    `CheckCodeConventions` 의 경로 맵 캐시를 비웁니다.

    그 맵은 **처음 한 번만** 채워지는 모듈 전역이다(정상 실행에서는 루트가 하나라 맞는 설계다).
    여기서는 조각마다 임시 루트가 다르므로, 비우지 않으면 앞 조각의 맵으로 판정해
    `Include/PathCasing` 같은 규칙이 조용히 안 걸린다.
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
        for category, relPath, content in ruleOwnedCases:
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
        for caseIndex, (category, files) in enumerate(_kWholeScanCases):
            # 같은 카테고리의 조각이 여럿이면(규칙의 갈래마다 하나) 서로 다른 폴더에 둔다 — 한 폴더면 하나만 살아 있어도 둘 다 잡힌 것으로 보인다.
            caseRoot = tempRoot / f"{category.replace('/', '_')}_{caseIndex}"
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

        # --- 체크아웃 경로에 제외 폴더 이름(`build`)이 든 저장소 ---
        errors.extend(checkCheckoutPathWithExcludedWordInternal(tempRoot))

        # --- 오탐 확인 (파일 단위) ---
        cleanRoot = tempRoot / "clean"
        cleanPath = writeFixtureInternal(cleanRoot, _kCleanCase[0], _kCleanCase[1])
        resetPathMapCacheInternal()
        cleanFound = categoriesForFileInternal(cleanRoot, cleanPath)

        if cleanFound:
            errors.append(f"깨끗해야 할 조각에서 위반이 나왔습니다 (오탐): {sorted(cleanFound)}")

        # --- 오탐 확인 (트리 전체) ---
        # 전체 스캔 전용 규칙은 위쪽 파일 단위 검사에서 **아예 돌지 않는다.** 그래서 그 규칙들의
        # 오탐은 여기서만 보인다.
        cleanTreeRoot = tempRoot / "clean_tree"
        for relPath, content in _kWholeScanCleanCase.items():
            writeFixtureInternal(cleanTreeRoot, relPath, content)
        resetPathMapCacheInternal()
        cleanTreeFound = categoriesForTreeInternal(cleanTreeRoot)

        if cleanTreeFound:
            errors.append(f"깨끗해야 할 트리에서 위반이 나왔습니다 (오탐): {sorted(cleanTreeFound)}")
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
