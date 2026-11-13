#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/feature_references.py

만들어 두고 아무 데도 싣지 않은 기능을 잡는다 — 리플렉션 컴포넌트 · 감시 값 · 개발 명령마다 콘텐츠 · 시나리오 · 코드의 참조를 세어 0 이면 위반이다.
**왜 린트인가.** "쓰는 곳이 있다" 는 형식이 말할 수 없다(컴포넌트는 데이터가 이름으로 부른다). 그래서 이 틈만 글로 본다. 근본 원인은 기능을 "코드 + 단위 테스트" 까지만 만들고
사용처를 완료 조건에 두지 않은 것이다(`docs/NEXT.md`).

- 컴포넌트: `REFLECT` 가 붙은 `*Component`(폴더 `Tests/` 제외). 참조 = `Content` 글 파일의 그 이름, 코드(`Tests/` 제외)의 `addComponent<이름>`, 그 이름을 기반으로 삼은 클래스.
- 감시 값: `SW_AUTOMATION_*WATCH`. 참조 = 시나리오의 `watch="이름"`, 등록 줄 밖 코드 · 스크립트의 `"이름"` 글자.
- 개발 명령: `SW_DEV_COMMAND`. 참조 = 시나리오 `line="…"` 의 낱말, 등록 줄 밖 코드 · 스크립트 글자의 첫 낱말.
- 등록 없는 명령 함수: 명령 모양(`static bool f( const vector<string>&, string& )`)의 함수가 그 파일에서 정의 말고 한 번도 불리지 않으면 위반이다 —
  `*DevCommands.cpp` 를 기능 파일로 합칠 때(165e16647c) `SW_DEV_COMMAND` 줄만 빠져 치트 명령 아홉이 함수만 남았는데, 익명 구조체의 static 멤버는 컴파일러가 경고하지 않는다.

예외는 데이터 한 곳(`Config/Rules/Lint/FeatureReferenceExceptions.json`)에 까닭과 함께 둔다. 까닭이 비었거나, 이제 참조가 생겼거나, 정의가 사라진 예외도 위반이다 — 표가 낡지 않게.

`py -3 -m Scripts lint feature-refs [--self-check]` 로 돌리고, 커밋 훅(`rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.exit_code import ExitCode  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from lint.index_cache import open_facts  # noqa: E402
from lint.lint_scope import LintScope, list_repo_files, make_whole_scope  # noqa: E402

CONFIG_PATH = "Config/Rules/Lint/FeatureReferenceExceptions.json"
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


@dataclass
class Sources:
    """훑을 글 묶음 — 시험이 파일 없이 넣을 수 있게 내용으로 든다."""

    dict_header: dict[str, str] = field(default_factory=dict)      # 경로 → 글(`Tests/` 밖 C++)
    dict_test_code: dict[str, str] = field(default_factory=dict)    # 경로 → 글(`Tests/` 안 C++)
    dict_content: dict[str, str] = field(default_factory=dict)     # 경로 → 글(Content 글 파일)
    dict_script: dict[str, str] = field(default_factory=dict)      # 경로 → 글(Scripts · Test · CMake)


@dataclass(frozen=True)
class Exceptions:
    """`Config/Rules/Lint/FeatureReferenceExceptions.json` 의 내용."""

    dict_folder: dict[str, str]        # 폴더 → 까닭(그 안의 컴포넌트 모두)
    dict_component: dict[str, str]
    dict_watch: dict[str, str]
    dict_command: dict[str, str]


def classify_path(relative: str) -> str | None:
    """파일이 드는 묶음(`Sources` 의 칸 이름) — 훑지 않는 파일은 None."""
    suffix = Path(relative).suffix.lower()
    if relative.startswith("Source/") and suffix in CPP_EXTENSIONS:
        return "dict_test_code" if "/Tests/" in relative else "dict_header"
    if relative.startswith("Content/") and suffix in CONTENT_EXTENSIONS:
        return "dict_content"
    if relative.startswith(("Scripts/", "Test/")) and suffix in SCRIPT_EXTENSIONS and relative != "Scripts/lint/feature_references.py":
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
    if bucket in ("dict_header", "dict_test_code", "dict_script"):
        facts["literal"] = sorted(set(LITERAL_PATTERN.findall(strip_registrations(text))))
    if bucket == "dict_content":
        facts["componentReference"] = sorted({word for word in WORD_PATTERN.findall(text) if word.endswith("Component")})
        if path.endswith(".scenario.xml"):
            facts["scenarioWatch"] = WATCH_USE_PATTERN.findall(text)
            facts["commandWord"] = [word for line in COMMAND_LINE_PATTERN.findall(text) for word in line.split()]
    return facts


def check_exceptions_table(kind: str, dict_defined: dict[str, str], set_reference: set[str], dict_exception: dict[str, str]) -> list[str]:
    """표의 줄이 살아 있는지 — 까닭이 있고, 정의가 있고, 아직 참조가 없어야 한다."""
    list_problem: list[str] = []
    for name, reason in dict_exception.items():
        if not reason.strip():
            list_problem.append(f"{CONFIG_PATH}: {kind} '{name}' 의 까닭이 비었다")
        elif name not in dict_defined:
            list_problem.append(f"{CONFIG_PATH}: {kind} '{name}' 의 정의가 없다 — 줄을 지운다")
        elif name in set_reference:
            list_problem.append(f"{CONFIG_PATH}: {kind} '{name}' 는 이제 참조가 있다 — 줄을 지운다")
    return list_problem


DEFINE_KEYS = ("component", "watch", "command")
COLLECT_KEYS = ("componentReference", "literal", "scenarioWatch", "commandWord")


def fold_tokens(path: str, facts: dict[str, list[str]]) -> list[str]:
    """파일 하나의 사실을 판정이 쓰는 토큰으로 — 정의(이름 · 경로), 참조 낱말(경로 없이), 등록 없는 명령 함수(경로 · 차례). 요약은 이 토큰의 수다."""
    list_token = [f"D	{key}	{name}	{path}" for key in DEFINE_KEYS for name in facts.get(key, ())]   # 탭으로 가른다 — 이름 · 경로에 탭은 없다
    list_token += [f"C	{key}	{name}" for key in COLLECT_KEYS for name in sorted(set(facts.get(key, ())))]
    list_token += [f"L	{path}	{index}	{name}" for index, name in enumerate(facts.get("lostRunner", ()))]
    return list_token


def evaluate_facts(map_fact: dict[str, dict[str, list[str]]], exceptions: Exceptions) -> list[str]:
    """저장소 전체 사실(경로 → 사실, 경로 순)로 참조 0 을 가린다 — 자체 검사의 입구. 커밋 훅은 접은 요약(`evaluate_summary`)을 쓴다."""
    return evaluate_summary([token for path, facts in sorted(map_fact.items()) for token in fold_tokens(path, facts)], exceptions)


def evaluate_summary(list_token: Iterable[str], exceptions: Exceptions) -> list[str]:
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
        folder_excepted = any(path.startswith(folder + "/") for folder in exceptions.dict_folder)
        if name not in set_component_reference and not folder_excepted and name not in exceptions.dict_component:
            list_problem.append(f"{path}: 컴포넌트 {name} 를 콘텐츠도 코드도 쓰지 않는다 — 배치하거나 {CONFIG_PATH} 에 까닭을 적는다")
    list_problem += check_exceptions_table("컴포넌트", dict_component, set_component_reference, exceptions.dict_component)

    dict_watch = define("watch")
    dict_command = define("command")
    set_literal = collect("literal")
    set_watch_reference = collect("scenarioWatch") | set_literal
    set_command_reference = collect("commandWord") | set_literal
    for name, path in sorted(dict_watch.items()):
        if name not in set_watch_reference and name not in exceptions.dict_watch:
            list_problem.append(f"{path}: 감시 값 {name} 를 아무도 읽지 않는다 — 판정에 쓰거나 지우거나 {CONFIG_PATH} 에 까닭을 적는다")
    list_problem += check_exceptions_table("감시 값", dict_watch, set_watch_reference, exceptions.dict_watch)
    for name, path in sorted(dict_command.items()):
        if name not in set_command_reference and name not in exceptions.dict_command:
            list_problem.append(f"{path}: 개발 명령 {name} 를 자동화가 부르지 않는다 — 시나리오로 잇거나 {CONFIG_PATH} 에 까닭을 적는다")
    list_problem += check_exceptions_table("개발 명령", dict_command, set_command_reference, exceptions.dict_command)
    for path, _, name in sorted(list_lost):
        list_problem.append(f"{path}: 명령 함수 {name} 를 등록(SW_DEV_COMMAND)도 호출도 하지 않는다 — 등록 줄을 되살리거나 함수를 지운다")
    return list_problem


def find_problems(sources: Sources, exceptions: Exceptions) -> list[str]:
    map_fact = {path: extract_facts(bucket, path, text) for bucket in ("dict_header", "dict_test_code", "dict_content", "dict_script")
               for path, text in getattr(sources, bucket).items()}
    return evaluate_facts(map_fact, exceptions)


def read_exceptions(project_root: Path) -> Exceptions:
    data = json.loads((project_root / CONFIG_PATH).read_text(encoding="utf-8"))
    return Exceptions(data["_folders"], data["_components"], data["_watches"], data["_devCommands"])


def read_summary(project_root: Path) -> list[str]:
    """저장소에 있는 파일만 본다(git 이 아는 파일 — 빌드 산출물 · 임포트 산출물을 줍지 않는다). 내용이 바뀐 파일만 다시 읽고(`index_cache`), 판정은 접은 토큰만 읽는다."""
    list_path = [path for path in list_repo_files(project_root) if classify_path(path) is not None]
    return list(open_facts(project_root, "feature-refs", [__file__], list_path, lambda path, text: extract_facts(classify_path(path), path, text), errors="ignore",
                          contribute=fold_tokens).summary)


def check_feature_references(project_root: Path, scope: LintScope) -> list[str]:
    """커밋 훅은 훑는 파일이 바뀌었을 때만 돈다(지운 파일 포함 — 참조를 지우면 참조 0 이 생긴다)."""
    if not scope.whole_repo and not any(classify_path(path) for path in (*scope.list_file, *scope.list_removed)):
        return []
    return evaluate_summary(read_summary(project_root), read_exceptions(project_root))


def run_self_check() -> None:
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

    empty = Exceptions({}, {}, {}, {})
    list_problem = find_problems(sources, empty)
    assert len(list_problem) == 4, list_problem
    assert any("runLost" in line for line in list_problem)
    assert any("LonelyComponent" in line for line in list_problem)
    assert any("A.Unread" in line for line in list_problem)
    assert any("cmd.alone" in line for line in list_problem)

    # 예외: 까닭과 함께면 받고, 낡은 줄은 위반이다.
    assert [line for line in find_problems(sources, Exceptions({"Source/A": "x"}, {}, {"A.Unread": "x"}, {"cmd.alone": "x"})) if "runLost" not in line] == []
    list_stale = find_problems(sources, Exceptions({}, {"LonelyComponent": "", "UsedComponent": "x", "GoneComponent": "x"}, {"A.Unread": "x"}, {"cmd.alone": "x"}))
    assert sum("까닭이 비었다" in line for line in list_stale) == 1, list_stale
    assert sum("이제 참조가 있다" in line for line in list_stale) == 1, list_stale
    assert sum("정의가 없다" in line for line in list_stale) == 1, list_stale


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="컴포넌트 · 감시 값 · 개발 명령의 참조 0 검사(머리말)")
    parser.add_argument("--self-check", action="store_true", help="규칙 자체를 시험한다")
    args = parser.parse_args(argv)
    if args.self_check:
        run_self_check()
        print("[FeatureReferences] self-check OK")
        return ExitCode.PASSED
    list_problem = check_feature_references(get_project_root(), make_whole_scope(get_project_root()))
    for line in list_problem:
        print(f"  - {line}")
    print(f"[FeatureReferences] {'FAIL' if list_problem else 'OK'} ({len(list_problem)}건)")
    return ExitCode.VIOLATION if list_problem else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
