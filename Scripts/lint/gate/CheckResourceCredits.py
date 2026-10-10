#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckResourceCredits.py

원본 자산은 도메인의 크레딧 표(`credits.md`)에 출처와 함께 있고, 라이선스는 CC0 급(`CC0 1.0`) 이거나 이 저장소에서 만든 것(`이 저장소`)이다.

리소스는 출처 표기 의무 · 재배포 금지가 붙지 않은 것만 들인다. 크레딧 표가 있어도 아무도 대조하지 않으면 표 밖 파일 · 규칙 밖 라이선스 철자
(`저장소와 같음` · `—`)가 생긴다 — 이 게이트가 대조한다.

대상(`Resource/` 아래):
  1) 원본 폴더(`models_raw` · `textures_raw` · `sounds` · `voice` · `heightfields_raw` · `music` · `fonts`)의 모델 · 이미지 · 소리 · 글꼴 파일.
  2) `textures/` 의 `.dds` 가운데 `textures_raw/` 에 같은 이름의 원본이 없는 것 — 가져온 것이 아니라 들여왔거나 코드로 만든 것이다.

판정: 파일의 도메인(`Resource/game/<팩>/` 또는 `Resource/<engine|common|editor>/`)의 `credits.md` 표(`| 파일 | 원본 | 라이선스 |`)에서 첫 칸의
백틱 패턴(`→` 앞, 도메인 기준 fnmatch)이 맞는 줄을 찾는다. 줄이 없으면 위반, 마지막 칸(라이선스)이 `CC0 1.0` · `이 저장소` 가 아니면 위반,
원본이 있는 도메인에 `credits.md` 가 없으면 위반. 출처를 아직 모르는 파일은 `Scripts/lint/rules/CheckResourceCredits.toml` 의 `[exemption]`(저장소 경로 → 이유)에 둔다.

  python Scripts/lint/gate/CheckResourceCredits.py [--root <repo>]
"""

from __future__ import annotations

import argparse
import fnmatch
import re
import sys
from pathlib import Path, PurePosixPath

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import collectRepositoryFiles  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kResourceRoot = "Resource"
_kCreditsFileName = "credits.md"
#: 원본을 두는 폴더 이름 — 이 아래의 아래 확장자가 대상이다.
_kSourceFolderNames = frozenset({"models_raw", "textures_raw", "sounds", "voice", "heightfields_raw", "music", "fonts"})
_kSourceSuffixes = frozenset({".glb", ".gltf", ".vrm", ".bin", ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".exr", ".hdr", ".psd",
                              ".ogg", ".wav", ".mp3", ".flac", ".ttf", ".otf"})
#: 라이선스 칸에 쓸 수 있는 철자 — CC0 급 하나와 이 저장소에서 만든 것 하나.
_kLicenseSpelling = ("CC0 1.0", "이 저장소")
_kBacktickRe = re.compile(r"`([^`]+)`")


def findDomainInternal(relPath: str) -> str:
    """`Resource/game/<팩>/…` → `Resource/game/<팩>`, `Resource/<domain>/…` → `Resource/<domain>`."""
    parts = relPath.split("/")
    return "/".join(parts[:3]) if len(parts) > 3 and parts[1] == "game" else "/".join(parts[:2])


def readCreditRowsInternal(text: str) -> list[tuple[list[str], str]]:
    """크레딧 표의 줄 — (첫 칸의 백틱 패턴들, 라이선스 칸)."""
    listRow: list[tuple[list[str], str]] = []
    for line in text.splitlines():
        if not line.startswith("|") or set(line.replace("|", "").strip()) <= set("-: "):
            continue
        listCell = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(listCell) < 3 or listCell[0] == "파일":
            continue
        listRow.append((_kBacktickRe.findall(listCell[0].split("→")[0]), listCell[-1]))
    return listRow


def isCreditedAssetInternal(relPath: str, setAllPath: set[str]) -> bool:
    """크레딧 표에 있어야 하는 파일인가 — 원본 폴더의 원본 파일, 또는 원본 없는 런타임 DDS."""
    parts = relPath.split("/")
    suffix = PurePosixPath(relPath).suffix.lower()
    if _kSourceFolderNames.intersection(parts[:-1]) and suffix in _kSourceSuffixes:
        return True
    if suffix != ".dds" or "textures" not in parts[:-1]:
        return False
    rawStem = relPath.replace("/textures/", "/textures_raw/", 1)[:-len(".dds")]
    return not any(path.startswith(rawStem + ".") for path in setAllPath)


class CheckResourceCreditsGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "Resource 의 원본 자산이 도메인 credits.md 에 있고 라이선스가 CC0 1.0 · 이 저장소인지 검사"
    buildComment = "Checking that source assets are listed in credits.md with an allowed license..."
    timeoutSeconds = 30
    preCommitPattern = ("Resource/*",)
    violationHeader = "크레딧 표 밖의 자산"
    hint = ("  자산의 도메인 credits.md 표에 `| \\`경로 패턴\\` | 출처(링크) | CC0 1.0 |` 한 줄을 더합니다(이 저장소에서 만든 것은 라이선스 칸에 '이 저장소').\n"
            "  출처 표기 의무 · 재배포 금지가 붙은 자산은 들이지 않습니다.")
    selfTestCases = [
        {
            "name": "크레딧 표에 없는 소리",
            "files": {
                "Resource/game/probe/credits.md": "| 파일 | 원본 | 라이선스 |\n|---|---|---|\n| `sounds/b.ogg` | 저장소 | 이 저장소 |\n",
                "Resource/game/probe/sounds/a.ogg": "probe",
            },
        },
        {
            "name": "라이선스가 CC BY 인 줄",
            "files": {
                "Resource/game/probe/credits.md": "| 파일 | 원본 | 라이선스 |\n|---|---|---|\n| `sounds/a.ogg` | 어딘가 | CC BY 4.0 |\n",
                "Resource/game/probe/sounds/a.ogg": "probe",
            },
        },
        {
            "name": "원본이 있는데 credits.md 가 없는 도메인",
            "files": {"Resource/common/models_raw/rock.glb": "probe"},
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listAllPath = [path.relative_to(repositoryRoot).as_posix()
                       for path in collectRepositoryFiles(repositoryRoot, (_kResourceRoot,), bAnySuffix=True)]
        setAllPath = set(listAllPath)
        mapDomainToRow: dict[str, list[tuple[list[str], str]]] = {}
        for relPath in listAllPath:
            if relPath.endswith("/" + _kCreditsFileName):
                text = (repositoryRoot / relPath).read_text(encoding="utf-8")
                mapDomainToRow[relPath[:-len("/" + _kCreditsFileName)]] = readCreditRowsInternal(text)

        listViolation: list[str] = []
        countAsset = 0
        for relPath in listAllPath:
            if not isCreditedAssetInternal(relPath, setAllPath):
                continue
            countAsset += 1
            if relPath in self.mapExemption:
                self.useExemption(relPath)
                continue
            domain = findDomainInternal(relPath)
            if domain not in mapDomainToRow:
                listViolation.append(f"{relPath}: 도메인 {domain} 에 {_kCreditsFileName} 가 없습니다")
                continue
            domainRelPath = relPath[len(domain) + 1:]
            license = next((licenseCell for listPattern, licenseCell in mapDomainToRow[domain]
                            if any(fnmatch.fnmatchcase(domainRelPath, pattern) for pattern in listPattern)), None)
            if license is None:
                listViolation.append(f"{relPath}: {domain}/{_kCreditsFileName} 표에 없습니다")
            elif license not in _kLicenseSpelling:
                listViolation.append(f"{relPath}: 라이선스 '{license}' 는 {' · '.join(_kLicenseSpelling)} 가 아닙니다")
        return GateResult(listViolation=listViolation, summary=f"원본 자산 {countAsset} · 크레딧 표 {len(mapDomainToRow)}")


main = CheckResourceCreditsGate.run

if __name__ == "__main__":
    sys.exit(main())
