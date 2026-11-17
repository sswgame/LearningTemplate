"""
씬 · 프리팹의 컴포넌트에 고정 지역 id(`_localID`)를 적고, 같은 오브젝트 안의 부착 키(`_attachComponent="이름#n"`)를 지역 id 키(`#<16진>`)로 바꿔 쓴다 — 감사 A-2 의 전수 이전.

  py -3 -m Scripts migrate-component-ids          # 바꿀 수만 찍는다(아무것도 쓰지 않는다)
  py -3 -m Scripts migrate-component-ids --fix    # 바꿔 쓴다

- id 는 C++ `ComponentStableKey::resolveLocalIDs` 와 같은 규칙으로 정한다 — 적힌 id 는 그대로, 없는 것은 옛 키 `이름#n`(이름은 `_componentName`, 없거나 `None` 이면 원소 이름)의
  64 비트 FNV-1a(대소문자 구분), 적힌 id 와 겹치거나 0 이면 같은 글을 앞 결과를 씨앗으로 다시 섞는다. 그래서 런타임이 옛 파일에서 정하던 id 와 같은 값을 적는다 —
  적은 뒤로는 형제를 지우거나 끼워도 id 가 그대로다.
- 다른 오브젝트를 가리키는 부착(`_attachOwnerID` 가 0 이 아니다) · 프리팹 덮어쓰기 키(`key` · `after`) · 중첩 노드 `attach` · 세이브 핸들의 옛 키는 아직 바꾸지 않는다 —
  런타임이 옮기는 동안 옛 키도 읽는다(`ComponentStableKey::matchesKey`). 바이트는 칸을 더하고 바꾼 자리 밖은 그대로다.
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path

from common.exit_code import ExitCode
from common.paths import get_project_root

CONTENT_FOLDER = "Content"
STATE_SUFFIXES = (".scene.xml", ".prefab.xml")
LIST_ELEMENT = "_listComponent"
LOCAL_ID = "_localID"
COMPONENT_NAME = "_componentName"
ATTACH_COMPONENT = "_attachComponent"
ATTACH_OWNER = "_attachOwnerID"
NO_NAME = "None"
FNV_OFFSET_64 = 14695981039346656037
FNV_PRIME_64 = 1099511628211
MASK_64 = (1 << 64) - 1
HEX_DIGITS = 16
KEY_PREFIX = "#"   # C++ `ComponentStableKey::kLocalIDPrefix`

TAG_PATTERN = re.compile(r"<(/?)([A-Za-z_][\w.:-]*)((?:[^>\"']|\"[^\"]*\"|'[^']*')*?)(/?)>")
ATTRIBUTE_PATTERN = re.compile(r"([A-Za-z_][\w.:-]*)\s*=\s*\"([^\"]*)\"")


def fnv1a_64(text: str, seed: int = FNV_OFFSET_64) -> int:
    """C++ `HashUtil::computeStringHash64( …, false, seed )` — UTF-8 바이트마다 xor 뒤 곱한다."""
    value = seed
    for byte in text.encode("utf-8"):
        value = ((value ^ byte) * FNV_PRIME_64) & MASK_64
    return value


def resolve_local_ids(list_slot: list[tuple[str, int]]) -> list[int]:
    """(이름, 적힌 id 또는 0) 목록의 지역 id — C++ `ComponentStableKey::resolveLocalIDs` 와 같은 규칙."""
    set_taken = {stored for _, stored in list_slot if stored != 0}
    map_occurrence: dict[str, int] = {}
    list_resolved = []
    for base_name, stored in list_slot:
        occurrence = map_occurrence.get(base_name, 0)
        map_occurrence[base_name] = occurrence + 1
        if stored != 0:
            list_resolved.append(stored)
            continue
        legacy = f"{base_name}#{occurrence}"
        local_id = fnv1a_64(legacy)
        while local_id == 0 or local_id in set_taken:
            local_id = fnv1a_64(legacy, local_id)
        set_taken.add(local_id)
        list_resolved.append(local_id)
    return list_resolved


def format_key(local_id: int) -> str:
    return KEY_PREFIX + format(local_id, f"0{HEX_DIGITS}x")


@dataclass
class ComponentTag:
    """`_listComponent` 의 직계 자식 시작 태그 하나 — 글 안 [start, end) 와 그 속성."""

    start: int
    end: int
    name: str
    attributes: dict[str, str]


def collect_component_lists(text: str) -> list[list[ComponentTag]]:
    """글 안의 `_listComponent` 마다 직계 자식 시작 태그 목록입니다(중첩된 목록 — 다른 오브젝트 — 도 따로 모은다)."""
    list_result: list[list[ComponentTag]] = []
    stack: list[tuple[str, list[ComponentTag] | None]] = []
    for match in TAG_PATTERN.finditer(text):
        closing, name, body, self_closing = match.group(1), match.group(2), match.group(3), match.group(4)
        if closing:
            while stack:
                open_name, _ = stack.pop()
                if open_name == name:
                    break
            continue
        parent_list = stack[-1][1] if stack else None
        if parent_list is not None:
            parent_list.append(ComponentTag(match.start(), match.end(), name, dict(ATTRIBUTE_PATTERN.findall(body))))
        if self_closing:
            continue
        own_list: list[ComponentTag] | None = None
        if name == LIST_ELEMENT:
            own_list = []
            list_result.append(own_list)
        stack.append((name, own_list))
    return list_result


def migrate_text(text: str) -> tuple[str, int, int]:
    """글 하나를 바꿉니다. 반환은 (바꾼 글, 더한 `_localID` 수, 바꾼 부착 키 수)."""
    list_edit: list[tuple[int, int, str]] = []   # (자리, 지울 길이, 넣을 글)
    added_count = 0
    attach_count = 0
    for list_tag in collect_component_lists(text):
        list_slot = []
        for tag in list_tag:
            label = tag.attributes.get(COMPONENT_NAME, "")
            base_name = tag.name if not label or label == NO_NAME else label
            stored_text = tag.attributes.get(LOCAL_ID, "")
            list_slot.append((base_name, int(stored_text) if stored_text.isdigit() else 0))
        list_resolved = resolve_local_ids(list_slot)
        map_legacy_to_key: dict[str, str] = {}
        map_occurrence: dict[str, int] = {}
        for (base_name, _), local_id in zip(list_slot, list_resolved):
            occurrence = map_occurrence.get(base_name, 0)
            map_occurrence[base_name] = occurrence + 1
            map_legacy_to_key[f"{base_name}#{occurrence}"] = format_key(local_id)
        for tag, (_, stored), local_id in zip(list_tag, list_slot, list_resolved):
            tag_text = text[tag.start:tag.end]
            if stored == 0:
                name_end = tag.start + 1 + len(tag.name)
                list_edit.append((name_end, 0, f' {LOCAL_ID}="{local_id}"'))
                added_count += 1
            attach = tag.attributes.get(ATTACH_COMPONENT)
            owner = tag.attributes.get(ATTACH_OWNER, "0")
            if attach and attach in map_legacy_to_key and owner in ("", "0"):
                needle = f'{ATTACH_COMPONENT}="{attach}"'
                offset = tag_text.find(needle)
                if offset >= 0:
                    value_start = tag.start + offset + len(ATTACH_COMPONENT) + 2
                    list_edit.append((value_start, len(attach), map_legacy_to_key[attach]))
                    attach_count += 1
    for position, length, insert in sorted(list_edit, key=lambda edit: edit[0], reverse=True):
        text = text[:position] + insert + text[position + length:]
    return text, added_count, attach_count


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts migrate-component-ids", description="씬 · 프리팹 컴포넌트에 고정 지역 id 를 적는다(감사 A-2)")
    parser.add_argument("--fix", action="store_true", help="바꿔 쓴다(없으면 찍기만 한다)")
    args = parser.parse_args(argv)

    root = get_project_root()
    content = root / CONTENT_FOLDER
    map_original: dict[str, str] = {}
    map_text: dict[str, str] = {}
    added_total = attach_total = 0
    for path in sorted(content.rglob("*.xml")):
        if not path.name.lower().endswith(STATE_SUFFIXES):
            continue
        asset_id = path.relative_to(content).as_posix()
        map_original[asset_id] = path.read_bytes().decode("utf-8", "surrogateescape")
        map_text[asset_id], added, attached = migrate_text(map_original[asset_id])
        added_total += added
        attach_total += attached
    # 둘째 단계: 다른 프리팹을 가리키는 옛 키(중첩 노드 id · Part 경로 · 덮어쓰기 key/after · 노드 attach). 첫 단계가 적은 컴포넌트 id 로 푼다.
    from dev.migrate_prefab_links import LinkMigration  # noqa: PLC0415 — 서로 읽는 두 모듈이다
    links = LinkMigration({asset_id: text for asset_id, text in map_text.items() if asset_id.lower().endswith(".prefab.xml")})
    for asset_id in map_text:
        map_text[asset_id] = links.migrate(asset_id, map_text[asset_id])
    for line in links.list_unresolved:
        print(f"  - {line}")
    list_changed = [asset_id for asset_id in map_text if map_text[asset_id] != map_original[asset_id]]
    if args.fix:
        for asset_id in list_changed:
            (content / asset_id).write_bytes(map_text[asset_id].encode("utf-8", "surrogateescape"))
    print(f"[migrate-component-ids] 파일 {len(list_changed)} 개 · 지역 id {added_total} 개 · 같은 오브젝트 부착 키 {attach_total} 개 · 따라가지 못한 옛 키 {len(links.list_unresolved)} 개"
          f"{'' if args.fix else ' (--fix 없이 — 아무것도 쓰지 않았다)'}")
    return ExitCode.PASSED
