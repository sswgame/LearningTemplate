"""
Scripts/generate/CookPrefabs.py

저작 기본은 XML입니다. Resource/game/<pack>/prefabs/*.prefab.xml → .prefab.bin (PFB2).
같은 이름의 .prefab.json은 XML이 없을 때만 쿠킹합니다 (도구 interchange).

바이너리 레이아웃 (리틀 엔디안, PrefabAsset::saveToBinaryFile 규격과 일치):
  u32 매직넘버 0x50464232 ('PFB2')
  u32 버전 (현재 0)
  u32 이름길이 + 이름 바이트열
  u32 XML본문길이 + XML본문 바이트열
"""

from __future__ import annotations

import concurrent.futures
import json
import os
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import getProjectRoot

kPfb2Magic = 0x50464232
kPfb2Version = 0


def readXmlPrefabInternal(path: Path) -> tuple[str, str]:
    """
    XML 포맷의 프리팹 파일을 읽어 이름(name)과 내부 요소(xmlBody)를 추출합니다.

    Args:
        path: 읽어들일 XML 파일 경로

    Returns:
        (프리팹 이름, XML 본문 문자열) 튜플
    """
    tree = ET.parse(path)
    root = tree.getroot()
    name = ""
    if (nameNode := root.find("name")) is not None and nameNode.text:
        name = nameNode.text.strip()
    elif root.get("name"):
        name = root.get("name", "")

    body = ""
    for tag in ("GameObject", "GameObjectState", "ObjectState"):
        if (node := root.find(tag)) is not None:
            body = ET.tostring(node, encoding="unicode")
            break
    if not body:
        body = path.read_text(encoding="utf-8")
    if not name:
        name = path.stem.split(".")[0]
    return name, body


def readJsonPrefabInternal(path: Path) -> tuple[str, str]:
    """
    JSON 포맷의 프리팹 파일을 읽어 이름(name)과 JSON 본문 문자열을 추출합니다.

    Args:
        path: 읽어들일 JSON 파일 경로

    Returns:
        (프리팹 이름, JSON 본문 문자열) 튜플
    """
    text = path.read_text(encoding="utf-8")
    data = json.loads(text)
    name = ""
    if isinstance(data, dict):
        name = str(data.get("Name", ""))
    elif isinstance(data, list) and len(data) > 0 and isinstance(data[0], dict):
        name = str(data[0].get("Name", ""))

    if not name:
        name = path.stem.split(".")[0]

    return name, text


def writePfb2Internal(outPath: Path, name: str, body: str) -> bool:
    """
    파싱된 프리팹 데이터를 자체적인 바이너리 형식(PFB2)으로 출력 파일에 기록합니다.
    (기존 바이너리와 동일하면 디스크 쓰기를 생략합니다.)

    Args:
        outPath: 저장할 출력 파일 경로
        name: 프리팹 이름
        body: 저장할 XML 또는 JSON 형식의 프리팹 본문

    Returns:
        새로 작성되었으면 True, 기존과 동일하면 False
    """
    nameBytes = name.encode("utf-8")
    bodyBytes = body.encode("utf-8")
    blob = (
        struct.pack("<II", kPfb2Magic, kPfb2Version)
        + struct.pack("<I", len(nameBytes))
        + nameBytes
        + struct.pack("<I", len(bodyBytes))
        + bodyBytes
    )
    if outPath.is_file() and outPath.read_bytes() == blob:
        return False

    outPath.parent.mkdir(parents=True, exist_ok=True)
    outPath.write_bytes(blob)
    return True


def cookPrefabs(resourceRoot: Path | None = None) -> int:
    """
    게임 리소스 폴더 내의 원본 프리팹 파일(.xml, .json)들을 검색하여
    바이너리(.bin) 형식으로 변환(쿠킹)합니다 (병렬 처리).
    """
    projectRoot = getProjectRoot()
    searchRoots: list[Path] = []
    if resourceRoot is not None:
        searchRoots.append(resourceRoot.resolve())
    else:
        gameRoot = projectRoot / "Resource" / "game"
        if gameRoot.is_dir():
            searchRoots.extend(sorted(path for path in gameRoot.glob("*/prefabs") if path.is_dir()))

    if not searchRoots:
        print("[CookPrefabs] Prefab directory not found under Resource/game/*/prefabs")
        return 1

    tasks: list[tuple[Path, bool]] = []
    for prefabDir in searchRoots:
        xmlSources = sorted(prefabDir.glob("*.prefab.xml"))
        jsonSources = sorted(prefabDir.glob("*.prefab.json"))
        xmlBins = {source.with_suffix(".bin") for source in xmlSources}

        for sourceFile in xmlSources:
            tasks.append((sourceFile, False))
        for sourceFile in jsonSources:
            if sourceFile.with_suffix(".bin") not in xmlBins:
                tasks.append((sourceFile, True))

    def cookOne(sourceFile: Path, isJson: bool) -> tuple[int, int]:
        if isJson:
            name, body = readJsonPrefabInternal(sourceFile)
        else:
            name, body = readXmlPrefabInternal(sourceFile)
        outputBinaryFile = sourceFile.with_suffix(".bin")
        wrote = writePfb2Internal(outputBinaryFile, name, body)
        if wrote:
            print(f"[CookPrefabs] {sourceFile.name} -> {outputBinaryFile.name} ('{name}')")
        return 1, (1 if wrote else 0)

    cookedCount = 0
    updatedCount = 0
    maxWorkers = min(32, (os.cpu_count() or 4) * 2)

    with concurrent.futures.ThreadPoolExecutor(max_workers=maxWorkers) as executor:
        futures = [executor.submit(cookOne, src, isJson) for src, isJson in tasks]
        for future in concurrent.futures.as_completed(futures):
            try:
                total, updated = future.result()
                cookedCount += total
                updatedCount += updated
            except Exception as exception:
                print(f"[CookPrefabs Error] {exception}")
                return 1

    print(f"[CookPrefabs] Done - {cookedCount} prefab(s) checked, {updatedCount} updated.")
    return 0


if __name__ == "__main__":
    raise SystemExit(cookPrefabs())
