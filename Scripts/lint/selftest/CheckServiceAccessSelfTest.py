#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
`CheckServiceAccess` 가 아직 살아 있는지 검사합니다 (음성 테스트 + 오탐 테스트).

**린트는 조용히 죽는다.** 규칙 하나가 정규식 한 글자 때문에 아무것도 못 잡게 되어도 결과는 "위반 0건" 이라 통과처럼 보인다.
그래서 모양마다 **일부러 어긴 조각**(잡혀야 한다)과 **어기지 않은 조각**(잡히면 오탐이다)을 둔다.
게이트가 반드시 잡아야 하는 대표 조각은 게이트의 `selfTestCases` 에도 있다 — 여기는 모양별 건수와 허용 쪽을 본다.

  python Scripts/lint/selftest/CheckServiceAccessSelfTest.py [--root <repo>]
"""
from __future__ import annotations

import argparse
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.gate import CheckServiceAccess  # noqa: E402
from lint.LintGate import GateResult, LintGate  # noqa: E402
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)

_kRetired = "ServiceAccess/RetiredWindow"
_kSingleton = "ServiceAccess/Singleton"

# (이름, 파일 -> 내용, 기대하는 (RetiredWindow, Singleton) 건수)
_kListCase: list[tuple[str, dict[str, str], tuple[int, int]]] = [
    ("옛 getService / getSceneService / isServiceBound",
     {"Source/Probe/A.cpp": "void f()\n{\n    game::getService<Foo>();\n    scene.getSceneService<Bar>();\n    if ( game::isServiceBound<Foo>() ) {}\n}\n"},
     (3, 0)),
    ("한정 없는 editor 조회", {"Source/Probe/B.cpp": "void f()\n{\n    getService< Foo >()->go();\n}\n"}, (1, 0)),
    ("getBoundEngineServices", {"Source/Probe/C.cpp": "void f()\n{\n    auto& s = engine::getBoundEngineServices();\n}\n"}, (1, 0)),
    ("locateSubsystem 으로 엔진 범위 찾기",
     {"Source/Probe/D.cpp": "void f()\n{\n    auto* p = locateSubsystem<Foo>( engine::getEngineSubsystems() );\n}\n"}, (1, 0)),
    ("이름 붙은 엔진 접근자", {"Test/Probe/E.cpp": "void f()\n{\n    sw::engine::getTypeRegistry();\n    engine::getTaskManager().clear();\n}\n"}, (2, 0)),
    ("도구 폴더도 본다", {"Tools/OnlineLoadBot/F.cpp": "void f()\n{\n    engine::getAssetManager();\n}\n"}, (1, 0)),
    ("헤더의 정적 getInstance", {"Source/Probe/G.h": "class Foo\n{\npublic:\n    static Foo& getInstance();\n};\n"}, (0, 1)),
    ("헤더의 인라인 정의 getInstance",
     {"Source/Probe/H.inl": "struct Bar\n{\n    static Bar& getInstance()\n    {\n        static Bar s;\n        return s;\n    }\n};\n"}, (0, 1)),
    ("중첩 클래스의 getInstance 는 안쪽 타입으로 판정",
     {"Source/Probe/I.h": "class RawAnimationCodec\n{\n    struct Inner\n    {\n        static Inner& getInstance();\n    };\n};\n"}, (0, 1)),
    # --- 어기지 않은 조각 ---
    ("주석 · 문자열 안의 언급",
     {"Source/Probe/J.cpp": '// getService<Foo>() 는 지워졌다. engine::getTypeRegistry() 도\n/* getSceneService<Bar>() */\nconst char* kText = "getService<Foo>()";\n'},
     (0, 0)),
    ("남는 엔진 이름과 새 창구",
     {"Source/Probe/K.cpp": ("void f()\n{\n    engine::getSubsystem<TaskManager>();\n    engine::findSubsystem<Foo>();\n"
                             "    engine::getEngineSubsystems();\n    engine::getParallelScratchSlot();\n"
                             "    engine::getParallelScratchSlotCount();\n    engine::getEngineAbiStamp();\n    game::findSubsystem<Foo>();\n}\n")},
     (0, 0)),
    ("생성 파일과 ThirdParty", {"Source/Probe/L.gen.cpp": "void f() { getService<Foo>(); }\n", "Source/ThirdParty/M.cpp": "void f() { getService<Foo>(); }\n"}, (0, 0)),
    ("cpp 의 getInstance 와 호출은 보지 않는다",
     {"Source/Probe/N.cpp": "Foo& Foo::getInstance()\n{\n    static Foo& s = Other::getInstance();\n    return s;\n}\n",
      "Source/Probe/N.h": "void f()\n{\n    static auto& s = Foo::getInstance();\n}\n"},
     (0, 0)),
    ("예외 표의 타입", {"Source/Probe/O.h": "class SQLiteDriver\n{\npublic:\n    static SQLiteDriver& getInstance();\n};\n"}, (0, 0)),
    ("예외 표의 템플릿 구조체",
     {"Source/Probe/P.h": "template <typename T>\nstruct EditorSettingsInstance\n{\n    static void* getInstance()\n    {\n        return nullptr;\n    }\n};\n"},
     (0, 0)),
]


def countCategory(listViolation: list[str], category: str) -> int:
    return sum(1 for line in listViolation if f"[{category}]" in line)


class CheckServiceAccessSelfTestGate(LintGate):
    description = "CheckServiceAccess 음성 · 오탐 테스트 — 모양마다 잡히는지, 허용 모양이 안 잡히는지"
    buildComment = "Checking that CheckServiceAccess still catches every retired window and singleton shape..."
    timeoutSeconds = 60
    selfTestSkipReason = "린트를 보는 린트 — 조각을 들지 않는다(대상이 린트 폴더 자체다)"
    violationHeader = "문제"

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listError: list[str] = []
        tempRoot = Path(tempfile.mkdtemp(prefix="swServiceAccessSelfTest"))
        try:
            for caseIndex, (name, mapFile, expected) in enumerate(_kListCase):
                caseRoot = tempRoot / f"case{caseIndex}"
                for relative, content in mapFile.items():
                    path = caseRoot / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(content, encoding="utf-8", newline="\n")
                listViolation = CheckServiceAccess.findServiceAccessViolations(caseRoot, None)
                found = (countCategory(listViolation, _kRetired), countCategory(listViolation, _kSingleton))
                if found != expected:
                    listError.append(f"{name}: (RetiredWindow, Singleton) 기대 {expected}, 실제 {found}")
        finally:
            shutil.rmtree(tempRoot, ignore_errors=True)
        return GateResult(listViolation=listError, summary=f"{len(_kListCase)} cases")


main = CheckServiceAccessSelfTestGate.run


if __name__ == "__main__":
    sys.exit(main())
