#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
서비스 접근이 **소유 수명마다 한 창구, 한 어휘**인지 검사한다.

접근 창구는 이름 둘이다 — `getSubsystem<T>()`(`T&`, 반드시 있음)와 `findSubsystem<T>()`(`T*`, 없을 수 있음). 범위는 엔진(`engine::`) · 게임(`game::`)
· 에디터(`editor::`) · 씬(`GameObjectManager::getSubsystem<T>()`)이고, 범위를 든 객체의 멤버(`EditorContext` · `GameInstanceBase` · `SubsystemScope`)는
그대로다. 이 게이트는 옛 창구가 다시 생기지 않게 막는다.

  - `ServiceAccess/RetiredWindow`: 지워진 이름 — `getService<` · `getSceneService<` · `isServiceBound<` · `getBoundEngineServices(`,
    `locateSubsystem<…>( engine::getEngineSubsystems() )`, 그리고 `engine::` 아래의 이름 붙은 접근자(`engine::getTypeRegistry()` 따위).
    `engine::` 아래에 남는 `get*` 은 규칙 데이터의 `allowed_engine_function` 뿐이다. 주석 · 문자열 안의 언급은 보지 않는다.
  - `ServiceAccess/Singleton`: 헤더의 `static … getInstance()` 와 `static T& get()`(참조를 돌려주는 인자 없는 `get`), 그리고 **이름이 무엇이든 같은 모양**.
    전역 단일체는 수명과 순서가 숨는다 — 새 서비스는 서브시스템으로 만들어 범위에 넣는다.
    이름만 보면 `instance()` · `shared()` 로 우회하므로 모양을 본다: (가) 클래스 안의 `static T& anything()`(자기 타입의 읽고 쓰는 참조를 돌려주는 인자 없는 정적 멤버),
    (나) 인자 없이 참조를 돌려주고 몸에 함수 안 정적 지역을 두어 그 이름을 돌려주는 정의(`T& anything() { static T s; return s; }`, 클래스 밖 자유 함수도).
    (다) 포인터 · 스마트 포인터를 돌려주는 정적 멤버는 팩토리(`static T* create()`)와 모양이 같아 모양으로 못 가른다 — 이 틈에만 **이름**
    (`instance` · `shared` · `global` · `singleton` …)을 본다.
    읽기 전용 참조(`const T&`)와 포인터의 참조(`T*&`)는 섞인 상태를 내주는 창구가 아니라 상수 표 · 링크 때 목록 머리라 세지 않는다.
    **보지 않는 곳**: `.cpp` — 익명 · `…Internal` 이름공간의 함수 안 정적 지역 접근자(`getList` · `getMutex` …)는 그 번역 단위 안에서만 보이는 도우미라 창구가
    아니다(헤더가 이름을 내놓은 적이 없다는 것을 2026-10-25 에 확인했다). 헤더가 `T& anything();` 로 선언만 하고 본문을 `.cpp` 에 두는 바깥 연결 자유 함수는
    선언만으로 단일체인지 알 수 없어 놓친다 — 새로 생기면 이 게이트가 아니라 리뷰에서 걸러야 하는 틈이다.
  - `ServiceAccess/ActiveWindow`: 헤더의 정적 활성 창구 `static T* findActive…()` · `static T* getActive…()`(포인터를 돌려주는 것). 정적 포인터 칸에 "지금 도는 것" 을
    적어 두는 모양이라 단일체와 같이 수명이 숨고, 둘째 인스턴스 · 내려간 인스턴스를 가리킨다(App 의 교체기 · 엔진의 셸 액션 · 디버그 HUD 가 이 모양이었다).
    소유자가 객체를 범위에 빌려 걸고(`SubsystemScope::bind`) 쓰는 쪽이 `findSubsystem<T>()` 로 찾는다.
    **형식으로 막지 못하는 틈이라 게이트를 둔다** — 정적 멤버 함수와 정적 변수는 언어가 늘 허락하고, 범위 창구는 쓰는 쪽이 고르는 길이라 타입이 강제할 수 없다.
  예외는 둘 다 `Scripts/lint/rules/CheckServiceAccess.toml` 의 `[exemption]`(타입 이름 → 예외 종류)이고, 더는 그 모양이 없는 타입은 낡은 예외로 실패한다.
  이유를 자유 글로 적으면 "단일체가 아닌 까닭" 이 줄마다 달라 판단이 쌓이지 않는다(널 객체 · 에디터 계측이 "서비스가 아니다" 로 남아 있었다). 그래서 값은
  종류 표 `[exemption_kind]` 의 키 넷 — `LinkTimeRegistry`(링크 때 등록 표) · `CrashPath`(크래시 · 할당 훅 경로) · `ScopeOwnedHotPath`(범위가 소유한 저장소의
  핫 패스 창구) · `Stateless`(상태 없는 함수 묶음) — 중 하나여야 하고, 표 밖 키는 검사가 서지 않는다(종료 2). 어느 종류에도 안 맞으면 단일체가 아니라
  값(소유자가 든다)이나 서브시스템으로 만든다.

대상은 `Source/` · `Test/` · `Tools/ReflectionParser/` · `Tools/OnlineLoadBot/` 의 `.h` · `.cpp` · `.inl` 이다(생성 `.gen.cpp` 와 ThirdParty 제외).

  python Scripts/lint/gate/CheckServiceAccess.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.CodeText import findFast  # noqa: E402
from common.Constants import kLintTargetRelDirs  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.RuleData import kKindReason, kKindTextList  # noqa: E402
from lint.LintGate import GateResult, LintGate  # noqa: E402
from lint.SourceScan import FileFindings, SourceFile, SourceScanResult, joinFindings, scanSourceGates  # noqa: E402

_kListScanRoot = kLintTargetRelDirs
_kSuffixes = (".h", ".cpp", ".inl")
_kHeaderSuffixes = (".h", ".inl")

_kRuleSchema = {"allowed_engine_function": kKindTextList, "exemption_kind": kKindReason}
_kRuleData = LintGate.readRules("CheckServiceAccess", _kRuleSchema, requiredKeys=("allowed_engine_function", "exemption_kind"))
_kSetRemainingEngineFunction = frozenset(_kRuleData["allowed_engine_function"])
_kMapReasonKind: dict[str, str] = _kRuleData["exemption_kind"]   # 예외의 종류 → 뜻 (`[exemption]` 의 값은 이 표의 키여야 한다)

_kRetiredWindowCategory = "ServiceAccess/RetiredWindow"
_kSingletonCategory = "ServiceAccess/Singleton"
_kActiveWindowCategory = "ServiceAccess/ActiveWindow"

# 지워진 창구 다섯 모양. 이름 붙은 접근자는 아래 `_kEngineNamedAccessorRe` 가 따로 본다(허용 이름을 데이터에서 빼야 해서).
_kRetiredWindowRe = (
    (re.compile(r"\b(?:getService|getSceneService|isServiceBound)\s*<"), "getSubsystem<T>() / findSubsystem<T>() 를 쓰십시오"),
    (re.compile(r"\bgetBoundEngineServices\s*\("), "engine::getSubsystem<T>() / findSubsystem<T>() 를 쓰십시오"),
    (re.compile(r"\blocateSubsystem\s*<[^;]*?>\s*\(\s*(?:\w+::)*getEngineSubsystems\s*\(\s*\)\s*\)"),
     "engine::findSubsystem<T>() 를 쓰십시오"),
)
_kEngineNamedAccessorRe = re.compile(r"(?<![\w$])engine::(get[A-Z]\w*)\s*\(")

# `static T& getInstance()` 와 `static T& get()` — 둘 다 전역 단일체 창구다. `get` 은 참조를 돌려주는 인자 없는 것만 본다(`&` 필수).
_kStaticGetInstanceRe = re.compile(r"\bstatic\b[^;{}()=]*?(?:\bgetInstance\s*\(|&\s*get\s*\(\s*\))")
_kStaticActiveWindowRe = re.compile(r"\bstatic\b[^;{}()=]*?\*\s*((?:find|get)\w*Active\w*)\s*\(\s*\)")
# 이름이 무엇이든(`instance()` · `shared()` · `global()` …) 단일체 창구인 두 모양 — 이름이 아니라 모양을 본다.
#   (가) 자기 타입의 참조를 돌려주는 인자 없는 정적 멤버(선언이든 정의든): `static T& anything()` 를 T 클래스 안에서.
#   (나) 인자 없이 참조를 돌려주고 몸에 함수 안 정적 지역(`static T s; … return s;`)을 두는 정의 — 클래스 안 · 밖 · 정적 여부와 무관하다.
_kStaticOwnReferenceRe = re.compile(r"\bstatic\b([^;{}()=]*?)&\s*(\w+)\s*\(\s*\)")
_kReferenceAccessorBodyRe = re.compile(r"&\s*(\w+)\s*\(\s*\)\s*(?:noexcept\s*)?\{")
# (다) 포인터 · 스마트 포인터로 내주는 단일체 창구. 반환형이 `T*` 인 정적 멤버는 팩토리(`static T* create()`)와 모양이 같아서 모양으로 가를 수 없다 —
#     **구조로 막지 못하는 틈이라 이름을 본다**(`instance` · `shared` … 가 붙은 인자 없는 정적 멤버가 `*` 나 `*_ptr<` 를 돌려주는 것). 참조 반환은 위 (가) 가 이미 센다.
_kStaticPointerNamedRe = re.compile(
    r"\bstatic\b([^;{}()=]*?(?:\*|\b\w*_ptr\s*<)[^;{}()=]*?)\b(instance|shared|global|singleton|sharedInstance|defaultInstance|getShared|getGlobal|getSingleton)\s*\(\s*\)")
_kStaticLocalStatementRe = re.compile(r"\bstatic\s+(?!assert\b)([^;]*);")
_kStaticLocalNameRe = re.compile(r"(\w+)\s*(?:=|\{|\(|\[|$)")
_kClassHeadRe = re.compile(r"\b(?:class|struct)\s+(?:(?:\w*API|alignas\s*\([^)]*\))\s+)*(\w+)")


def getExemptionKinds() -> dict[str, str]:
    """예외 종류 표(`[exemption_kind]`)의 사본입니다."""
    return dict(_kMapReasonKind)


def findUnknownExemptionKinds(mapExemption: dict[str, str], mapKind: dict[str, str]) -> list[str]:
    """예외 표에서 값이 종류 표(`[exemption_kind]`)에 없는 줄을 돌려줍니다. 자유 글 이유를 쓰면 여기서 걸립니다."""
    return [f"{typeName} = {kind!r}" for typeName, kind in mapExemption.items() if kind not in mapKind]


def lineOfInternal(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def findEnclosingClassNames(code: str) -> list[tuple[int, str]]:
    """`{` 마다 그 중괄호가 여는 클래스 이름을 모아 (여는 위치, 이름) 으로 돌려줍니다. 클래스가 아닌 중괄호는 이름이 빈 문자열입니다."""
    listOpen: list[tuple[int, str]] = []
    segmentStart = 0
    for match in re.finditer(r"[;{}]", code):
        if match.group(0) == "{":
            segment = code[segmentStart:match.start()]
            head = _kClassHeadRe.search(segment) if re.search(r"\benum\b", segment) is None else None
            listOpen.append((match.start(), head.group(1) if head else ""))
        segmentStart = match.end()
    return listOpen


def classNameAtInternal(code: str, offset: int) -> str:
    """`offset` 를 감싸는 가장 안쪽 클래스의 이름입니다(없으면 빈 문자열)."""
    stack: list[str] = []
    listOpen = dict(findEnclosingClassNames(code))
    for match in re.finditer(r"[{}]", code[:offset]):
        if match.group(0) == "{":
            stack.append(listOpen.get(match.start(), ""))
        elif stack:
            stack.pop()
    for name in reversed(stack):
        if name:
            return name
    return ""


def findRetiredWindowUses(relative: str, text: str, code: str) -> list[str]:
    listOriginalLine = text.splitlines()
    listViolation: list[str] = []
    listHit: list[tuple[int, str, str]] = []
    for regex, advice in _kRetiredWindowRe:
        if findFast(regex, code) is not None:
            listHit.extend((match.start(), match.group(0), advice) for match in regex.finditer(code))
    for match in _kEngineNamedAccessorRe.finditer(code) if findFast(_kEngineNamedAccessorRe, code) is not None else ():
        if match.group(1) not in _kSetRemainingEngineFunction and match.group(1) != "getBoundEngineServices":  # 뒤쪽은 위 표가 이미 센다
            listHit.append((match.start(), match.group(0), "engine::getSubsystem<T>() / findSubsystem<T>() 를 쓰십시오"))
    for offset, spelling, advice in sorted(listHit):
        lineNumber = lineOfInternal(code, offset)
        listViolation.append(f"{relative}:{lineNumber}: [{_kRetiredWindowCategory}] {spelling.strip()} — {advice} | {listOriginalLine[lineNumber - 1].strip()}")
    return listViolation


def findBlockEnd(code: str, openOffset: int) -> int:
    """`code[openOffset]` 의 `{` 와 짝인 `}` 의 위치(없으면 글 끝)."""
    depth = 0
    for match in re.finditer(r"[{}]", code[openOffset:]):
        depth += 1 if match.group(0) == "{" else -1
        if depth == 0:
            return openOffset + match.start()
    return len(code)


def returnsStaticLocal(body: str) -> bool:
    """함수 몸이 함수 안 정적 지역을 만들고 그 이름을 돌려주는가(`static T s; return s;` · `return *s;`)."""
    for statement in _kStaticLocalStatementRe.finditer(body):
        nameMatch = _kStaticLocalNameRe.search(statement.group(1))
        if nameMatch is not None and re.search(rf"\breturn\s+\*?\s*{nameMatch.group(1)}\s*;", body[statement.end():]) is not None:
            return True
    return False


def findShapeSingletons(code: str) -> list[tuple[int, str, str, str]]:
    """
    이름이 무엇이든 단일체 창구인 모양(위 `_kStaticOwnReferenceRe` · `_kReferenceAccessorBodyRe`)을 (위치, 클래스 이름, 함수 이름, 모양 설명)으로 모은다.
    `static … getInstance(` · `static … &get()` 은 `_kStaticGetInstanceRe` 가 이미 센다 — 부른 쪽이 그 자리에 든 것을 걸러낸다.
    """
    listHit: list[tuple[int, str, str, str]] = []
    for match in _kStaticOwnReferenceRe.finditer(code):
        functionName = match.group(2)
        if functionName in ("get", "getInstance"):
            continue
        className = classNameAtInternal(code, match.start())
        typeText = re.sub(r"<[^<>]*>", "", match.group(1))
        typeWords = re.findall(r"\w+", typeText)
        # 읽기 전용 참조(`const T&`)와 포인터의 참조(`T*&`, 링크 때 목록의 머리)는 섞인 상태를 내주는 창구가 아니라 값 · 목록 머리다.
        if className != "" and typeWords and typeWords[-1] == className and "const" not in typeWords and "*" not in typeText:
            listHit.append((match.start(2), className, functionName, "자기 타입의 참조를 돌려주는 정적 멤버"))
    for match in _kStaticPointerNamedRe.finditer(code):
        if "&" in match.group(1):
            continue   # `T*&` 는 링크 때 목록 머리이고, 참조 반환은 (가) 의 몫이다
        className = classNameAtInternal(code, match.start())
        if className != "":
            listHit.append((match.start(2), className, match.group(2), "이름이 단일체 창구인 정적 멤버(포인터 · 스마트 포인터 반환)"))
    setSeen = {offset for offset, _, _, _ in listHit}
    for match in _kReferenceAccessorBodyRe.finditer(code):
        openOffset = match.end() - 1
        if match.start(1) in setSeen:
            continue
        declarationHead = code[max(code.rfind(";", 0, match.start()), code.rfind("{", 0, match.start()), code.rfind("}", 0, match.start())) + 1:match.start()]
        if re.search(r"\bconst\b", declarationHead) is not None:
            continue   # 읽기 전용 참조를 돌려주는 함수 안 정적 지역은 늦게 만드는 상수다
        if returnsStaticLocal(code[openOffset + 1:findBlockEnd(code, openOffset)]):
            listHit.append((match.start(1), classNameAtInternal(code, match.start(1)), match.group(1), "함수 안 정적 지역을 참조로 돌려주는 접근자"))
    return listHit


def findSingletons(relative: str, text: str, code: str) -> list[str]:
    gate = CheckServiceAccessGate
    listOriginalLine = text.splitlines()
    listViolation: list[str] = []
    for _, className in findEnclosingClassNames(code):
        if className in gate.mapExemption:
            gate.seeExemption(className)   # 타입은 있는데 getInstance() · 활성 창구가 없으면 낡은 예외로 실패한다
    for match in _kStaticGetInstanceRe.finditer(code):
        typeName = classNameAtInternal(code, match.start())
        lineNumber = lineOfInternal(code, match.start())
        if typeName in gate.mapExemption:
            gate.useExemption(typeName)
            continue
        owner = typeName or "(클래스 밖)"
        listViolation.append(f"{relative}:{lineNumber}: [{_kSingletonCategory}] {owner}::{'get' if match.group(0).rstrip().endswith(')') else 'getInstance'}() — 전역 단일체입니다. "
                             f"새 서비스는 서브시스템으로 만들어 범위에 넣으십시오 | {listOriginalLine[lineNumber - 1].strip()}")
    listNamed = [match.span() for match in _kStaticGetInstanceRe.finditer(code)]
    for offset, className, functionName, shape in findShapeSingletons(code):
        if any(start <= offset < end for start, end in listNamed):
            continue
        lineNumber = lineOfInternal(code, offset)
        if className in gate.mapExemption:   # 클래스 밖의 자유 함수는 예외 표에 올릴 수 없다(열쇠가 타입 이름이다)
            gate.useExemption(className)
            continue
        listViolation.append(f"{relative}:{lineNumber}: [{_kSingletonCategory}] {className + '::' if className else ''}{functionName}() — 전역 단일체입니다({shape}). "
                             f"이름이 달라도 단일체 창구입니다. 새 서비스는 서브시스템으로 만들어 범위에 넣으십시오 | {listOriginalLine[lineNumber - 1].strip()}")
    for match in _kStaticActiveWindowRe.finditer(code):
        typeName = classNameAtInternal(code, match.start())
        if typeName == "":
            continue   # 클래스 밖의 정적 자유 함수는 .cpp 안의 도우미다(헤더에는 없다)
        lineNumber = lineOfInternal(code, match.start())
        if typeName in gate.mapExemption:
            gate.useExemption(typeName)
            continue
        listViolation.append(f"{relative}:{lineNumber}: [{_kActiveWindowCategory}] {typeName}::{match.group(1)}() — 정적 활성 창구입니다. "
                             f"소유자가 범위에 걸고(SubsystemScope::bind) findSubsystem<T>() 로 찾으십시오 | {listOriginalLine[lineNumber - 1].strip()}")
    return listViolation


def findServiceAccessViolationsInFile(source: SourceFile) -> list[str]:
    relative = source.relative
    if relative.endswith(".gen.cpp") or "/ThirdParty/" in f"/{relative}":
        return []
    listViolation = findRetiredWindowUses(relative, source.text, source.masked)
    if relative.endswith(_kHeaderSuffixes):
        listViolation.extend(findSingletons(relative, source.text, source.masked))
    return listViolation


def findServiceAccessViolations(repositoryRoot: Path, listTargetFile: list[str] | None) -> list[str]:
    """`repositoryRoot`(셀프테스트의 조각 트리) 를 이 게이트 하나로 훑은 위반 — 게이트와 같은 훑기다."""
    gate = CheckServiceAccessGate()
    args = argparse.Namespace(files=listTargetFile)
    return gate.finishSourceScan(repositoryRoot, args, scanSourceGates(repositoryRoot, [(gate, args)])[0]).listViolation


class CheckServiceAccessGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 깨끗한 조각과 예외 쪽은 `selftest/CheckServiceAccessSelfTest.py` 가 본다."""

    description = "서비스 접근이 한 어휘(getSubsystem / findSubsystem)인지 · 헤더에 새 전역 단일체(getInstance) · 정적 활성 창구(findActive)가 없는지 검사"
    buildComment = "Checking service access vocabulary and singletons..."
    timeoutSeconds = 60
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    sourceScanRoots = _kListScanRoot
    sourceSuffixes = _kSuffixes
    ruleSchema = _kRuleSchema
    violationHeader = "서비스 접근 위반"
    hint = (
        "  서비스 접근은 범위마다 getSubsystem<T>()(반드시 있음) / findSubsystem<T>()(없을 수 있음) 둘입니다:\n"
        "      engine::getSubsystem<TaskManager>()      game::findSubsystem<T>()      editor::findSubsystem<T>()      GameObjectManager::getSubsystem<T>()\n"
        "  새 서비스는 전역 단일체(static getInstance())가 아니라 서브시스템으로 만들어 범위에 넣습니다.\n"
        "  \"지금 도는 것\" 을 정적 포인터로 내주는 창구(static T* findActive())도 두지 않습니다 — 소유자가 범위에 빌려 겁니다(SubsystemScope::bind).\n"
        "  정말 단일체가 필요하면 Scripts/lint/rules/CheckServiceAccess.toml 의 [exemption] 에 타입 이름과 예외 종류([exemption_kind] 의 키)를 적습니다."
    )
    selfTestCases = [
        {
            "name": "옛 getService 조회",
            "expectedViolation": "Source/Probe/ProbeService.cpp:3: [ServiceAccess/RetiredWindow] getService<",
            "files": {
                "Source/Probe/ProbeService.cpp": "void probe()\n{\n    auto* pManager = game::getService<InputManager>();\n}\n",
            },
        },
        {
            "name": "이름 붙은 엔진 접근자",
            "expectedViolation": "Source/Probe/ProbeNamed.cpp:3: [ServiceAccess/RetiredWindow] engine::getTypeRegistry(",
            "files": {
                "Source/Probe/ProbeNamed.cpp": "void probe()\n{\n    engine::getTypeRegistry().find( 1 );\n}\n",
            },
        },
        {
            "name": "새 전역 단일체",
            "expectedViolation": "Source/Probe/ProbeSingleton.h:4: [ServiceAccess/Singleton] ProbeSingleton::getInstance()",
            "files": {
                "Source/Probe/ProbeSingleton.h": "class ProbeSingleton\n{\npublic:\n    static ProbeSingleton& getInstance();\n};\n",
            },
        },
        {
            "name": "이름을 바꾼 단일체(instance) — 자기 타입의 참조를 돌려주는 정적 멤버",
            "expectedViolation": "Source/Probe/ProbeInstance.h:4: [ServiceAccess/Singleton] ProbeInstance::instance()",
            "files": {
                "Source/Probe/ProbeInstance.h": "class ProbeInstance\n{\npublic:\n    static ProbeInstance& instance();\n};\n",
            },
        },
        {
            "name": "이름을 바꾼 단일체(shared) — 함수 안 정적 지역을 돌려주는 자유 함수",
            "expectedViolation": "Source/Probe/ProbeShared.h:2: [ServiceAccess/Singleton] shared()",
            "files": {
                "Source/Probe/ProbeShared.h": "struct Cache;\ninline Cache& shared()\n{\n    static Cache s_cache;\n    return s_cache;\n}\n",
            },
        },
        {
            "name": "정적 활성 창구",
            "expectedViolation": "Source/Probe/ProbeActive.h:4: [ServiceAccess/ActiveWindow] ProbeActive::findActive()",
            "files": {
                "Source/Probe/ProbeActive.h": "class ProbeActive\n{\npublic:\n    static ProbeActive* findActive();\n};\n",
            },
        },
        {
            "name": "getInstance() 를 더는 두지 않는 예외 타입(낡은 예외)",
            "expectedViolation": "[낡은 예외] 'RawAnimationCodec' 의 대상은 그대로인데 규칙을 어기지 않습니다",
            "files": {
                "Source/Probe/ProbeCodec.h": "class RawAnimationCodec\n{\n};\n",
            },
        },
    ]

    def beginRun(self) -> int:
        """기반의 확인에 더해, 예외의 이유가 종류 표 안의 키인지 봅니다(표 밖 키는 검사가 서지 않는다 — 종료 2)."""
        returnCode = super().beginRun()
        if returnCode != ExitCode.Passed:
            return returnCode
        listUnknown = findUnknownExemptionKinds(self.mapExemption, _kMapReasonKind)
        if listUnknown:
            print(f"[{self.name}] 예외 종류가 표 밖입니다: {', '.join(listUnknown)} — 아는 종류: {', '.join(_kMapReasonKind)} "
                  f"(Scripts/lint/rules/CheckServiceAccess.toml 의 [exemption_kind])", file=sys.stderr)
            return ExitCode.CheckInvalid
        return ExitCode.Passed

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 Source · Test · Tools 전체)")

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        return FileFindings(listViolation=findServiceAccessViolationsInFile(source))

    def finishSourceScan(self, repositoryRoot: Path, args: argparse.Namespace, result: SourceScanResult) -> GateResult:
        listViolation, _ = joinFindings(result.listFindings)
        listRetired = [line for line in listViolation if f"[{_kRetiredWindowCategory}]" in line]
        listActive = [line for line in listViolation if f"[{_kActiveWindowCategory}]" in line]
        summary = (f"{' · '.join(_kListScanRoot)} — {_kRetiredWindowCategory} {len(listRetired)} · "
                   f"{_kSingletonCategory} {len(listViolation) - len(listRetired) - len(listActive)} · {_kActiveWindowCategory} {len(listActive)}")
        return GateResult(listViolation=listViolation, summary=summary)


main = CheckServiceAccessGate.run


if __name__ == "__main__":
    sys.exit(main())
