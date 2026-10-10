#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckProductNames.py

제품 이름(SQLite · PostgreSQL · Valkey · OpenSSL · XAudio2 · Tracy · Jolt …)은 그 제품을 감싼 드라이버 · 제공자 · 백엔드 폴더에만 있다.

키트 · 엔진의 나머지는 인터페이스(`ISQLDriver` · `INetSecurityProvider` · `IAudioSystem` · `IPhysicsScene3D` …)와 등록부를 쓴다. 제품 이름이
그 밖의 식별자에 새면 제품을 바꿀 때 그 자리가 모두 따라 바뀐다 — `SQLLocalSlotStorage` 가 등록부(`SQLDriverRegistry::findDriver`)를 두고
`SQLiteDriver::getInstance()` 를 직접 부르고 있었다.

  1) 주석 · 문자열 밖의 식별자에 제품 낱말이 들면 위반이다 — 낱말 머리(`SQLiteDriver`)든 camelCase 가운데(`getTracyPort`)든.
     `Resp` 는 `Response` 와 겹쳐 `Resp` + 대문자만 본다.
  2) 허용 자리: 그 제품을 감싼 라이브러리의 허용 뿌리(`CheckThirdPartyIsolation._kListLibraryRule` — 목록을 두 벌 두지 않는다)와
     `*/Driver/*` · `*/Windows/*` · `*/Linux/*` 폴더(드라이버 · 플랫폼 구현).
  3) 제품을 골라 올리는 조립점(등록부 · 팩토리 · 외부 도구 실행기)은 예외 표(파일 fnmatch → 이유)에 둔다.

제품 낱말 표(`[product]` · `capital_follows`)와 예외 표(`[exemption]`)는 `Scripts/lint/rules/CheckProductNames.toml` 에 있다.

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
from common.RuleData import kKindText, kKindTextList  # noqa: E402
from gate.CheckThirdPartyIsolation import _kListLibraryRule  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx")
#: `rules/CheckProductNames.toml` 에서 예외 표 말고 읽는 것 — 제품 낱말 → 라이브러리 규칙 이름, 뒤에 대문자가 와야 하는 낱말.
_kRuleSchema = {"capital_follows": kKindTextList, "product": kKindText}
_kRuleData = LintGate.readRules("CheckProductNames", _kRuleSchema, requiredKeys=("product",))
_kMapProductToLibrary: dict[str, str] = _kRuleData["product"]
#: 제품 낱말 — 긴 것부터 맞춘다(`PostgreSQL` 이 `Postgres` 보다 먼저).
_kProductNameRe = re.compile(r"(?<![A-Z])(" + "|".join(
    re.escape(word) + ("(?=[A-Z])" if word in _kRuleData["capital_follows"] else "")
    for word in sorted(_kMapProductToLibrary, key=len, reverse=True)) + r")(?![a-z])")
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

    ruleSchema = _kRuleSchema
    description = "제품 이름 식별자가 드라이버 · 제공자 · 백엔드 · 플랫폼 폴더 밖에 새지 않는지 검사"
    buildComment = "Checking that product names stay inside driver and backend folders..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*" for root in kLintTargetRelDirs)
    preCommitFileArgument = "--files"
    violationHeader = "드라이버 · 백엔드 폴더 밖의 제품 이름"
    hint = ("  인터페이스(ISQLDriver · INetSecurityProvider · IAudioSystem …)와 등록부(SQLDriverRegistry::findDriver 등)로 고릅니다.\n"
            "  제품을 골라 올리는 조립점이면 Scripts/lint/rules/CheckProductNames.toml 의 [exemption] 에 이유와 함께 한 줄.")
    selfTestCases = [
        {
            "name": "키트 저장소가 SQLite 드라이버를 직접 부른다",
            "files": {"Source/GameFramework/Kits/Feature/Storage/Probe/ProbeStorage.cpp": "void probe()\n{\n    SQLiteDriver& driver = SQLiteDriver::getInstance();\n}\n"},
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
