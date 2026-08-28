"""
Scripts/generate/CookScenes.py

저작 기본은 XML입니다. Resource/**/scenes/*.scene.xml -> .scene.bin (SCN1).

바이너리 레이아웃 (리틀 엔디안, SceneDescriptor::saveSceneDescriptorToBinary 규격과 일치):
  u32 매직넘버 0x53434E31 ('SCN1')
  u32 버전 (현재 0)
  u32 씬이름길이 + 씬이름 바이트열
  u32 엔티티 개수
  각 엔티티:
    u32 엔티티이름길이 + 이름 바이트열
    u32 프리팹경로길이 + 경로 바이트열
    u32 임베디드XML길이 + XML본문 바이트열
"""

from __future__ import annotations

import concurrent.futures
import os
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import getProjectRoot

kScn1Magic = 0x53434E31
kScn1Version = 0


def readXmlSceneInternal(path: Path) -> tuple[str, list[tuple[str, str, str]]]:
    """
    XML 포맷의 씬 파일을 읽어 씬 이름과 엔티티 목록을 추출합니다.

    Args:
        path: 읽어들일 XML 파일 경로

    Returns:
        (씬 이름, [(엔티티이름, 프리팹경로, 임베디드XML), ...]) 튜플
    """
    tree = ET.parse(path)
    root = tree.getroot()
    sceneName = root.get("name", "")
    if (nameNode := root.find("name")) is not None and nameNode.text:
        sceneName = nameNode.text.strip()
    if not sceneName:
        sceneName = path.stem.split(".")[0]

    entitiesList: list[tuple[str, str, str]] = []
    if (entitiesNode := root.find("entities")) is not None:
        for entityNode in entitiesNode.findall("entity"):
            entityName = entityNode.get("name", "")
            if (entNameChild := entityNode.find("name")) is not None and entNameChild.text:
                entityName = entNameChild.text.strip()
            if not entityName:
                entityName = "Entity"

            prefabPath = entityNode.get("prefab", "")
            if (prefabChild := entityNode.find("prefab")) is not None and prefabChild.text:
                prefabPath = prefabChild.text.strip()

            embeddedXml = ""
            if (stateNode := entityNode.find("GameObject")) is not None:
                embeddedXml = ET.tostring(stateNode, encoding="unicode")
            elif (stateNode := entityNode.find("GameObjectState")) is not None:
                embeddedXml = ET.tostring(stateNode, encoding="unicode")

            entitiesList.append((entityName, prefabPath, embeddedXml))

    return sceneName, entitiesList


def writeScn1Internal(outputPath: Path, sceneName: str, entitiesList: list[tuple[str, str, str]]) -> bool:
    """
    파싱된 씬 데이터를 자체적인 바이너리 형식(SCN1)으로 출력 파일에 기록합니다.
    (기존 바이너리와 동일하면 디스크 쓰기를 생략합니다.)

    Args:
        outputPath: 저장할 출력 파일 경로
        sceneName: 씬 이름
        entitiesList: [(엔티티이름, 프리팹경로, 임베디드XML), ...] 목록

    Returns:
        새로 작성되었으면 True, 기존과 동일하면 False
    """
    nameBytes = sceneName.encode("utf-8")
    chunks = [
        struct.pack("<II", kScn1Magic, kScn1Version),
        struct.pack("<I", len(nameBytes)),
        nameBytes,
        struct.pack("<I", len(entitiesList)),
    ]

    for entityName, prefabPath, embeddedXml in entitiesList:
        entityNameBytes = entityName.encode("utf-8")
        prefabBytes = prefabPath.encode("utf-8")
        xmlBytes = embeddedXml.encode("utf-8")
        chunks.extend([
            struct.pack("<I", len(entityNameBytes)),
            entityNameBytes,
            struct.pack("<I", len(prefabBytes)),
            prefabBytes,
            struct.pack("<I", len(xmlBytes)),
            xmlBytes,
        ])

    blob = b"".join(chunks)
    if outputPath.is_file() and outputPath.read_bytes() == blob:
        return False

    outputPath.parent.mkdir(parents=True, exist_ok=True)
    outputPath.write_bytes(blob)
    return True


def cookScenes(resourceRoot: Path | None = None) -> int:
    """
    지정된 리소스 루트 내의 모든 .scene.xml 파일을 .scene.bin으로 변환합니다 (병렬 처리).

    Args:
        resourceRoot: 리소스 루트 경로 (None일 경우 프로젝트 루트/Resource 사용)

    Returns:
        성공 시 0
    """
    root = resourceRoot or (getProjectRoot() / "Resource")
    if not root.is_dir():
        print(f"[CookScenes] Resource dir not found: {root}")
        return 0

    sceneFiles = sorted(root.rglob("*.scene.xml"))
    if not sceneFiles:
        print(f"[CookScenes] No .scene.xml found under {root}")
        return 0

    def cookOneScene(xmlPath: Path) -> tuple[int, int]:
        outputBinaryFile = xmlPath.with_suffix("").with_suffix(".bin")
        sceneName, entitiesList = readXmlSceneInternal(xmlPath)
        wrote = writeScn1Internal(outputBinaryFile, sceneName, entitiesList)
        if wrote:
            print(f"[CookScenes] Cooked {xmlPath.relative_to(root)} -> {outputBinaryFile.name} ({len(entitiesList)} entities)")
        return 1, (1 if wrote else 0)

    cookedCount = 0
    updatedCount = 0
    maxWorkers = min(32, (os.cpu_count() or 4) * 2)

    with concurrent.futures.ThreadPoolExecutor(max_workers=maxWorkers) as executor:
        futures = [executor.submit(cookOneScene, xmlPath) for xmlPath in sceneFiles]
        for future in concurrent.futures.as_completed(futures):
            try:
                total, updated = future.result()
                cookedCount += total
                updatedCount += updated
            except Exception as exception:
                print(f"[CookScenes Error] {exception}")
                return 1

    print(f"[CookScenes] Finished checking {cookedCount} scenes, {updatedCount} updated.")
    return 0


def main() -> int:
    resourceRoot = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else None
    return cookScenes(resourceRoot)


if __name__ == "__main__":
    raise SystemExit(main())
