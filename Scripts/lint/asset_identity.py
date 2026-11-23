#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/asset_identity.py

에셋 정체성(GUID) 검사와 정리 — C++ `AssetDatabase`(Source/Engine/Asset/AssetDatabase.h)와 같은 규칙 · 같은 규칙 파일(`Config/Dev/AssetIdentity.json`)이다.

  py -3 -m Scripts asset-ids              # 작업 트리 검사(결함을 찍는다)
  py -3 -m Scripts asset-ids --fix        # 없는 GUID 를 만들고, 짝 잃은 .meta 를 새 자리 원본에 다시 붙이고, 복사본에 새 GUID 를 준다
  py -3 -m Scripts asset-ids --self-check # 머리 읽기 · 쓰기 규칙(C++ AssetGUIDTest 와 같은 사례)과 규칙 파일 대조

- 우리 형식 글(`listHeaderSuffix` — XML)은 맨 앞 처리 지시 `<?asset guid="…"?>`(XML 선언 바로 뒤)에, 원본(png · ogg · glb …)은 곁의 `<파일>.meta`
  (`<AssetMeta _guid="…" _source="<파일 이름>" />`, 그 에셋만의 임포트 예외는 자식 `<_listImportOverride>` 의 `<AssetImportOverride _key _value/>`)에 정체성을 든다. GUID 는 소문자 16진 32 자 하나의 철자다.
- `--fix` 는 조용히 새 GUID 를 만들지 않는다: 머리를 잃은 글은 HEAD 의 같은 경로에 있던 GUID 를 되살리고, 짝 잃은 `.meta` 는 원본을 내용(HEAD 의 블롭 해시)
  · 이름으로 찾아 다시 붙인다. 붙일 곳이 하나로 정해지지 않으면 오류다. 새 GUID 는 정체성이 처음 생기는 파일(새 글 · 새 원본)과 복사본에만 준다.
- 커밋 훅(린트 규칙 `asset-ids` — `check_asset_identity` 가 `check_staged_asset_identity` 를 부른다): 바뀐 파일과 그 짝의 머리 없음 · 짝 없음, 새 GUID 와 인덱스의 겹침(`git grep`), 그리고 HEAD 에 있던
  경로의 GUID 가 바뀐 것(한 커밋에서 같은 경로를 지우고 다시 만들었다 — 내용 바꾸기는 GUID 를 지킨다)을 막는다. 저장소 전체는 `asset-ids` 가 본다.
"""

from __future__ import annotations

import fnmatch
import json
import secrets
import sys
import tempfile
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator
from xml.sax.saxutils import quoteattr

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.git import run_git_bytes, run_git_in_hook_repository  # noqa: E402
from lint.lint_scope import LintScope  # noqa: E402

RULES_PATH = "Config/Dev/AssetIdentity.json"
CONTENT_FOLDER = "Content"
META_EXTENSION = ".meta"
DECLARATION = "<?xml"
OPENING = "<?asset"
END_PI = "?>"
GUID_KEY = 'guid="'
GUID_LENGTH = 32
HEX_DIGITS = frozenset("0123456789abcdef")

SKIP, HEADER, SIDECAR, SOURCE = "skip", "header", "sidecar", "source"
FOUND, MISSING, INVALID = "found", "missing", "invalid"


@dataclass(frozen=True)
class ImportedFolder:
    """임포트 산출물 폴더 하나 — 산출물은 커밋하지 않고 같은 자리의 원본 폴더에서 만든다(C++ `AssetImportedFolder`)."""

    folder: str
    source_folder: str
    list_source_extension: tuple[str, ...]
    generated_referrers: bool   # 산출물 안의 글 에셋(임포터가 쓴 머티리얼 · 프리팹)은 참조자로 훑지 않는다 — 임포터의 몫이다


@dataclass(frozen=True)
class Rules:
    """`Config/Dev/AssetIdentity.json` 의 내용(C++ `AssetIdentityRules`)."""

    list_header_suffix: tuple[str, ...]
    list_skip_pattern: tuple[str, ...]
    list_imported: tuple[ImportedFolder, ...]


@dataclass(frozen=True)
class Issue:
    """결함 하나(C++ `AssetIdentityIssue`). `asset_id` 는 Content 기준 리소스 id."""

    kind: str
    asset_id: str
    other: str = ""

    def __str__(self) -> str:
        path = f"{CONTENT_FOLDER}/{self.asset_id}"
        return {
            "duplicate": f"{path}: GUID 가 {CONTENT_FOLDER}/{self.other} 와 겹친다 — 복사본에는 새 GUID 를 준다(`asset-ids --fix`)",
            "missing-header": f"{path}: 머리 `<?asset guid?>` 가 없다(`asset-ids --fix`)",
            "invalid-header": f"{path}: 머리 `<?asset guid?>` 를 읽지 못했다(꼴 · GUID 표기) — 손으로 고친다",
            "missing-meta": f"{path}: 곁에 .meta 가 없다(`asset-ids --fix`)",
            "orphan-meta": f"{path}: 원본이 없다 — 원본과 함께 옮기거나 지운다(`asset-ids --fix` 가 옮긴 원본을 내용 · 이름으로 찾아 다시 붙인다)",
            "invalid-meta": f"{path}: .meta 를 읽지 못했거나 다른 원본을 적었다 — 손으로 고친다",
            "unreadable": f"{path}: 읽지 못했다(UTF-8 이 아니다)",
            "changed": f"{path}: HEAD 의 GUID {self.other} 가 바뀌었다 — 같은 경로를 지우고 다시 만들었나? 내용 바꾸기는 GUID 를 지킨다(`asset-ids --fix` 는 바뀐 GUID 를 되돌리지 않는다 — HEAD 의 머리 · .meta 를 되살린다)",
        }[self.kind]


def load_rules(project_root: Path) -> Rules:
    document = json.loads((project_root / RULES_PATH).read_text(encoding="utf-8"))
    list_value = []
    for key in ("listHeaderSuffix", "listSkipPattern"):
        value = document.get(key)
        if not isinstance(value, list) or not value or not all(isinstance(item, str) and item for item in value):
            raise ValueError(f"{RULES_PATH}: '{key}' 는 빈 글이 없는 목록이어야 한다")
        list_value.append(tuple(item.lower() for item in value))
    list_imported = []
    for entry in document.get("listImportedFolder") or []:
        if not (isinstance(entry, dict) and set(entry) == {"folder", "sourceFolder", "listSourceExtension", "generatedReferrers"} and isinstance(entry["generatedReferrers"], bool)
                and all(isinstance(text, str) and text for text in (entry["folder"], entry["sourceFolder"], *entry["listSourceExtension"]))):
            raise ValueError(f"{RULES_PATH}: listImportedFolder 의 원소가 틀렸다: {entry}")
        list_imported.append(ImportedFolder(entry["folder"], entry["sourceFolder"], tuple(entry["listSourceExtension"]), entry["generatedReferrers"]))
    if not list_imported:
        raise ValueError(f"{RULES_PATH}: listImportedFolder 가 없다")
    return Rules(*list_value, tuple(list_imported))


def iterate_imported_sources(output_id: str, list_imported: tuple[ImportedFolder, ...]) -> Iterator[tuple[str, str]]:
    """산출물 자리의 (원본 후보 리소스 id, 산출물 이름)을 가까운 것부터 낸다(있는지는 보지 않는다) — C++ `AssetIdentityRules::collectImportedSources` 와 같은 규칙.

    원본은 산출물 자리의 어느 조상이다: `models/a/b/c.mesh` 는 `models_raw/a/b/c.glb` → `a/b.glb` → `a.glb` 순서다."""
    lowered = output_id.lower()
    for imported in list_imported:
        marker = f"/{imported.folder}/"
        position = lowered.find(marker)
        while position >= 0:
            tail_start = position + len(marker)
            segment_end = len(lowered)
            while segment_end > tail_start:
                slash = lowered.rfind("/", 0, segment_end)
                segment = lowered[slash + 1:segment_end]
                for stem_length in dict.fromkeys(end if end >= 0 else len(segment) for end in (segment.find("."), segment.rfind("."))):
                    if stem_length == 0:
                        continue
                    stem_end = slash + 1 + stem_length
                    for extension in imported.list_source_extension:
                        yield f"{lowered[:position]}/{imported.source_folder}/{lowered[tail_start:stem_end]}{extension}", lowered[stem_end:]
                if slash < tail_start:
                    break
                segment_end = slash
            position = lowered.find(marker, position + 1)


_MASK64 = (1 << 64) - 1
_FNV_OFFSET64 = 14695981039346656037
_FNV_PRIME64 = 1099511628211
_GOLDEN_RATIO64 = 0x9E3779B97F4A7C15


def _fnv64(data: bytes, seed: int) -> int:
    value = seed
    for byte in data:
        value = ((value ^ byte) * _FNV_PRIME64) & _MASK64
    return value


def _mix64(value: int) -> int:
    value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & _MASK64
    value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & _MASK64
    return value ^ (value >> 31)


def make_name_guid(name: str) -> str:
    """C++ `AssetGUID::fromName( name )` — 씨앗이 다른 FNV-1a 64 두 개를 섞은 값(ASCII 대소문자 무시)."""
    data = bytes(byte + 32 if 0x41 <= byte <= 0x5A else byte for byte in name.encode("utf-8"))
    high, low = _mix64(_fnv64(data, _FNV_OFFSET64)), _mix64(_fnv64(data, _FNV_OFFSET64 ^ _GOLDEN_RATIO64))
    return f"{high:016x}{low if high or low else _GOLDEN_RATIO64:016x}"


def make_output_guid(source_guid: str, output_name: str) -> str:
    """임포트 산출물의 GUID — C++ `AssetGUID::fromName( 원본 GUID, 산출물 이름 )`."""
    return make_name_guid(source_guid + output_name)


def find_output_guid(output_id: str, map_path_to_guid: dict[str, str], list_imported: tuple[ImportedFolder, ...]) -> str | None:
    """산출물 자리의 GUID — 원본이 색인(소문자 리소스 id → GUID)에 있으면 그 원본과 산출물 이름에서, 없으면 None. 산출물이 디스크에 없어도 된다."""
    for source_id, output_name in iterate_imported_sources(output_id, list_imported):
        source_guid = map_path_to_guid.get(source_id)
        if source_guid is not None:
            return make_output_guid(source_guid, output_name)
    return None


def classify(asset_id: str, rules: Rules) -> str:
    lower = asset_id.lower()
    if any(fnmatch.fnmatchcase(lower, pattern) for pattern in rules.list_skip_pattern):
        return SKIP
    if lower.endswith(META_EXTENSION):
        return SIDECAR
    if lower.endswith(rules.list_header_suffix):
        return HEADER
    return SOURCE


def is_guid(text: str) -> bool:
    return len(text) == GUID_LENGTH and set(text) <= HEX_DIGITS and set(text) != {"0"}


def create_guid() -> str:
    while True:
        guid = secrets.token_hex(GUID_LENGTH // 2)
        if is_guid(guid):
            return guid


def _skip_space(text: str, position: int) -> int:
    while position < len(text) and text[position] in " \t\r\n":
        position += 1
    return position


def _starts_pi(text: str, position: int, opening: str) -> bool:
    after = position + len(opening)
    return text.startswith(opening, position) and after < len(text) and (text[after] in " \t\r\n?")


def _find_head(text: str) -> tuple[int, int, int] | None:
    """(넣을 자리, 머리 시작, 머리 끝) — 머리가 없으면 시작 · 끝은 -1. XML 선언이 닫히지 않았으면 None."""
    position = _skip_space(text, 1 if text.startswith("﻿") else 0)
    if _starts_pi(text, position, DECLARATION):
        end = text.find(END_PI, position)
        if end < 0:
            return None
        position = end + len(END_PI)
        if text.startswith("\r", position):
            position += 1
        if text.startswith("\n", position):
            position += 1
    insert_at = position
    position = _skip_space(text, position)
    if _starts_pi(text, position, OPENING):
        end = text.find(END_PI, position)
        if end >= 0:
            return insert_at, position, end + len(END_PI)
    return insert_at, -1, -1


def read_header(text: str) -> tuple[str, str | None]:
    """C++ `AssetGUIDHeader::read` 와 같은 규칙 — (found · missing · invalid, GUID)."""
    head = _find_head(text)
    if head is None:
        return INVALID, None
    _, begin, end = head
    if begin < 0:   # 머리는 맨 앞 자리의 것 하나다 — 그 뒤의 글(주석이 적은 머리 모양)은 보지 않는다
        return MISSING, None
    body = text[begin + len(OPENING):end - len(END_PI)]
    position = _skip_space(body, 0)
    if not body.startswith(GUID_KEY, position):
        return INVALID, None
    position += len(GUID_KEY)
    quote = body.find('"', position)
    if quote < 0 or _skip_space(body, quote + 1) != len(body) or not is_guid(body[position:quote]):
        return INVALID, None
    return FOUND, body[position:quote]


def write_header(text: str, guid: str) -> str:
    """C++ `AssetGUIDHeader::write` 와 같은 규칙 — 머리가 있으면 그 자리를, 없으면 선언 뒤(없으면 맨 앞)에 한 줄."""
    head = _find_head(text)
    if head is None:
        raise ValueError("XML 선언이 닫히지 않았다")
    insert_at, begin, end = head
    header = f'{OPENING} guid="{guid}"{END_PI}'
    if begin >= 0:
        return text[:begin] + header + text[end:]
    new_line = "\r\n" if "\r\n" in text else "\n"
    return text[:insert_at] + header + new_line + text[insert_at:]


Overrides = tuple[tuple[str, str], ...]   # 임포트 예외 (칸 이름, 값 글) — C++ `AssetImportOverride`


def make_meta_text(source_id: str, guid: str, overrides: Overrides = ()) -> str:
    """C++ `AssetMeta::makeText`(리플렉션 직렬화)와 같은 글. 예외가 없으면 칸을 쓰지 않는다(`SkipIfEmpty`)."""
    head = f"<AssetMeta _guid={quoteattr(guid)} _source={quoteattr(source_id.rsplit('/', 1)[-1])}"
    if not overrides:
        return head + " />"
    items = "".join(f"\t\t<AssetImportOverride _key={quoteattr(key)} _value={quoteattr(value)} />\n" for key, value in overrides)
    return f"{head}>\n\t<_listImportOverride>\n{items}\t</_listImportOverride>\n</AssetMeta>"


def parse_meta(text: str) -> tuple[str, str, Overrides] | None:
    """(GUID, 원본, 임포트 예외) — 다른 꼴(모르는 속성 · 자식 · 틀린 GUID)이면 None(C++ 은 엄격한 리플렉션 읽기)."""
    try:
        root = ElementTree.fromstring(text)
    except ElementTree.ParseError:
        return None
    if root.tag != "AssetMeta" or set(root.attrib) != {"_guid", "_source"} or not is_guid(root.attrib["_guid"]):
        return None
    if len(root) == 0:
        return root.attrib["_guid"], root.attrib["_source"], ()
    block = root[0]
    if len(root) != 1 or block.tag != "_listImportOverride" or block.attrib:
        return None
    if any(item.tag != "AssetImportOverride" or set(item.attrib) != {"_key", "_value"} or len(item) for item in block):
        return None
    return root.attrib["_guid"], root.attrib["_source"], tuple((item.attrib["_key"], item.attrib["_value"]) for item in block)


def read_identity(asset_id: str, text: str | None, rules: Rules) -> str | None:
    """정체성을 드는 파일(머리 글 · .meta)의 GUID — 없거나 읽지 못하면 None."""
    kind = classify(asset_id, rules)
    if text is None:
        return None
    if kind == HEADER:
        state, guid = read_header(text)
        return guid if state == FOUND else None
    if kind == SIDECAR:
        meta = parse_meta(text)
        return meta[0] if meta else None
    return None


def collect_issues(map_text: dict[str, str | None], rules: Rules) -> tuple[list[Issue], dict[str, str]]:
    """C++ `AssetDatabase::build` 와 같은 규칙. `mapText` 는 리소스 id → 글(머리 글 · .meta 만, 읽지 못하면 None). 반환은 (결함, GUID → 리소스 id)."""
    list_issue: list[Issue] = []
    map_guid: dict[str, str] = {}
    set_source = {asset_id.lower() for asset_id in map_text if classify(asset_id, rules) == SOURCE}
    set_sidecar = {asset_id.lower() for asset_id in map_text if classify(asset_id, rules) == SIDECAR}

    def register(guid: str, asset_id: str) -> None:
        if guid in map_guid:
            list_issue.append(Issue("duplicate", asset_id, map_guid[guid]))
        else:
            map_guid[guid] = asset_id

    for asset_id in sorted(map_text):
        kind = classify(asset_id, rules)
        text = map_text[asset_id]
        if kind == SKIP:
            continue
        if kind in (HEADER, SIDECAR) and text is None:
            list_issue.append(Issue("unreadable", asset_id))
        elif kind == HEADER:
            state, guid = read_header(text)
            if state == FOUND:
                register(guid, asset_id)
            else:
                list_issue.append(Issue("missing-header" if state == MISSING else "invalid-header", asset_id))
        elif kind == SIDECAR:
            source_id = asset_id[:-len(META_EXTENSION)]
            if source_id.lower() not in set_source:
                list_issue.append(Issue("orphan-meta", asset_id))
                continue
            meta = parse_meta(text)
            if meta is None or meta[1] != source_id.rsplit("/", 1)[-1]:
                list_issue.append(Issue("invalid-meta", asset_id))
                continue
            register(meta[0], source_id)
        elif (asset_id + META_EXTENSION).lower() not in set_sidecar:
            list_issue.append(Issue("missing-meta", asset_id))
    return list_issue, map_guid


# --- git ------------------------------------------------------------------------------------------------------------------------


def _git(project_root: Path, list_argument: list[str], request: bytes | None = None) -> bytes:
    # 커밋 훅이 부르면 지금 커밋하는 인덱스(`git commit -a` 의 임시 인덱스)를 본다 — 훅 밖에서는 보통 git 과 같다
    return_code, output, error = run_git_bytes(list_argument, cwd=project_root, input_bytes=request or b"", hook_repository=True)
    if return_code != 0:
        raise RuntimeError(f"git {' '.join(list_argument[:2])} 실패: {error.strip()}")
    return output


def read_blobs(project_root: Path, list_spec: list[str]) -> dict[str, bytes | None]:
    """`git cat-file --batch` 로 `<rev>:<경로>` 들을 읽습니다. 없는 것은 None."""
    output = _git(project_root, ["cat-file", "--batch"], "".join(f"{spec}\n" for spec in list_spec).encode("utf-8")) if list_spec else b""
    map_blob: dict[str, bytes | None] = {}
    cursor = 0
    for spec in list_spec:
        header_end = output.index(b"\n", cursor)
        header = output[cursor:header_end].split()
        cursor = header_end + 1
        if len(header) < 3 or header[1] != b"blob":
            map_blob[spec] = None   # `<spec> missing`
            continue
        size = int(header[2])
        map_blob[spec] = output[cursor:cursor + size]
        cursor += size + 1
    return map_blob


def _decode(data: bytes | None) -> str | None:
    if data is None:
        return None
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return None


def list_worktree_ids(project_root: Path) -> list[str]:
    """작업 트리에 있는 Content 파일(추적 + 무시하지 않는 새 파일)의 리소스 id — 무시하는 임포트 산출물은 보지 않는다."""
    output = _git(project_root, ["ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", CONTENT_FOLDER])
    prefix = CONTENT_FOLDER + "/"
    list_id = {path[len(prefix):] for path in output.decode("utf-8").split("\0") if path.startswith(prefix)}
    return sorted(asset_id for asset_id in list_id if (project_root / prefix / asset_id).is_file())


def read_worktree_texts(project_root: Path, list_id: list[str], rules: Rules) -> dict[str, str | None]:
    map_text: dict[str, str | None] = {}
    for asset_id in list_id:
        kind = classify(asset_id, rules)
        map_text[asset_id] = _decode((project_root / CONTENT_FOLDER / asset_id).read_bytes()) if kind in (HEADER, SIDECAR) else ""
    return map_text


def read_head_identities(project_root: Path, list_id: list[str], rules: Rules) -> dict[str, str]:
    """HEAD 의 같은 경로에 있던 GUID — 리소스 id(머리 글 · .meta) → GUID."""
    list_spec = [f"HEAD:{CONTENT_FOLDER}/{asset_id}" for asset_id in list_id]
    map_blob = read_blobs(project_root, list_spec)
    map_guid: dict[str, str] = {}
    for asset_id, spec in zip(list_id, list_spec):
        guid = read_identity(asset_id, _decode(map_blob[spec]), rules)
        if guid:
            map_guid[asset_id] = guid
    return map_guid


# --- 커밋 훅 --------------------------------------------------------------------------------------------------------------------


def _pair_of(asset_id: str, rules: Rules) -> str | None:
    """원본 ↔ 곁의 .meta 짝 — 머리 글 · 건너뛰는 파일은 짝이 없다."""
    kind = classify(asset_id, rules)
    if kind == SOURCE:
        return asset_id + META_EXTENSION
    if kind == SIDECAR:
        return asset_id[:-len(META_EXTENSION)]
    return None


def _find_guid_owners(project_root: Path, list_guid: list[str], rules: Rules) -> dict[str, list[str]]:
    """인덱스에서 GUID 들을 든 파일 — GUID → 정체성을 든 리소스 id 목록. `git grep` 이 인덱스를 훑는다(전체 블롭을 읽지 않는다 — 에셋이 수십만이어도 커밋마다 돈다)."""
    if not list_guid:
        return {}
    # 패턴은 파일로 넘긴다 — 한 커밋의 GUID 가 수천이면 명령줄 길이(Windows 32K)를 넘는다.
    with tempfile.NamedTemporaryFile("w", encoding="ascii", suffix=".txt", delete=False) as pattern_file:
        pattern_file.write("\n".join(list_guid) + "\n")
    try:
        return_code, output, error = run_git_bytes(["grep", "--cached", "-l", "-F", "-f", pattern_file.name, "--", CONTENT_FOLDER], cwd=project_root, input_bytes=b"",
                                                   hook_repository=True)
    finally:
        Path(pattern_file.name).unlink()
    if return_code not in (0, 1):   # 1 은 "찾은 것 없음"
        raise RuntimeError(f"git grep 실패: {error.strip()}")
    prefix = CONTENT_FOLDER + "/"
    list_hit = [line[len(prefix):] for line in output.decode("utf-8").splitlines() if line.startswith(prefix)]
    list_hit = [asset_id for asset_id in list_hit if classify(asset_id, rules) in (HEADER, SIDECAR)]
    map_blob = read_blobs(project_root, [f":{prefix}{asset_id}" for asset_id in list_hit])
    set_wanted = set(list_guid)
    map_holder: dict[str, list[str]] = {}
    for asset_id in list_hit:
        guid = read_identity(asset_id, _decode(map_blob[f":{prefix}{asset_id}"]), rules)
        if guid in set_wanted:
            map_holder.setdefault(guid, []).append(asset_id if classify(asset_id, rules) == HEADER else asset_id[:-len(META_EXTENSION)])
    return map_holder


def check_staged_asset_identity(project_root: Path, scope: LintScope | None = None) -> list[str] | None:
    """
    커밋 전 검사 — 스테이지에 Content 변경이 없으면 None. 있으면 **바뀐 파일과 그 짝**만 본다(인덱스 전체를 읽지 않는다 — 에셋 수가 수십만이 되어도 커밋마다 돈다):
    머리 없음 · 깨짐, 원본만 지우거나 옮겨 남은 .meta, .meta 없는 새 원본, 새 GUID 가 인덱스의 다른 파일과 겹침(`git grep`), HEAD 에 있던 경로의 GUID 가 바뀜.
    저장소 전체는 `py -3 -m Scripts asset-ids` 가 본다(묶음 검증).
    """
    status = run_git_in_hook_repository(["diff", "--cached", "--name-status", "--no-renames"], cwd=project_root)
    if status.return_code != 0:
        raise RuntimeError(f"git diff --cached 실패: {status.stderr.strip()}")
    prefix = CONTENT_FOLDER + "/"
    list_change = [line.split("\t", 1) for line in status.stdout.splitlines() if "\t" in line]
    list_change = [(code.strip(), path.strip().replace("\\", "/")) for code, path in list_change]
    # 범위가 주어지면 그 파일만 — 변경 종류(A · M · D)는 인덱스에서 읽고, 판정 대상은 커밋 훅이 넘긴 범위다(범위 밖 스테이지 파일을 읽지 않는다 — lint-shape 의 읽기 상한).
    if scope is not None:
        set_scope = {*scope.list_file, *scope.list_removed}
        list_change = [(code, path) for code, path in list_change if path in set_scope]
    list_change = [(code, path[len(prefix):]) for code, path in list_change if path.startswith(prefix)]
    if not list_change:
        return None
    rules = load_rules(project_root)
    set_index = set(_git(project_root, ["ls-files", "-z", "--cached", "--", CONTENT_FOLDER]).decode("utf-8").split("\0"))
    set_local: set[str] = set()
    for _, asset_id in list_change:
        for candidate in (asset_id, _pair_of(asset_id, rules)):
            if candidate and prefix + candidate in set_index:
                set_local.add(candidate)
    list_carrier = sorted(asset_id for asset_id in set_local if classify(asset_id, rules) in (HEADER, SIDECAR))
    map_blob = read_blobs(project_root, [f":{prefix}{asset_id}" for asset_id in list_carrier])
    map_text: dict[str, str | None] = {asset_id: "" for asset_id in set_local}
    for asset_id in list_carrier:
        map_text[asset_id] = _decode(map_blob[f":{prefix}{asset_id}"])
    list_issue, map_guid = collect_issues(map_text, rules)
    # 이 커밋의 GUID 가 인덱스의 다른 파일과 겹치는가(복사).
    for guid, list_holder in _find_guid_owners(project_root, sorted(map_guid), rules).items():
        for holder in sorted(list_holder):
            if holder != map_guid[guid] and not any(issue.kind == "duplicate" and issue.asset_id == holder for issue in list_issue):
                list_issue.append(Issue("duplicate", max(holder, map_guid[guid]), min(holder, map_guid[guid])))
    # HEAD 에 있던 경로의 GUID 가 바뀌었는가 — 고친(M) 파일만 본다(같은 경로를 지우고 다시 만들면 git 에는 고침으로 보인다).
    list_modified = [asset_id for code, asset_id in list_change if code == "M" and classify(asset_id, rules) in (HEADER, SIDECAR)]
    map_head = read_head_identities(project_root, list_modified, rules)
    for asset_id in list_modified:
        current = read_identity(asset_id, map_text.get(asset_id), rules)
        if asset_id in map_head and current and current != map_head[asset_id]:
            list_issue.append(Issue("changed", asset_id, map_head[asset_id]))
    return [str(issue) for issue in list_issue]


# --- 고치기 ---------------------------------------------------------------------------------------------------------------------


def _hash_objects(project_root: Path, list_path: list[str]) -> dict[str, str]:
    """작업 트리 파일의 git 블롭 id(필터 적용 — LFS 포인터면 포인터의 id)."""
    if not list_path:
        return {}
    output = _git(project_root, ["hash-object", "--stdin-paths"], "".join(f"{path}\n" for path in list_path).encode("utf-8"))
    return dict(zip(list_path, output.decode("ascii").split()))


def _head_blob_ids(project_root: Path, list_path: list[str]) -> dict[str, str]:
    if not list_path:
        return {}
    output = _git(project_root, ["cat-file", "--batch-check"], "".join(f"HEAD:{path}\n" for path in list_path).encode("utf-8"))
    map_id: dict[str, str] = {}
    for path, line in zip(list_path, output.decode("utf-8").splitlines()):
        parts = line.split()
        if len(parts) >= 2 and parts[1] == "blob":
            map_id[path] = parts[0]
    return map_id


def _write(path: Path, text: str) -> None:
    path.write_bytes(text.encode("utf-8"))


def fix_worktree(project_root: Path) -> int:
    """`--fix` — 고친 뒤 남은 결함 수를 돌려준다(0 이면 깨끗하다)."""
    rules = load_rules(project_root)
    content_root = project_root / CONTENT_FOLDER
    list_id = list_worktree_ids(project_root)
    map_text = read_worktree_texts(project_root, list_id, rules)
    list_issue, _ = collect_issues(map_text, rules)
    map_issue: dict[str, list[Issue]] = {}
    for issue in list_issue:
        map_issue.setdefault(issue.kind, []).append(issue)

    list_carrier = [asset_id for asset_id in list_id if classify(asset_id, rules) in (HEADER, SIDECAR)]
    map_head = read_head_identities(project_root, list_carrier, rules)
    set_used = {guid for asset_id in list_carrier if (guid := read_identity(asset_id, map_text[asset_id], rules))}
    counts = {"header-new": 0, "header-restored": 0, "meta-new": 0, "meta-reattached": 0, "copy-renewed": 0}

    def set_identity(asset_id: str, guid: str) -> None:
        """머리 글이면 머리를, 원본이면 곁의 .meta 를 쓴다."""
        if classify(asset_id, rules) == HEADER:
            text = write_header(map_text[asset_id] or "", guid)
            _write(content_root / asset_id, text)
            map_text[asset_id] = text
        else:
            copied = parse_meta(map_text.get(asset_id + META_EXTENSION) or "")   # 복사본은 베낀 임포트 예외를 지킨다(C++ `assignNewIdentity` 와 같다)
            _write(content_root / (asset_id + META_EXTENSION), make_meta_text(asset_id, guid, copied[2] if copied else ()))
        set_used.add(guid)

    # ① 겹친 GUID — HEAD 에서 그 GUID 를 들던 경로가 원본이고 나머지가 복사본이다. 원본을 가를 수 없으면(둘 다 새 파일) 정렬 순서로 먼저인 쪽이 남는다.
    map_duplicate: dict[str, list[str]] = {}
    for issue in map_issue.get("duplicate", []):
        map_duplicate.setdefault(issue.other, []).append(issue.asset_id)
    def carrier_of(asset_id: str) -> str:
        """정체성을 든 파일 — 머리 글은 그 자신, 원본은 곁의 .meta."""
        return asset_id if classify(asset_id, rules) == HEADER else asset_id + META_EXTENSION

    for first, list_copy in map_duplicate.items():
        list_holder = [first] + list_copy
        guid = read_identity(carrier_of(first), map_text.get(carrier_of(first)), rules)
        list_owner = [holder for holder in list_holder if map_head.get(carrier_of(holder)) == guid]
        keep = list_owner[0] if len(list_owner) == 1 else first
        for holder in list_holder:
            if holder != keep:
                set_identity(holder, create_guid())
                counts["copy-renewed"] += 1

    # ② 머리 없는 글 — HEAD 의 같은 경로에 있던 GUID 를 되살린다(편집 도구가 머리를 빼고 다시 썼다). 처음 생긴 글만 새 GUID.
    for issue in map_issue.get("missing-header", []):
        restored = map_head.get(issue.asset_id)
        if restored and restored not in set_used:
            set_identity(issue.asset_id, restored)
            counts["header-restored"] += 1
        else:
            set_identity(issue.asset_id, create_guid())
            counts["header-new"] += 1

    # ③ 짝 잃은 .meta — 원본을 옮긴 것이다. HEAD 의 옛 원본 블롭과 내용이 같은 새 원본, 없으면 이름이 같은 새 원본이 하나뿐일 때 다시 붙인다.
    list_orphan = [issue.asset_id for issue in map_issue.get("orphan-meta", [])]
    list_lonely = [issue.asset_id for issue in map_issue.get("missing-meta", [])]
    map_lonely_blob = _hash_objects(project_root, [f"{CONTENT_FOLDER}/{asset_id}" for asset_id in list_lonely])
    map_old_blob = _head_blob_ids(project_root, [f"{CONTENT_FOLDER}/{orphan[:-len(META_EXTENSION)]}" for orphan in list_orphan])
    list_unattached: list[str] = []
    for orphan in list_orphan:
        old_source = orphan[:-len(META_EXTENSION)]
        meta = parse_meta(map_text.get(orphan) or "")
        old_blob = map_old_blob.get(f"{CONTENT_FOLDER}/{old_source}")
        list_match = [asset_id for asset_id in list_lonely if old_blob and map_lonely_blob.get(f"{CONTENT_FOLDER}/{asset_id}") == old_blob]
        if len(list_match) != 1:
            list_match = [asset_id for asset_id in list_lonely if asset_id.rsplit("/", 1)[-1] == old_source.rsplit("/", 1)[-1]]
        if meta is None or len(list_match) != 1:
            list_unattached.append(orphan)
            continue
        target = list_match[0]
        _write(content_root / (target + META_EXTENSION), make_meta_text(target, meta[0], meta[2]))
        (content_root / orphan).unlink()
        list_lonely.remove(target)
        counts["meta-reattached"] += 1

    # ④ 남은 .meta 없는 원본 — 처음 들어온 원본이다.
    for asset_id in list_lonely:
        set_identity(asset_id, create_guid())
        counts["meta-new"] += 1

    print(f"[asset-ids] 고침 — 새 머리 {counts['header-new']} · 되살린 머리 {counts['header-restored']} · 새 .meta {counts['meta-new']} · "
          f"다시 붙인 .meta {counts['meta-reattached']} · 복사본 새 GUID {counts['copy-renewed']}")
    for orphan in list_unattached:
        print(f"  - {CONTENT_FOLDER}/{orphan}: 다시 붙일 원본을 하나로 정하지 못했다 — 원본을 지웠으면 .meta 도 지우고, 옮겼으면 그 자리 곁으로 손으로 옮긴다")
    list_left, _ = collect_issues(read_worktree_texts(project_root, list_worktree_ids(project_root), rules), rules)
    for issue in list_left:
        print(f"  - {issue}")
    return len(list_left)


def write_keeping_identity(path: Path, text: str) -> None:
    """생성기가 우리 형식 글을 통째로 다시 쓸 때 — 있던 파일의 머리(정체성)를 새 글에 옮겨 쓴다(C++ `XMLDocument::saveToFile` 과 같은 규칙)."""
    if path.is_file() and read_header(text)[0] == MISSING:
        state, guid = read_header(path.read_text(encoding="utf-8"))
        if state == FOUND:
            text = write_header(text, guid)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def run_self_check(project_root: Path | None = None) -> None:
    """C++ `AssetGUIDTest` 와 같은 사례 · 규칙 파일의 산출물 폴더가 참조 칸 표의 임포트 폴더와 같은가."""
    if project_root is None:
        from common.paths import get_project_root
        project_root = get_project_root()
    sample = "0123456789abcdef0011223344556677"
    with_declaration = write_header('<?xml version="1.0" encoding="utf-8"?>\n<!-- note -->\n<Root/>\n', sample)
    assert with_declaration == f'<?xml version="1.0" encoding="utf-8"?>\n<?asset guid="{sample}"?>\n<!-- note -->\n<Root/>\n', with_declaration
    assert read_header(with_declaration) == (FOUND, sample)
    assert write_header("<Root/>\r\n", sample) == f'<?asset guid="{sample}"?>\r\n<Root/>\r\n'
    other = create_guid()
    assert read_header(write_header(with_declaration, other)) == (FOUND, other) and len(write_header(with_declaration, other)) == len(with_declaration)
    assert read_header('<?xml version="1.0"?>\n<Root/>')[0] == MISSING
    assert read_header('<?asset guid="nope"?>\n<Root/>')[0] == INVALID
    assert read_header(f'<?asset guid="{sample}" extra="1"?>\n<Root/>')[0] == INVALID
    assert read_header(f'<Root/>\n<?asset guid="{sample}"?>')[0] == MISSING
    assert read_header(f'<?asset guid="{sample}"?>\n<!-- <?asset guid?> -->\n<Root/>') == (FOUND, sample)
    assert read_header('<?xml version="1.0"\n<Root/>')[0] == INVALID
    assert not is_guid(sample.upper()) and not is_guid("0" * GUID_LENGTH) and not is_guid(sample[:-1])
    assert parse_meta(make_meta_text("a/b/hit.ogg", sample)) == (sample, "hit.ogg", ())
    overrides = (("animationCodec", "raw"), ("generateMips", "false"))
    assert parse_meta(make_meta_text("a/horse.glb", sample, overrides)) == (sample, "horse.glb", overrides)
    assert parse_meta(make_meta_text("a/horse.glb", sample, overrides).replace('_value="raw"', '_value="raw" _note="x"')) is None
    assert parse_meta(f'<AssetMeta _guid="{sample}" _source="hit.ogg"><Other /></AssetMeta>') is None
    assert parse_meta(f'<AssetMeta _guid="{sample}" _source="hit.ogg" _extra="1" />') is None

    rules = load_rules(project_root)
    assert make_output_guid("0123456789abcdef0011223344556677", ".MESH") == "28016f06802dfbf1a257d90c3347b2fb"   # C++ AssetGUIDTest.OutputIdentityComesFromSourceAndOutputName 과 같은 글
    assert next(iterate_imported_sources("common/models/a/b/c.mesh", rules.list_imported)) == ("common/models_raw/a/b/c.glb", ".mesh")
    map_source = {"common/models_raw/a.glb": sample}
    assert find_output_guid("common/models/a/b/c.mesh", map_source, rules.list_imported) == make_output_guid(sample, "/b/c.mesh")
    assert find_output_guid("common/models/a.mesh", map_source, rules.list_imported) == make_output_guid(sample, ".mesh")
    assert find_output_guid("common/models/_shared/x.material", map_source, rules.list_imported) is None
    assert find_output_guid("common/prefabs/a.prefab.xml", map_source, rules.list_imported) is None
    list_issue, _ = collect_issues({"a/x.prefab.xml": write_header("<P/>", sample), "a/y.prefab.xml": write_header("<P/>", sample), "a/z.ogg": "",
                                  "a/w.ogg.meta": make_meta_text("a/w.ogg", other), "a/golden/q.ppm": ""}, rules)
    assert [(issue.kind, issue.asset_id) for issue in list_issue] == [("orphan-meta", "a/w.ogg.meta"), ("duplicate", "a/y.prefab.xml"), ("missing-meta", "a/z.ogg")], list_issue


def check_asset_identity(project_root: Path, scope: LintScope) -> list[str]:
    """린트 규칙(`rules.py`): 커밋 훅은 스테이지의 바뀐 파일과 그 짝만(`check_staged_asset_identity`), `--all` 은 작업 트리 전체."""
    if not scope.whole_repo:
        return check_staged_asset_identity(project_root, scope) or []
    rules = load_rules(project_root)
    list_issue, _ = collect_issues(read_worktree_texts(project_root, list_worktree_ids(project_root), rules), rules)
    return [str(issue) for issue in list_issue]


def main(argv: list[str] | None = None) -> int:
    import argparse
    from common.exit_code import ExitCode
    from common.paths import get_project_root
    parser = argparse.ArgumentParser(description="에셋 정체성(GUID — 우리 형식 글의 머리 · 원본의 .meta)을 검사하고 --fix 로 채운다")
    parser.add_argument("--fix", action="store_true", help="없는 GUID 를 만들고, 짝 잃은 .meta 를 다시 붙이고, 복사본에 새 GUID 를 준다")
    parser.add_argument("--self-check", action="store_true", help="머리 읽기 · 쓰기 규칙과 규칙 파일 대조만 본다")
    arguments = parser.parse_args(argv)
    project_root = get_project_root()
    if arguments.self_check:
        run_self_check(project_root)
        print("[asset-ids] self-check OK")
        return ExitCode.PASSED
    if arguments.fix:
        left = fix_worktree(project_root)
        print(f"[asset-ids] {'FAIL' if left else 'OK'} — 남은 결함 {left}건")
        return ExitCode.VIOLATION if left else ExitCode.PASSED
    rules = load_rules(project_root)
    list_issue, map_guid = collect_issues(read_worktree_texts(project_root, list_worktree_ids(project_root), rules), rules)
    for issue in list_issue:
        print(f"  - {issue}")
    print(f"[asset-ids] {'FAIL' if list_issue else 'OK'} — 정체성 {len(map_guid)}개, 결함 {len(list_issue)}건")
    return ExitCode.VIOLATION if list_issue else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
