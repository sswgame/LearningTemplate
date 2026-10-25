#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckResourceCredits.py

원본 자산은 도메인의 크레딧 표(`credits.md`)에 출처와 함께 있고, 라이선스는 CC0 급(`CC0 1.0`) 이거나 이 저장소에서 만든 것(`이 저장소`)이다.

리소스는 출처 표기 의무 · 재배포 금지가 붙지 않은 것만 들인다. 크레딧 표가 있어도 아무도 대조하지 않으면 표 밖 파일 · 규칙 밖 라이선스 철자
(`저장소와 같음` · `—`)가 생긴다 — 이 게이트가 대조한다.

대상(`Resource/` 아래):
  1) 원본 폴더(`models_raw` · `textures_raw` · `sounds` · `voice` · `heightfields_raw` · `rigs_raw` · `music` · `fonts`)의 모델 · 이미지 · 소리 · 글꼴 파일.
  2) `textures/` 의 `.dds` 가운데 `textures_raw/` 에 같은 이름의 원본이 없는 것 — 가져온 것이 아니라 들여왔거나 코드로 만든 것이다.

판정: 파일의 도메인(`Resource/game/<팩>/` 또는 `Resource/<engine|common|editor>/`)의 `credits.md` 표(`| 파일 | 원본 | 라이선스 |`)에서 첫 칸의
백틱 패턴(`→` 앞, 도메인 기준)이 맞는 줄을 찾는다. 줄이 없으면 위반, 둘 이상이면 위반(표 순서로 출처가 갈리지 않게), 마지막 칸(라이선스)이
`CC0 1.0` · `이 저장소` 가 아니면 위반, 원본이 있는 도메인에 `credits.md` 가 없으면 위반. 패턴은 경로 마디 단위다 — `*` · `?` 는 `/` 를 넘지 않고,
여러 폴더를 넘으려면 `**` 를 쓴다(새 팩 폴더의 파일이 위 폴더의 넓은 패턴에 조용히 붙지 않게).
출처를 아직 모르는 파일은 `Scripts/lint/rules/CheckResourceCredits.toml` 의 `[exemption]`(저장소 경로 → 이유)에 둔다.

출처 목록(`Resource/**/*.sources.json` — 공용 팩 `audio.sources.json` · 엔진 팩 `textures.sources.json` 의 형식)도 읽는다. 팩 `folder` 는 목록이 있는
폴더 기준이다. 적힌 파일이 없거나 SHA-256 이 다르면, 팩 폴더에 목록 밖 원본이 있으면, 라이선스가 `CC0 1.0` 이 아니면, 팩 폴더가 서로 겹치면 위반이다.

  python Scripts/lint/gate/CheckResourceCredits.py [--root <repo>]
"""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import re
import sys
from pathlib import Path, PurePosixPath

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.Search import collectRepositoryFiles  # noqa: E402
from lint.LintGate import GateResult, LintGate  # noqa: E402

_kResourceRoot = "Resource"
_kCreditsFileName = "credits.md"
_kSourceManifestSuffix = ".sources.json"
#: 출처 목록이 받는 라이선스 — 내려받은 것만 적으므로 CC0 하나다.
_kSourceManifestLicense = "CC0 1.0"
#: 원본을 두는 폴더 이름 — 이 아래의 아래 확장자가 대상이다.
_kSourceFolderNames = frozenset({"models_raw", "textures_raw", "sounds", "voice", "heightfields_raw", "rigs_raw", "music", "fonts"})
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


def matchPathPatternInternal(listPart: list[str], listPatternPart: list[str]) -> bool:
    """경로 마디 단위 맞춤 — 마디 안은 fnmatch, `**` 는 마디 0개 이상."""
    if not listPatternPart:
        return not listPart
    if listPatternPart[0] == "**":
        return any(matchPathPatternInternal(listPart[index:], listPatternPart[1:]) for index in range(len(listPart) + 1))
    return (bool(listPart) and fnmatch.fnmatchcase(listPart[0], listPatternPart[0])
            and matchPathPatternInternal(listPart[1:], listPatternPart[1:]))


def checkSourceManifestInternal(repositoryRoot: Path, manifestRelPath: str, listAllPath: list[str]) -> list[str]:
    """출처 목록 하나 — 적힌 파일의 존재 · 해시, 팩 폴더의 목록 밖 원본, 라이선스, 팩 폴더 겹침."""
    baseFolder = manifestRelPath.rsplit("/", 1)[0]
    try:
        listPack = json.loads((repositoryRoot / manifestRelPath).read_text(encoding="utf-8"))["packs"]
        listEntry = [(f"{baseFolder}/{pack['folder']}", pack["license"], [(file["name"], file["sha256"]) for file in pack["files"]])
                     for pack in listPack]
    except (ValueError, KeyError, TypeError) as error:
        return [f"{manifestRelPath}: 출처 목록을 읽지 못했습니다 ({type(error).__name__}: {error})"]
    listViolation: list[str] = []
    listFolder = [folder for folder, _, _ in listEntry]
    listViolation += [f"{manifestRelPath}: 팩 폴더 {inner} 가 팩 폴더 {outer} 와 겹칩니다"
                      for indexOuter, outer in enumerate(listFolder) for indexInner, inner in enumerate(listFolder)
                      if indexOuter != indexInner and (inner.startswith(outer + "/") or (inner == outer and indexInner > indexOuter))]
    for folder, license, listNameHash in listEntry:
        mapNameToHash = dict(listNameHash)
        if license != _kSourceManifestLicense:
            listViolation.append(f"{manifestRelPath}: {folder} 의 라이선스 '{license}' 는 {_kSourceManifestLicense} 가 아닙니다")
        if len(mapNameToHash) != len(listNameHash):
            listViolation.append(f"{manifestRelPath}: {folder} 에 같은 name 이 둘 이상입니다")
        for name, sha256 in mapNameToHash.items():
            path = repositoryRoot / folder / name
            if not path.is_file():
                listViolation.append(f"{folder}/{name}: {manifestRelPath} 에 있는데 파일이 없습니다")
            elif hashlib.sha256(path.read_bytes()).hexdigest() != sha256:
                listViolation.append(f"{folder}/{name}: SHA-256 이 {manifestRelPath} 와 다릅니다")
        listViolation += [f"{relPath}: {manifestRelPath} 의 팩 {folder} 목록 밖의 파일입니다" for relPath in listAllPath
                          if relPath.startswith(folder + "/") and PurePosixPath(relPath).suffix.lower() in _kSourceSuffixes
                          and relPath[len(folder) + 1:] not in mapNameToHash]
    return listViolation


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
            "  패턴의 * 는 폴더를 넘지 않고(넘으려면 **), 한 파일에는 한 줄만 맞아야 합니다 — 겹치면 넓은 줄을 이름으로 좁힙니다.\n"
            "  출처 목록(*.sources.json)의 해시가 다르면 받은 바이트를 확인하고 목록을 고칩니다(MakeCc0Import · MakeCommonAudio verify).\n"
            "  출처 표기 의무 · 재배포 금지가 붙은 자산은 들이지 않습니다.")
    selfTestCases = [
        {
            "name": "크레딧 표에 없는 소리",
            "expectedViolation": "Resource/game/probe/sounds/a.ogg: Resource/game/probe/credits.md 표에 없습니다",
            "files": {
                "Resource/game/probe/credits.md": "| 파일 | 원본 | 라이선스 |\n|---|---|---|\n| `sounds/b.ogg` | 저장소 | 이 저장소 |\n",
                "Resource/game/probe/sounds/a.ogg": "probe",
            },
        },
        {
            "name": "라이선스가 CC BY 인 줄",
            "expectedViolation": "Resource/game/probe/sounds/a.ogg: 라이선스 'CC BY 4.0' 는 CC0 1.0 · 이 저장소 가 아닙니다",
            "files": {
                "Resource/game/probe/credits.md": "| 파일 | 원본 | 라이선스 |\n|---|---|---|\n| `sounds/a.ogg` | 어딘가 | CC BY 4.0 |\n",
                "Resource/game/probe/sounds/a.ogg": "probe",
            },
        },
        {
            "name": "원본이 있는데 credits.md 가 없는 도메인",
            "expectedViolation": "Resource/common/models_raw/rock.glb: 도메인 Resource/common 에 credits.md 가 없습니다",
            "files": {"Resource/common/models_raw/rock.glb": "probe"},
        },
        {
            "name": "두 줄이 맞는 파일",
            "expectedViolation": "Resource/game/probe/sounds/a.ogg: Resource/game/probe/credits.md 표의 줄 2 개가 맞습니다",
            "files": {
                "Resource/game/probe/credits.md": "| 파일 | 원본 | 라이선스 |\n|---|---|---|\n| `sounds/*.ogg` | 가 | CC0 1.0 |\n| `sounds/a.ogg` | 나 | CC0 1.0 |\n",
                "Resource/game/probe/sounds/a.ogg": "probe",
            },
        },
        {
            "name": "* 가 하위 폴더를 넘지 않는다",
            "expectedViolation": "Resource/game/probe/models_raw/newpack/a.glb: Resource/game/probe/credits.md 표에 없습니다",
            "files": {
                "Resource/game/probe/credits.md": "| 파일 | 원본 | 라이선스 |\n|---|---|---|\n| `models_raw/*.glb` | 가 | CC0 1.0 |\n",
                "Resource/game/probe/models_raw/newpack/a.glb": "probe",
            },
        },
        {
            "name": "출처 목록과 해시가 다른 파일",
            "expectedViolation": "Resource/common/sounds/pack/a.ogg: SHA-256 이 Resource/common/audio.sources.json 와 다릅니다",
            "files": {
                "Resource/common/credits.md": "| 파일 | 원본 | 라이선스 |\n|---|---|---|\n| `sounds/pack/*.ogg` | 가 | CC0 1.0 |\n",
                "Resource/common/audio.sources.json": ('{"packs": [{"folder": "sounds/pack", "license": "CC0 1.0", "files": '
                                                       '[{"name": "a.ogg", "sha256": "' + "0" * 64 + '"}]}]}'),
                "Resource/common/sounds/pack/a.ogg": "probe",
            },
        },
        {
            "name": "출처 목록 밖의 파일",
            "expectedViolation": "Resource/engine/textures_raw/pack/b.png: Resource/engine/textures.sources.json 의 팩 Resource/engine/textures_raw/pack 목록 밖의 파일입니다",
            "files": {
                "Resource/engine/credits.md": "| 파일 | 원본 | 라이선스 |\n|---|---|---|\n| `textures_raw/pack/*.png` | 가 | CC0 1.0 |\n",
                "Resource/engine/textures.sources.json": '{"packs": [{"folder": "textures_raw/pack", "license": "CC0 1.0", "files": []}]}',
                "Resource/engine/textures_raw/pack/b.png": "probe",
            },
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
            listPart = relPath[len(domain) + 1:].split("/")
            listMatch = [(listPattern, licenseCell) for listPattern, licenseCell in mapDomainToRow[domain]
                         if any(matchPathPatternInternal(listPart, pattern.split("/")) for pattern in listPattern)]
            if not listMatch:
                listViolation.append(f"{relPath}: {domain}/{_kCreditsFileName} 표에 없습니다")
            elif len(listMatch) > 1:
                listViolation.append(f"{relPath}: {domain}/{_kCreditsFileName} 표의 줄 {len(listMatch)} 개가 맞습니다 — "
                                     + " / ".join(" · ".join(listPattern) for listPattern, _ in listMatch))
            elif listMatch[0][1] not in _kLicenseSpelling:
                listViolation.append(f"{relPath}: 라이선스 '{listMatch[0][1]}' 는 {' · '.join(_kLicenseSpelling)} 가 아닙니다")

        listManifest = [relPath for relPath in listAllPath if relPath.endswith(_kSourceManifestSuffix)]
        for manifestRelPath in listManifest:
            listViolation += checkSourceManifestInternal(repositoryRoot, manifestRelPath, listAllPath)
        return GateResult(listViolation=listViolation, checkedCount=countAsset,
                          summary=f"원본 자산 {countAsset} · 크레딧 표 {len(mapDomainToRow)} · 출처 목록 {len(listManifest)}")


main = CheckResourceCreditsGate.run

if __name__ == "__main__":
    sys.exit(main())
