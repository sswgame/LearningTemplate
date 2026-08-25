"""
Scripts/generate/CookScenes.py

저작 기본은 XML입니다. Resource/**/scenes/*.scene.xml -> .scene.bin (SCN1).

Binary layout (little-endian, matches SceneDescriptor::saveSceneDescriptorToBinary):
  u32 magic 0x53434E31 ('SCN1')
  u32 version (currently 0)
  u32 nameLen + name bytes
  u32 entityCount
  for each entity:
    u32 nameLen + name bytes
    u32 prefabLen + prefab bytes
    u32 embeddedXmlLen + embeddedXml bytes
"""

from __future__ import annotations

import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ConfigHelper import getProjectRoot

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
    nameNode = root.find("name")
    if nameNode is not None and nameNode.text:
        sceneName = nameNode.text.strip()
    if not sceneName:
        sceneName = path.stem.split(".")[0]

    entitiesList: list[tuple[str, str, str]] = []
    entitiesNode = root.find("entities")
    if entitiesNode is not None:
        for entityNode in entitiesNode.findall("entity"):
            entName = entityNode.get("name", "")
            entNameChild = entityNode.find("name")
            if entNameChild is not None and entNameChild.text:
                entName = entNameChild.text.strip()
            if not entName:
                entName = "Entity"

            prefabPath = entityNode.get("prefab", "")
            prefabChild = entityNode.find("prefab")
            if prefabChild is not None and prefabChild.text:
                prefabPath = prefabChild.text.strip()

            embeddedXml = ""
            stateNode = entityNode.find("GameObjectState")
            if stateNode is not None:
                embeddedXml = ET.tostring(stateNode, encoding="unicode")

            entitiesList.append((entName, prefabPath, embeddedXml))

    return sceneName, entitiesList


def writeScn1Internal(outPath: Path, sceneName: str, entitiesList: list[tuple[str, str, str]]) -> None:
    """
    파싱된 씬 데이터를 자체 바이너리 형식(SCN1)으로 출력 파일에 기록합니다.

    Args:
        outPath: 저장할 출력 파일 경로
        sceneName: 씬 이름
        entitiesList: 엔티티 튜플 리스트
    """
    nameBytes = sceneName.encode("utf-8")
    blob = struct.pack("<I", kScn1Magic)
    blob += struct.pack("<I", kScn1Version)
    blob += struct.pack("<I", len(nameBytes)) + nameBytes
    blob += struct.pack("<I", len(entitiesList))

    for entName, prefabPath, embeddedXml in entitiesList:
        entBytes = entName.encode("utf-8")
        prefabBytes = prefabPath.encode("utf-8")
        xmlBytes = embeddedXml.encode("utf-8")
        blob += struct.pack("<I", len(entBytes)) + entBytes
        blob += struct.pack("<I", len(prefabBytes)) + prefabBytes
        blob += struct.pack("<I", len(xmlBytes)) + xmlBytes

    outPath.parent.mkdir(parents=True, exist_ok=True)
    outPath.write_bytes(blob)


def cookScenes(resourceRoot: Path | None = None) -> int:
    """
    지정된 리소스 루트 내의 모든 .scene.xml 파일을 .scene.bin으로 변환합니다.

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
    cookedCount = 0

    for xmlPath in sceneFiles:
        outBin = xmlPath.with_suffix("").with_suffix(".bin")
        sceneName, entitiesList = readXmlSceneInternal(xmlPath)
        writeScn1Internal(outBin, sceneName, entitiesList)
        cookedCount += 1
        print(f"[CookScenes] Cooked {xmlPath.relative_to(root)} -> {outBin.name} ({len(entitiesList)} entities)")

    print(f"[CookScenes] Finished cooking {cookedCount} scenes.")
    return 0


def main() -> int:
    resourceRoot: Path | None = None
    if len(sys.argv) > 1:
        resourceRoot = Path(sys.argv[1]).resolve()
    return cookScenes(resourceRoot)


if __name__ == "__main__":
    raise SystemExit(main())
