#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
같은 네임스페이스에 같은 이름의 class · struct 정의가 두 파일에 있으면 막는다.

  키트(`GF_*`) · 게임 모듈은 각자 DLL 이지만 시험 실행 파일 · 배포본(정적 링크)은 그것들을 한 프로그램에 함께 올린다. 그때 같은
  `sw::MonsterCatalog` 가 둘이면 ODR 위반이다 — 링커는 아무 말이 없고, 한쪽 생성자로 만든 객체를 다른 쪽 멤버 함수가 다른 배치로 읽어
  메모리가 깨진다(병합 때 ActionCombat 의 `MonsterCatalog` 와 MonsterCollector 의 `MonsterCatalog` 가 이렇게 부딪쳤다). 키트마다 이름에 키트를
  붙인다(`MonsterCollectorCatalog`).

  - 대상: `Source/` 의 `.h` · `.cpp`, 이름 있는 네임스페이스 바로 안의 정의. 중첩 타입 · 함수 안 타입 · 익명 네임스페이스(파일 지역)는 보지 않는다.
  - 같은 이름이 같은 줄기의 `.h` · `.cpp`(선언과 정의를 나눈 것)에만 있으면 하나로 본다.

  python Scripts/lint/gate/CheckDuplicateTypeNames.py [--root <repo>]
"""
from __future__ import annotations

import argparse
import os
import re
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import blankCommentsAndLiterals  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kTypeRe = re.compile(r"^\s*(?:template\s*<[^;{]*>\s*)?(?:class|struct)\s+(?:SW_\w+\s+|alignas\s*\([^)]*\)\s+)*(\w+)(?:\s+final)?\s*(?::[^;{]*)?\{?\s*$")
_kNamespaceRe = re.compile(r"^\s*namespace(?:\s+([\w:]+))?\s*\{?\s*$")


def collectTypeDefinitions(path: Path, relativePath: str, mapDefinition: dict[str, set[str]]) -> None:
    """파일 하나의 네임스페이스 수준 정의를 `네임스페이스::이름 → 파일들` 에 더한다."""
    lines = blankCommentsAndLiterals(path.read_text(encoding="utf-8", errors="replace")).split("\n")
    depth = 0
    stackNamespace: list[tuple[str, int]] = []
    pendingNamespace: str | None = None
    for line in lines:
        # 정규식 · 글자 루프는 그것이 반드시 품는 글자가 줄에 있을 때만 돈다 — 결과는 같다.
        namespaceMatch = _kNamespaceRe.match(line) if "namespace" in line else None
        if namespaceMatch is not None:
            pendingNamespace = namespaceMatch.group(1) or ""
        typeMatch = _kTypeRe.match(line) if ("class" in line or "struct" in line) else None
        bInNamedNamespace = bool(stackNamespace) and all(name != "" for name, _ in stackNamespace)
        if typeMatch is not None and bInNamedNamespace and depth == stackNamespace[-1][1]:
            key = "::".join(name for name, _ in stackNamespace) + "::" + typeMatch.group(1)
            mapDefinition[key].add(relativePath)
        if "{" not in line and "}" not in line:
            continue
        for character in line:
            if character == "{":
                depth += 1
                if pendingNamespace is not None:
                    stackNamespace.append((pendingNamespace, depth))
                    pendingNamespace = None
            elif character == "}":
                if stackNamespace and stackNamespace[-1][1] == depth:
                    stackNamespace.pop()
                depth -= 1


class CheckDuplicateTypeNamesGate(LintGate):
    """`selfTestCases` 는 이 린트가 반드시 잡아야 하는 조각이다."""

    description = "같은 이름의 타입 정의 검사(ODR)"
    buildComment = "Checking that no two files define a type of the same name in one namespace..."
    timeoutSeconds = 30
    # 위반은 `class` · `struct` 정의 줄이나 `namespace` 줄이 생기거나 바뀔 때만 생긴다(깊이는 균형 잡힌 중괄호라 다른 줄은 영향이 없다).
    # 바뀐 줄(+ · -)에 그 낱말이 있는 커밋만 돈다 — 파일 삭제 · 이름 바꿈은 옛 줄이 - 로 남아 잡힌다.
    preCommitChangedLinePattern = (
        ("Source/*.h", r"\b(?:class|struct|namespace)\b"),
        ("Source/*.cpp", r"\b(?:class|struct|namespace)\b"),
    )
    preCommitFileArgument = ""
    violationHeader = "같은 이름의 타입 정의"
    selfTestCases = [
        {
            "name": "두 키트가 같은 이름의 클래스를 정의",
            "files": {
                "Source/GameFramework/Kits/KitA/Catalog.h": "#pragma once\nnamespace sw\n{\n    class SharedCatalog\n    {\n    };\n} // namespace sw\n",
                "Source/GameFramework/Kits/KitB/Other.h": "#pragma once\nnamespace sw\n{\n    struct SharedCatalog\n    {\n        int value;\n    };\n} // namespace sw\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        pass

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listFile = self.selectTargetFiles(repositoryRoot, None, listScanRoot=("Source",), suffixes=(".h", ".cpp"))
        mapDefinition: dict[str, set[str]] = defaultdict(set)
        for path in listFile:
            collectTypeDefinitions(path, path.relative_to(repositoryRoot).as_posix(), mapDefinition)

        listViolation: list[str] = []
        for key, setFile in sorted(mapDefinition.items()):
            setStem = {os.path.splitext(relativePath)[0] for relativePath in setFile}
            if len(setStem) > 1:
                listViolation.append(f"{key}: {' · '.join(sorted(setFile))} 가 같은 이름을 정의합니다 — 한 프로그램에 함께 올라가면 ODR 위반입니다. "
                                     f"키트 · 모듈 이름을 붙여 가르세요")
        return GateResult(listViolation=listViolation, summary=f"{len(listFile)} files, {len(mapDefinition)} types")


main = CheckDuplicateTypeNamesGate.run


if __name__ == "__main__":
    sys.exit(main())
