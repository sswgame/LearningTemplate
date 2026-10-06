#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckProductNames.py

제품 이름(SQLite · PostgreSQL · Valkey · OpenSSL · XAudio2 · Tracy · Jolt …)은 그 제품을 감싼 드라이버 · 제공자 · 백엔드 폴더에만 있다.

키트 · 엔진의 나머지는 인터페이스(`ISqlDriver` · `INetSecurityProvider` · `IAudioSystem` · `IPhysicsScene3D` …)와 등록부를 쓴다. 제품 이름이
그 밖의 식별자에 새면 제품을 바꿀 때 그 자리가 모두 따라 바뀐다 — `SqlLocalSlotStorage` 가 등록부(`SqlDriverRegistry::findDriver`)를 두고
`SqliteDriver::getInstance()` 를 직접 부르고 있었다.

  1) 주석 · 문자열 밖의 식별자에 제품 낱말이 들면 위반이다 — 낱말 머리(`SqliteDriver`)든 camelCase 가운데(`getTracyPort`)든.
     `Resp` 는 `Response` 와 겹쳐 `Resp` + 대문자만 본다.
  2) 허용 자리: 그 제품을 감싼 라이브러리의 허용 뿌리(`CheckThirdPartyIsolation._kListLibraryRule` — 목록을 두 벌 두지 않는다)와
     `*/Driver/*` · `*/Windows/*` · `*/Linux/*` 폴더(드라이버 · 플랫폼 구현).
  3) 제품을 골라 올리는 조립점(등록부 · 팩토리 · 외부 도구 실행기)은 `mapExemption`(파일 fnmatch → 이유)에 둔다.

  python Scripts/lint/gate/CheckProductNames.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import fnmatch
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate · gate

from common import blankCommentsAndLiterals, kLintTargetRelDirs, normalizePath  # noqa: E402
from gate.CheckThirdPartyIsolation import _kListLibraryRule  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx")
#: 제품 낱말 → 그 제품을 감싼 라이브러리 규칙 이름(`_kListLibraryRule`, 없으면 빈 글자 — 드라이버 · 플랫폼 폴더에만 산다).
_kMapProductToLibrary = {
    "Sqlite": "SQLite", "SQLite": "SQLite", "Postgres": "PostgreSQL", "PostgreSql": "PostgreSQL", "PostgreSQL": "PostgreSQL",
    "OpenSsl": "OpenSSL", "OpenSSL": "OpenSSL", "Tracy": "Tracy", "Jolt": "Jolt", "Box2D": "Box2D", "Recast": "Recast", "Detour": "Recast",
    "Valkey": "", "Garnet": "", "Resp": "", "XAudio2": "", "XInput": "",
}
#: 제품 낱말 — 식별자의 낱말 머리나 camelCase 가운데(앞이 대문자가 아님)에 들면 걸린다. 뒤에 소문자가 이어지면 다른 낱말이다.
_kProductNameRe = re.compile(r"(?<![A-Z])(Sqlite|SQLite|PostgreSQL|PostgreSql|Postgres|Valkey|Garnet|OpenSsl|OpenSSL|XAudio2|XInput|Tracy|Jolt|Box2D|Recast|Detour|"
                             r"Resp(?=[A-Z]))(?![a-z])")
#: 서드파티 격리 표 밖에서도 제품 이름을 쓰는 폴더 — 드라이버 · 플랫폼 구현.
_kListProductFolderPattern = ("*/Driver/*", "*/Windows/*", "*/Linux/*")
_kMapLibraryToRoot: dict[str, tuple[str, ...]] = {rule.name: rule.listAllowedRoot for rule in _kListLibraryRule}


def isProductAllowedInternal(relative: str, productWord: str) -> bool:
    """이 파일에서 이 제품 이름이 제자리인가 — 그 제품을 감싼 라이브러리의 허용 뿌리 · 드라이버 · 플랫폼 폴더."""
    if any(fnmatch.fnmatchcase(relative, pattern) for pattern in _kListProductFolderPattern):
        return True
    return relative.startswith(_kMapLibraryToRoot.get(_kMapProductToLibrary[productWord], ("<none>",)))


class CheckProductNamesGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    #: 제품을 골라 올리는 조립점(fnmatch) → 이유.
    mapExemption = {
        "Source/GameFramework/Kits/Storage/SqlStore/Sql/SqlDriverRegistry.cpp": "등록부 — 키트가 든 드라이버(SQLite)를 올리는 자리",
        "Source/GameFramework/Kits/Storage/Server/SqlStore/ServiceStoreFactory.cpp": "서버 키트의 조립점 — PostgreSQL 드라이버를 올린다",
        "Source/Engine/Network/EngineNetSecurity.cpp": "보안 제공자 조립점 — OpenSSL 제공자를 고른다",
        "Source/Engine/Audio/IAudioSystem.cpp": "오디오 백엔드 팩토리 — XAudio2 를 고른다",
        "Source/Editor/Common/Commands/EditorTracyLauncher.*": "외부 프로파일러 GUI 를 띄우는 실행기 — 이름이 곧 대상 도구",
        "Source/Editor/Panels/ProfilerPanel.*": "프로파일러 패널의 'Open Tracy' 버튼 — 외부 뷰어를 띄우는 자리(이름이 곧 대상 도구)",
        "Source/Engine/Utility/Profiling/ProfilerBackend.*": "프로파일러 백엔드 선택점 — Tracy 를 켜고 포트를 묻는 창구(외부 뷰어와 맞물린다)",
        "Source/Engine/Physics/PhysicsSystem.cpp": "물리 백엔드 팩토리 — Jolt · Box2D 백엔드를 고른다",
        "Source/GameFramework/Kits/Storage/Server/CacheStore/CacheStoreFactory.*": "서버 캐시 키트의 조립점 — RESP(Valkey · Garnet) 드라이버를 올린다",
    }

    description = "제품 이름 식별자가 드라이버 · 제공자 · 백엔드 · 플랫폼 폴더 밖에 새지 않는지 검사"
    buildComment = "Checking that product names stay inside driver and backend folders..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*" for root in kLintTargetRelDirs)
    preCommitFileArgument = "--files"
    violationHeader = "드라이버 · 백엔드 폴더 밖의 제품 이름"
    hint = ("  인터페이스(ISqlDriver · INetSecurityProvider · IAudioSystem …)와 등록부(SqlDriverRegistry::findDriver 등)로 고릅니다.\n"
            "  제품을 골라 올리는 조립점이면 CheckProductNames.py 의 mapExemption 에 이유와 함께 한 줄.")
    selfTestCases = [
        {
            "name": "키트 저장소가 SQLite 드라이버를 직접 부른다",
            "files": {"Source/GameFramework/Kits/Storage/Probe/ProbeStorage.cpp": "void probe()\n{\n    SqliteDriver& driver = SqliteDriver::getInstance();\n}\n"},
        },
        {
            "name": "엔진 씬이 Jolt 형식을 든다",
            "files": {"Source/Engine/Scene/Probe.h": "#pragma once\nstruct Probe\n{\n    JoltBodyHandle _body;\n};\n"},
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listPath = self.selectTargetFiles(repositoryRoot, args.files, listScanRoot=kLintTargetRelDirs, suffixes=_kSuffixes)
        listViolation: list[str] = []
        for path, text in self.readFiles(listPath):
            relative = normalizePath(str(path.relative_to(repositoryRoot)))
            if relative.startswith("Test/"):
                continue
            exemptKey = self.findExemptionKey(relative)
            if exemptKey is not None:
                self.seeExemption(exemptKey)
            if _kProductNameRe.search(text) is None:
                continue
            listOriginalLine = text.splitlines()
            for lineIndex, line in enumerate(blankCommentsAndLiterals(text).splitlines(), start=1):
                listWord = [match.group(1) for match in _kProductNameRe.finditer(line) if not isProductAllowedInternal(relative, match.group(1))]
                if not listWord:
                    continue
                if exemptKey is not None:
                    self.useExemption(exemptKey)
                    continue
                listViolation.append(f"{relative}:{lineIndex}: 제품 이름 '{listWord[0]}' | {listOriginalLine[lineIndex - 1].strip()}")
        return GateResult(listViolation=listViolation, summary="린트 대상(시험 제외)의 제품 이름 식별자")


main = CheckProductNamesGate.run

if __name__ == "__main__":
    sys.exit(main())
