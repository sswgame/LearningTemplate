"""
Scripts/generate/CookPrefabs.py

Convert Resource/Game/Prefabs/*.prefab.xml|json → .prefab.bin (PFB1 format).

Binary layout (little-endian, matches PrefabAsset::saveToBinaryFile):
  u32 magic 0x50464231 ('PFB1')
  u32 nameLen + name bytes
  u32 xmlBodyLen + xmlBody bytes
"""

from __future__ import annotations

import json
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ConfigHelper import GetProjectRoot

PFB1_MAGIC = 0x50464231


def _read_xml_prefab(path: Path) -> tuple[str, str]:
    tree = ET.parse(path)
    root = tree.getroot()
    name = ""
    name_node = root.find("name")
    if name_node is not None and name_node.text:
        name = name_node.text.strip()
    elif root.get("name"):
        name = root.get("name", "")

    body = ""
    for tag in ("GameObjectState", "ObjectState"):
        node = root.find(tag)
        if node is not None:
            body = ET.tostring(node, encoding="unicode")
            break
    if not body:
        body = path.read_text(encoding="utf-8")
    return name, body


def _read_json_prefab(path: Path) -> tuple[str, str]:
    data = json.loads(path.read_text(encoding="utf-8"))
    name = str(data.get("name", ""))
    body = str(data.get("xmlBody", ""))
    if not body:
        raise ValueError(f"Missing xmlBody in {path}")
    return name, body


def _write_pfb1(out_path: Path, name: str, xml_body: str) -> None:
    name_bytes = name.encode("utf-8")
    body_bytes = xml_body.encode("utf-8")
    blob = struct.pack("<I", PFB1_MAGIC)
    blob += struct.pack("<I", len(name_bytes)) + name_bytes
    blob += struct.pack("<I", len(body_bytes)) + body_bytes
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(blob)


def cook_prefabs(resource_root: Path | None = None) -> int:
    project_root = GetProjectRoot()
    prefab_dir = (resource_root or (project_root / "Resource" / "Game" / "Prefabs")).resolve()
    if not prefab_dir.is_dir():
        print(f"[CookPrefabs] Prefab directory not found: {prefab_dir}")
        return 1

    cooked = 0
    for src in sorted(prefab_dir.glob("*.prefab.*")):
        if src.suffix.lower() not in {".xml", ".json"}:
            continue
        try:
            if src.suffix.lower() == ".json":
                name, body = _read_json_prefab(src)
            else:
                name, body = _read_xml_prefab(src)
            out = src.with_suffix(".bin")
            _write_pfb1(out, name, body)
            print(f"[CookPrefabs] {src.name} -> {out.name} ('{name}')")
            cooked += 1
        except Exception as exc:
            print(f"[CookPrefabs] Failed {src}: {exc}")
            return 1

    print(f"[CookPrefabs] Done - {cooked} prefab(s) cooked.")
    return 0


if __name__ == "__main__":
    raise SystemExit(cook_prefabs())
