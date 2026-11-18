"""
꾸미기 스키마 출시 대조 — 콘텐츠의 꾸미기 스키마(`<CustomizationSchemaCatalog>`)를 마지막 출시 스냅숏(`Config/Game/Release/CustomizationSchemas.json`)과 견준다.

패치가 꾸미기 스키마를 바꿔도 옛 세이브 · 공유 코드가 자연스럽게 새 스키마로 올라가게, 런타임의 판 올림 단계(`<Migration toVersion>`)가 빠진 콘텐츠는
쿠킹에서 멈춘다(CookAssets 가 `check --fix` 를 먼저 돈다). 그래서 런타임에는 "단계가 빠진 패치" 가 있을 수 없다.

| 바뀜 | 처리 |
| --- | --- |
| 매개변수 추가(기본값은 늘 있다) | 단계 없이 통과 — 옛 값에 없으면 기본값이다 |
| 매개변수 제거(같은 종류의 새 이름이 없다) | 추론할 수 있다 — `--fix` 가 `<Remove parameter>` 단계와 판 올림을 소스에 적는다(디자이너가 판 번호를 손으로 올리지 않는다) |
| 이름 바꾸기로 보이는 것(사라진 이름 + 같은 종류의 새 이름) · 종류 바꾸기 | 추론할 수 없다 — 명시 단계(Rename · Split · PaletteIndexToColor · Remove)가 없으면 오류와 적을 줄을 안내한다 |
| 판이 출시 판보다 낮다 | 오류 |

언리얼 `FCustomVersion` 은 판을 사람이 올리고 Serialize 분기를 손으로 쓰며, 유니티는 `FormerlySerializedAs` 를 손으로 단다 — 둘 다 빠뜨리면 조용히 값을 잃는다.
여기는 출시 스냅숏과 견주어 빠진 단계를 쿠킹에서 잡고, 추론할 수 있는 것(제거)은 도구가 적는다.

사용법:
    py -3 -m Scripts customization-release            # check — 바뀜을 보고, 단계가 빠졌으면 실패
    py -3 -m Scripts customization-release --fix      # 추론할 수 있는 단계(제거)를 소스에 적고, 남은 오류가 있으면 실패
    py -3 -m Scripts customization-release accept     # 출시: 지금 스키마를 스냅숏으로 쓴다
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

#: 출시 스냅숏(판마다 스키마의 판 · 매개변수 이름 → 종류). 출시 때 `accept` 가 쓴다.
SNAPSHOT_PATH = Path("Config/Game/Release/CustomizationSchemas.json")
#: 꾸미기 스키마가 있는 외형 데이터 폴더(`<게임>/data/appearance/*.xml`).
CONTENT_GLOB = "**/data/appearance/*.xml"
CATALOG_TAG = "CustomizationSchemaCatalog"
PARAMETER_KINDS = ("Slider", "Color", "Choice", "Attachment")
FIRST_MIGRATION_VERSION = 2


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
        schemas[node.get("id", "")] = SchemaInfo(int(node.get("version", "1")), parameters, migrations)
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


def load_snapshot(path: Path) -> dict[str, SchemaInfo]:
    data = json.loads(path.read_text(encoding="utf-8"))
    return {schema_id: SchemaInfo(entry["version"], dict(entry["parameters"])) for schema_id, entry in data["schemas"].items()}


def write_snapshot(path: Path, schemas: dict[str, SchemaInfo]) -> None:
    data = {
        "_comment": "꾸미기 스키마 출시 스냅숏 — 출시 때 `py -3 -m Scripts customization-release accept` 가 쓴다. 쿠킹이 이것과 견주어 판 올림 단계가 빠진 바뀜을 막는다.",
        "schemas": {schema_id: {"version": info.version, "parameters": dict(sorted(info.parameters.items()))} for schema_id, info in sorted(schemas.items())},
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=4) + "\n", encoding="utf-8", newline="\n")


def run(project_root: Path, mode: str, fix: bool) -> int:
    files = find_content_files(project_root)
    snapshot_path = project_root / SNAPSHOT_PATH
    if mode == "accept":
        merged: dict[str, SchemaInfo] = {}
        for path in files:
            merged.update(parse_catalog(path.read_text(encoding="utf-8")))
        write_snapshot(snapshot_path, merged)
        print(f"[customization-release] wrote {SNAPSHOT_PATH} ({len(merged)} schemas)")
        return 0
    if not snapshot_path.exists():
        print(f"[customization-release] {SNAPSHOT_PATH} is missing - run `accept` at release", file=sys.stderr)
        return 1
    released = load_snapshot(snapshot_path)
    failed = False
    for path in files:
        text = path.read_text(encoding="utf-8")
        current = parse_catalog(text)
        issues = compare(released, current)
        if fix:
            by_schema: dict[str, list[str]] = {}
            for issue in issues:
                if issue.fixable:
                    by_schema.setdefault(issue.schema, []).append(issue.removable)
            for schema_id, names in by_schema.items():
                text = apply_removals(text, schema_id, released[schema_id].version, current[schema_id].version, names)
                print(f"[customization-release] {path.relative_to(project_root)}: schema '{schema_id}' - wrote <Remove> for {', '.join(names)} (commit this change)")
            if by_schema:
                path.write_text(text, encoding="utf-8", newline="\n")
                issues = compare(released, parse_catalog(text))
        for issue in issues:
            failed = True
            print(f"[customization-release] {path.relative_to(project_root)}: {issue.message}", file=sys.stderr)
    return 1 if failed else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="customization-release", description="꾸미기 스키마를 출시 스냅숏과 견준다(쿠킹 검사)")
    parser.add_argument("mode", nargs="?", default="check", choices=("check", "accept"))
    parser.add_argument("--fix", action="store_true", help="추론할 수 있는 단계(제거)를 소스에 적는다")
    parser.add_argument("--root", type=Path, default=None, help="프로젝트 뿌리(기본: 이 저장소)")
    arguments = parser.parse_args(argv)
    return run(arguments.root or get_project_root(), arguments.mode, arguments.fix)


if __name__ == "__main__":
    sys.exit(main())
