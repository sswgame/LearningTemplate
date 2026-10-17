#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
공용 팩의 CC0 오디오(`Resource/common/sounds/` · `Resource/common/music/`)를 출처 목록 `Resource/common/audio.sources.json` 과 대조하거나 다시 받습니다.

목록이 정본이다. 팩마다 공식 페이지(`page`) · 받는 주소(`download`) · 라이선스(`license` — `CC0 1.0` 만) · 라이선스 원문 인용(`license_quote`)을,
파일마다 저장소 이름(`name`) · 압축 안 경로(`member`, 압축일 때만) · SHA-256 을 적는다.

  verify  내려받지 않는다. 목록의 라이선스 · 주소 호스트가 허용 범위인지, 저장소 파일이 목록과 같은 바이트(SHA-256)이고 Ogg Vorbis 인지,
          두 폴더에 목록 밖 파일이 없는지 본다.
  fetch   공식 페이지에 라이선스 표시(`page_license_marker`)가 있는지 본 뒤 받아서, 해시가 목록과 같을 때만 쓴다. 다르면 쓰지 않고 멈춘다.

주의: 저장소에는 Ogg Vorbis 만 둔다(wav · mp3 원본을 두지 않는다). 받는 곳은 https 의 kenney.nl · opengameart.org 만이다.
압축은 메모리에서 목록의 `member` 만 읽고 풀지 않는다.

사용법:
  py -3 -I Scripts/dev/MakeCommonAudio.py verify
  py -3 -I Scripts/dev/MakeCommonAudio.py fetch
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import pathlib
import sys
import urllib.parse
import urllib.request
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))   # Scripts
from common import getProjectRoot  # noqa: E402 — import 하면 콘솔이 UTF-8 이 된다

_kRepositoryRoot = getProjectRoot()
_kCommonRoot = _kRepositoryRoot / "Resource" / "common"
_kManifestPath = _kCommonRoot / "audio.sources.json"
#: 목록이 다루는 폴더(공용 팩 기준) — 이 아래의 파일은 모두 목록에 있어야 한다.
_kAudioFolderNames = ("sounds", "music")
_kAllowedLicense = "CC0 1.0"
_kAllowedHosts = frozenset({"kenney.nl", "opengameart.org"})
_kTimeoutSeconds = 120


def readManifestInternal() -> dict:
    return json.loads(_kManifestPath.read_text(encoding="utf-8"))


def checkUrlInternal(url: str) -> str | None:
    """https 이고 허용 호스트면 None, 아니면 이유."""
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme != "https" or parsed.hostname not in _kAllowedHosts:
        return f"허용 밖의 주소: {url}"
    return None


def checkVorbisInternal(data: bytes) -> bool:
    """Ogg 컨테이너의 첫 패킷이 Vorbis 식별 헤더인가."""
    return data[:4] == b"OggS" and b"\x01vorbis" in data[:64]


def listPackProblemInternal(pack: dict) -> list[str]:
    listProblem: list[str] = []
    if pack["license"] != _kAllowedLicense:
        listProblem.append(f"{pack['folder']}: 라이선스 '{pack['license']}' 는 {_kAllowedLicense} 가 아닙니다")
    for url in (pack["page"], pack["download"]):
        reason = checkUrlInternal(url)
        if reason:
            listProblem.append(f"{pack['folder']}: {reason}")
    bArchive = pack["download"].lower().endswith(".zip")
    for entry in pack["files"]:
        if bArchive != ("member" in entry):
            listProblem.append(f"{pack['folder']}/{entry['name']}: 압축이면 member 가 있어야 하고, 단일 파일이면 없어야 합니다")
        if not entry["name"].endswith(".ogg"):
            listProblem.append(f"{pack['folder']}/{entry['name']}: .ogg 만 둡니다")
    if not bArchive and len(pack["files"]) != 1:
        listProblem.append(f"{pack['folder']}: 단일 파일 다운로드인데 파일이 {len(pack['files'])} 개입니다")
    return listProblem


def runVerify(manifest: dict) -> int:
    listProblem: list[str] = []
    setListed: set[pathlib.Path] = set()
    for pack in manifest["packs"]:
        listProblem += listPackProblemInternal(pack)
        for entry in pack["files"]:
            path = _kCommonRoot / pack["folder"] / entry["name"]
            setListed.add(path)
            if not path.is_file():
                listProblem.append(f"{path.relative_to(_kRepositoryRoot).as_posix()}: 파일이 없습니다")
                continue
            data = path.read_bytes()
            if hashlib.sha256(data).hexdigest() != entry["sha256"]:
                listProblem.append(f"{path.relative_to(_kRepositoryRoot).as_posix()}: SHA-256 이 목록과 다릅니다")
            if not checkVorbisInternal(data):
                listProblem.append(f"{path.relative_to(_kRepositoryRoot).as_posix()}: Ogg Vorbis 가 아닙니다")
    for folderName in _kAudioFolderNames:
        for path in sorted((_kCommonRoot / folderName).rglob("*")):
            if path.is_file() and path not in setListed:
                listProblem.append(f"{path.relative_to(_kRepositoryRoot).as_posix()}: 출처 목록에 없습니다")
    for problem in listProblem:
        print(problem)
    countFile = len(setListed)
    print(f"[MakeCommonAudio] 팩 {len(manifest['packs'])} · 파일 {countFile} · 문제 {len(listProblem)}")
    return 1 if listProblem else 0


def downloadInternal(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "MakeCommonAudio"})
    with urllib.request.urlopen(request, timeout=_kTimeoutSeconds) as response:
        return response.read()


def runFetch(manifest: dict) -> int:
    listProblem = [problem for pack in manifest["packs"] for problem in listPackProblemInternal(pack)]
    if listProblem:
        print("\n".join(listProblem))
        return 1
    for pack in manifest["packs"]:
        page = downloadInternal(pack["page"]).decode("utf-8", errors="replace")
        if pack["page_license_marker"] not in page:
            print(f"{pack['page']}: 라이선스 표시 '{pack['page_license_marker']}' 가 페이지에 없습니다 — 받지 않습니다")
            return 1
        payload = downloadInternal(pack["download"])
        archive = zipfile.ZipFile(io.BytesIO(payload)) if "member" in pack["files"][0] else None
        for entry in pack["files"]:
            data = archive.read(entry["member"]) if archive else payload
            if hashlib.sha256(data).hexdigest() != entry["sha256"]:
                print(f"{pack['download']} {entry.get('member', '')}: SHA-256 이 목록과 다릅니다 — 쓰지 않습니다")
                return 1
            path = _kCommonRoot / pack["folder"] / entry["name"]
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        print(f"[MakeCommonAudio] {pack['folder']}: {len(pack['files'])} 개")
    return runVerify(manifest)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="공용 팩 CC0 오디오를 출처 목록과 대조(verify)하거나 다시 받는다(fetch).")
    parser.add_argument("mode", choices=("verify", "fetch"))
    args = parser.parse_args(argv)
    manifest = readManifestInternal()
    return runVerify(manifest) if args.mode == "verify" else runFetch(manifest)


if __name__ == "__main__":
    sys.exit(main())
