"""
Scripts/lint/feature_references.py

만들어 두고 아무 데도 싣지 않은 기능을 잡는다 — 리플렉션 컴포넌트 · 감시 값 · 개발 명령마다 콘텐츠 · 시나리오 · 코드의 참조를 세어 0 이면 위반이다.
**왜 린트인가.** "쓰는 곳이 있다" 는 형식이 말할 수 없다(컴포넌트는 데이터가 이름으로 부른다). 그래서 이 틈만 글로 본다. 근본 원인은 기능을 "코드 + 단위 테스트" 까지만 만들고
사용처를 완료 조건에 두지 않은 것이다.

- 컴포넌트: `REFLECT` 가 붙은 `*Component`(폴더 `Tests/` 제외). 참조 = `Content` 글 파일의 그 이름, 코드(`Tests/` 제외)의 `addComponent<이름>`, 그 이름을 기반으로 삼은 클래스.
- 감시 값: `SW_AUTOMATION_*WATCH`. 참조 = 시나리오의 `watch="이름"`, 등록 줄 밖 코드 · 스크립트의 `"이름"` 글자.
- 개발 명령: `SW_DEV_COMMAND`. 참조 = 시나리오 `line="…"` 의 낱말, 등록 줄 밖 코드 · 스크립트 글자의 첫 낱말.
- 등록 없는 명령 함수: 명령 모양(`static bool f( const vector<string>&, string& )`)의 함수가 그 파일에서 정의 말고 한 번도 불리지 않으면 위반이다 —
  `*DevCommands.cpp` 를 기능 파일로 합칠 때(165e16647c) `SW_DEV_COMMAND` 줄만 빠져 치트 명령 아홉이 함수만 남았는데, 익명 구조체의 static 멤버는 컴파일러가 경고하지 않는다.
- 낡은 기대 로그 id(방향이 거꾸로 — 참조가 정의를 요구한다): `SW_EXPECT_LOG( "X" )` · 시나리오 `<ExpectLog id="X">` · `<AllowLog id="X">` 의 X 가 정의에 없으면 위반이다 —
  id 를 바꾸거나 지우면 기대가 조용히 무의미해진다(`docs/agents/Testing.md` 시나리오). 정의 = `Tests/` 밖 C++ 의 id 모양(`영역.사건`) 글자 리터럴 전부(주석 · `SW_EXPECT_LOG` 인자 제외),
  `Tests/` 안에서는 `SW_*_ID( "X"` 의 첫 인자. 로그 매크로만 보지 않는 까닭: id 는 `addErrorWithID` · `report` · `parseBoolToken` · `#define SW_LOG_ID_…` 같은 도우미를 거쳐
  매크로에 닿아(`"" id` 이어 붙이기) 매크로 자리에는 이름만 남는다. 실행 중에 이어 붙인 id 는 리터럴이 없으니 예외 표(`_logIDs`)에 까닭과 함께 둔다.

예외는 데이터 한 곳(`Config/Rules/Lint/FeatureRefs.json`, 칸마다 이름 → 까닭)에 둔다. 까닭이 비었거나, 이제 참조가 생겼거나, 정의가 사라진 예외도 위반이다 — 표 형식(`rule_tables.ExceptionTable`)이 판정이 묻지 않은 줄을 낡은 예외로 낸다.

`py -3 -m Scripts lint feature-refs [--self-check]` 로 돌리고, 커밋 훅이 돌린다.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Mapping

from lint.index_cache import open_facts
from lint.lint_rule import LintRule
from lint.lint_scope import LintScope, list_repo_files
from lint.rule_tables import ExceptionTable

RULE_ID = "feature-refs"
SECTIONS = ("_folders", "_components", "_watches", "_devCommands", "_logIDs")
CPP_EXTENSIONS = (".h", ".cpp", ".inl", ".xxx")
CONTENT_EXTENSIONS = (".xml", ".json")
SCRIPT_EXTENSIONS = (".py", ".cmake", ".sh", ".txt")

CLASS_PATTERN = re.compile(r'^\s*class\s+(?:SW_\w+\s+)?(\w+Component)\b[^;{]*?:\s*(?:public|private|protected)\s+([\w:]+)', re.M)
BASE_PATTERN = re.compile(r'^\s*(?:class|struct)\s+(?:SW_\w+\s+)?\w+\b[^;{(]*?:\s*(?:public|private|protected)\s+(?:\w+::)*(\w+Component)\b', re.M)
ADD_COMPONENT_PATTERN = re.compile(r'addComponent<\s*(?:\w+::)*(\w+Component)\s*>')
WATCH_PATTERN = re.compile(r'SW_AUTOMATION_(?:WALL_|COMPONENT_|GETTER_)?WATCH\(\s*\w+\s*,\s*"([\w.]+)"[^;]*?\)\s*;?', re.S)
COMMAND_PATTERN = re.compile(r'SW_DEV_COMMAND\(\s*\w+\s*,\s*"([\w.]+)".*?\)\s*;', re.S)
COMMAND_RUNNER_PATTERN = re.compile(r'static\s+bool\s+(\w+)\(\s*const\s+vector<string>&\s*\w+\s*,\s*string&\s*\w+\s*\)')
WATCH_USE_PATTERN = re.compile(r'\bwatch="([^"]+)"')
COMMAND_LINE_PATTERN = re.compile(r'\bline="([^"]+)"')
WORD_PATTERN = re.compile(r'[A-Za-z_]\w*')
LITERAL_PATTERN = re.compile(r'"([A-Za-z][\w.]*)(?:[ "])')
REFLECT_LOOK_BACK_LINES = 6
CPP_TOKEN_PATTERN = re.compile(r'//[^\n]*|/\*.*?\*/|R"(\w*)\((?:.*?)\)\1"|\'(?:[^\'\\\n]|\\.)+\'|"((?:[^"\\\n]|\\.)*)"', re.S)   # 주석 · 원시 글자 · 문자를 먼저 먹어 그 안의 따옴표에 어긋나지 않는다
LOG_ID_SHAPE = re.compile(r'[A-Za-z]\w*(?:\.\w+)+')
LOG_ID_CALL_PATTERN = re.compile(r'\bSW_\w+_ID\(\s*"([^"]*)"')
EXPECT_LOG_PATTERN = re.compile(r'\bSW_EXPECT_LOG\(\s*"([^"]*)"\s*\)')
SCENARIO_LOG_PATTERN = re.compile(r'<(?:ExpectLog|AllowLog)\b[^>]*?\bid="([^"]*)"')


@dataclass
class Sources:
    """훑을 글 묶음 — 시험이 파일 없이 넣을 수 있게 내용으로 든다."""

    dict_header: dict[str, str] = field(default_factory=dict)      # 경로 → 글(`Tests/` 밖 C++)
    dict_test_code: dict[str, str] = field(default_factory=dict)    # 경로 → 글(`Tests/` 안 C++)
    dict_content: dict[str, str] = field(default_factory=dict)     # 경로 → 글(Content 글 파일)
    dict_script: dict[str, str] = field(default_factory=dict)      # 경로 → 글(Scripts · IntegrationTests · CMake)


def classify_path(relative: str) -> str | None:
    """파일이 드는 묶음(`Sources` 의 칸 이름) — 훑지 않는 파일은 None."""
    suffix = Path(relative).suffix.lower()
    if relative.startswith("Source/") and suffix in CPP_EXTENSIONS:
        return "dict_test_code" if "/Tests/" in relative else "dict_header"
    if relative.startswith("Content/") and suffix in CONTENT_EXTENSIONS:
        return "dict_content"
    if relative.startswith(("Scripts/", "IntegrationTests/")) and suffix in SCRIPT_EXTENSIONS and relative != "Scripts/lint/feature_references.py":
        return "dict_script"
    return None


def strip_registrations(text: str) -> str:
    for pattern in (WATCH_PATTERN, COMMAND_PATTERN):
        text = pattern.sub("", text)
    return text


def extract_facts(bucket: str, path: str, text: str) -> dict[str, list[str]]:
    """파일 하나의 사실 — 정의(컴포넌트 · 감시 값 · 명령), 참조(컴포넌트 이름 · 글자 첫 낱말 · 시나리오 낱말), 등록 없는 명령 함수. 색인(`index_cache`)에 둔다."""
    facts: dict[str, list[str]] = {}
    if bucket == "dict_header":
        if path.endswith(".h"):
            facts["component"] = [match.group(1) for match in CLASS_PATTERN.finditer(text)
                                  if any("REFLECT(" in line for line in text[:match.start()].splitlines()[-REFLECT_LOOK_BACK_LINES:])]
        if not (path.endswith("AutomationWatch.h") or path.endswith("AutomationComponentWatch.h")):   # 매크로 정의 자체
            facts["watch"] = [match.group(1) for match in WATCH_PATTERN.finditer(text)]
            facts["command"] = [match.group(1) for match in COMMAND_PATTERN.finditer(text)]
        facts["componentReference"] = ADD_COMPONENT_PATTERN.findall(text) + BASE_PATTERN.findall(text)
        facts["lostRunner"] = [match.group(1) for match in COMMAND_RUNNER_PATTERN.finditer(text) if len(re.findall(rf"\b{match.group(1)}\b", text)) < 2]
        facts["logID"] = sorted({match.group(2) for match in CPP_TOKEN_PATTERN.finditer(EXPECT_LOG_PATTERN.sub("", text))
                                 if match.group(2) is not None and LOG_ID_SHAPE.fullmatch(match.group(2))})
    if bucket == "dict_test_code":
        facts["logID"] = LOG_ID_CALL_PATTERN.findall(text)   # 테스트가 스스로 내는 로그만 — 테스트 글자 표는 정의가 아니다
        facts["logReference"] = EXPECT_LOG_PATTERN.findall(text)
    if bucket in ("dict_header", "dict_test_code", "dict_script"):
        facts["literal"] = sorted(set(LITERAL_PATTERN.findall(strip_registrations(text))))
    if bucket == "dict_content":
        facts["componentReference"] = sorted({word for word in WORD_PATTERN.findall(text) if word.endswith("Component")})
        if path.endswith(".scenario.xml"):
            facts["scenarioWatch"] = WATCH_USE_PATTERN.findall(text)
            facts["commandWord"] = [word for line in COMMAND_LINE_PATTERN.findall(text) for word in line.split()]
            facts["logReference"] = SCENARIO_LOG_PATTERN.findall(text)
    return facts


DEFINE_KEYS = ("component", "watch", "command", "logReference")   # 짝이 있어야 하는 이름(경로와 함께) — 로그 id 는 참조 쪽이 정의를 요구한다
COLLECT_KEYS = ("componentReference", "literal", "scenarioWatch", "commandWord", "logID")


def fold_tokens(path: str, facts: dict[str, list[str]]) -> list[str]:
    """파일 하나의 사실을 판정이 쓰는 토큰으로 — 정의(이름 · 경로), 참조 낱말(경로 없이), 등록 없는 명령 함수(경로 · 차례). 요약은 이 토큰의 수다."""
    list_token = [f"D	{key}	{name}	{path}" for key in DEFINE_KEYS for name in facts.get(key, ())]   # 탭으로 가른다 — 이름 · 경로에 탭은 없다
    list_token += [f"C	{key}	{name}" for key in COLLECT_KEYS for name in sorted(set(facts.get(key, ())))]
    list_token += [f"L	{path}	{index}	{name}" for index, name in enumerate(facts.get("lostRunner", ()))]
    return list_token


def evaluate_facts(map_fact: dict[str, dict[str, list[str]]], table: ExceptionTable) -> list[str]:
    """저장소 전체 사실(경로 → 사실, 경로 순)로 참조 0 을 가린다 — 자체 검사의 입구. 커밋 훅은 접은 요약(`evaluate_summary`)을 쓴다."""
    return evaluate_summary([token for path, facts in sorted(map_fact.items()) for token in fold_tokens(path, facts)], table)


def evaluate_summary(list_token: Iterable[str], table: ExceptionTable) -> list[str]:
    """요약 토큰으로 참조 0 을 가린다 — 참조는 어느 파일에서 와도 되므로 판정은 늘 전체(의 요약)로 낸다."""
    map_define: dict[str, dict[str, str]] = {key: {} for key in DEFINE_KEYS}
    map_collect: dict[str, set[str]] = {key: set() for key in COLLECT_KEYS}
    list_lost: list[tuple[str, int, str]] = []
    for token in list_token:
        row = token.split("	")
        if row[0] == "D":
            _, key, name, path = row
            map_define[key][name] = max(path, map_define[key].get(name, path))   # 같은 이름은 경로 순으로 뒤 파일이 이긴다
        elif row[0] == "C":
            map_collect[row[1]].add(row[2])
        else:
            list_lost.append((row[1], int(row[2]), row[3]))

    def collect(key: str) -> set[str]:
        return map_collect[key]

    def define(key: str) -> dict[str, str]:
        return map_define[key]

    list_problem: list[str] = []
    dict_component = define("component")
    set_component_reference = collect("componentReference")
    for name, path in sorted(dict_component.items()):
        if name in set_component_reference or table.allows("_components", name) or any(path.startswith(folder + "/") for folder in table.get("_folders")):
            continue
        list_problem.append(f"{path}: 컴포넌트 {name} 를 콘텐츠도 코드도 쓰지 않는다 — 배치하거나 {table.hint('_components')}")

    dict_watch = define("watch")
    dict_command = define("command")
    set_literal = collect("literal")
    set_watch_reference = collect("scenarioWatch") | set_literal
    set_command_reference = collect("commandWord") | set_literal
    for name, path in sorted(dict_watch.items()):
        if name not in set_watch_reference and not table.allows("_watches", name):
            list_problem.append(f"{path}: 감시 값 {name} 를 아무도 읽지 않는다 — 판정에 쓰거나 지우거나 {table.hint('_watches')}")
    for name, path in sorted(dict_command.items()):
        if name not in set_command_reference and not table.allows("_devCommands", name):
            list_problem.append(f"{path}: 개발 명령 {name} 를 자동화가 부르지 않는다 — 시나리오로 잇거나 {table.hint('_devCommands')}")
    dict_log_reference = define("logReference")
    set_log_id = collect("logID")
    for name, path in sorted(dict_log_reference.items()):
        if name not in set_log_id and not table.allows("_logIDs", name):
            list_problem.append(f"{path}: 기대 로그 id {name} 를 내는 코드가 없다(바꿨거나 지웠다) — 지금 id 로 고치거나 기대를 지우거나 {table.hint('_logIDs')}")
    for path, _, name in sorted(list_lost):
        list_problem.append(f"{path}: 명령 함수 {name} 를 등록(SW_DEV_COMMAND)도 호출도 하지 않는다 — 등록 줄을 되살리거나 함수를 지운다")
    # 참조는 어느 파일에서 와도 되므로 판정은 늘 전체 요약이다 — 낡은 줄(이제 참조 · 정의가 생겼거나 이름이 사라진 줄)도 늘 센다. 폴더 줄은 예전처럼 세지 않는다.
    return list_problem + table.list_problems(whole_repo=True, stale_sections=("_components", "_watches", "_devCommands", "_logIDs"))


def find_problems(sources: Sources, table: ExceptionTable) -> list[str]:
    map_fact = {path: extract_facts(bucket, path, text) for bucket in ("dict_header", "dict_test_code", "dict_content", "dict_script")
               for path, text in getattr(sources, bucket).items()}
    return evaluate_facts(map_fact, table)


def make_table(**sections: dict[str, str]) -> ExceptionTable:
    """판정 테스트의 표 — 빠진 칸은 빈 칸이다."""
    return ExceptionTable.make(RULE_ID, **{**{section: {} for section in SECTIONS}, **sections})


def read_summary(project_root: Path) -> list[str]:
    """저장소에 있는 파일만 본다(git 이 아는 파일 — 빌드 산출물 · 임포트 산출물을 줍지 않는다). 내용이 바뀐 파일만 다시 읽고(`index_cache`), 판정은 접은 토큰만 읽는다."""
    list_path = [path for path in list_repo_files(project_root) if classify_path(path) is not None]
    return list(open_facts(project_root, "feature-refs", [__file__], list_path, lambda path, text: extract_facts(classify_path(path), path, text), errors="ignore",
                          contribute=fold_tokens).summary)


class FeatureRefsRule(LintRule):
    id = "feature-refs"
    group = "참조"
    blocks = "참조 0 인 컴포넌트 · 감시 값 · 개발 명령, 정의 없는 기대 로그 id"
    why_not_type = "쓰는 곳 · 내는 곳이 있는지는 형식이 말하지 못한다"
    tables = ("",)

    def evaluate(self, scope: LintScope, found: Mapping[str, object]) -> list[str]:
        """커밋 훅은 훑는 파일이 바뀌었을 때만 돈다(지운 파일 포함 — 참조를 지우면 참조 0 이 생긴다)."""
        if not scope.whole_repo and not any(classify_path(path) for path in (*scope.list_file, *scope.list_removed)):
            return []
        return evaluate_summary(read_summary(self.root), self.load_exceptions(*SECTIONS))

    def self_check(self) -> None:
        """규칙 자체의 시험 — 저장소를 읽지 않는다."""
        sources = Sources()
        sources.dict_header["Source/A/Used.h"] = 'REFLECT( Category = "A" )\nclass SW_API UsedComponent : public Component\n{\n};\n'
        sources.dict_header["Source/A/Lonely.h"] = 'REFLECT()\nclass SW_API LonelyComponent : public Component\n{\n};\n'
        sources.dict_header["Source/A/Base.h"] = 'REFLECT()\nclass SW_API BaseThingComponent : public Component\n{\n};\nclass DerivedOne : public BaseThingComponent\n{\n};\n'
        sources.dict_header["Source/A/Plain.h"] = 'class SW_API PlainComponent : public Component\n{\n};\n'
        sources.dict_header["Source/A/Coded.h"] = 'REFLECT()\nclass CodedComponent : public Component\n{\n};\n'
        sources.dict_header["Source/A/Spawner.cpp"] = 'void f( GameObject& o ) { o.addComponent<CodedComponent>(); }\n'
        sources.dict_header["Source/A/W.cpp"] = ('SW_AUTOMATION_WATCH( a, "A.Read", "tip", &read );\nSW_AUTOMATION_WALL_WATCH( b,\n "A.Unread", "tip", &read );\n'
                                                'SW_DEV_COMMAND( c, "cmd.used", "cmd.used", "tip", &run );\nSW_DEV_COMMAND( d, "cmd.alone", "cmd.alone <x>",\n "tip", &run );\n'
                                                'SW_DEV_COMMAND( e, "cmd.sent", "cmd.sent", "tip", &run );\nvoid g() { send( "cmd.sent 1" ); }\n')
        sources.dict_header["Source/A/Cmd.cpp"] = ('struct I { static bool runLost( const vector<string>& listArgument, string& outReply ) { return true; }\n'
                                                  ' static bool runKept( const vector<string>& listArgument, string& outReply ) { return true; } };\n'
                                                  'SW_DEV_COMMAND( k, "cmd.kept", "cmd.kept", "tip", &I::runKept );\n')
        sources.dict_content["Content/x.scene.xml"] = "<UsedComponent/>\n"
        sources.dict_content["Content/x.scenario.xml"] = '<Expect watch="A.Read" equals="1"/>\n<DevCommand line="netplay.command clients cmd.used 3"/>\n<DevCommand line="cmd.kept"/>\n'

        empty = make_table()
        list_problem = find_problems(sources, empty)
        assert len(list_problem) == 4, list_problem
        assert any("runLost" in line for line in list_problem)
        assert any("LonelyComponent" in line for line in list_problem)
        assert any("A.Unread" in line for line in list_problem)
        assert any("cmd.alone" in line for line in list_problem)

        # 예외: 까닭과 함께면 받고, 낡은 줄은 위반이다.
        assert [line for line in find_problems(sources, make_table(_folders={"Source/A": "x"}, _watches={"A.Unread": "x"}, _devCommands={"cmd.alone": "x"})) if "runLost" not in line] == []
        list_stale = find_problems(sources, make_table(_components={"LonelyComponent": "", "UsedComponent": "x", "GoneComponent": "x"}, _watches={"A.Unread": "x"}, _devCommands={"cmd.alone": "x"}))
        assert sum("까닭이 비었다" in line for line in list_stale) == 1, list_stale
        assert sorted(line.split("'")[1] for line in list_stale if "낡은 예외" in line) == ["GoneComponent", "UsedComponent"], list_stale   # 이제 참조가 있다 · 정의가 없다
