"""
바뀐 파일이 닿는 시나리오를 고른다 — 시나리오가 여는 씬 · 에셋의 참조 닫힘과, 시나리오가 쓰는 단계 · 감시 값 · 로그 id 를 정의한 소스.

묶음 검증의 changed 단계(`test_history.py changed`)는 시나리오 XML · 골든이 바뀐 것만 골라, 셰이더 · 머티리얼 · 게임 데이터 · 감시 값이 바뀌어 지는 시나리오는 gpu-release 끝에서야 졌다.

  에셋   시나리오 글(주석 밖)이 적은 Content 경로 · GUID 에서 시작해, 그 파일들이 적은 것을 끝까지 따라간다(씬 → 프리팹 → 머티리얼 → 셰이더 → `#include`).
         임포트 산출물(`models/` · `textures/` …)은 커밋하지 않으므로 같은 자리의 원본(`models_raw/` …)으로 푼다(`Config/Dev/AssetIdentity.json`).
         엔진이 코드로 고르는 기본 에셋(패스 셰이더)과 이름으로 고르는 카탈로그 항목은 보지 못한다 — 모든 시나리오에 닿는 것이라 gpu-release 의 몫이다.
  소스   바뀐 .cpp 가 정의한 감시 값 · 단계 · 로그 id 를 시나리오가 `watch=` · `<단계` · `<ExpectLog id=` 로 쓰면 닿는다.
  상한   파일 하나가 닿는 시나리오가 `_changedReachPerFile` 을 넘으면(공통 셰이더 · `Assets.Pending`) 고르게 뽑은 그 수만, 모두 합쳐 `_changedReachTotal` 까지만 고른다
         (`Config/Dev/Verify.json`). 빼 놓은 수는 줄로 남기고 gpu-release 가 돈다.
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Callable, Iterable, Mapping, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from lint.asset_identity import META_EXTENSION, ImportedFolder, iterate_imported_sources, make_output_guid  # noqa: E402

SCENARIO_SUFFIX = ".scenario.xml"
#: 다른 파일을 적는 글 에셋. 나머지(그림 · 소리 · 모델)는 잎이다.
TEXT_SUFFIXES = (".xml", ".material", ".json", ".hlsl", ".hlsli", ".pipeline")
COMMENT = re.compile(r"<!--.*?-->|/\*.*?\*/", re.S)
PATH_TOKEN = re.compile(r"[A-Za-z0-9_\-]+(?:/[A-Za-z0-9_.\-]+)+")
GUID_TOKEN = re.compile(r"\b[0-9a-f]{32}\b")
OWN_GUID = re.compile(r'<\?asset guid="([0-9a-f]{32})"\?>|_guid="([0-9a-f]{32})"')
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
WATCH_DEFINITION = re.compile(r'\bSW_AUTOMATION_(?:\w+_)?WATCH\(\s*\w+,\s*"([^"]+)"')
STEP_DEFINITION = re.compile(r'\bSW_AUTOMATION_STEP\(\s*\w+,\s*"([^"]+)"')
LOG_DEFINITION = re.compile(r'\bSW_LOG_\w+_ID\(\s*"([^"]+)"')
WATCH_USE = re.compile(r'\bwatch="([^"]+)"')
STEP_USE = re.compile(r"<([A-Za-z]\w*)\b")
LOG_USE = re.compile(r'<ExpectLog\b[^>]*\bid="([^"]+)"')


@dataclass(frozen=True)
class Reach:
    """바뀐 파일 하나가 닿는 시나리오들(Content 기준, 이름 순)."""

    path: str
    list_scenario: tuple[str, ...]


def strip_comments(text: str) -> str:
    return COMMENT.sub(" ", text)


def resolve_token(token: str, set_content: frozenset[str] | set[str], list_imported: Sequence[ImportedFolder]) -> str | None:
    """글의 경로 낱말이 가리키는 커밋한 파일(Content 기준). 임포트 산출물이면 그 원본, 없으면 None."""
    path = token.lower().rstrip(".")
    if path in set_content:
        return path
    parts = path.split("/")
    for imported in list_imported:
        if imported.folder not in parts[:-1]:
            continue
        index = parts.index(imported.folder)
        source_folder = "/".join(parts[:index] + [imported.source_folder] + parts[index + 1:-1])
        stem = parts[-1].split(".")[0]
        # 산출물 이름은 원본 이름으로 시작한다(`tree.glb` → `tree.mesh` · `tree_leaf.material`) — 가장 긴 이름이 그 원본이다.
        for length in range(len(stem), 0, -1):
            if length < len(stem) and stem[length] != "_":
                continue
            for extension in imported.list_source_extension:
                candidate = f"{source_folder}/{stem[:length]}{extension}"
                if candidate in set_content:
                    return candidate
    return None


def collect_references(path: str, text: str, set_content: frozenset[str] | set[str], map_guid: Mapping[str, str], list_imported: Sequence[ImportedFolder]) -> set[str]:
    """글 파일 하나가 가리키는 Content 파일들."""
    plain = strip_comments(text)
    set_reference = {resolved for token in PATH_TOKEN.findall(plain) if (resolved := resolve_token(token, set_content, list_imported)) is not None}
    set_reference |= {map_guid[guid] for guid in GUID_TOKEN.findall(plain) if guid in map_guid}
    if path.endswith((".hlsl", ".hlsli")):
        folder = PurePosixPath(path).parent
        for name in INCLUDE.findall(plain):
            candidate = (folder / name).as_posix()
            if candidate in set_content:
                set_reference.add(candidate)
    set_reference.discard(path)
    return set_reference


def map_output_sources(list_output: Iterable[str], map_guid: Mapping[str, str], list_imported: Sequence[ImportedFolder]) -> dict[str, str]:
    """임포트 산출물(디스크의 리소스 id)의 GUID → 그 원본. 산출물 GUID 는 원본 GUID 와 산출물 이름의 해시라 거꾸로 풀 수 없어 있는 산출물로 만든다."""
    # shortcut: 임포트하지 않은 작업 트리(산출물 없음)에서는 산출물 GUID 로 적은 참조가 이어지지 않는다 — 원본 이름표(.meta)에 산출물 목록이 생기면 그것으로 바꾼다
    map_source_guid = {path.lower(): guid for guid, path in map_guid.items()}
    map_output: dict[str, str] = {}
    for output in list_output:
        for source, output_name in iterate_imported_sources(output, tuple(list_imported)):
            if source in map_source_guid:
                map_output[make_output_guid(map_source_guid[source], output_name)] = source
                break
    return map_output


def find_asset_of(content_path: str) -> str:
    """`.meta` 는 그 에셋의 것이다."""
    return content_path[:-len(META_EXTENSION)] if content_path.endswith(META_EXTENSION) else content_path


def find_asset_reach(list_changed: Iterable[str], map_reference: Mapping[str, set[str]]) -> list[Reach]:
    """바뀐 Content 파일(시나리오 · 골든 밖)마다 참조를 거꾸로 따라 닿는 시나리오."""
    map_referrer: dict[str, set[str]] = {}
    for path, set_reference in map_reference.items():
        for reference in set_reference:
            map_referrer.setdefault(reference, set()).add(path)
    list_reach: list[Reach] = []
    for changed in list_changed:
        asset = find_asset_of(changed)
        if asset.endswith(SCENARIO_SUFFIX) or "/golden/" in asset:
            continue
        set_seen = {asset}
        list_open = [asset]
        while list_open:
            for referrer in map_referrer.get(list_open.pop(), ()):
                if referrer not in set_seen:
                    set_seen.add(referrer)
                    list_open.append(referrer)
        list_scenario = tuple(sorted(path for path in set_seen if path.endswith(SCENARIO_SUFFIX)))
        if list_scenario:
            list_reach.append(Reach(changed, list_scenario))
    return list_reach


def find_source_reach(map_source: Mapping[str, str], map_scenario_text: Mapping[str, str]) -> list[Reach]:
    """바뀐 소스(경로 → 글)마다 그 파일이 정의한 감시 값 · 단계 · 로그 id 를 쓰는 시나리오."""
    map_use: dict[tuple[str, str], set[str]] = {}
    for scenario, text in map_scenario_text.items():
        plain = strip_comments(text)
        for kind, pattern in (("watch", WATCH_USE), ("step", STEP_USE), ("log", LOG_USE)):
            for name in pattern.findall(plain):
                map_use.setdefault((kind, name), set()).add(scenario)
    list_reach: list[Reach] = []
    for path, text in map_source.items():
        set_scenario: set[str] = set()
        for kind, pattern in (("watch", WATCH_DEFINITION), ("step", STEP_DEFINITION), ("log", LOG_DEFINITION)):
            for name in pattern.findall(text):
                set_scenario |= map_use.get((kind, name), set())
        if set_scenario:
            list_reach.append(Reach(path, tuple(sorted(set_scenario))))
    return list_reach


def sample(list_item: Sequence[str], limit: int) -> list[str]:
    """고르게 뽑은 @p limit 개(모자라면 전부) — 같은 입력이면 늘 같다."""
    if len(list_item) <= limit:
        return list(list_item)
    return [list_item[index * len(list_item) // limit] for index in range(limit)]


def select_reached(list_reach: Sequence[Reach], set_selected: set[str], per_file: int, total: int) -> tuple[dict[str, list[str]], list[str]]:
    """(시나리오 → 그것을 고른 바뀐 파일들, 알림 줄). 이미 고른 시나리오(`set_selected`)는 다시 세지 않는다."""
    map_chosen: dict[str, list[str]] = {}
    list_line: list[str] = []
    for reach in sorted(list_reach, key=lambda reach: (len(reach.list_scenario), reach.path)):   # 좁게 닿는 파일부터 — 상한에 걸려도 그 파일의 시나리오가 남는다
        list_rest = [scenario for scenario in reach.list_scenario if scenario not in set_selected]
        list_picked = sample(list_rest, per_file)
        if len(list_picked) < len(list_rest):
            list_line.append(f"{reach.path} → 시나리오 {len(list_rest)} 개 가운데 {len(list_picked)} 개(나머지는 gpu-release)")
        for scenario in list_picked:
            if scenario in map_chosen:
                map_chosen[scenario].append(reach.path)
            elif len(map_chosen) < total:
                map_chosen[scenario] = [reach.path]
    set_reached = {scenario for reach in list_reach for scenario in reach.list_scenario} - set_selected
    if len(map_chosen) < len(set_reached):
        list_line.append(f"바뀐 파일이 닿는 시나리오 {len(set_reached)} 개 가운데 {len(map_chosen)} 개를 고른다(상한 {total})")
    return map_chosen, list_line


def read_reach(project_root: Path, list_changed: Sequence[str], list_content: Sequence[str]) -> list[Reach]:
    """저장소에서 읽어 닿는 시나리오를 구한다. `list_changed` 는 저장소 기준 경로, `list_content` 는 추적하는 Content 파일(저장소 기준)이다."""
    from lint.asset_identity import load_rules
    content_root = project_root / "Content"
    list_id = [path[len("Content/"):] for path in list_content if path.startswith("Content/") and "/golden/" not in path]
    set_content = frozenset(path for path in list_id if not path.endswith(META_EXTENSION))
    read: Callable[[str], str] = lambda path: (content_root / path).read_text(encoding="utf-8", errors="replace")
    map_text = {path: read(path) for path in list_id if path.endswith(TEXT_SUFFIXES)}
    map_guid: dict[str, str] = {}
    for path in list_id:
        is_meta = path.endswith(META_EXTENSION)
        if not is_meta and path not in map_text:
            continue
        match = OWN_GUID.search((read(path) if is_meta else map_text[path])[:400])
        if match is not None:
            map_guid[match.group(1) or match.group(2)] = find_asset_of(path)
    list_imported = load_rules(project_root).list_imported
    list_output = [path.relative_to(content_root).as_posix() for folder in {imported.folder for imported in list_imported} for path in content_root.glob(f"**/{folder}/**/*") if path.is_file()]
    map_guid.update(map_output_sources(list_output, map_guid, list_imported))
    map_reference = {path: collect_references(path, text, set_content, map_guid, list_imported) for path, text in map_text.items()}
    list_changed_content = [path[len("Content/"):] for path in list_changed if path.startswith("Content/")]
    map_source = {path: (project_root / path).read_text(encoding="utf-8", errors="replace")
                  for path in list_changed if path.startswith("Source/") and path.endswith(".cpp") and "/Tests/" not in path}
    map_scenario_text = {path: text for path, text in map_text.items() if path.endswith(SCENARIO_SUFFIX)}
    return find_asset_reach(list_changed_content, map_reference) + find_source_reach(map_source, map_scenario_text)
