#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/common_shapes.py

공통 기반이 있는데 손으로 다시 적은 모양을 막는다. 기반을 만들어 일부만 옮겨 태우면 옛 모양이 남아 복사되는 것이 근본 원인이었다(2026-11-07 조사 `refactor_common.md`).
**린트는 최후의 수단이다.** 접근 제어 · 선언 막기로 막을 수 있는 모양은 코드로 옮겼다(2026-11-12): 다시 읽기 횟수 손 대조(`SharedAssetCache::getReloadCount` 비공개,
`AssetReloadWatch` 만 friend), 캔버스 지우기 손 구간(`ed::BeginDelete` 를 `EditorInputBan.h` 가 사용 중단으로 선언, 입구 `EditorNodeGraph.cpp` 만 받지 않는다),
타입 부모 사슬 손 걷기(`TypeInfo::getParentType` 비공개), 에디터 자동화 포인터 손 옮기기(`EditorSelfTestInput::moveMouse` 비공개, 끌기는 `moveByOffset`).
남은 열은 항목마다 적은 까닭으로 형식이 말하지 못한다 — 합법인 C++ 의 글자 · 칸 이름 · 인자의 뜻이거나, 정당한 사용처가 여러 층에 걸쳐 friend 로 묶을 수 없다.

1. 철자 같은 열거 이름 표: 모든 case 가 열거자 철자 글을 돌려주는 switch. `ENUM()` + `getEnumName` 이 맡는다(`Core` 는 리플렉션 아래 층이라 보지 않는다).
   형식으로 못 막는 까닭: switch 의 case 가 돌려주는 글의 내용이다.
2. 첫 컴포넌트 손 찾기: `if ( pFound == nullptr ) pFound = …` 람다. `AutomationComponentWatch::findFirst` 가 맡는다.
   형식으로 못 막는 까닭: `forEachComponentOfType` 은 모든 순회가 쓰는 공개 함수이고, 걸리는 것은 람다 본문의 모양이다.
3. 스키마만 읽는 한 줄 검사 함수: `AutomationStep::validateProperties<TParams>` 가 맡는다.
   형식으로 못 막는 까닭: `readProperties` 는 실행 함수도 쓰는 공개 함수이고, 걸리는 것은 "그것만 하는 함수" 라는 본문 모양이다.
4. 저장 글 칸 · 부모 차이 손 저장: `string _savedText` 는 `EditorDocumentState`, `DataInheritance::makeDelta` 는 `ResourceComposer::saveDelta` 가 맡는다.
   형식으로 못 막는 까닭: 칸 이름이고, `makeDelta` 의 정당한 사용처(카탈로그 · 머티리얼 · 오디오 · 에디터 임포트)가 엔진과 에디터 층에 걸쳐 friend 로 묶으면 엔진이 에디터를 안다.
5. id 를 버리는 리소스 입구: 이름이 `load…` 인 입구(`loadFromAsset` · `loadFromFile` · `loadFromPack` · `load` …)가 리소스 글(`AssetIO::readTextAsset`)을 읽은 뒤
   id 없이 글 입구로 넘기면 덮어쓰기 층 · 다시 읽기 기록 · 접속 대조를 받지 못한다(조용히 빠진다). id 를 싣는 길(`ResourceContext::withAsset` ·
   `XMLDocument::parseResourceText` · `XMLReadUtil::parseResourceRoot` · `loadResource…`)을 지나거나, 읽은 id 변수를 그대로 다음 함수에 넘겨야 한다.
   예전에는 이름 둘(`loadFromAsset` · `loadFromFile( const AssetID& )`)만 보아 다른 이름의 입구(타일맵 `load`)가 빠졌다 — 입구는 이름이 아니라 "load 로 시작하고 리소스 글을 읽는다" 로 고른다.
   형식으로 못 막는 까닭: 읽은 글과 id 가 같은 함수 안에서 이어지는지(자료 흐름)다.
6. 엔진 자식 손 명령: `ProcessCommand::at(` 은 `EngineChildProcess::makeCommand` 가 맡는다 — 자동화 실행의 자식에 `-unattended` 를 넘긴다.
   빠지면 넷 플레이 클라이언트가 사람의 실행으로 떠 사양으로 품질을 골라 서버와 합성이 갈렸다. 엔진이 아닌 도구(Tracy · tint · 편집기)는 예외 표에 까닭과 함께.
   형식으로 못 막는 까닭: 띄우는 실행 파일이 엔진인지는 실행 때의 값이고, 정당한 사용처(Core 테스트 실행기 · 엔진 · 에디터 도구)가 층에 걸쳐 Core 가 friend 로 알 수 없다.
7. 에디터의 리소스 트리 손 훑기: 에디터 코드(`/Editor/` 아래)의 `FileUtil::collectFiles(` · `collectFolders(` 는 `EditorContentScan` 이 맡는다 —
   다른 프로세스의 시나리오 사본을 뺀다. 빠지면 Quick Open 이 남의 사본을 집고 임포터가 남의 사본에 임포트했다. 리소스 트리 밖(Saved · Config · 소스) 폴더는
   예외 표에 `경로|첫 인자` 와 까닭으로 둔다. 쿠커 · 팩(`AssetPaths::collectContentFiles`)은 에디터 밖이라 보지 않는다(scratch 전체를 뺀다).
   형식으로 못 막는 까닭: 걸리는 것은 훑는 폴더(첫 인자)가 리소스 트리인지이고, 같은 함수로 트리 밖 폴더를 훑는 정당한 자리가 많다.
8. 이름 해시를 손으로 키로 쓰는 표: `hashed_string::getHash()`(32 비트) 결과를 맵 첨자 · `emplace` · `insert…` · `find` · `count` · `contains` · `tryClaim` 의
   키로 넘기거나 `…key` · `…Key` · `…_id` 변수에 넣는 모양. 같은 해시의 두 이름을 가리지 못해 나중 것이 조용히 이긴다(감사 C-8) — 저장 · 조회 키는
   `NameHashIndex`(공용 충돌 검사 색인)로 넣는다. 섞기 · 씨앗 · `LogOnce` 처럼 겹쳐도 데이터가 바뀌지 않는 자리는 예외 표에 까닭과 함께 둔다.
   형식으로 못 막는 까닭: `getHash()` 는 섞기 · 씨앗 · 직렬화가 쓰는 공개 함수이고, 걸리는 것은 그 값을 키로 쓰는지다.

9. 손으로 짝지은 상태 읽기 · 쓰기(옛 `state-shape`): `writeState( Archive& …` · `readState( Archive& …` 의 선언과 .cpp 정의는 `SW_DECLARE_STATE_IO()` 와
   `serializeState( StateIO& io )` 하나가 맡는다(2026-11-07 — 125 형식이 두 함수의 칸 순서를 사람이 맞췄다). 머리의 인라인 정의는 `StateIO` 입구로 넘길 때만 된다.
   형식으로 못 막는 까닭: 스냅샷(`ComponentStateStore`)은 `serializeState` 만 받지만, `Archive` 는 엔진 곳곳의 바이트 그릇이라 그것을 받는 멤버 짝을 새로 짓는 것은 막지 못한다.
10. 프레임 함수 안의 리터럴 이름(옛 `frame-intern`): 틱 · 갱신 · 칠하기 함수(`onTick` · `tick…` · `update…` · `paint…`) 안의 `hashed_string( "글자" )` 는 프레임마다
   글자 → FNV → 샤드 뮤텍스를 지난다 — 함수 안 `static const hashed_string` 이나 데이터 칸으로 옮긴다(`.cpp` 만 본다). 예외 열쇠는 `경로|함수`.
   형식으로 못 막는 까닭: 리터럴 암시적 생성은 정상 코드이고, 자리마다 한 번 만드는 형식(C++20 고정 문자열 템플릿 인자)은 표준이 C++17 이라 미뤘다(2026-11-12).

예외는 `Config/Rules/Lint/CommonShapes.json` 에 까닭과 함께 적는다. 낡은 예외(더는 없는 자리)도 위반이다.
`py -3 -m Scripts lint common-shapes [--self-check]` 로 돌리고, 커밋 훅(`rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.exit_code import ExitCode  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from lint.cpp_shape import is_target, strip_code  # noqa: E402
from lint.function_spans import find_closing_brace, find_function_spans, run_self_check as run_function_spans_self_check  # noqa: E402
from lint.lint_scope import LintScope, make_whole_scope, read_texts  # noqa: E402
from lint.rule_tables import get_table_path  # noqa: E402

CONFIG_PATH = get_table_path("common-shapes")
EXTENSIONS = (".h", ".cpp", ".inl")
SKIP_PARTS = ("ThirdParty", "Tests")
COMMENT_PATTERN = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)

SWITCH_PATTERN = re.compile(r"\bswitch\s*\([^\n]*\)\s*\{")
CASE_PATTERN = re.compile(r"\bcase\s+([\w:]+)\s*:\s*(?:\{\s*)?return\s+\"([^\"]*)\"")
ANY_CASE_PATTERN = re.compile(r"\bcase\s+[\w:]+\s*:")
DEFAULT_NULL_PATTERN = re.compile(r"\bdefault\s*:\s*return\s+nullptr")
FIRST_COMPONENT_PATTERN = re.compile(r"forEachComponentOfType<[^>]*>\([^\n]*\n\s*\{\s*\n\s*if \( pFound == nullptr \)\s*\n\s*pFound = ")
ONE_LINE_VALIDATOR_PATTERN = re.compile(r"expected<void>\s+(?:\w+::)?\w+\(\s*const AutomationStep& step\s*\)\s*\{\s*\w+ params;\s*return step\.readProperties\( params \);\s*\}")
SAVED_TEXT_PATTERN = re.compile(r"^\s*string\s+_savedText\s*;", re.M)
MAKE_DELTA_PATTERN = re.compile(r"\bDataInheritance::makeDelta\(")
READ_TEXT_ASSET_PATTERN = re.compile(r"\bAssetIO::readTextAsset\(\s*(\w+)?")
ENGINE_CHILD_PATTERN = re.compile(r"\bProcessCommand::at\(")
EDITOR_SCAN_PATTERN = re.compile(r"\bFileUtil::collect(?:Files|Folders)\(\s*([^,]+?)\s*,")
# 첨자는 getHash() 로 끝나는 안쪽만(`list[ i ]._id.getHash()` 의 앞 첨자는 아니다), 호출은 첫 인자 안의 getHash() 만 본다.
NAME_HASH_KEY_PATTERN = re.compile(r"\[\s*[^\[\]\n]*\bgetHash\(\s*\)\s*\]|\b(?:emplace|try_emplace|insert|insert_or_assign|find|count|contains|tryClaim)\(\s*[^;\n]*?\bgetHash\(\s*\)|\b\w*(?:[Kk]ey|_id)\s*=\s*[^;=\n]*\bgetHash\(\s*\)")
ASSET_ENTRANCE_NAME = re.compile(r"\w+::load\w*")   # 리소스 입구 이름(정의는 FunctionSpans 가 찾는다)
ASSET_CARRIER_PATTERN = re.compile(r"withAsset\(|parseResourceText\(|parseResourceRoot\(|loadResource\w*\(|loadFromAsset\(")
STATE_ENTRY_FILE = "Source/Engine/Serialization/Format/StateIO.h"   # 입구 자신
STATE_SIGNATURE = re.compile(r"\b(?:(\w+)\s*::\s*)?(writeState|readState)\s*\(\s*(?:sw::|::sw::)?Archive\s*&[^)]*\)\s*(?:const\s*)?(?:override\s*)?(?:=\s*0\s*)?([;{])")
STATE_ENTRY_CALL = re.compile(r"\bStateIO\s+\w+\s*\(|\bStateIO::(?:writeObject|readAtomic|readInPlace)\s*\(")
FRAME_FUNCTION_NAME = re.compile(r"(?:\w+::)+(onTick|tick\w*|update\w*|paint\w*)")   # 프레임 함수 정의 이름(정의는 FunctionSpans 가 찾는다)
LITERAL_NAME = re.compile(r"hashed_string\s*[({]\s*\"")


def find_same_spelling_switches(code: str) -> list[str]:
    """모든 case 가 열거자 철자를 돌려주는 switch 의 열거 이름을 돌려준다. `default: return nullptr`(거르기)는 이름 표가 아니라 보지 않는다."""
    list_enum: list[str] = []
    for match in SWITCH_PATTERN.finditer(code):
        body = code[match.end() - 1:find_closing_brace(code, match.end() - 1)]
        list_case = CASE_PATTERN.findall(body)
        if len(list_case) < 2 or len(list_case) != len(ANY_CASE_PATTERN.findall(body)) or DEFAULT_NULL_PATTERN.search(body):
            continue
        if all(case.split("::")[-1] == text for case, text in list_case):
            parts = list_case[0][0].split("::")
            list_enum.append(parts[-2] if len(parts) >= 2 else "?")
    return list_enum


def find_id_dropping_entrances(code: str) -> list[str]:
    """글을 읽은 뒤 id 를 싣지 않는 리소스 입구(`형식::함수`)를 돌려준다."""
    list_entrance: list[str] = []
    for span in find_function_spans(code):
        if ASSET_ENTRANCE_NAME.fullmatch(span.name) is None:
            continue
        body = code[span.open_index:span.close_index]
        read_match = READ_TEXT_ASSET_PATTERN.search(body)
        if read_match is None or ASSET_CARRIER_PATTERN.search(body, read_match.end()):
            continue
        id_name = read_match.group(1)
        passes_id = id_name is not None and id_name != "AssetID" and re.search(rf"[(,]\s*{re.escape(id_name)}\s*[,)]", body[read_match.end():]) is not None
        if not passes_id:
            list_entrance.append(span.name)
    return list_entrance


def find_state_pairs(text: str) -> list[tuple[int, str, str]]:
    """손으로 짝지은 `writeState` · `readState( Archive& )` 의 (줄, 함수, 설명). 글은 주석 · 문자열 · 전처리 줄을 지운 `strip_code` 로 본다."""
    list_found: list[tuple[int, str, str]] = []
    code = strip_code(text)
    for match in STATE_SIGNATURE.finditer(code):
        line = code.count("\n", 0, match.start()) + 1
        owner, name, end = match.group(1), match.group(2), match.group(3)
        if owner is not None or end == ";":
            list_found.append((line, name, f"손으로 짝지은 `{name}( Archive& )` — `SW_DECLARE_STATE_IO()` 와 `serializeState( StateIO& io )` 하나로 적는다"))
        elif STATE_ENTRY_CALL.search(code[match.end():match.end() + 200]) is None:
            list_found.append((line, name, f"인라인 `{name}( Archive& )` 는 `StateIO` 입구로만 넘긴다"))
    return list_found


def find_literal_names(code: str) -> list[tuple[str, str]]:
    """주석을 뺀 코드에서 프레임 함수 안 리터럴 이름의 (함수 이름, 줄) 을 돌려준다. `static` 줄은 한 번만 만들어지므로 뺀다."""
    list_found: list[tuple[str, str]] = []
    for span in find_function_spans(code):
        name_match = FRAME_FUNCTION_NAME.fullmatch(span.name)
        if name_match is None:
            continue
        for line in code[span.open_index + 1:span.close_index].split("\n"):
            if LITERAL_NAME.search(line) and "static" not in line:
                list_found.append((name_match.group(1), line.strip()))
    return list_found


def collect_violations(path: str, text: str) -> list[tuple[str, str, str]]:
    """글 하나의 (규칙, 예외 열쇠, 설명) 들을 돌려준다."""
    code = COMMENT_PATTERN.sub("", text)
    list_violation: list[tuple[str, str, str]] = []
    if not path.startswith("Source/Core/"):
        for enum_name in find_same_spelling_switches(code):
            list_violation.append(("enumNameSwitch", f"{path}|{enum_name}", f"철자 같은 열거 이름 표({enum_name}) — ENUM() + getEnumName"))
    if FIRST_COMPONENT_PATTERN.search(code):
        list_violation.append(("firstComponent", path, "첫 컴포넌트 손 찾기 — AutomationComponentWatch::findFirst"))
    if ONE_LINE_VALIDATOR_PATTERN.search(code):
        list_violation.append(("oneLineValidator", path, "스키마만 읽는 한 줄 검사 함수 — AutomationStep::validateProperties<TParams>"))
    if SAVED_TEXT_PATTERN.search(code):
        list_violation.append(("savedText", path, "저장 글 칸 — EditorDocumentState"))
    if MAKE_DELTA_PATTERN.search(code):
        list_violation.append(("makeDelta", path, "부모 차이 손 저장 — ResourceComposer::saveDelta"))
    for entrance in find_id_dropping_entrances(code):
        list_violation.append(("assetEntrance", f"{path}|{entrance}", f"id 를 버리는 리소스 입구({entrance}) — ResourceContext::withAsset · parseResourceText · parseResourceRoot"))
    if ENGINE_CHILD_PATTERN.search(code):
        list_violation.append(("engineChildCommand", path, "ProcessCommand::at 손 명령 — 엔진 자식은 EngineChildProcess::makeCommand"))
    if NAME_HASH_KEY_PATTERN.search(code):
        list_violation.append(("nameHashKey", path, "getHash() 를 손으로 키로 쓴다 — 저장 · 조회 키는 NameHashIndex(같은 해시의 다른 이름을 거절)"))
    if "/Editor/" in path:
        for argument in sorted(set(match.group(1) for match in EDITOR_SCAN_PATTERN.finditer(code))):
            list_violation.append(("editorContentScan", f"{path}|{argument}", f"FileUtil 손 훑기({argument}) — 리소스 트리는 EditorContentScan"))
    if is_target(path) and path != STATE_ENTRY_FILE:
        for line, name, message in find_state_pairs(text):
            list_violation.append(("statePair", f"{path}|{name}", f"{line}줄: {message}"))
    if path.endswith(".cpp"):
        for function, line in find_literal_names(code):
            list_violation.append(("frameIntern", f"{path}|{function}", f"프레임 함수({function})에서 리터럴로 이름을 만든다 — 함수 안 static const 로 옮긴다: {line}"))
    return list_violation


def find_problems(list_source: Iterable[tuple[str, str]], exceptions: dict, whole_repo: bool = True) -> list[str]:
    list_problem: list[str] = []
    seen_key: set[tuple[str, str]] = set()
    for path, text in list_source:
        for rule, key, message in collect_violations(path, text):
            seen_key.add(("_" + rule, key))
            if key not in exceptions.get("_" + rule, {}):
                list_problem.append(f"{path}: {message} (예외는 {CONFIG_PATH} 의 _{rule} 에 '{key}' 와 까닭)")
    for rule, table in exceptions.items() if whole_repo else ():   # 낡은 예외는 저장소 전체를 봐야 안다
        if rule == "_comment":
            continue
        for key in table:
            if (rule, key) not in seen_key:
                list_problem.append(f"{CONFIG_PATH}: 낡은 예외 {rule} '{key}' — 그 모양이 더는 없다, 지운다")
    return list_problem


def read_sources(project_root: Path, scope: LintScope) -> list[tuple[str, str]]:
    return read_texts(project_root, scope.select(("Source",), EXTENSIONS, SKIP_PARTS))


def check_common_shapes(project_root: Path, scope: LintScope) -> list[str]:
    exceptions = json.loads((project_root / CONFIG_PATH).read_text(encoding="utf-8"))
    return find_problems(read_sources(project_root, scope), exceptions, scope.whole_repo)


def run_self_check() -> None:
    run_function_spans_self_check()   # 공용 함수 범위 도우미(`경로|함수` 열쇠)도 함께 본다
    same = 'const utf8* f( Kind k ) { switch ( k )\n{\ncase Kind::Aa: return "Aa";\ncase Kind::Bb: return "Bb";\n}\n}'
    other = 'const utf8* f( Kind k ) { switch ( k )\n{\ncase Kind::Aa: return "aa";\ncase Kind::Bb: return "Bb";\n}\n}'
    filtered = 'const utf8* f( Kind k ) { switch ( k )\n{\ncase Kind::Aa: return "Aa";\ncase Kind::Bb: return "Bb";\ndefault: return nullptr;\n}\n}'
    assert find_same_spelling_switches(same) == ["Kind"]
    assert find_same_spelling_switches(other) == []
    assert find_same_spelling_switches(filtered) == []
    assert len(find_problems([("Source/A/a.cpp", same)], {})) == 1
    assert find_problems([("Source/Core/a.cpp", same)], {}) == []
    assert find_problems([("Source/A/a.cpp", same)], {"_enumNameSwitch": {"Source/A/a.cpp|Kind": "이유"}}) == []
    stale = find_problems([("Source/A/a.cpp", "int x;")], {"_enumNameSwitch": {"Source/A/a.cpp|Kind": "이유"}})
    assert len(stale) == 1 and "낡은" in stale[0], stale
    validator = "static expected<void> validateX( const AutomationStep& step )\n{\n    MyParams params;\n    return step.readProperties( params );\n}"
    assert [v[0] for v in collect_violations("Source/A/a.cpp", validator)] == ["oneLineValidator"]
    dropping = "bool A::loadFromAsset( const AssetID& assetID )\n{\n    string text;\n    if ( AssetIO::readTextAsset( assetID, text ) == false )\n        return false;\n    return loadFromXMLText( text, assetID.view() );\n}"
    carrying = dropping.replace("loadFromXMLText( text, assetID.view() )", "loadComposed( text, assetID.view(), ResourceContext::forType( *StaticType() ).withAsset( assetID ) )")
    assert find_id_dropping_entrances(dropping) == ["A::loadFromAsset"], find_id_dropping_entrances(dropping)
    assert find_id_dropping_entrances(carrying) == []
    other_name = "bool B::loadFromPack( const DataPath& root )\n{\n    string text;\n    if ( AssetIO::readTextAsset( AssetID{ path }, text ) == false )\n        return false;\n    return loadFromXMLText( text, path );\n}"
    assert find_id_dropping_entrances(other_name) == ["B::loadFromPack"], "이름이 loadFromAsset 이 아니어도 입구다"
    passing = "bool C::loadDesc( const AssetID& assetID )\n{\n    string text;\n    if ( AssetIO::readTextAsset( assetID, text ) == false )\n        return false;\n    return parseDesc( assetID.view(), assetID, text );\n}"
    assert find_id_dropping_entrances(passing) == [], "읽은 id 를 그대로 넘기면 싣는 것이다"
    calling = "void D::f()\n{\n    if ( E::loadThing( x ) )\n    {\n        AssetIO::readTextAsset( AssetID{ p }, t );\n    }\n}"
    assert find_id_dropping_entrances(calling) == [], "호출은 입구가 아니다"
    child = "const ProcessCommand command = ProcessCommand::at( *executable, std::move( listArgument ) );"
    assert [v[0] for v in collect_violations("Source/Editor/a.cpp", child)] == ["engineChildCommand"]
    assert collect_violations("Source/Editor/a.cpp", "EngineChildProcess::makeCommand( parent, path, {} );") == []
    scan = "(void)FileUtil::collectFiles( resourceRoot, \"\", listFile, true );"
    assert [v[1] for v in collect_violations("Source/Editor/a.cpp", scan)] == ["Source/Editor/a.cpp|resourceRoot"]
    assert collect_violations("Source/Engine/a.cpp", scan) == [], "에디터 밖(쿠커 · 엔진)은 보지 않는다"
    assert collect_violations("Source/Editor/a.cpp", "(void)EditorContentScan::collectFiles( root, root, \"\", listFile, true );") == []
    assert [v[0] for v in collect_violations("Source/A/a.cpp", "_mapByHash[name.getHash()] = value;")] == ["nameHashKey"]
    assert [v[0] for v in collect_violations("Source/A/a.cpp", "map.emplace( name.getHash(), value );")] == ["nameHashKey"]
    assert [v[0] for v in collect_violations("Source/A/a.cpp", "const uint64 key = name.getHash();")] == ["nameHashKey"]
    assert collect_violations("Source/A/a.cpp", "if ( list[ index ]._id.getHash() == other ) {}") == [], "앞 첨자는 키가 아니다"
    assert collect_violations("Source/A/a.cpp", "(void)index.assign( name.view(), name.getHash() );") == [], "공용 색인으로 넣는 것은 걸리지 않는다"
    # 손으로 짝지은 상태 읽기 · 쓰기(옛 state-shape)
    assert len(find_state_pairs("void A::writeState( Archive& outArchive ) const\n{\n}\n")) == 1
    assert len(find_state_pairs("bool A::readState( sw::Archive& archive )\n{\n}\n")) == 1
    assert len(find_state_pairs("void writeState( Archive& outArchive ) const;\n")) == 1
    assert len(find_state_pairs("void writeState( Archive& outArchive ) const { outArchive << _x; }\n")) == 1
    assert find_state_pairs("void writeState( Archive& outArchive ) const { StateIO::writeObject( *this, outArchive ); }\n") == []
    assert find_state_pairs("bool readState( Archive& archive, const C& c )\n{\n    StateIO io( archive );\n    return io.isOk();\n}\n") == []
    assert find_state_pairs("void A::serializeState( StateIO& io )\n{\n}\nSW_DECLARE_STATE_IO();\n") == []
    assert find_state_pairs("// void writeState( Archive& outArchive ) const;\n") == []
    assert find_state_pairs("void writeState( BitWriter& writer ) const;\n") == []
    pair = "void A::writeState( Archive& outArchive ) const\n{\n}\n"
    assert [v[1] for v in collect_violations("Source/A/a.cpp", pair)] == ["Source/A/a.cpp|writeState"]
    assert collect_violations(STATE_ENTRY_FILE, pair) == [] and collect_violations("Source/ThirdParty/x/a.cpp", pair) == [], "입구 자신 · 남의 코드는 보지 않는다"
    # 프레임 함수 안의 리터럴 이름(옛 frame-intern)
    tick = 'void Foo::onTick( float32 deltaTime )\n{\n    use( hashed_string( "A" ) );\n}\n'
    assert [v[1] for v in collect_violations("Source/A/a.cpp", tick)] == ["Source/A/a.cpp|onTick"], "틱 안의 리터럴은 잡아야 한다"
    assert find_problems([("Source/A/a.cpp", tick)], {"_frameIntern": {"Source/A/a.cpp|onTick": "까닭"}}) == [], "예외는 통과해야 한다"
    assert collect_violations("Source/A/a.h", tick) == [], "머리는 보지 않는다"
    clean = 'void Foo::onTick( float32 deltaTime )\n{\n    static const hashed_string s_name( "A" );\n    use( s_name );\n}\nvoid Foo::load()\n{\n    use( hashed_string( "B" ) );\n}\n'
    assert collect_violations("Source/A/a.cpp", clean) == [], "static 과 프레임 밖 함수는 통과해야 한다"
    assert collect_violations("Source/A/a.cpp", 'void Foo::updateView()\n{\n    // hashed_string( "A" )\n}\n') == [], "주석은 통과해야 한다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="공통 기반이 있는데 손으로 다시 적은 모양을 본다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        run_self_check()
        print("[CommonShapes] self-check OK")
        return ExitCode.PASSED
    list_problem = check_common_shapes(get_project_root(), make_whole_scope(get_project_root()))
    for problem in list_problem:
        print(f"  - {problem}")
    print(f"[CommonShapes] {'FAIL' if list_problem else 'OK'} ({len(list_problem)}건)")
    return ExitCode.VIOLATION if list_problem else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
