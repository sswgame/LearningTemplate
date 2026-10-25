#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
공용 팩의 CC0 오디오(`Resource/common/sounds/` · `Resource/common/music/`)를 출처 목록 `Resource/common/audio.sources.json` 과 대조하거나 다시 받습니다.

목록이 정본이다. 팩마다 공식 페이지(`page`) · 받는 주소(`download`) · 라이선스(`license` — `CC0 1.0` 만) · 라이선스 원문 인용(`license_quote`)을,
파일마다 저장소 이름(`name`) · 압축 안 경로(`member`, 압축일 때만) · 저장소 ogg 의 SHA-256(`sha256`)을 적는다.
원본이 wav · mp3 인 팩은 `convert`(인코더 · 버전 · Vorbis 품질)를 적고, 파일마다 원본 바이트의 SHA-256(`source_sha256`)을 함께 적는다.

  verify  내려받지 않는다. 목록의 라이선스 · 주소 호스트가 허용 범위인지, 저장소 파일이 목록과 같은 바이트(SHA-256)이고 Ogg Vorbis 인지,
          두 폴더에 목록 밖 파일이 없는지 본다. 변환을 다시 하지 않는다.
  fetch   공식 페이지에 라이선스 표시(`page_license_marker`)가 있는지 본 뒤 받는다. 원본 해시(변환 팩은 `source_sha256`)가 다르면 멈춘다.
          변환 팩은 python-soundfile 로 Ogg Vorbis 를 만들고, 결과가 목록의 `sha256` 과 같을 때만 쓴다.

변환(`convert`):
  - python-soundfile(BSD-3, 안의 libsndfile 은 LGPL)은 개발 도구로만 쓴다. 저장소에는 결과 ogg 만 들어간다. 없으면 설치 안내와 함께 멈춘다.
  - Vorbis 품질은 `compression_level` 로 준다. libsndfile 은 Vorbis 품질을 `1 - compression_level` 로 정한다(0.6 → q0.4, 곧 oggenc -q4).
  - 같은 바이트를 다시 만들기 위해 Ogg 논리 스트림 일련번호를 원본 해시 앞 8 자리로 고정한다. libsndfile 은 이 번호를 난수로 골라
    같은 입력에서도 매번 다른 파일을 쓴다. 번호를 바꾼 뒤 페이지마다 CRC 를 다시 계산한다.
  - 설치된 soundfile · libsndfile 버전이 목록과 다르면 결과 바이트가 달라질 수 있어 멈춘다. 버전을 올리려면 목록의 버전과 해시를 함께 고친다.

주의: 저장소에는 Ogg Vorbis 만 둔다(wav · mp3 원본을 두지 않는다). 받는 곳은 https 의 kenney.nl · opengameart.org 만이다.
압축은 메모리에서 목록의 `member` 만 읽고 풀지 않는다.
주의: soundfile 0.14 · libsndfile 1.2.2 에서 1 분이 넘는 스테레오를 `write` 한 번에 넘기면 Vorbis 인코더가 스택 넘침(0xC00000FD)으로 프로세스를 죽인다.
`_kEncodeBlockFrames` 프레임씩 나눠 쓴다.

사용법:
  py -3 -I Scripts/dev/MakeCommonAudio.py verify
  <soundfile 이 있는 파이썬> Scripts/dev/MakeCommonAudio.py fetch
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import pathlib
import struct
import sys
import urllib.parse
import urllib.request
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))   # Scripts
from common.Paths import getProjectRoot  # noqa: E402 — import 하면 콘솔이 UTF-8 이 된다
from common.ExitCode import ExitCode  # noqa: E402

_kRepositoryRoot = getProjectRoot()
_kCommonRoot = _kRepositoryRoot / "Resource" / "common"
_kManifestPath = _kCommonRoot / "audio.sources.json"
#: 목록이 다루는 폴더(공용 팩 기준) — 이 아래의 파일은 모두 목록에 있어야 한다.
_kAudioFolderNames = ("sounds", "music")
_kAllowedLicense = "CC0 1.0"
_kAllowedHosts = frozenset({"kenney.nl", "opengameart.org"})
_kTimeoutSeconds = 120
#: 인코더에 한 번에 넘기는 프레임 수 — 머리말의 스택 넘침 주의.
_kEncodeBlockFrames = 4096
_kSoundFileInstallHint = ("python-soundfile 이 없습니다. 저장소 밖 가상환경에 설치하고 그 파이썬으로 돌립니다:\n"
                          "  py -3 -m venv <임시 폴더>/sfvenv\n"
                          "  <임시 폴더>/sfvenv/Scripts/python.exe -m pip install soundfile numpy")
#: Ogg 페이지 머리의 고정 길이와 필드 위치(RFC 3533).
_kOggPageHeaderSize = 27
_kOggSerialOffset = 14
_kOggCrcOffset = 22
_kOggSegmentCountOffset = 26


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


def makeOggCrcTableInternal() -> list[int]:
    listCrc: list[int] = []
    for index in range(256):
        crc = index << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if crc & 0x80000000 else (crc << 1) & 0xFFFFFFFF
        listCrc.append(crc)
    return listCrc


_kOggCrcTable = makeOggCrcTableInternal()


def computeOggCrcInternal(page: bytes) -> int:
    crc = 0
    for value in page:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ _kOggCrcTable[((crc >> 24) & 0xFF) ^ value]
    return crc


def fixOggSerialInternal(data: bytes, serial: int) -> bytes:
    """모든 Ogg 페이지의 스트림 일련번호를 `serial` 로 바꾸고 CRC 를 다시 계산한다(한 논리 스트림 파일만)."""
    result = bytearray(data)
    offset = 0
    while offset < len(result):
        if result[offset:offset + 4] != b"OggS":
            raise ValueError(f"Ogg 페이지 머리가 아닙니다(오프셋 {offset})")
        countSegment = result[offset + _kOggSegmentCountOffset]
        pageSize = _kOggPageHeaderSize + countSegment + sum(result[offset + _kOggPageHeaderSize:offset + _kOggPageHeaderSize + countSegment])
        struct.pack_into("<I", result, offset + _kOggSerialOffset, serial)
        struct.pack_into("<I", result, offset + _kOggCrcOffset, 0)
        struct.pack_into("<I", result, offset + _kOggCrcOffset, computeOggCrcInternal(bytes(result[offset:offset + pageSize])))
        offset += pageSize
    return bytes(result)


def importSoundFileInternal(convert: dict) -> object | None:
    """soundfile 모듈 — 없거나 버전이 목록과 다르면 이유를 출력하고 None."""
    try:
        import soundfile
    except ImportError:
        print(_kSoundFileInstallHint)
        return None
    installed = (soundfile.__version__, soundfile.__libsndfile_version__)
    listed = (convert["soundfile_version"], convert["libsndfile_version"])
    if installed != listed:
        print(f"soundfile · libsndfile 버전 {installed} 이 목록 {listed} 과 다릅니다 — 결과 바이트가 달라질 수 있어 멈춥니다")
        return None
    return soundfile


def convertToVorbisInternal(soundfile: object, source: bytes, convert: dict, sourceSha256: str) -> bytes:
    """wav · mp3 바이트 → 일련번호를 고정한 Ogg Vorbis 바이트."""
    samples, sampleRate = soundfile.read(io.BytesIO(source), always_2d=True)
    output = io.BytesIO()
    with soundfile.SoundFile(output, "w", sampleRate, samples.shape[1], format="OGG", subtype="VORBIS",
                             compression_level=convert["compression_level"]) as writer:
        for start in range(0, len(samples), _kEncodeBlockFrames):
            writer.write(samples[start:start + _kEncodeBlockFrames])
    return fixOggSerialInternal(output.getvalue(), int(sourceSha256[:8], 16))


def listPackProblemInternal(pack: dict) -> list[str]:
    listProblem: list[str] = []
    if pack["license"] != _kAllowedLicense:
        listProblem.append(f"{pack['folder']}: 라이선스 '{pack['license']}' 는 {_kAllowedLicense} 가 아닙니다")
    for url in (pack["page"], pack["download"]):
        reason = checkUrlInternal(url)
        if reason:
            listProblem.append(f"{pack['folder']}: {reason}")
    bArchive = pack["download"].lower().endswith(".zip")
    bConvert = "convert" in pack
    for entry in pack["files"]:
        if bArchive != ("member" in entry):
            listProblem.append(f"{pack['folder']}/{entry['name']}: 압축이면 member 가 있어야 하고, 단일 파일이면 없어야 합니다")
        if bConvert != ("source_sha256" in entry):
            listProblem.append(f"{pack['folder']}/{entry['name']}: 변환 팩이면 source_sha256 이 있어야 하고, 아니면 없어야 합니다")
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
    print(f"[MakeCommonAudio] 팩 {len(manifest['packs'])} · 파일 {len(setListed)} · 문제 {len(listProblem)}")
    return 1 if listProblem else 0


def downloadInternal(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "MakeCommonAudio"})
    with urllib.request.urlopen(request, timeout=_kTimeoutSeconds) as response:
        return response.read()


def runFetch(manifest: dict) -> int:
    listProblem = [problem for pack in manifest["packs"] for problem in listPackProblemInternal(pack)]
    if listProblem:
        print("\n".join(listProblem))
        return ExitCode.Violation
    if any(importSoundFileInternal(pack["convert"]) is None for pack in manifest["packs"] if "convert" in pack):
        return ExitCode.Violation   # 받기 전에 변환 도구부터 확인한다 — 반쯤 쓴 상태로 멈추지 않게
    for pack in manifest["packs"]:
        soundfile = importSoundFileInternal(pack["convert"]) if "convert" in pack else None
        if "convert" in pack and soundfile is None:
            return ExitCode.Violation
        page = downloadInternal(pack["page"]).decode("utf-8", errors="replace")
        if pack["page_license_marker"] not in page:
            print(f"{pack['page']}: 라이선스 표시 '{pack['page_license_marker']}' 가 페이지에 없습니다 — 받지 않습니다")
            return ExitCode.Violation
        payload = downloadInternal(pack["download"])
        archive = zipfile.ZipFile(io.BytesIO(payload)) if "member" in pack["files"][0] else None
        for entry in pack["files"]:
            data = archive.read(entry["member"]) if archive else payload
            label = f"{pack['download']} {entry.get('member', '')}"
            if soundfile is not None:
                if hashlib.sha256(data).hexdigest() != entry["source_sha256"]:
                    print(f"{label}: 원본 SHA-256 이 목록과 다릅니다 — 쓰지 않습니다")
                    return ExitCode.Violation
                data = convertToVorbisInternal(soundfile, data, pack["convert"], entry["source_sha256"])
            if hashlib.sha256(data).hexdigest() != entry["sha256"]:
                print(f"{label}: SHA-256 이 목록과 다릅니다 — 쓰지 않습니다")
                return ExitCode.Violation
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
