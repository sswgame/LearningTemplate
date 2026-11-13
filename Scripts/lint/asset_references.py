#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/asset_references.py

에셋 참조 검사 — 에셋은 경로 하나로 가리키므로(식별자 표 · `.meta` 는 없다, 언리얼 소프트 경로와 같다) 끊어진 경로를 커밋 전에 잡는다.

- 어떤 칸이 경로인지는 리플렉션 선언이 정하고 파서가 낸 표(`Config/Dev/AssetReferenceFields.generated.json`) 한 곳에 모인다. 도메인 · 확장자 · 임포트 폴더는
  `Config/Dev/AssetReferenceFields.json` 이다. 같은 파일을 C++ 검사(`App --check-asset-refs`,
  ctest `ToolCheckAssetRefs`)와 에디터 이름 바꾸기 · 옮기기(`AssetReferenceFields`), `py -3 -m Scripts move-asset` 이 읽는다.
  글 모양을 짐작하는 정규식으로 찾지 않는다 — 칸 이름이 목록에 있는 속성 · 요소 글 · JSON 키의 값만, 그 안에서도 도메인(`engine/` …)으로 시작하는 낱말만 경로다.
- 있음 판정은 **스테이지(인덱스)** 기준이다. 커밋하지 않은 파일은 없는 것으로 본다(작업 트리에만 있는 파일을 가리킨 커밋은 다른 체크아웃에서 끊긴다).
  임포트 산출물(`models/` · `textures/` · `heightfields/`)은 커밋하지 않으므로 같은 자리의 원본(`models_raw/<이름>.glb` 등)이 있으면 있다.
- 옮긴 에셋의 옛 id(도메인 뿌리의 `redirects.xml`, 런타임 `AssetRedirect`)는 엔진이 풀어 읽지만, 저장소 안의 참조는 새 id 로 고친다 — 남은 옛 참조를
  보고한다(언리얼 "Fix Up Redirectors" 자리). 리다이렉트는 저장소 밖(세이브 · 서버 · 배포된 팩)에 적힌 옛 경로를 살리려고 있다.
- 검사 범위는 인덱스의 모든 참조 파일(Content 의 글 에셋 + `listCMakeFile`)이다 — 지우거나 옮긴 에셋을 가리키던 다른 파일(역참조)도 같은 판정에서 잡힌다.
  참조 파일마다 뽑은 경로는 블롭 id 키 색인(`FactCache`)에 두어 커밋마다 바뀐 파일만 다시 읽는다. 저장소가 깨끗해야 하므로(ctest 가 지킨다) 걸린 것은 모두 이 커밋이 고칠 몫이다.
"""

from __future__ import annotations

import json
import re
import sys
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Iterable

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.git import run_git, run_git_bytes  # noqa: E402
from lint.index_cache import Facts, get_index_keys, open_facts  # noqa: E402
from lint.lint_scope import LintScope  # noqa: E402

CONFIG_PATH = "Config/Dev/AssetReferenceFields.json"
GENERATED_PATH = "Config/Dev/AssetReferenceFields.generated.json"   # 리플렉션 파서가 낸 경로 칸 표 — Scripts/generate/asset_reference_field_list.py 가 쓴다
CONTENT_FOLDER = "Content"
REDIRECT_FILE_NAME = "redirects.xml"   # 런타임 `AssetRedirect::kFileName` 과 같은 이름 — 도메인 뿌리마다 하나
# 경로 낱말을 가르는 글자(값 하나에 경로 여럿 — 쉼표 · 세미콜론 · 공백). 경로 안에는 오지 않는다(리소스 이름 규칙 `[a-z0-9_.-/]`).
TOKEN_SEPARATORS = ",; \t\r\n#"  # `#` 뒤는 파일 안의 항목 id 다(`_base="common/data/items.xml#helmet_knight"`) - 참조는 앞의 파일이다
# 이 글자가 든 낱말은 경로가 아니라 틀이다(`{bind:…}` · CMake `${…}` · 와일드카드).
TEMPLATE_CHARACTERS = "{}$*%<>"


@dataclass(frozen=True)
class ImportedFolder:
    """임포트 산출물 폴더 하나 — 산출물은 커밋하지 않고 같은 자리의 원본 폴더에서 만든다."""

    folder: str
    source_folder: str
    list_source_extension: tuple[str, ...]
    generated_referrers: bool   # 산출물 안의 글 에셋(임포터가 쓴 머티리얼 · 프리팹)은 참조자로 훑지 않는다 — 임포터의 몫이다


@dataclass(frozen=True)
class FieldConfig:
    """`Config/Dev/AssetReferenceFields.json` 의 내용."""

    list_domain: tuple[str, ...]
    list_unchecked_domain: tuple[str, ...]
    list_text_extension: tuple[str, ...]
    set_field: frozenset[str]
    list_imported: tuple[ImportedFolder, ...]
    list_cmake_file: tuple[str, ...]
    list_cmake_argument: tuple[str, ...]


@dataclass(frozen=True)
class Reference:
    """참조 하나 — 글 안 [start, end) 의 낱말이 경로 `path`(소문자 · `/`)다."""

    line: int
    field: str
    start: int
    end: int
    path: str


def _require(document: dict, key: str, kind: type):
    if key not in document or not isinstance(document[key], kind):
        raise ValueError(f"{CONFIG_PATH}: '{key}' 가 없거나 {kind.__name__} 가 아닙니다")
    return document[key]


def load_config(project_root: Path) -> FieldConfig:
    """규칙(도메인 · 확장자 · 임포트 폴더 · CMake)과 칸 목록(파서가 낸 표 하나)을 읽습니다. 파일이 없거나 칸이 빠지면 예외다(기본값으로 검사하지 않는다)."""
    document = json.loads((project_root / CONFIG_PATH).read_text(encoding="utf-8"))
    generated = json.loads((project_root / GENERATED_PATH).read_text(encoding="utf-8"))
    set_field: set[str] = set()
    for owner in _require(generated, "listOwner", list):
        set_field.update(_require(owner, "listField", list))
    list_imported = []
    for entry in _require(document, "listImportedFolder", list):
        list_imported.append(ImportedFolder(_require(entry, "folder", str), _require(entry, "sourceFolder", str),
                                           tuple(_require(entry, "listSourceExtension", list)), _require(entry, "generatedReferrers", bool)))
    return FieldConfig(tuple(_require(document, "listDomain", list)), tuple(_require(document, "listUncheckedDomain", list)),
                       tuple(_require(document, "listTextExtension", list)), frozenset(set_field), tuple(list_imported),
                       tuple(_require(document, "listCMakeFile", list)), tuple(_require(document, "listCMakeArgument", list)))


# ----------------------------------------------------------------------------------------------------------------------------------
# 뽑기 — C++ `AssetReferenceFields::extract` 와 같은 규칙이다(둘을 고치면 함께 고친다)
# ----------------------------------------------------------------------------------------------------------------------------------
def _append_tokens(text: str, value_start: int, value_end: int, field: str, config: FieldConfig, get_line_of: Callable[[int], int], out: list[Reference]) -> None:
    """값 [valueStart, valueEnd) 을 낱말로 갈라 도메인으로 시작하는 것만 참조로 더합니다."""
    cursor = value_start
    while cursor < value_end:
        while cursor < value_end and text[cursor] in TOKEN_SEPARATORS:
            cursor += 1
        token_start = cursor
        while cursor < value_end and text[cursor] not in TOKEN_SEPARATORS:
            cursor += 1
        token = text[token_start:cursor]
        if not token or any(character in TEMPLATE_CHARACTERS for character in token):
            continue
        normalized = token.replace("\\", "/").lower()
        if any(normalized.startswith(domain + "/") for domain in config.list_domain):
            out.append(Reference(get_line_of(token_start), field, token_start, cursor, normalized))


def _make_line_of(text: str) -> Callable[[int], int]:
    list_line_start = [0] + [match.end() for match in re.finditer("\n", text)]

    def get_line_of(offset: int) -> int:
        low, high = 0, len(list_line_start)
        while low + 1 < high:
            middle = (low + high) // 2
            if list_line_start[middle] <= offset:
                low = middle
            else:
                high = middle
        return low + 1
    return get_line_of


_XML_ATTRIBUTE = re.compile(r"([A-Za-z_][\w.:-]*)\s*=\s*(\"([^\"]*)\"|'([^']*)')")
_XML_TAG_NAME = re.compile(r"</?\s*([A-Za-z_][\w.:-]*)")


def _extract_xml(text: str, config: FieldConfig) -> list[Reference]:
    """XML: 목록에 든 이름의 속성 값과, 목록에 든 이름의 요소 안 글(`<_listX><item>경로</item></_listX>` 포함). 주석은 건너뛴다."""
    out: list[Reference] = []
    get_line_of = _make_line_of(text)
    list_open_field: list[str] = []
    cursor = 0
    length = len(text)
    while cursor < length:
        tag_start = text.find("<", cursor)
        text_end = length if tag_start < 0 else tag_start
        if list_open_field and text_end > cursor:
            _append_tokens(text, cursor, text_end, list_open_field[-1], config, get_line_of, out)
        if tag_start < 0:
            break
        if text.startswith("<!--", tag_start):
            comment_end = text.find("-->", tag_start + 4)
            cursor = length if comment_end < 0 else comment_end + 3
            continue
        if text.startswith("<?", tag_start) or text.startswith("<!", tag_start):
            tag_end = text.find(">", tag_start)
            cursor = length if tag_end < 0 else tag_end + 1
            continue
        tag_end = _find_tag_end(text, tag_start)
        tag = text[tag_start:tag_end]
        name_match = _XML_TAG_NAME.match(tag)
        if name_match is None:
            cursor = tag_start + 1
            continue
        name = name_match.group(1)
        if tag.startswith("</"):
            if list_open_field and list_open_field[-1] == name:
                list_open_field.pop()
        else:
            for attribute in _XML_ATTRIBUTE.finditer(tag, name_match.end()):
                if attribute.group(1) in config.set_field:
                    group = 3 if attribute.group(3) is not None else 4
                    _append_tokens(text, tag_start + attribute.start(group), tag_start + attribute.end(group), attribute.group(1), config, get_line_of, out)
            self_closing = tag.rstrip(">").rstrip().endswith("/")
            if not self_closing and (name in config.set_field or (list_open_field and name == "item")):
                list_open_field.append(name if name in config.set_field else list_open_field[-1])
        cursor = tag_end
    return out


def _find_tag_end(text: str, tag_start: int) -> int:
    """`>` 다음 자리입니다. 따옴표 안의 `>` 는 건너뛴다."""
    quote = ""
    for index in range(tag_start + 1, len(text)):
        character = text[index]
        if quote:
            if character == quote:
                quote = ""
        elif character in "\"'":
            quote = character
        elif character == ">":
            return index + 1
    return len(text)


_JSON_KEY = re.compile(r"\"([A-Za-z_][\w.-]*)\"\s*:\s*")
_JSON_STRING = re.compile(r"\"((?:[^\"\\]|\\.)*)\"")


def _extract_json(text: str, config: FieldConfig) -> list[Reference]:
    """JSON: 목록에 든 키의 글 값, 또는 글 배열의 원소."""
    out: list[Reference] = []
    get_line_of = _make_line_of(text)
    for key_match in _JSON_KEY.finditer(text):
        field = key_match.group(1)
        if field not in config.set_field:
            continue
        cursor = key_match.end()
        if cursor < len(text) and text[cursor] == "\"":
            value_match = _JSON_STRING.match(text, cursor)
            if value_match:
                _append_tokens(text, value_match.start(1), value_match.end(1), field, config, get_line_of, out)
        elif cursor < len(text) and text[cursor] == "[":
            array_end = text.find("]", cursor)
            for value_match in _JSON_STRING.finditer(text, cursor, len(text) if array_end < 0 else array_end):
                _append_tokens(text, value_match.start(1), value_match.end(1), field, config, get_line_of, out)
    return out


def _extract_cmake(text: str, config: FieldConfig) -> list[Reference]:
    """CMake: `listCMakeArgument` 의 인자(`--render-portraits=<경로>`) 값. 앞은 공백 · 따옴표 · 줄 처음이어야 한다."""
    out: list[Reference] = []
    get_line_of = _make_line_of(text)
    for argument in config.list_cmake_argument:
        prefix = argument + "="
        start = text.find(prefix)
        while start >= 0:
            boundary = start == 0 or text[start - 1] in " \t\r\n\"("
            value_start = start + len(prefix)
            value_end = value_start
            while value_end < len(text) and text[value_end] not in " \t\r\n\")":
                value_end += 1
            if boundary:
                _append_tokens(text, value_start, value_end, argument, config, get_line_of, out)
            start = text.find(prefix, value_end)
    return out


def extract_references(relative_path: str, text: str, config: FieldConfig) -> list[Reference]:
    """파일 하나(저장소 기준 경로로 종류를 고른다)의 참조를 뽑습니다."""
    normalized = relative_path.replace("\\", "/")
    if normalized in config.list_cmake_file:
        return _extract_cmake(text, config)
    if normalized.lower().endswith(".json"):
        return _extract_json(text, config)
    return _extract_xml(text, config)


# ----------------------------------------------------------------------------------------------------------------------------------
# 있음 판정
# ----------------------------------------------------------------------------------------------------------------------------------
class AssetSet:
    """있는 리소스 id(소문자, `Content/` 뒤)와 그 폴더들입니다."""

    def __init__(self, list_asset_id: Iterable[str]):
        self.set_file: set[str] = set()
        self.set_folder: set[str] = set()
        for asset_id in list_asset_id:
            lowered = asset_id.lower()
            self.set_file.add(lowered)
            slash = lowered.rfind("/")
            while slash > 0:
                folder = lowered[:slash]
                if folder in self.set_folder:
                    break
                self.set_folder.add(folder)
                slash = lowered.rfind("/", 0, slash)

    def contains(self, asset_id: str) -> bool:
        return asset_id in self.set_file or asset_id in self.set_folder


def is_imported_from_source(asset_id: str, assets: AssetSet, config: FieldConfig) -> bool:
    """`<…>/models/a/b/c.mesh` 처럼 임포트 산출물 자리면, 같은 자리의 원본(`<…>/models_raw/a/b/c.glb` → `a/b.glb` → `a.glb`)이 있는지 봅니다."""
    for imported in config.list_imported:
        marker = "/" + imported.folder + "/"
        position = asset_id.find(marker)
        while position >= 0:
            base = asset_id[:position] + "/" + imported.source_folder + "/"
            list_segment = asset_id[position + len(marker):].split("/")
            for count in range(len(list_segment), 0, -1):
                head = "/".join(list_segment[:count - 1])
                last = list_segment[count - 1]
                for stem in {last.split(".")[0], last.rsplit(".", 1)[0]}:
                    candidate = base + (head + "/" if head else "") + stem
                    if any(assets.contains(candidate + extension) for extension in imported.list_source_extension):
                        return True
            position = asset_id.find(marker, position + 1)
    return False


def resolves(asset_id: str, assets: AssetSet, config: FieldConfig) -> bool:
    return assets.contains(asset_id) or is_imported_from_source(asset_id, assets, config)


def is_checked(reference: Reference, config: FieldConfig) -> bool:
    return is_checked_path(reference.path, config)


def is_checked_path(asset_id: str, config: FieldConfig) -> bool:
    return not any(asset_id.startswith(domain + "/") for domain in config.list_unchecked_domain)


def is_referrer(relative_path: str, config: FieldConfig) -> bool:
    """참조를 훑는 파일인지 — Content 의 글 에셋(사본 도메인 · 임포터가 쓴 산출물 밖)과 CMake 파일."""
    normalized = relative_path.replace("\\", "/")
    if normalized in config.list_cmake_file:
        return True
    lowered = normalized.lower()
    if not lowered.startswith(CONTENT_FOLDER.lower() + "/") or not lowered.endswith(config.list_text_extension):
        return False
    if lowered.rsplit("/", 1)[-1] == REDIRECT_FILE_NAME:
        return False   # 리다이렉트 표는 옛 id 를 적는 글이다 — 참조가 아니다(`from` 은 다른 형식의 경로 칸 이름이기도 하다, C++ `AssetReferenceFields::isReferrer` 와 같다)
    asset_id = lowered[len(CONTENT_FOLDER) + 1:]
    if any(asset_id.startswith(domain + "/") for domain in config.list_unchecked_domain):
        return False
    return not any(("/" + imported.folder + "/") in "/" + asset_id and imported.generated_referrers for imported in config.list_imported)


# ----------------------------------------------------------------------------------------------------------------------------------
# 인덱스(스테이지) 읽기
# ----------------------------------------------------------------------------------------------------------------------------------
def list_index_files(project_root: Path) -> list[str]:
    result = run_git(["ls-files", "-z", "--cached"], cwd=project_root)
    if result.return_code != 0:
        raise RuntimeError(f"git ls-files 실패: {result.stderr.strip()}")
    return [path for path in result.stdout.split("\0") if path]


def read_index_blobs(project_root: Path, list_relative_path: list[str]) -> dict[str, str]:
    """인덱스의 내용(`:경로`)을 한 프로세스(`git cat-file --batch`)로 읽습니다."""
    request = "".join(f":{path}\n" for path in list_relative_path).encode("utf-8")
    return_code, output, error_text = run_git_bytes(["cat-file", "--batch"], cwd=project_root, input_bytes=request, hook_repository=True)   # 커밋하는 인덱스의 블롭
    if return_code != 0:
        raise RuntimeError(f"git cat-file 실패: {error_text.strip()}")
    map_text: dict[str, str] = {}
    cursor = 0
    for path in list_relative_path:
        header_end = output.index(b"\n", cursor)
        header = output[cursor:header_end].decode("utf-8", "replace").split()
        cursor = header_end + 1
        if len(header) < 3 or header[1] != "blob":
            raise RuntimeError(f"git cat-file: '{path}' 를 읽지 못했습니다({' '.join(header)})")
        size = int(header[2])
        map_text[path] = output[cursor:cursor + size].decode("utf-8", "replace")
        cursor += size + 1
    return map_text




def is_redirect_table(relative_path: str) -> bool:
    """`Content/<도메인>/redirects.xml`(게임 팩은 `Content/game/<팩>/redirects.xml`)인지."""
    parts = relative_path.replace("\\", "/").split("/")
    return parts[0] == CONTENT_FOLDER and parts[-1] == REDIRECT_FILE_NAME and len(parts) in (3, 4)


def parse_redirects(map_text: dict[str, str]) -> dict[str, str]:
    """리다이렉트 표들의 옛 id → 새 id 입니다. 틀린 표는 런타임이 기동을 멈추므로 여기서는 읽히는 줄만 모은다."""
    map_old_to_new: dict[str, str] = {}
    for relative_path, text in map_text.items():
        try:
            root = ElementTree.fromstring(text)
        except ElementTree.ParseError:
            continue
        for node in root.iter("Redirect"):
            source, target = node.get("from"), node.get("to")
            if source and target:
                map_old_to_new[source] = target
    return map_old_to_new


def make_redirect_table_id(asset_id: str) -> str:
    """리소스 id 가 속한 도메인의 표 id(`<도메인>/redirects.xml`) — 도메인은 첫 마디, `game/<팩>` · `dlc/<묶음>` 은 두 마디(C++ `AssetRedirect::makeTableID`)."""
    parts = asset_id.split("/")
    domain = "/".join(parts[:2]) if parts[0] in ("game", "dlc") and len(parts) > 1 else parts[0]
    return f"{domain}/{REDIRECT_FILE_NAME}"


REDIRECT_ID_CHARACTERS = frozenset("abcdefghijklmnopqrstuvwxyz0123456789_.-/")


def add_redirects(table_text: str | None, list_move: list[tuple[str, str]]) -> str:
    """표 글(없으면 None)에 옮기기(옛 id, 새 id)를 더한 글을 돌려준다 — C++ `AssetRedirect::addToXMLText` 와 같은 규칙 · 같은 글 모양(에디터 옮기기와 diff 가 같게).

    옮긴 자리(새 id)를 옛 id 로 둔 줄은 뺀다(그 id 에 다시 에셋이 산다 — 남기면 되돌린 옮기기가 순환이 된다). 같은 옛 id 의 줄은 새 줄로 바꾼다. 사슬은 둔다.
    틀린 표 · 맞춤 꼴이 아닌 id · 순환이면 ValueError — 런타임이 기동을 멈출 표를 쓰지 않는다.
    """
    list_entry: list[tuple[str, str]] = []
    if table_text is not None:
        root = ElementTree.fromstring(table_text)   # 틀린 XML 은 ParseError(ValueError 가 아니다 — 부른 쪽이 그대로 멈춘다)
        if root.tag != "Redirects":
            raise ValueError(f"리다이렉트 표의 뿌리가 <Redirects> 가 아니다: <{root.tag}>")
        for node in root:
            if node.tag != "Redirect" or not node.get("from") or not node.get("to"):
                raise ValueError(f"리다이렉트 표에 틀린 줄이 있다: <{node.tag} {node.attrib}>")
            list_entry.append((node.get("from"), node.get("to")))
    for old_id, new_id in list_move:
        for asset_id in (old_id, new_id):
            if not asset_id or not set(asset_id) <= REDIRECT_ID_CHARACTERS or asset_id.startswith("/") or "//" in asset_id or "/../" in f"/{asset_id}/":
                raise ValueError(f"'{asset_id}' 는 맞춤 꼴 리소스 id 가 아니다")
        if old_id == new_id:
            raise ValueError(f"'{old_id}' 를 자기 자신으로 옮긴다")
        list_entry = [entry for entry in list_entry if entry[0] not in (old_id, new_id)] + [(old_id, new_id)]
    map_old_to_new = dict(list_entry)
    if len(map_old_to_new) != len(list_entry):
        raise ValueError("리다이렉트 표에 같은 옛 id 가 두 번 있다")
    for start in map_old_to_new:
        target, steps = map_old_to_new[start], 0
        while target in map_old_to_new:
            target, steps = map_old_to_new[target], steps + 1
            if steps > len(map_old_to_new):
                raise ValueError(f"리다이렉트가 '{start}' 에서 순환한다")
    return "<Redirects>\n" + "".join(f'\t<Redirect from="{source}" to="{target}" />\n' for source, target in list_entry) + "</Redirects>\n"


def collect_problems(list_relative_path: list[str], map_text: dict[str, str], assets: AssetSet, config: FieldConfig, map_redirect: dict[str, str] | None = None) -> list[str]:
    """참조 파일들에서 없는 경로와 옮긴 에셋의 옛 id 를 `파일:줄: 칸 = '경로'` 로 모읍니다."""
    return collect_reference_problems({path: extract_references(path, map_text[path], config) for path in list_relative_path}, assets, config, map_redirect)


def collect_reference_problems(map_reference: dict[str, list[Reference]], assets: AssetSet, config: FieldConfig, map_redirect: dict[str, str] | None = None) -> list[str]:
    list_problem: list[str] = []
    map_redirect = map_redirect or {}
    for relative_path, list_reference in map_reference.items():
        for reference in list_reference:
            if not is_checked(reference, config):
                continue
            if reference.path in map_redirect:
                list_problem.append(f"{relative_path}:{reference.line}: {reference.field} = '{reference.path}' 는 옮긴 에셋의 옛 id 다 — '{map_redirect[reference.path]}' 로 고친다")
            elif not resolves(reference.path, assets, config):
                list_problem.append(f"{relative_path}:{reference.line}: {reference.field} = '{reference.path}' 가 없습니다")
    return list_problem


def list_meta_problems(list_index: list[str]) -> list[str]:
    """인덱스에 오른 `Content/**/*.meta` 입니다 — 에셋은 경로 하나로 가리키고 `.meta` 는 없다.
    `.gitignore` 는 새 파일만 막고, 옛 커밋을 옮기는 체리픽 · 리베이스는 그 파일을 그대로 들여온다(작업자 브랜치 병합에서 아홉 개가 다시 들어왔다)."""
    prefix = CONTENT_FOLDER + "/"
    return [f"{path}: .meta 는 쓰지 않는다 — 지운다(에셋은 경로로만 가리킨다)" for path in list_index if path.startswith(prefix) and path.endswith(".meta")]


def check_asset_references(project_root: Path, scope: LintScope) -> list[str]:
    """
    린트 규칙(`rules.py`): 인덱스 전체의 끊어진 참조 — 지우거나 옮긴 에셋을 가리키던 다른 파일(역참조)까지 잡으려고 판정은 늘 전체로 낸다.
    참조 파일마다 뽑은 경로는 블롭 id 를 키로 한 색인(`FactCache`)에 두어 바뀐 파일만 다시 읽는다. 커밋 훅은 Content · CMake 참조 파일이 바뀌었을 때만 돈다
    (칸 목록이 바뀌면 `Rule.inputs` 가 저장소 전체로 돌린다).
    """
    config = load_config(project_root)
    if not scope.whole_repo and not scope.touches((CONTENT_FOLDER,), files=config.list_cmake_file):
        return []
    map_key = get_index_keys(project_root)
    list_index = list(map_key)
    prefix = CONTENT_FOLDER + "/"
    assets = AssetSet(path[len(prefix):] for path in list_index if path.startswith(prefix))
    list_referrer = [path for path in list_index if is_referrer(path, config)]
    list_redirect = [path for path in list_index if is_redirect_table(path)]
    map_redirect = parse_redirects(read_index_blobs(project_root, list_redirect)) if list_redirect else {}
    def open_references(**options: Any) -> Facts:
        return open_facts(project_root, "asset-refs", [__file__, str(project_root / CONFIG_PATH), str(project_root / GENERATED_PATH)], list_referrer,
                         lambda path, text: [[reference.line, reference.field, reference.start, reference.end, reference.path] for reference in extract_references(path, text, config)],
                         map_key=map_key, read_texts=lambda list_path: read_index_blobs(project_root, list_path), contribute=lambda _, list_row: [row[4] for row in list_row], **options)

    # 판정은 참조한 경로만으로 난다(옮긴 옛 id · 없는 경로) — 요약(경로마다 참조 수)으로 거르고, 걸린 것이 있을 때만 사실 전체를 읽어 줄 번호를 낸다.
    list_failing = [path for path in open_references().summary if is_checked_path(path, config) and (path in map_redirect or not resolves(path, assets, config))]
    if not list_failing:
        return list_meta_problems(list_index)
    map_fact = open_references(want=list_referrer).map_fact   # 걸린 때만 — 줄 번호를 내려고 모든 조각을 읽는다
    map_reference = {path: [Reference(*row) for row in list_row] for path, list_row in map_fact.items()}
    return list_meta_problems(list_index) + collect_reference_problems(map_reference, assets, config, map_redirect)


def check_working_tree(project_root: Path) -> list[str]:
    """작업 트리 전체 검사(손으로 돌릴 때) — 인덱스의 파일 목록, 작업 트리의 내용."""
    config = load_config(project_root)
    list_index = [path for path in list_index_files(project_root) if (project_root / path).is_file()]
    prefix = CONTENT_FOLDER + "/"
    assets = AssetSet(path[len(prefix):] for path in list_index if path.startswith(prefix))
    list_referrer = [path for path in list_index if is_referrer(path, config)]
    map_text = {path: (project_root / path).read_text(encoding="utf-8", errors="replace") for path in list_referrer}
    map_redirect = parse_redirects({path: (project_root / path).read_text(encoding="utf-8", errors="replace") for path in list_index if is_redirect_table(path)})
    return list_meta_problems(list_index) + collect_problems(list_referrer, map_text, assets, config, map_redirect)


def run_self_check() -> None:
    """리다이렉트 표 쓰기 규칙 — C++ `AssetRedirectTest.MovesAddLinesAndMovingBackReplacesThem` 과 같은 사례 · 같은 글."""
    assert make_redirect_table_id("game/empty/maps/a.scene.xml") == "game/empty/redirects.xml"
    assert make_redirect_table_id("common/data/a.xml") == "common/redirects.xml"
    first = add_redirects(None, [("common/data/a.xml", "common/data/b.xml")])
    assert first == '<Redirects>\n\t<Redirect from="common/data/a.xml" to="common/data/b.xml" />\n</Redirects>\n', first
    chained = add_redirects(first, [("common/data/b.xml", "common/data/c.xml")])
    assert parse_redirects({"t": chained}) == {"common/data/a.xml": "common/data/b.xml", "common/data/b.xml": "common/data/c.xml"}
    back = add_redirects(chained, [("common/data/c.xml", "common/data/a.xml")])
    assert parse_redirects({"t": back}) == {"common/data/b.xml": "common/data/c.xml", "common/data/c.xml": "common/data/a.xml"}, back
    for bad in (lambda: add_redirects("<Moves/>", [("common/a.xml", "common/b.xml")]), lambda: add_redirects(None, [("Common/A.xml", "common/b.xml")]),
                lambda: add_redirects(None, [("common/a.xml", "common/a.xml")])):
        try:
            bad()
        except ValueError:
            continue
        raise AssertionError("틀린 표 · id 를 받았다")


def main(argv: list[str] | None = None) -> int:
    import argparse
    from common.exit_code import ExitCode
    from common.paths import get_project_root
    parser = argparse.ArgumentParser(description="작업 트리의 에셋 참조(Config/Dev/AssetReferenceFields.json 의 칸)가 모두 있는지 본다")
    parser.add_argument("--self-check", action="store_true", help="리다이렉트 표 쓰기 규칙만 본다")
    if parser.parse_args(argv).self_check:
        run_self_check()
        print("[AssetReferences] self-check OK")
        return ExitCode.PASSED
    list_problem = check_working_tree(get_project_root())
    for problem in list_problem:
        print(f"  - {problem}")
    print(f"[AssetReferences] {'FAIL' if list_problem else 'OK'} — 끊어진 참조 {len(list_problem)}건")
    return ExitCode.VIOLATION if list_problem else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
