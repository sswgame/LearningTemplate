"""
프리팹 · 씬이 다른 프리팹을 가리키는 옛 순번 키를 고정 지역 id 키로 바꿔 쓴다 — 감사 A-2 의 둘째 전수 이전(옛 키 수축 준비). `migrate-component-ids` 가 부른다.

- 중첩 노드(`<PrefabInstance>`)에 `localID="#<16진>"` 이 없으면 준다(값은 `<프리팹 id>#node<순번>` 의 FNV-1a 64 — 다시 돌려도 같다).
- 인스턴스(`<PrefabInstance prefab>` · 씬 `<entity prefab>`)의 `<PrefabOverrides>` 안:
  - `<Part path="1/0">` 의 옛 인덱스 토막 → 그 노드의 id 키(`#…/#…`). 원본 프리팹의 노드 목록을 따라 내려간다.
  - `<Override key>` · `<Remove key>` · `<Add after>` 의 옛 컴포넌트 키 `이름#n` → 그 대상 프리팹 루트 컴포넌트의 id 키(`<Part>` 안이면 그 부품 프리팹의 것).
- 중첩 노드의 `attach="이름#n"` → 부모 오브젝트(프리팹 루트 또는 부모 노드의 원본 루트) 컴포넌트의 id 키.
- 남는 것(보고만): 다른 오브젝트의 부착(`_attachOwnerID` ≠ 0), 세이브 핸들 — 세이브는 저장소 밖이라 런타임이 옛 키를 읽는다(`ComponentStableKey::matchesKey`).
"""

from __future__ import annotations

from dataclasses import dataclass, field

from dev.migrate_component_ids import (ATTRIBUTE_PATTERN, COMPONENT_NAME, LIST_ELEMENT, LOCAL_ID, NO_NAME, TAG_PATTERN, fnv1a_64, format_key,
                                       resolve_local_ids)

NODE_ELEMENT = "PrefabInstance"
ENTITY_ELEMENT = "entity"
OVERRIDES_ELEMENT = "PrefabOverrides"
PART_ELEMENT = "Part"
NODE_LOCAL_ID = "localID"
KEY_PREFIX = "#"


@dataclass
class Element:
    """시작 태그 하나 — 글 안 [start, end), 속성, 자식."""

    name: str
    start: int
    end: int
    attributes: dict[str, str]
    children: list["Element"] = field(default_factory=list)
    parent: "Element | None" = None


def parse_elements(text: str) -> Element:
    """글의 원소 나무(시작 태그 자리만)입니다. 맨 위는 가짜 뿌리입니다."""
    root = Element("", 0, 0, {})
    stack = [root]
    for match in TAG_PATTERN.finditer(text):
        closing, name, body, self_closing = match.group(1), match.group(2), match.group(3), match.group(4)
        if closing:
            for depth in range(len(stack) - 1, 0, -1):
                if stack[depth].name == name:
                    del stack[depth:]
                    break
            continue
        element = Element(name, match.start(), match.end(), dict(ATTRIBUTE_PATTERN.findall(body)), [], stack[-1])
        stack[-1].children.append(element)
        if not self_closing:
            stack.append(element)
    return root


def find_child(element: Element, name: str) -> Element | None:
    return next((child for child in element.children if child.name == name), None)


@dataclass
class PrefabInfo:
    """프리팹 하나 — 루트 컴포넌트 옛 키 → id 키, 노드(문서 순서 — C++ `readNodesFromXML` 의 앞순회)."""

    map_component_key: dict[str, str]
    list_node: list[Element]
    list_node_key: list[str]


def component_key_map(game_object: Element | None) -> dict[str, str]:
    """오브젝트 원소의 컴포넌트 옛 키(`이름#n`) → id 키입니다(C++ `ComponentStableKey::resolveLocalIDs`)."""
    list_component = find_child(game_object, LIST_ELEMENT) if game_object else None
    if list_component is None:
        return {}
    list_slot = []
    for tag in list_component.children:
        label = tag.attributes.get(COMPONENT_NAME, "")
        stored = tag.attributes.get(LOCAL_ID, "")
        list_slot.append((tag.name if not label or label == NO_NAME else label, int(stored) if stored.isdigit() else 0))
    map_key: dict[str, str] = {}
    map_occurrence: dict[str, int] = {}
    for (base_name, _), local_id in zip(list_slot, resolve_local_ids(list_slot)):
        occurrence = map_occurrence.get(base_name, 0)
        map_occurrence[base_name] = occurrence + 1
        map_key[f"{base_name}#{occurrence}"] = format_key(local_id)
    return map_key


def collect_nodes(element: Element, out: list[Element]) -> None:
    for child in element.children:
        if child.name == NODE_ELEMENT:
            out.append(child)
            collect_nodes(child, out)


def read_prefab(asset_id: str, text: str) -> PrefabInfo:
    root = parse_elements(text)
    prefab = find_child(root, "Prefab")
    game_object = find_child(prefab, "GameObject") if prefab else None
    list_node: list[Element] = []
    if prefab:
        collect_nodes(prefab, list_node)
    list_key = []
    for index, node in enumerate(list_node):
        written = node.attributes.get(NODE_LOCAL_ID, "")
        list_key.append(written if written else format_key(fnv1a_64(f"{asset_id}#node{index}") or 1))
    return PrefabInfo(component_key_map(game_object), list_node, list_key)


class LinkMigration:
    """모든 프리팹을 읽은 뒤 글마다 바꿀 자리를 셉니다."""

    def __init__(self, map_prefab_text: dict[str, str]):
        self.map_prefab = {asset_id.lower(): read_prefab(asset_id.lower(), text) for asset_id, text in map_prefab_text.items()}
        self.list_unresolved: list[str] = []

    def traverse_part_path(self, prefab_id: str, path: str) -> tuple[str, str | None]:
        """부품 경로를 id 토막으로 바꾼 경로와, 그 부품의 원본 프리팹 id 입니다. 못 따라가면 (원래 경로, None)."""
        current = prefab_id.lower()
        list_out = []
        for segment in path.split("/"):
            info = self.map_prefab.get(current)
            if info is None:
                return path, None
            if segment.startswith(KEY_PREFIX):
                index = info.list_node_key.index(segment) if segment in info.list_node_key else -1
            else:
                index = int(segment) if segment.isdigit() else -1
            if index < 0 or index >= len(info.list_node):
                return path, None
            list_out.append(info.list_node_key[index])
            current = info.list_node[index].attributes.get("prefab", "").lower()
        return "/".join(list_out), current

    def edit_overrides(self, text: str, overrides: Element, prefab_id: str, label: str, list_edit: list) -> None:
        info = self.map_prefab.get(prefab_id.lower())
        for entry in overrides.children:
            if entry.name == PART_ELEMENT:
                path = entry.attributes.get("path", "")
                new_path, part_prefab = self.traverse_part_path(prefab_id, path)
                if part_prefab is None:
                    self.list_unresolved.append(f"{label}: Part path '{path}' 를 '{prefab_id}' 의 노드로 따라가지 못했다")
                    continue
                if new_path != path:
                    add_value_edit(text, entry, "path", new_path, list_edit)
                inner = find_child(entry, OVERRIDES_ELEMENT)
                if inner is not None:
                    self.edit_overrides(text, inner, part_prefab, label, list_edit)
                continue
            attribute = "after" if entry.name == "Add" else "key"
            legacy = entry.attributes.get(attribute, "")
            if not legacy or legacy.startswith(KEY_PREFIX):
                continue
            if info is None or legacy not in info.map_component_key:
                self.list_unresolved.append(f"{label}: {entry.name} {attribute}='{legacy}' 를 '{prefab_id}' 의 컴포넌트로 찾지 못했다(지운 컴포넌트면 그대로 둔다)")
                continue
            add_value_edit(text, entry, attribute, info.map_component_key[legacy], list_edit)

    def migrate(self, asset_id: str, text: str) -> str:
        root = parse_elements(text)
        list_edit: list[tuple[int, int, str]] = []
        own = self.map_prefab.get(asset_id.lower())
        prefab_root = find_child(root, "Prefab")
        if own is not None and prefab_root is not None:
            own_map = component_key_map(find_child(prefab_root, "GameObject"))
            list_node_here: list[Element] = []
            collect_nodes(prefab_root, list_node_here)   # 위치 · 속성은 지금 고치는 글에서 읽는다(생성 때 읽은 글은 이미 이전된 글과 어긋난다)
            for index, node in enumerate(list_node_here):
                if NODE_LOCAL_ID not in node.attributes:
                    list_edit.append((node.start + 1 + len(node.name), 0, f' {NODE_LOCAL_ID}="{own.list_node_key[index]}"'))
                attach = node.attributes.get("attach", "")
                if attach and not attach.startswith(KEY_PREFIX) and attach != NO_NAME:
                    parent = node.parent
                    parent_map = own_map if parent is None or parent.name != NODE_ELEMENT else self.root_map_of(parent.attributes.get("prefab", ""))
                    if attach in parent_map:
                        add_value_edit(text, node, "attach", parent_map[attach], list_edit)
                    else:
                        self.list_unresolved.append(f"{asset_id}: 노드 attach='{attach}' 를 부모 오브젝트에서 찾지 못했다")
        for holder in iterate(root):
            prefab_id = holder.attributes.get("prefab", "")
            if holder.name not in (NODE_ELEMENT, ENTITY_ELEMENT) or not prefab_id:
                continue
            overrides = find_child(holder, OVERRIDES_ELEMENT)
            if overrides is not None:
                self.edit_overrides(text, overrides, prefab_id, asset_id, list_edit)
        for position, length, insert in sorted(list_edit, key=lambda edit: edit[0], reverse=True):
            text = text[:position] + insert + text[position + length:]
        return text

    def root_map_of(self, prefab_id: str) -> dict[str, str]:
        info = self.map_prefab.get(prefab_id.lower())
        return info.map_component_key if info else {}


def iterate(element: Element):
    for child in element.children:
        yield child
        yield from iterate(child)


def add_value_edit(text: str, element: Element, attribute: str, value: str, list_edit: list) -> None:
    """원소 시작 태그 안의 `attribute="…"` 값을 @p value 로 바꾸는 자리를 더합니다."""
    tag_text = text[element.start:element.end]
    needle = f'{attribute}="'
    offset = tag_text.find(" " + needle)
    if offset < 0:
        return
    value_start = element.start + offset + 1 + len(needle)
    value_end = text.index('"', value_start)
    list_edit.append((value_start, value_end - value_start, value))
