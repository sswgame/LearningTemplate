#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/CommonShapes.py

공통 기반이 있는데 손으로 다시 적은 모양을 막는다. 기반을 만들어 일부만 옮겨 태우면 옛 모양이 남아 복사되는 것이 근본 원인이었다(2026-11-07 조사 `refactor_common.md`).
**왜 형식이 아니라 린트인가.** 여섯 다 합법인 C++ 이다 — 칸 이름 · switch 의 글자 · 어떤 함수를 불렀는가는 형식이 말하지 못한다.

1. 철자 같은 열거 이름 표: 모든 case 가 열거자 철자 글을 돌려주는 switch. `ENUM()` + `getEnumName` 이 맡는다(`Core` 는 리플렉션 아래 층이라 보지 않는다).
2. 다시 읽기 횟수 손 대조: `_seen…ReloadCount` 칸 · `getReloadCount` 호출. `AssetReloadWatch` 가 맡는다.
3. 첫 컴포넌트 손 찾기: `if ( pFound == nullptr ) pFound = …` 람다. `AutomationComponentWatch::findFirst` 가 맡는다.
4. 스키마만 읽는 한 줄 검사 함수: `AutomationStep::validateProperties<TParams>` 가 맡는다.
5. 캔버스 지우기 손 구간: `ed::BeginDelete`. `EditorNodeGraph::collectRemovals` 가 맡는다.
6. 저장 글 칸 · 부모 차이 손 저장: `string _savedText` 는 `EditorDocumentState`, `DataInheritance::makeDelta` 는 `ResourceComposer::saveDelta` 가 맡는다.
7. id 를 버리는 리소스 입구: 이름이 `load…` 인 입구(`loadFromAsset` · `loadFromFile` · `loadFromPack` · `load` …)가 리소스 글(`AssetIO::readTextAsset`)을 읽은 뒤
   id 없이 글 입구로 넘기면 덮어쓰기 층 · 다시 읽기 기록 · 접속 대조를 받지 못한다(조용히 빠진다). id 를 싣는 길(`ResourceContext::withAsset` ·
   `XMLDocument::parseResourceText` · `XMLReadUtil::parseResourceRoot` · `loadResource…`)을 지나거나, 읽은 id 변수를 그대로 다음 함수에 넘겨야 한다.
   예전에는 이름 둘(`loadFromAsset` · `loadFromFile( const AssetID& )`)만 보아 다른 이름의 입구(타일맵 `load`)가 빠졌다 — 입구는 이름이 아니라 "load 로 시작하고 리소스 글을 읽는다" 로 고른다.
8. 엔진 자식 손 명령: `ProcessCommand::at(` 은 `EngineChildProcess::makeCommand` 가 맡는다 — 자동화 실행의 자식에 `-unattended` 를 넘긴다.
   빠지면 넷 플레이 클라이언트가 사람의 실행으로 떠 사양으로 품질을 골라 서버와 합성이 갈렸다. 엔진이 아닌 도구(Tracy · tint · 편집기)는 예외 표에 까닭과 함께.
9. 에디터의 리소스 트리 손 훑기: 에디터 코드(`/Editor/` 아래)의 `FileUtil::collectFiles(` · `collectFolders(` 는 `EditorContentScan` 이 맡는다 —
   다른 프로세스의 시나리오 사본을 뺀다. 빠지면 Quick Open 이 남의 사본을 집고 임포터가 남의 사본에 임포트했다. 리소스 트리 밖(Saved · Config · 소스) 폴더는
   예외 표에 `경로|첫 인자` 와 까닭으로 둔다. 쿠커 · 팩(`AssetPaths::collectContentFiles`)은 에디터 밖이라 보지 않는다(scratch 전체를 뺀다).
10. 이름 해시를 손으로 키로 쓰는 표: `hashed_string::getHash()`(32 비트) 결과를 맵 첨자 · `emplace` · `insert…` · `find` · `count` · `contains` · `tryClaim` 의
   키로 넘기거나 `…key` · `…Key` · `…_id` 변수에 넣는 모양. 같은 해시의 두 이름을 가리지 못해 나중 것이 조용히 이긴다(감사 C-8) — 저장 · 조회 키는
   `NameHashIndex`(공용 충돌 검사 색인)로 넣는다. 섞기 · 씨앗 · `LogOnce` 처럼 겹쳐도 데이터가 바뀌지 않는 자리는 예외 표에 까닭과 함께 둔다.
11. 타입 부모 사슬 손 걷기: `p = p->getParentType()` 로 사슬을 따라가는 루프. 고리 · 깊이 상한은 `TypeInfo::getTypeChain` 한 곳이 막는다 —
   손 걷기마다 고리 방지를 따로 두다 하나가 빠져 고리 사슬에서 끝나지 않았다(필드 이름 해시 검사, 단위 실행이 시한에 멈췄다).
12. 에디터 자동화 포인터 손 옮기기: `EditorSelfTestInput::moveMouse(` 는 레이블 판정을 건너뛴다 — 레이블을 누르고 호버하는 길은
   `EditorSelfTestInput::aimAtMark` · `pressAtMark`(가림 판정)만 지난다. 손 옮기기로 레이블 좌표를 누르면 오버레이 툴바 · 팝업이 덮어도 단계가 성공했다
   (editor.gizmotoplevel). 입구로 누른 뒤의 끌기 경로(중간 · 끝 점)만 예외 표에 `경로|함수` 와 까닭으로 둔다 — 파일 단위로 두면 예외 파일 안의 새 함수가
   빠져나갔다. 입구 파일(`EditorSelfTestInput.cpp`)은 이름 없이 부르는 `moveMouse(` 도 본다(정의 줄은 보지 않는다).

예외는 `Config/Rules/Lint/CommonShapeExceptions.json` 에 까닭과 함께 적는다. 낡은 예외(더는 없는 자리)도 위반이다.
`py -3 -m Scripts lint common-shapes [--self-check]` 로 돌리고, 커밋 훅(`Rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from lint.FunctionSpans import findClosingBrace, findEnclosingName, findFunctionSpans, runSelfCheck as runFunctionSpansSelfCheck  # noqa: E402
from lint.LintScope import LintScope, makeWholeScope, readTexts  # noqa: E402

kConfigPath = "Config/Rules/Lint/CommonShapeExceptions.json"
kExtensions = (".h", ".cpp", ".inl")
kSkipParts = ("ThirdParty", "Tests")
kCommentPattern = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)

kSwitchPattern = re.compile(r"\bswitch\s*\([^\n]*\)\s*\{")
kCasePattern = re.compile(r"\bcase\s+([\w:]+)\s*:\s*(?:\{\s*)?return\s+\"([^\"]*)\"")
kAnyCasePattern = re.compile(r"\bcase\s+[\w:]+\s*:")
kDefaultNullPattern = re.compile(r"\bdefault\s*:\s*return\s+nullptr")
kSeenCountPattern = re.compile(r"\b_seen\w*ReloadCount\b")
kReloadCallPattern = re.compile(r"::getReloadCount\(\s*\)")
kFirstComponentPattern = re.compile(r"forEachComponentOfType<[^>]*>\([^\n]*\n\s*\{\s*\n\s*if \( pFound == nullptr \)\s*\n\s*pFound = ")
kOneLineValidatorPattern = re.compile(r"expected<void>\s+(?:\w+::)?\w+\(\s*const AutomationStep& step\s*\)\s*\{\s*\w+ params;\s*return step\.readProperties\( params \);\s*\}")
kCanvasDeletePattern = re.compile(r"\bed::BeginDelete\(")
kSavedTextPattern = re.compile(r"^\s*string\s+_savedText\s*;", re.M)
kMakeDeltaPattern = re.compile(r"\bDataInheritance::makeDelta\(")
kReadTextAssetPattern = re.compile(r"\bAssetIO::readTextAsset\(\s*(\w+)?")
kEngineChildPattern = re.compile(r"\bProcessCommand::at\(")
kEditorScanPattern = re.compile(r"\bFileUtil::collect(?:Files|Folders)\(\s*([^,]+?)\s*,")
# 첨자는 getHash() 로 끝나는 안쪽만(`list[ i ]._id.getHash()` 의 앞 첨자는 아니다), 호출은 첫 인자 안의 getHash() 만 본다.
kNameHashKeyPattern = re.compile(r"\[\s*[^\[\]\n]*\bgetHash\(\s*\)\s*\]|\b(?:emplace|try_emplace|insert|insert_or_assign|find|count|contains|tryClaim)\(\s*[^;\n]*?\bgetHash\(\s*\)|\b\w*(?:[Kk]ey|_id)\s*=\s*[^;=\n]*\bgetHash\(\s*\)")
kEditorPointerMovePattern = re.compile(r"\bEditorSelfTestInput::moveMouse\(")
kEditorPointerBarePattern = re.compile(r"(?<![\w.>:])moveMouse\(")
kEditorPointerBaseFile = "Source/Editor/SelfTest/EditorSelfTestInput.cpp"
kEditorPointerDefinitionPattern = re.compile(r"[ \t]*void\s+(?:EditorSelfTestInput::)?moveMouse\(")
kTypeChainWalkPattern = re.compile(r"\b(\w+)\s*=\s*\1->getParentType\(\s*\)")
kAssetEntranceName = re.compile(r"\w+::load\w*")   # 리소스 입구 이름(정의는 FunctionSpans 가 찾는다)
kAssetCarrierPattern = re.compile(r"withAsset\(|parseResourceText\(|parseResourceRoot\(|loadResource\w*\(|loadFromAsset\(")


def findSameSpellingSwitches(code: str) -> list[str]:
    """모든 case 가 열거자 철자를 돌려주는 switch 의 열거 이름을 돌려준다. `default: return nullptr`(거르기)는 이름 표가 아니라 보지 않는다."""
    listEnum: list[str] = []
    for match in kSwitchPattern.finditer(code):
        body = code[match.end() - 1:findClosingBrace(code, match.end() - 1)]
        listCase = kCasePattern.findall(body)
        if len(listCase) < 2 or len(listCase) != len(kAnyCasePattern.findall(body)) or kDefaultNullPattern.search(body):
            continue
        if all(case.split("::")[-1] == text for case, text in listCase):
            parts = listCase[0][0].split("::")
            listEnum.append(parts[-2] if len(parts) >= 2 else "?")
    return listEnum


def findIdDroppingEntrances(code: str) -> list[str]:
    """글을 읽은 뒤 id 를 싣지 않는 리소스 입구(`형식::함수`)를 돌려준다."""
    listEntrance: list[str] = []
    for span in findFunctionSpans(code):
        if kAssetEntranceName.fullmatch(span.name) is None:
            continue
        body = code[span.openIndex:span.closeIndex]
        readMatch = kReadTextAssetPattern.search(body)
        if readMatch is None or kAssetCarrierPattern.search(body, readMatch.end()):
            continue
        idName = readMatch.group(1)
        bPassesID = idName is not None and idName != "AssetID" and re.search(rf"[(,]\s*{re.escape(idName)}\s*[,)]", body[readMatch.end():]) is not None
        if not bPassesID:
            listEntrance.append(span.name)
    return listEntrance


def findPointerMoves(path: str, code: str) -> list[str]:
    """레이블 판정을 건너뛰는 포인터 손 옮기기를 담은 함수 이름들이다(`editorPointerMove`)."""
    listMatch = list(kEditorPointerMovePattern.finditer(code))
    if path == kEditorPointerBaseFile:
        listMatch += list(kEditorPointerBarePattern.finditer(code))
    listFunction: set[str] = set()
    listSpan = findFunctionSpans(code) if listMatch else []
    for match in listMatch:
        lineStart = code.rfind("\n", 0, match.start()) + 1
        if kEditorPointerDefinitionPattern.match(code, lineStart):
            continue
        listFunction.add(findEnclosingName(listSpan, match.start()))
    return sorted(listFunction)


def collectViolations(path: str, text: str) -> list[tuple[str, str, str]]:
    """글 하나의 (규칙, 예외 열쇠, 설명) 들을 돌려준다."""
    code = kCommentPattern.sub("", text)
    listViolation: list[tuple[str, str, str]] = []
    if not path.startswith("Source/Core/"):
        for enumName in findSameSpellingSwitches(code):
            listViolation.append(("enumNameSwitch", f"{path}|{enumName}", f"철자 같은 열거 이름 표({enumName}) — ENUM() + getEnumName"))
    if kSeenCountPattern.search(code):
        listViolation.append(("reloadCount", path, "_seen…ReloadCount 칸 — AssetReloadWatch"))
    if kReloadCallPattern.search(code):
        listViolation.append(("reloadCount", path, "getReloadCount 손 호출 — AssetReloadWatch"))
    if kFirstComponentPattern.search(code):
        listViolation.append(("firstComponent", path, "첫 컴포넌트 손 찾기 — AutomationComponentWatch::findFirst"))
    if kOneLineValidatorPattern.search(code):
        listViolation.append(("oneLineValidator", path, "스키마만 읽는 한 줄 검사 함수 — AutomationStep::validateProperties<TParams>"))
    if kCanvasDeletePattern.search(code):
        listViolation.append(("canvasDelete", path, "캔버스 지우기 손 구간 — EditorNodeGraph::collectRemovals"))
    if kSavedTextPattern.search(code):
        listViolation.append(("savedText", path, "저장 글 칸 — EditorDocumentState"))
    if kMakeDeltaPattern.search(code):
        listViolation.append(("makeDelta", path, "부모 차이 손 저장 — ResourceComposer::saveDelta"))
    for entrance in findIdDroppingEntrances(code):
        listViolation.append(("assetEntrance", f"{path}|{entrance}", f"id 를 버리는 리소스 입구({entrance}) — ResourceContext::withAsset · parseResourceText · parseResourceRoot"))
    if kEngineChildPattern.search(code):
        listViolation.append(("engineChildCommand", path, "ProcessCommand::at 손 명령 — 엔진 자식은 EngineChildProcess::makeCommand"))
    for function in findPointerMoves(path, code):
        listViolation.append(("editorPointerMove", f"{path}|{function}", f"moveMouse 손 옮기기({function}) — 레이블은 EditorSelfTestInput::aimAtMark · pressAtMark(가림 판정)로 누른다"))
    if kTypeChainWalkPattern.search(code):
        listViolation.append(("typeChainWalk", path, "부모 사슬 손 걷기 — TypeInfo::getTypeChain(고리 · 상한을 한 곳에서 막는다)"))
    if kNameHashKeyPattern.search(code):
        listViolation.append(("nameHashKey", path, "getHash() 를 손으로 키로 쓴다 — 저장 · 조회 키는 NameHashIndex(같은 해시의 다른 이름을 거절)"))
    if "/Editor/" in path:
        for argument in sorted(set(match.group(1) for match in kEditorScanPattern.finditer(code))):
            listViolation.append(("editorContentScan", f"{path}|{argument}", f"FileUtil 손 훑기({argument}) — 리소스 트리는 EditorContentScan"))
    return listViolation


def findProblems(listSource: Iterable[tuple[str, str]], exceptions: dict, bWholeRepo: bool = True) -> list[str]:
    listProblem: list[str] = []
    seenKey: set[tuple[str, str]] = set()
    for path, text in listSource:
        for rule, key, message in collectViolations(path, text):
            seenKey.add((rule, key))
            if key not in exceptions.get(rule, {}):
                listProblem.append(f"{path}: {message} (예외는 {kConfigPath} 의 {rule} 에 '{key}' 와 까닭)")
    for rule, table in exceptions.items() if bWholeRepo else ():   # 낡은 예외는 저장소 전체를 봐야 안다
        if rule.startswith("_"):
            continue
        for key in table:
            if (rule, key) not in seenKey:
                listProblem.append(f"{kConfigPath}: 낡은 예외 {rule} '{key}' — 그 모양이 더는 없다, 지운다")
    return listProblem


def readSources(projectRoot: Path, scope: LintScope) -> list[tuple[str, str]]:
    return readTexts(projectRoot, scope.select(("Source",), kExtensions, kSkipParts))


def checkCommonShapes(projectRoot: Path, scope: LintScope) -> list[str]:
    exceptions = json.loads((projectRoot / kConfigPath).read_text(encoding="utf-8"))
    return findProblems(readSources(projectRoot, scope), exceptions, scope.bWholeRepo)


def runSelfCheck() -> None:
    runFunctionSpansSelfCheck()   # 공용 함수 범위 도우미(`경로|함수` 열쇠)도 함께 본다
    same = 'const utf8* f( Kind k ) { switch ( k )\n{\ncase Kind::Aa: return "Aa";\ncase Kind::Bb: return "Bb";\n}\n}'
    other = 'const utf8* f( Kind k ) { switch ( k )\n{\ncase Kind::Aa: return "aa";\ncase Kind::Bb: return "Bb";\n}\n}'
    filtered = 'const utf8* f( Kind k ) { switch ( k )\n{\ncase Kind::Aa: return "Aa";\ncase Kind::Bb: return "Bb";\ndefault: return nullptr;\n}\n}'
    assert findSameSpellingSwitches(same) == ["Kind"]
    assert findSameSpellingSwitches(other) == []
    assert findSameSpellingSwitches(filtered) == []
    assert len(findProblems([("Source/A/a.cpp", same)], {})) == 1
    assert findProblems([("Source/Core/a.cpp", same)], {}) == []
    assert findProblems([("Source/A/a.cpp", same)], {"enumNameSwitch": {"Source/A/a.cpp|Kind": "이유"}}) == []
    stale = findProblems([("Source/A/a.cpp", "int x;")], {"enumNameSwitch": {"Source/A/a.cpp|Kind": "이유"}})
    assert len(stale) == 1 and "낡은" in stale[0], stale
    validator = "static expected<void> validateX( const AutomationStep& step )\n{\n    MyParams params;\n    return step.readProperties( params );\n}"
    assert [v[0] for v in collectViolations("Source/A/a.cpp", validator)] == ["oneLineValidator"]
    assert collectViolations("Source/A/a.cpp", "// ed::BeginDelete()\nint x;") == []
    assert [v[0] for v in collectViolations("Source/A/a.cpp", "if ( ed::BeginDelete() )")] == ["canvasDelete"]
    dropping = "bool A::loadFromAsset( const AssetID& assetID )\n{\n    string text;\n    if ( AssetIO::readTextAsset( assetID, text ) == false )\n        return false;\n    return loadFromXMLText( text, assetID.view() );\n}"
    carrying = dropping.replace("loadFromXMLText( text, assetID.view() )", "loadComposed( text, assetID.view(), ResourceContext::forType( *StaticType() ).withAsset( assetID ) )")
    assert findIdDroppingEntrances(dropping) == ["A::loadFromAsset"], findIdDroppingEntrances(dropping)
    assert findIdDroppingEntrances(carrying) == []
    otherName = "bool B::loadFromPack( const DataPath& root )\n{\n    string text;\n    if ( AssetIO::readTextAsset( AssetID{ path }, text ) == false )\n        return false;\n    return loadFromXMLText( text, path );\n}"
    assert findIdDroppingEntrances(otherName) == ["B::loadFromPack"], "이름이 loadFromAsset 이 아니어도 입구다"
    passing = "bool C::loadDesc( const AssetID& assetID )\n{\n    string text;\n    if ( AssetIO::readTextAsset( assetID, text ) == false )\n        return false;\n    return parseDesc( assetID.view(), assetID, text );\n}"
    assert findIdDroppingEntrances(passing) == [], "읽은 id 를 그대로 넘기면 싣는 것이다"
    calling = "void D::f()\n{\n    if ( E::loadThing( x ) )\n    {\n        AssetIO::readTextAsset( AssetID{ p }, t );\n    }\n}"
    assert findIdDroppingEntrances(calling) == [], "호출은 입구가 아니다"
    child = "const ProcessCommand command = ProcessCommand::at( *executable, std::move( listArgument ) );"
    assert [v[0] for v in collectViolations("Source/Editor/a.cpp", child)] == ["engineChildCommand"]
    assert collectViolations("Source/Editor/a.cpp", "EngineChildProcess::makeCommand( parent, path, {} );") == []
    scan = "(void)FileUtil::collectFiles( resourceRoot, \"\", listFile, true );"
    assert [v[1] for v in collectViolations("Source/Editor/a.cpp", scan)] == ["Source/Editor/a.cpp|resourceRoot"]
    assert collectViolations("Source/Engine/a.cpp", scan) == [], "에디터 밖(쿠커 · 엔진)은 보지 않는다"
    assert collectViolations("Source/Editor/a.cpp", "(void)EditorContentScan::collectFiles( root, root, \"\", listFile, true );") == []
    assert [v[0] for v in collectViolations("Source/A/a.cpp", "for ( const TypeInfo* p = &t; p != nullptr; p = p->getParentType() ) {}")] == ["typeChainWalk"]
    assert collectViolations("Source/A/a.cpp", "for ( const TypeInfo* p : t.getTypeChain() ) {}") == []
    pointerMove = "static bool runDrag( Runner& runner )\n{\n    if ( x )\n    {\n        EditorSelfTestInput::moveMouse( float2{ 1.0f, 2.0f } );\n    }\n    return true;\n}"
    assert [v[1] for v in collectViolations("Source/Editor/a.cpp", pointerMove)] == ["Source/Editor/a.cpp|runDrag"], "열쇠는 담은 함수다(제어문이 아니다)"
    inLambda = pointerMove.replace("    if ( x )\n    {\n        EditorSelfTestInput::moveMouse", "    each( [&]( int v )\n    {\n        EditorSelfTestInput::moveMouse").replace("    }\n    return true;", "    } );\n    return true;")
    assert [v[1] for v in collectViolations("Source/Editor/a.cpp", inLambda)] == ["Source/Editor/a.cpp|runDrag#lambda1"], "예외 함수 안 새 람다는 다른 열쇠다"
    base = "int x;\n    void EditorSelfTestInput::moveMouse( const float2& p )\n{\n}\n\nvoid EditorSelfTestInput::parkMouse()\n{\n    moveMouse( p );\n}"
    assert [v[1] for v in collectViolations(kEditorPointerBaseFile, base)] == [kEditorPointerBaseFile + "|EditorSelfTestInput::parkMouse"], "입구 파일은 이름 없는 호출도 보고 정의 줄은 보지 않는다"
    assert collectViolations("Source/Editor/a.cpp", "(void)EditorSelfTestInput::pressAtMark( pKey, anchor, 0, cover );") == [], "입구는 걸리지 않는다"
    assert [v[0] for v in collectViolations("Source/A/a.cpp", "_mapByHash[name.getHash()] = value;")] == ["nameHashKey"]
    assert [v[0] for v in collectViolations("Source/A/a.cpp", "map.emplace( name.getHash(), value );")] == ["nameHashKey"]
    assert [v[0] for v in collectViolations("Source/A/a.cpp", "const uint64 key = name.getHash();")] == ["nameHashKey"]
    assert collectViolations("Source/A/a.cpp", "if ( list[ index ]._id.getHash() == other ) {}") == [], "앞 첨자는 키가 아니다"
    assert collectViolations("Source/A/a.cpp", "(void)index.assign( name.view(), name.getHash() );") == [], "공용 색인으로 넣는 것은 걸리지 않는다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="공통 기반이 있는데 손으로 다시 적은 모양을 본다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[CommonShapes] self-check OK")
        return ExitCode.Passed
    listProblem = checkCommonShapes(getProjectRoot(), makeWholeScope(getProjectRoot()))
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[CommonShapes] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
