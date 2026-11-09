#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/TickSceneWalk.py

컴포넌트 틱(`onTick` · `onSubTick`)에서 닿는 씬 전체 순회를 찾는다 — 매니저의 `forEachGameObject` · `forEachComponent` ·
`forEachComponentOfType` · `getAllGameObjects`.

**왜 린트인가.** 틱은 오브젝트 단위로 워커에 나뉘어 돈다(`SceneTickScheduler`). 틱 하나가 씬 전체를 돌면 그 일은 한 워커에 몰리고(오브젝트
단위 병렬을 버린다), 같은 타입이 N 개면 N × 씬 크기(N²)가 되며, 다른 오브젝트에 쓰면 그 오브젝트의 틱과 겹친다. 순회 함수는 틱 밖(개발 명령 ·
프레임 시스템 · 에디터)에서는 정상이라 형식으로 막을 수 없다. 대신 쓸 것은 `docs/agents/TickPlacement.md` 에 있다 — 찾기는
`ComponentRegistry`(등록 순서 · 잠금 없는 읽기), 순서는 서브틱 선행 조건, 같은 타입의 일은 일괄 틱, 씬 전체 집계는 프레임 시스템.

**어떻게 따라가나.** 틱에서 직접 부르지 않고 도우미(`findFirstOpen` · 틱 뒤로 미룬 `updateZone`)를 거치는 것이 대부분이라, 소스 전체의 함수 정의를
모아 "씬을 도는 함수" 집합을 부르는 쪽으로 넓힌다. 한정 이름 호출(`SessionStateComponent::findFirstOpen(`)은 파일을 넘어 따라가고, 맨 이름 호출은
같은 파일의 같은 클래스 · 클래스 밖 도우미로만 푼다(`pX->method(` 의 형식은 글로 알 수 없어 따라가지 않는다). 자기 오브젝트 순회
(`pOwner->forEachComponentOfType`)는 잡지 않는다 — 받는 쪽 이름에 `anager` 가 든 것과 `getManager()->` 만 본다.
예외는 `Config/Dev/TickSceneWalkExceptions.json` 에 `경로::클래스::함수` 와 까닭을 적는다.

`py -3 -m Scripts lint tick-scene-walk [--self-check]` 로 돌리고, 커밋 훅(`Rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402

kConfigPath = "Config/Dev/TickSceneWalkExceptions.json"
kComment = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
kString = re.compile(r'"(?:\\.|[^"\\\n])*"')
kDefinition = re.compile(r"\b(?:(\w+)::)?(\w+)\s*\((?:[^;{}()]|\([^()]*\))*\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:->\s*[\w:<>]+\s*)?\{")
kSceneWalk = re.compile(r"(?:\b\w*[mM]anager\w*|getManager\s*\(\s*\))\s*(?:->|\.)\s*(forEachGameObject|forEachComponentOfType|forEachComponent|getAllGameObjects)\b")
kQualifiedCall = re.compile(r"\b(\w+)::(\w+)\s*\(")
kBareCall = re.compile(r"(?<![\w:.>])(\w+)\s*\(")
kMemberCall = re.compile(r"(?:\.|->)\s*(\w+)\s*\(")
kNotFunction = {"if", "for", "while", "switch", "catch", "return", "sizeof", "decltype", "static_assert", "alignof", "SW_DELEGATE_LAMBDA"}
kTickNames = ("onTick", "onSubTick")


@dataclass
class FunctionBody:
    path: str
    owner: str   # 클래스 이름(정의가 `클래스::함수` 일 때) — 클래스 안 인라인 정의 · 도우미는 빈 글
    name: str
    body: str

    @property
    def key(self) -> str:
        return f"{self.owner}::{self.name}" if self.owner else f"{self.path}::{self.name}"


def readBodies(path: str, text: str) -> list[FunctionBody]:
    """글 하나의 함수 정의를 (클래스, 이름, 몸) 으로 모은다. 람다 · 제어문은 이름이 없어 바깥 함수의 몸에 남는다."""
    code = kString.sub('""', kComment.sub("", text))
    listBody: list[FunctionBody] = []
    for match in kDefinition.finditer(code):
        name = match.group(2)
        if name in kNotFunction:
            continue
        depth = 1
        cursor = match.end()
        while cursor < len(code) and depth > 0:
            if code[cursor] == "{":
                depth += 1
            elif code[cursor] == "}":
                depth -= 1
            cursor += 1
        listBody.append(FunctionBody(path, match.group(1) or "", name, code[match.end():cursor]))
    return listBody


def findWalkingTicks(listSource: Sequence[tuple[str, str]]) -> dict[str, str]:
    """씬 전체 순회에 닿는 틱을 `경로::클래스::함수` → 근거 줄(처음 닿는 순회) 로 돌려준다."""
    listBody = [body for path, text in listSource for body in readBodies(path, text)]
    byQualified: dict[str, list[FunctionBody]] = {}
    byLocal: dict[tuple[str, str], list[FunctionBody]] = {}
    for body in listBody:
        byQualified.setdefault(body.key, []).append(body)
        if body.owner == "":
            byLocal.setdefault((body.path, body.name), []).append(body)
    reason: dict[str, str] = {}
    for body in listBody:
        match = kSceneWalk.search(body.body)
        if match:
            reason.setdefault(body.key, match.group(0))

    def callees(body: FunctionBody) -> set[str]:
        listKey: set[str] = set()
        for owner, name in kQualifiedCall.findall(body.body):
            if f"{owner}::{name}" in byQualified:
                listKey.add(f"{owner}::{name}")
            for helper in byLocal.get((body.path, name), []):   # 익명 네임스페이스의 `…Internal` 구조체 안에 정의한 정적 도우미
                listKey.add(helper.key)
        for name in kMemberCall.findall(body.body):   # 틱 뒤로 미룬 람다의 `self.update…(` — 같은 클래스의 멤버로만 푼다
            if body.owner and f"{body.owner}::{name}" in byQualified:
                listKey.add(f"{body.owner}::{name}")
        for name in kBareCall.findall(body.body):
            if body.owner and f"{body.owner}::{name}" in byQualified:
                listKey.add(f"{body.owner}::{name}")
            for helper in byLocal.get((body.path, name), []):
                listKey.add(helper.key)
        return listKey

    graph = {id(body): callees(body) for body in listBody}
    bChanged = True
    while bChanged:
        bChanged = False
        for body in listBody:
            if body.key in reason:
                continue
            for callee in graph[id(body)]:
                if callee in reason:
                    reason[body.key] = f"{callee} → {reason[callee]}"
                    bChanged = True
                    break
    found: dict[str, str] = {}
    for body in listBody:
        if body.name in kTickNames and body.key in reason:
            found[f"{body.path}::{body.owner}::{body.name}"] = reason[body.key]
    return found


def findProblems(listSource: Sequence[tuple[str, str]], exceptions: dict) -> list[str]:
    found = findWalkingTicks(listSource)
    listProblem = [f"{key}: 틱에서 씬 전체를 돈다 — 등록 목록(ComponentRegistry) · 서브틱 선행 조건 · 프레임 시스템으로(docs/agents/TickPlacement.md): {line}"
                   for key, line in sorted(found.items()) if key not in exceptions]
    for key in sorted(set(exceptions) - set(found)):
        listProblem.append(f"{key}: 낡은 예외 — 그 틱은 씬을 돌지 않는다. {kConfigPath} 에서 뺀다")
    return listProblem


def readSources(projectRoot: Path) -> list[tuple[str, str]]:
    listSource: list[tuple[str, str]] = []
    for path in (projectRoot / "Source").rglob("*.cpp"):
        if "Tests" in path.parts:
            continue
        try:
            listSource.append((path.relative_to(projectRoot).as_posix(), path.read_text(encoding="utf-8")))
        except (UnicodeDecodeError, OSError):
            continue
    return listSource


def checkTickSceneWalk(projectRoot: Path) -> list[str]:
    exceptions = json.loads((projectRoot / kConfigPath).read_text(encoding="utf-8"))["_exceptions"]
    return findProblems(readSources(projectRoot), exceptions)


def runSelfCheck() -> None:
    direct = ("a.cpp", "void Foo::onTick( float32 deltaTime )\n{\n    pManager->forEachComponentOfType<Bar>( []( Bar* ) {} );\n}\n")
    assert len(findProblems([direct], {})) == 1, "틱 안의 매니저 순회는 잡아야 한다"
    assert findProblems([direct], {"a.cpp::Foo::onTick": "까닭"}) == [], "예외는 통과해야 한다"
    helper = ("b.cpp", "void Foo::onTick( float32 deltaTime )\n{\n    pManager->executeOrDeferPostTickOnComponent( *this, []( Foo& self ) { self.scan(); } );\n    scan();\n}\n"
                       "void Foo::scan()\n{\n    getOwner()->getManager()->forEachGameObject( []( GameObject* ) {} );\n}\n")
    assert len(findProblems([helper], {})) == 1, "같은 클래스 도우미를 거친 순회도 잡아야 한다"
    deferred = ("b2.cpp", "void Foo::onTick( float32 deltaTime )\n{\n    pManager->executeOrDeferPostTickOnComponent( *this, []( Foo& self, GameObjectManager& manager ) { self.updateZone( manager ); } );\n}\n"
                          "void Foo::updateZone( GameObjectManager& manager )\n{\n    manager.forEachComponentOfType<Bar>( []( Bar* ) {} );\n}\n")
    assert len(findProblems([deferred], {})) == 1, "틱 뒤로 미룬 람다의 self. 멤버 호출도 따라가야 한다"
    internal = ("b3.cpp", "struct FooInternal\n{\n    static void align( GameObjectManager& manager )\n    {\n        manager.forEachGameObject( []( GameObject* ) {} );\n    }\n};\n"
                          "void Foo::onTick( float32 deltaTime )\n{\n    FooInternal::align( *pManager );\n}\n")
    assert len(findProblems([internal], {})) == 1, "익명 네임스페이스 Internal 구조체의 정적 도우미도 따라가야 한다"
    finder = ("c.cpp", "Bar* Bar::findInScene( const GameObjectManager& manager )\n{\n    manager.forEachComponentOfType<Bar>( []( Bar* ) {} );\n    return nullptr;\n}\n")
    caller = ("d.cpp", "void Baz::onSubTick( uint32 id, float32 deltaTime )\n{\n    Bar::findInScene( *pManager );\n}\n")
    assert len(findProblems([finder, caller], {})) == 1, "다른 파일의 한정 이름 찾기도 잡아야 한다"
    other = ("e.cpp", "void Foo::onTick( float32 deltaTime )\n{\n    stepOnce();\n}\nvoid Foo::stepOnce()\n{\n}\n"
                      "void Qux::stepOnce()\n{\n    manager.getAllGameObjects( list );\n}\n")
    assert findProblems([other], {}) == [], "다른 클래스의 같은 이름 함수로 잘못 따라가면 안 된다"
    clean = ("f.cpp", "void Foo::onTick( float32 deltaTime )\n{\n    pOwner->forEachComponentOfType<Mesh>( []( Mesh* ) {} );\n}\nvoid Foo::runCommand()\n{\n    pManager->forEachGameObject( []( GameObject* ) {} );\n}\n")
    assert findProblems([clean], {}) == [], "자기 오브젝트 순회와 틱이 부르지 않는 함수는 통과해야 한다"
    assert len(findProblems([clean], {"f.cpp::Foo::onTick": "까닭"})) == 1, "쓰이지 않는 예외는 낡았다고 알려야 한다"
    comment = ("g.cpp", "void Foo::onTick( float32 deltaTime )\n{\n    // manager.forEachGameObject\n}\n")
    assert findProblems([comment], {}) == [], "주석은 통과해야 한다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="컴포넌트 틱에서 닿는 씬 전체 순회를 찾는다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[TickSceneWalk] self-check OK")
        return ExitCode.Passed
    listProblem = checkTickSceneWalk(getProjectRoot())
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[TickSceneWalk] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
