"""
저장 형식 출시 대조 — 판을 올리지 않고 저장 배치를 바꾼 것을 마지막 출시 스냅숏(`Config/Build/Release/FormatSnapshot.json`)과 견주어 잡는다. 두 가지를 본다:

  꾸미기 스키마(`<CustomizationSchemaCatalog>` — 콘텐츠 글): 판 올림 단계(`<Migration toVersion>`)가 빠진 바뀜
  상태 배치(`PROPERTY( RuntimeState )` 칸 — `App --write-state-layouts=<파일>` 이 쓴 글): `REFLECT( Version )` 을 올리지 않은 칸 더하기 · 빼기 · 이름 · 형식 · 차례 바꾸기.
  읽기는 배치 해시가 다른 바이트를 거절하므로(`StateIO.ReflectedLayoutChanged`) 빠뜨린 판 올림은 플레이어의 세이브를 잃는다.

스냅숏의 `released` 한 줄이 규칙을 가른다. false(출시 전): 옛 데이터가 없어 판을 올리지 않는다 — 바뀜을 알리기만 하고 통과한다. true: 아래 표대로 실패한다.
출시할 때 `accept` 로 지금 배치를 쓰고 `released` 를 true 로 바꾼다.

| 바뀜(출시 뒤) | 처리 |
| --- | --- |
| 꾸미기 매개변수 추가(기본값은 늘 있다) · 새 상태 타입 | 단계 없이 통과 |
| 꾸미기 매개변수 제거(같은 종류의 새 이름이 없다) | 추론할 수 있다 — `--fix` 가 `<Remove parameter>` 단계와 판 올림을 소스에 적는다(디자이너가 판 번호를 손으로 올리지 않는다) |
| 이름 바꾸기로 보이는 것(사라진 이름 + 같은 종류의 새 이름) · 종류 바꾸기 | 추론할 수 없다 — 명시 단계(Rename · Split · PaletteIndexToColor · Remove)가 없으면 오류와 적을 줄을 안내한다 |
| 상태 배치가 다른데 판이 그대로 · 판이 출시 판보다 낮다 | 오류 — `REFLECT( Version )` 을 올리고 `SW_TYPE_VERSION_PATCH` 를 더한다 |

언리얼 `FCustomVersion` 은 판을 사람이 올리고 Serialize 분기를 손으로 쓰며, 유니티는 `FormerlySerializedAs` 를 손으로 단다 — 둘 다 빠뜨리면 조용히 값을 잃는다.

사용법:
    py -3 -m Scripts format-release                        # check — 꾸미기 스키마(쿠킹이 먼저 돈다)
    py -3 -m Scripts format-release --layouts <파일>       # check — 상태 배치도
    py -3 -m Scripts format-release --fix                  # 추론할 수 있는 단계(제거)를 소스에 적고, 남은 오류가 있으면 실패
    py -3 -m Scripts format-release accept --layouts <파일> # 출시: 지금 스키마 · 배치를 스냅숏으로 쓴다
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # Scripts
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다
from common.paths import get_project_root  # noqa: E402

#: 출시 스냅숏 — `released`(출시 전이면 false) · `customization`(스키마마다 판과 매개변수 이름 → 종류) · `states`(상태 타입마다 판과 칸 배치). `accept` 가 쓴다.
SNAPSHOT_PATH = Path("Config/Build/Release/FormatSnapshot.json")
#: 꾸미기 스키마가 있는 외형 데이터 폴더(`<게임>/data/appearance/*.xml`).
CONTENT_GLOB = "**/data/appearance/*.xml"
CATALOG_TAG = "CustomizationSchemaCatalog"
PARAMETER_KINDS = ("Slider", "Color", "Choice", "Attachment")


@dataclass
class MigrationOp:
    kind: str
    source: str
    targets: list[str] = field(default_factory=list)


@dataclass
class SchemaInfo:
    version: int
    parameters: dict[str, str]
    migrations: list[tuple[int, list[MigrationOp]]] = field(default_factory=list)


@dataclass
class Issue:
    schema: str
    message: str
    removable: str = ""  # 비어 있지 않으면 `--fix` 가 이 매개변수의 Remove 단계를 적는다

    @property
    def fixable(self) -> bool:
        return self.removable != ""


def parse_catalog(text: str) -> dict[str, SchemaInfo]:
    """`<CustomizationSchemaCatalog>` 글에서 스키마마다 판 · 매개변수 · 단계를 읽는다. 다른 루트면 빈 표."""
    root = ElementTree.fromstring(text.encode("utf-8"))  # 바이트로 — 글 머리의 encoding 선언을 받는다
    if root.tag != CATALOG_TAG:
        return {}
    schemas: dict[str, SchemaInfo] = {}
    for node in root.findall("Schema"):
        parameters = {child.get("name", ""): child.tag for child in node if child.tag in PARAMETER_KINDS}
        migrations = []
        for migration in node.findall("Migration"):
            ops = []
            for op in migration:
                if op.tag in ("Rename", "Split"):
                    ops.append(MigrationOp(op.tag, op.get("from", ""), op.get("to", "").replace(",", " ").split()))
                elif op.tag in ("PaletteIndexToColor", "Remove"):
                    ops.append(MigrationOp(op.tag, op.get("parameter", "")))
            migrations.append((int(migration.get("toVersion", "0")), ops))
        schemas[node.get("id", "")] = SchemaInfo(int(node.get("version", "0")), parameters, migrations)
    return schemas


def expected_parameters(released: SchemaInfo, current: SchemaInfo) -> dict[str, str]:
    """출시 판의 매개변수에 출시 뒤 단계(toVersion > 출시 판)를 건 결과 — 지금 콘텐츠가 가져야 할 옛 매개변수(이름 → 종류)."""
    names = dict(released.parameters)
    for to_version, ops in current.migrations:
        if to_version <= released.version:
            continue
        for op in ops:
            if op.kind == "Remove":
                names.pop(op.source, None)
            elif op.kind in ("Rename", "Split") and op.source in names:
                kind = names.pop(op.source)
                for target in op.targets:
                    names[target] = kind
            elif op.kind == "PaletteIndexToColor" and op.source in names:
                names[op.source] = "Color"
    return names


def compare(released: dict[str, SchemaInfo], current: dict[str, SchemaInfo]) -> list[Issue]:
    """출시 스냅숏과 지금 스키마를 견준다. 추가는 통과, 단계 없는 제거는 고칠 수 있는 바뀜, 이름 · 종류 바뀜은 오류."""
    issues: list[Issue] = []
    for schema_id, before in sorted(released.items()):
        after = current.get(schema_id)
        if after is None:
            continue  # 스키마째 지웠다 — 그 값을 받을 매개변수가 없어 읽을 때 버려진다(옮길 데가 없다)
        if after.version < before.version:
            issues.append(Issue(schema_id, f"schema '{schema_id}' version {after.version} is lower than the released {before.version}"))
            continue
        expected = expected_parameters(before, after)
        added = {name: kind for name, kind in after.parameters.items() if name not in expected}
        for name, kind in sorted(expected.items()):
            if name not in after.parameters:
                twins = sorted(new for new, new_kind in added.items() if new_kind == kind)
                if twins:
                    issues.append(Issue(schema_id, f"schema '{schema_id}': {kind} '{name}' is gone and {kind} {', '.join(repr(t) for t in twins)} is new - "
                                                       f"a rename cannot be inferred: write <Rename from=\"{name}\" to=\"{twins[0]}\"/> (or <Remove parameter=\"{name}\"/> if it was removed) "
                                                       f"in <Migration toVersion=\"{max(after.version, before.version + 1)}\">"))
                else:
                    issues.append(Issue(schema_id, f"schema '{schema_id}': {kind} '{name}' was removed without a step - --fix writes <Remove parameter=\"{name}\"/>", removable=name))
            elif after.parameters[name] != kind:
                issues.append(Issue(schema_id, f"schema '{schema_id}': '{name}' changed kind {kind} -> {after.parameters[name]} - a kind change cannot be inferred: write "
                                                   f"<PaletteIndexToColor parameter=\"{name}\" palette=\"…\"/> (palette index slider -> color) or <Remove parameter=\"{name}\"/> "
                                                   f"in <Migration toVersion=\"{max(after.version, before.version + 1)}\">"))
    return issues


def apply_removals(text: str, schema_id: str, released_version: int, current_version: int, names: list[str]) -> str:
    """스키마 @p schema_id 에 Remove 단계를 적는다. 판이 출시 판 그대로면 판을 올리고 새 `<Migration>` 을, 이미 올렸으면 마지막 `<Migration>` 끝에 더한다."""
    opening = re.compile(r'<Schema\b[^>]*\bid="' + re.escape(schema_id) + r'"[^>]*>')
    match = opening.search(text)
    if match is None:
        raise ValueError(f"schema '{schema_id}' not found")
    end = text.index("</Schema>", match.end())
    block = text[match.end():end]
    removes = "".join(f'<Remove parameter="{name}"/>' for name in names)
    indent_match = re.match(r"\s*\n([ \t]*)", block)
    indent = indent_match.group(1) if indent_match else "    "
    if current_version > released_version:
        last = list(re.finditer(r'<Migration\b[^>]*\btoVersion="' + str(current_version) + r'"[^>]*?(/?)>', block))
        if not last:
            raise ValueError(f"schema '{schema_id}' is version {current_version} without <Migration toVersion=\"{current_version}\">")
        tag = last[-1]
        if tag.group(1) == "/":
            replaced = tag.group(0)[:-2].rstrip() + ">" + removes + "</Migration>"
            block = block[:tag.start()] + replaced + block[tag.end():]
        else:
            close = block.index("</Migration>", tag.end())
            block = block[:close] + removes + block[close:]
        return text[:match.end()] + block + text[end:]
    new_version = released_version + 1
    head = match.group(0)
    if re.search(r'\bversion="\d+"', head):
        head = re.sub(r'\bversion="\d+"', f'version="{new_version}"', head)
    else:
        head = head[:-1] + f' version="{new_version}">'
    migration = f'\n{indent}<Migration toVersion="{new_version}">{removes}</Migration>'
    closes = [m.end() for m in re.finditer(r"</Migration>|<Migration\b[^>]*/>", block)]
    at = closes[-1] if closes else 0
    block = block[:at] + migration + block[at:]
    return text[:match.start()] + head + block + text[end:]


def find_content_files(project_root: Path) -> list[Path]:
    files = []
    for path in sorted((project_root / "Content").glob(CONTENT_GLOB)):
        text = path.read_text(encoding="utf-8")
        if f"<{CATALOG_TAG}" in text:
            files.append(path)
    return files


@dataclass
class Snapshot:
    released: bool
    schemas: dict[str, SchemaInfo]
    states: dict[str, dict]


def load_snapshot(path: Path) -> Snapshot:
    data = json.loads(path.read_text(encoding="utf-8"))
    schemas = {schema_id: SchemaInfo(entry["version"], dict(entry["parameters"])) for schema_id, entry in data["customization"].items()}
    return Snapshot(data["released"], schemas, data["states"])


def write_snapshot(path: Path, snapshot: Snapshot) -> None:
    data = {
        "_comment": "저장 형식 출시 스냅숏 — `py -3 -m Scripts format-release accept` 가 쓴다. released 가 false(출시 전)면 대조는 알리기만 하고, true 면 판을 올리지 않은 바뀜이 실패한다.",
        "released": snapshot.released,
        "customization": {schema_id: {"version": info.version, "parameters": dict(sorted(info.parameters.items()))} for schema_id, info in sorted(snapshot.schemas.items())},
        "states": dict(sorted(snapshot.states.items())),
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=4) + "\n", encoding="utf-8", newline="\n")


def compare_states(released: dict[str, dict], current: dict[str, dict]) -> list[str]:
    """판을 올리지 않고 배치를 바꾼 상태 타입 · 판이 내려간 타입. 지금 빌드에 없는 타입(다른 타깃 · 지운 타입)과 새 타입은 보지 않는다."""
    issues = []
    for name, before in sorted(released.items()):
        after = current.get(name)
        if after is None:
            continue
        if after["version"] < before["version"]:
            issues.append(f"state '{name}' version {after['version']} is lower than the released {before['version']}")
        elif after["version"] == before["version"] and after["layout"] != before["layout"]:
            issues.append(f"state '{name}' changed its RuntimeState fields without raising REFLECT( Version ) - raise it and add SW_TYPE_VERSION_PATCH ({before['layout']} -> {after['layout']})")
    return issues


def run(project_root: Path, mode: str, fix: bool, layouts_path: Path | None = None) -> int:
    files = find_content_files(project_root)
    snapshot_path = project_root / SNAPSHOT_PATH
    if not snapshot_path.exists():
        print(f"[format-release] {SNAPSHOT_PATH} is missing", file=sys.stderr)
        return 1
    released = load_snapshot(snapshot_path)
    states = json.loads(layouts_path.read_text(encoding="utf-8")) if layouts_path is not None else {}
    if mode == "accept":
        merged: dict[str, SchemaInfo] = {}
        for path in files:
            merged.update(parse_catalog(path.read_text(encoding="utf-8")))
        refused = compare_states(released.states, states) if released.released else []
        for message in refused:
            print(f"[format-release] not accepted: {message}", file=sys.stderr)
        if refused:
            return 1
        write_snapshot(snapshot_path, Snapshot(released.released, merged, {**released.states, **states}))   # 이 빌드에 없는 타입(다른 타깃)은 남긴다
        print(f"[format-release] wrote {SNAPSHOT_PATH} ({len(merged)} schemas, {len(states)} states)")
        return 0
    list_message = compare_states(released.states, states)
    for path in files:
        text = path.read_text(encoding="utf-8")
        current = parse_catalog(text)
        issues = compare(released.schemas, current)
        if fix:
            by_schema: dict[str, list[str]] = {}
            for issue in issues:
                if issue.fixable:
                    by_schema.setdefault(issue.schema, []).append(issue.removable)
            for schema_id, names in by_schema.items():
                text = apply_removals(text, schema_id, released.schemas[schema_id].version, current[schema_id].version, names)
                print(f"[format-release] {path.relative_to(project_root)}: schema '{schema_id}' - wrote <Remove> for {', '.join(names)} (commit this change)")
            if by_schema:
                path.write_text(text, encoding="utf-8", newline="\n")
                issues = compare(released.schemas, parse_catalog(text))
        list_message += [f"{path.relative_to(project_root)}: {issue.message}" for issue in issues]
    for message in list_message:
        print(f"[format-release] {'' if released.released else '(not released yet - no version step needed) '}{message}", file=sys.stderr if released.released else sys.stdout)
    return 1 if list_message and released.released else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="format-release", description="저장 형식(꾸미기 스키마 · 상태 배치)을 출시 스냅숏과 견준다")
    parser.add_argument("mode", nargs="?", default="check", choices=("check", "accept"))
    parser.add_argument("--fix", action="store_true", help="추론할 수 있는 꾸미기 단계(제거)를 소스에 적는다")
    parser.add_argument("--layouts", type=Path, default=None, help="상태 배치 글(App --write-state-layouts=<파일>)")
    arguments = parser.parse_args(argv)
    return run(get_project_root(), arguments.mode, arguments.fix, arguments.layouts)


if __name__ == "__main__":
    sys.exit(main())
