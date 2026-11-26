"""
Scripts/lint/asset_references.py

에셋 참조 검사 — 경로 글로 적은 참조(CMake · 정체성이 없는 폴더 · 작업 사본처럼 글로 남는 것)가 끊어지면 커밋 전에 잡는다.
GUID 로 적은 참조(`guid:<32자>`)는 색인을 짓는 쪽(`App --check-asset-refs` · 쿠킹)이 본다. 정체성(GUID · `.meta`)의 짝은 `asset_identity.py`(`asset-ids`)가 본다.

- 어떤 칸이 경로인지는 리플렉션 선언이 정하고 파서가 낸 표(`Config/Dev/AssetReferenceFields.generated.json`) 한 곳에 모인다. 도메인 · 확장자는
  `Config/Dev/AssetReferenceFields.json`, 임포트 폴더는 `Config/Dev/AssetIdentity.json` 이다. 같은 파일을 C++ 검사(`App --check-asset-refs`,
  ctest `ToolCheckAssetRefs`)와 에디터 이름 바꾸기 · 옮기기(`AssetReferenceFields`), `py -3 -m Scripts move-asset` 이 읽는다.
  글 모양을 짐작하는 정규식으로 찾지 않는다 — 칸 이름이 목록에 있는 속성 · 요소 글 · JSON 키의 값만, 그 안에서도 도메인(`engine/` …)으로 시작하는 낱말만 경로다.
- 있음 판정은 스테이지(인덱스) 기준이다. 커밋하지 않은 파일은 없는 것으로 본다(작업 트리에만 있는 파일을 가리킨 커밋은 다른 체크아웃에서 끊긴다).
  임포트 산출물(`models/` · `textures/` · `heightfields/`)은 커밋하지 않으므로 같은 자리의 원본(`models_raw/<이름>.glb` 등)이 있으면 있다.
- 검사 범위는 인덱스의 모든 참조 파일(Content 의 글 에셋 + `listCMakeFile`)이다 — 지우거나 옮긴 에셋을 가리키던 다른 파일(역참조)도 같은 판정에서 잡힌다.
  참조 파일마다 뽑은 경로는 블롭 id 키 색인(`index_cache`)에 두어 커밋마다 바뀐 파일만 다시 읽는다. 저장소가 깨끗해야 하므로(ctest 가 지킨다) 걸린 것은 모두 이 커밋이 고칠 몫이다.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable

from common.git import run_git, run_git_bytes
from lint.asset_identity import META_EXTENSION, ImportedFolder, iterate_imported_sources, load_rules

CONFIG_PATH = "Config/Dev/AssetReferenceFields.json"
GENERATED_PATH = "Config/Dev/AssetReferenceFields.generated.json"   # 리플렉션 파서가 낸 경로 칸 표 — Scripts/generate/asset_reference_field_list.py 가 쓴다
CONTENT_FOLDER = "Content"
REDIRECT_FILE_NAME = "redirects.xml"   # 런타임 `AssetRedirect::kFileName` 과 같은 이름 — 도메인 뿌리마다 하나
# 경로 낱말을 가르는 글자(값 하나에 경로 여럿 — 쉼표 · 세미콜론 · 공백). 경로 안에는 오지 않는다(리소스 이름 규칙 `[a-z0-9_.-/]`).
TOKEN_SEPARATORS = ",; \t\r\n#"  # `#` 뒤는 파일 안의 항목 id 다(`_base="common/data/items.xml#helmet_knight"`) - 참조는 앞의 파일이다
# 이 글자가 든 낱말은 경로가 아니라 틀이다(`{bind:…}` · CMake `${…}` · 와일드카드).
TEMPLATE_CHARACTERS = "{}$*%<>"


@dataclass(frozen=True)
class FieldConfig:
    """`Config/Dev/AssetReferenceFields.json` 의 내용과 임포트 산출물 폴더(`Config/Dev/AssetIdentity.json`)."""

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
    list_imported = load_rules(project_root).list_imported
    return FieldConfig(tuple(_require(document, "listDomain", list)), tuple(_require(document, "listUncheckedDomain", list)),
                       tuple(_require(document, "listTextExtension", list)), frozenset(set_field), list_imported,
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
    """임포트 산출물 자리(`<…>/models/a/b/c.mesh`)면 같은 자리의 원본이 있는지 봅니다(`iterate_imported_sources`). 저장소 밖 원본은 서술만 있다(짝 잃은 서술은 `asset_identity` 가 잡는다)."""
    return any(assets.contains(source_id) or assets.contains(source_id + META_EXTENSION) for source_id, _ in iterate_imported_sources(asset_id, config.list_imported))


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
        return False   # 리다이렉트 표는 저장된 기록의 옛 경로를 적는 글이다 — 참조가 아니다(`from` 은 다른 형식의 경로 칸 이름이기도 하다, C++ `AssetReferenceFields::isReferrer` 와 같다)
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




def make_redirect_table_id(asset_id: str) -> str:
    """리소스 id 가 속한 도메인의 표 id(`<도메인>/redirects.xml`) — 도메인은 첫 마디, `game/<팩>` · `dlc/<묶음>` 은 두 마디(C++ `AssetRedirect::makeTableID`)."""
    parts = asset_id.split("/")
    domain = "/".join(parts[:2]) if parts[0] in ("game", "dlc") and len(parts) > 1 else parts[0]
    return f"{domain}/{REDIRECT_FILE_NAME}"


def collect_problems(list_relative_path: list[str], map_text: dict[str, str], assets: AssetSet, config: FieldConfig) -> list[str]:
    """참조 파일들에서 없는 경로를 `파일:줄: 칸 = '경로'` 로 모읍니다."""
    return collect_reference_problems({path: extract_references(path, map_text[path], config) for path in list_relative_path}, assets, config)


def collect_reference_problems(map_reference: dict[str, list[Reference]], assets: AssetSet, config: FieldConfig) -> list[str]:
    list_problem: list[str] = []
    for relative_path, list_reference in map_reference.items():
        for reference in list_reference:
            if not is_checked(reference, config):
                continue
            if not resolves(reference.path, assets, config):
                list_problem.append(f"{relative_path}:{reference.line}: {reference.field} = '{reference.path}' 가 없습니다")
    return list_problem


def check_working_tree(project_root: Path) -> list[str]:
    """작업 트리 전체 검사(손으로 돌릴 때) — 인덱스의 파일 목록, 작업 트리의 내용."""
    config = load_config(project_root)
    list_index = [path for path in list_index_files(project_root) if (project_root / path).is_file()]
    prefix = CONTENT_FOLDER + "/"
    assets = AssetSet(path[len(prefix):] for path in list_index if path.startswith(prefix))
    list_referrer = [path for path in list_index if is_referrer(path, config)]
    map_text = {path: (project_root / path).read_text(encoding="utf-8", errors="replace") for path in list_referrer}
    return collect_problems(list_referrer, map_text, assets, config)
