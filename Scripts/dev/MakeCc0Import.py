#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
엔진 팩의 CC0 텍스처 원본(`Resource/engine/textures_raw/` 의 `sky` · `surfaces` · `ui/kenney_*` · `particles` · `pixel`)을 출처 목록
`Resource/engine/textures.sources.json` 과 대조하거나 다시 받습니다.

목록이 정본이다(`Scripts/dev/MakeCommonAudio.py` 의 오디오 목록 `audio.sources.json` 과 같은 형식 · 칸, `folder` 는 목록이 있는 도메인 기준). 팩마다 공식 페이지(`page`) · 페이지의 라이선스 표시(`page_license_marker`) ·
라이선스(`license` — `CC0 1.0` 만) · 라이선스 근거(`license_source` — 묶음 안 파일 이름 또는 `page`) · 원문 인용(`license_quote`)을,
묶음이면 받는 주소(`download`)와 그 바이트의 SHA-256(`download_sha256`)을 적는다. 파일마다 저장소 이름(`name`, 팩 `folder` 기준) ·
묶음 안 경로(`member`) 또는 받는 주소(`download`) · 저장소 파일의 SHA-256(`sha256`)을 적는다.

  verify  내려받지 않는다. 목록의 라이선스 · 주소 호스트가 허용 범위인지, 저장소 파일이 목록과 같은 바이트(SHA-256)인지,
          팩 폴더에 목록 밖 이미지가 없는지 본다.
  fetch   공식 페이지에 라이선스 표시가 있는지 보고 받는다. 묶음 해시 · 라이선스 인용 · 파일 해시가 목록과 다르면 아무것도 쓰기 전에 멈춘다.

CI 린트가 돌리는 스크립트가 아니라 사람이 가끔 돌리는 도구다. 해시 · 목록 밖 파일 대조는 린트 `CheckResourceCredits` 도 모든 `*.sources.json` 에 한다.
변환: 없다. 원본 바이트를 그대로 쓴다(HDRI 는 2K · 재질은 1K 를 그 크기로 받는다). 크기를 줄여야 하면 받는 해상도를 바꾸고 목록의 해시를 새로 적는다.
목록의 키 규칙은 `ResourceDataSchemaTest` 의 출처 목록 종류(`*.sources.json`)가 본다 — 모르는 키는 오류다.
주의: 받는 곳은 https 의 polyhaven.com · dl.polyhaven.org · kenney.nl · opengameart.org 만이다. 묶음은 메모리에서 목록의 `member` 만 읽고 풀지 않는다
(같이 든 Unity 패키지 · 바로가기 파일은 디스크에 닿지 않는다). 쓴 원본은 `App --import-textures` 로 DDS 와 `import.stamp` 를 만든다.

사용법:
  py -3 -I Scripts/dev/MakeCc0Import.py verify
  py -3 -I Scripts/dev/MakeCc0Import.py fetch
"""

from __future__ import annotations

import argparse
import hashlib
import html
import io
import json
import pathlib
import posixpath
import re
import sys
import urllib.parse
import urllib.request
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))   # Scripts
from common import getProjectRoot  # noqa: E402 — import 하면 콘솔이 UTF-8 이 된다

_kRepositoryRoot = getProjectRoot()
_kEngineRoot = _kRepositoryRoot / "Resource" / "engine"
_kManifestPath = _kEngineRoot / "textures.sources.json"
_kAllowedLicense = "CC0 1.0"
_kAllowedHosts = frozenset({"polyhaven.com", "dl.polyhaven.org", "kenney.nl", "opengameart.org"})
#: 목록이 다루는 원본 확장자 — 팩 폴더의 이 확장자 파일은 모두 목록에 있어야 한다.
_kImageSuffixes = frozenset({".png", ".jpg", ".hdr"})
_kResourceNameRe = re.compile(r"[a-z0-9_./-]+")
_kTimeoutSeconds = 120
_kUserAgent = "LearningTemplate-MakeCc0Import"


def readManifestInternal() -> dict:
    return json.loads(_kManifestPath.read_text(encoding="utf-8"))


def checkUrlInternal(url: str) -> str | None:
    """https 이고 허용 호스트면 None, 아니면 이유."""
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme != "https" or parsed.hostname not in _kAllowedHosts:
        return f"허용 밖의 주소: {url}"
    return None


def fetchBytesInternal(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": _kUserAgent})
    with urllib.request.urlopen(request, timeout=_kTimeoutSeconds) as response:
        return response.read()


def normalizeTextInternal(text: str) -> str:
    """태그를 걷고 공백을 하나로 접는다 — 표시 · 인용을 원문에서 찾기 위해."""
    return re.sub(r"\s+", " ", html.unescape(re.sub(r"<[^>]+>", " ", text))).strip()


def sha256Internal(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def checkManifestInternal(manifest: dict) -> list[str]:
    """내려받지 않고 볼 수 있는 목록 규칙 — 라이선스 · 주소 · 이름."""
    listError: list[str] = []
    for pack in manifest["packs"]:
        folder = pack["folder"]
        if pack["license"] != _kAllowedLicense:
            listError.append(f"{folder}: 라이선스 '{pack['license']}' 는 {_kAllowedLicense} 가 아닙니다")
        listUrl = [pack["page"]] + ([pack["download"]] if "download" in pack else []) + [file["download"] for file in pack["files"] if "download" in file]
        listError += [f"{folder}: {reason}" for reason in map(checkUrlInternal, listUrl) if reason is not None]
        for file in pack["files"]:
            relPath = f"{folder}/{file['name']}"
            if _kResourceNameRe.fullmatch(relPath) is None or ".." in relPath.split("/"):
                listError.append(f"{relPath}: Resource 이름 규칙([a-z0-9_.-])에 맞지 않습니다")
            if posixpath.splitext(file["name"])[1] not in _kImageSuffixes:
                listError.append(f"{relPath}: 텍스처 원본({sorted(_kImageSuffixes)})이 아닙니다")
            if ("member" in file) == ("download" in file) or ("member" in file) != ("download" in pack):
                listError.append(f"{relPath}: 묶음 팩은 member, 낱개 팩은 download 하나만 적습니다")
    return listError


def verify(manifest: dict) -> list[str]:
    listError = checkManifestInternal(manifest)
    for pack in manifest["packs"]:
        folderPath = _kEngineRoot / pack["folder"]
        setListed = {file["name"] for file in pack["files"]}
        for file in pack["files"]:
            path = folderPath / file["name"]
            if not path.is_file():
                listError.append(f"{pack['folder']}/{file['name']}: 파일이 없습니다")
            elif sha256Internal(path.read_bytes()) != file["sha256"]:
                listError.append(f"{pack['folder']}/{file['name']}: SHA-256 이 목록과 다릅니다")
        for path in sorted(folderPath.rglob("*")):
            relName = path.relative_to(folderPath).as_posix()
            if path.is_file() and path.suffix in _kImageSuffixes and relName not in setListed:
                listError.append(f"{pack['folder']}/{relName}: 목록 밖의 파일입니다")
    return listError


def fetchPackInternal(pack: dict, mapPageText: dict[str, str]) -> list[tuple[pathlib.Path, bytes]]:
    folder = pack["folder"]
    if pack["page"] not in mapPageText:
        mapPageText[pack["page"]] = normalizeTextInternal(fetchBytesInternal(pack["page"]).decode("utf-8"))
    if normalizeTextInternal(pack["page_license_marker"]) not in mapPageText[pack["page"]]:
        raise ValueError(f"{folder}: 페이지 {pack['page']} 에 라이선스 표시 '{pack['page_license_marker']}' 가 없습니다")
    archive: zipfile.ZipFile | None = None
    if "download" in pack:
        data = fetchBytesInternal(pack["download"])
        if sha256Internal(data) != pack["download_sha256"]:
            raise ValueError(f"{folder}: 묶음 {pack['download']} 의 SHA-256 이 목록과 다릅니다")
        archive = zipfile.ZipFile(io.BytesIO(data))
    if pack["license_source"] == "page":
        evidence = mapPageText[pack["page"]]
    else:
        if archive is None:
            raise ValueError(f"{folder}: 낱개 팩의 라이선스 근거는 page 입니다")
        evidence = normalizeTextInternal(archive.read(pack["license_source"]).decode("utf-8"))
    if normalizeTextInternal(pack["license_quote"]) not in evidence:
        raise ValueError(f"{folder}: 라이선스 인용이 {pack['license_source']} 원문에 없습니다")
    listOutput: list[tuple[pathlib.Path, bytes]] = []
    for file in pack["files"]:
        data = archive.read(file["member"]) if archive is not None else fetchBytesInternal(file["download"])
        if sha256Internal(data) != file["sha256"]:
            raise ValueError(f"{folder}/{file['name']}: 받은 바이트의 SHA-256 이 목록과 다릅니다")
        listOutput.append((_kEngineRoot / folder / file["name"], data))
    return listOutput


def fetch(manifest: dict) -> list[str]:
    listError = checkManifestInternal(manifest)
    if listError:
        return listError
    mapPageText: dict[str, str] = {}
    listOutput: list[tuple[pathlib.Path, bytes]] = []
    try:
        for pack in manifest["packs"]:
            listOutput += fetchPackInternal(pack, mapPageText)
    except ValueError as error:
        return [str(error)]
    for path, data in listOutput:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    print(f"[MakeCc0Import] 팩 {len(manifest['packs'])} · 파일 {len(listOutput)} · {sum(len(data) for _, data in listOutput) / 1e6:.1f} MB")
    return []


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="엔진 팩 CC0 텍스처 원본을 출처 목록(textures.sources.json)과 대조하거나 다시 받는다")
    parser.add_argument("command", choices=("verify", "fetch"), help="verify: 내려받지 않고 대조 · fetch: 받아서 대조한 뒤 쓴다")
    args = parser.parse_args(argv)
    manifest = readManifestInternal()
    listError = verify(manifest) if args.command == "verify" else fetch(manifest)
    for error in listError:
        print(f"[MakeCc0Import] {error}", file=sys.stderr)
    if not listError and args.command == "verify":
        print(f"[MakeCc0Import] OK (팩 {len(manifest['packs'])} · 파일 {sum(len(pack['files']) for pack in manifest['packs'])})")
    return 1 if listError else 0


if __name__ == "__main__":
    sys.exit(main())
