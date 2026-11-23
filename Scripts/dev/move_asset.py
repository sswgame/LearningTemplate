"""
에셋 파일 · 폴더를 옮기고(이름 바꾸기 포함) 그것을 가리키는 참조를 한 번에 고친다 — 언리얼 Fix Up Redirectors 를 옮기면서 바로 하는 것과 같다.

  py -3 -m Scripts move-asset common/prefabs/props/bow.prefab.xml common/prefabs/weapons/bow.prefab.xml [--dry-run]
  py -3 -m Scripts move-asset game/playground/materials game/playground/surfaces
  py -3 -m Scripts move-asset <옛1> <새1> <옛2> <새2> …   # 여러 쌍을 차례로 — 끊어진 참조 검사(5)는 끝에 한 번

경로는 리소스 id(`Content/` 뒤 — 앞에 `Content/` 를 붙여도 된다)다. 하는 일:
  1. `git mv` — 원본 곁의 정체성 서술(`<파일>.meta`)도 함께 옮기고 그 안의 원본 이름을 새 이름으로 쓴다(GUID 는 그대로 — 옮겨도 정체성은 같다).
  2. 참조 파일(Content 의 글 에셋 · 테스트 CMake)에서 경로 칸(`Config/Dev/AssetReferenceFields.json` — 커밋 전 검사 · `App --check-asset-refs` ·
     에디터 이름 바꾸기와 같은 목록)의 값만 새 경로로. 폴더면 그 아래 경로 전부, 원본(`models_raw/x.glb`)이면 그 임포트 산출물 경로(`models/x…`)도.
     칸 밖의 글(주석 · 문서 · 출처 표)은 고치지 않는다 — 끝에 남은 옛 경로를 찍는다.
  3. 옛 id 도메인의 리다이렉트 표(`Content/<도메인>/redirects.xml`)에 옮긴 파일마다 줄을 더한다 — 저장소 밖(세이브 · 서버 · 배포된 팩)에 적힌 옛 id 가 산다
     (언리얼이 옮긴 자리에 리다이렉터를 남기는 것과 같다). 규칙은 에디터 옮기기와 같다(`add_redirects` = C++ `AssetRedirect::addToXMLText`).
     도메인 뿌리를 통째로 옮기면(`game/<옛>` → `game/<새>`) 옛 도메인 폴더가 사라지므로 새 도메인 표에 접두 한 줄(`<PrefixRedirect>`)만 둔다
     — 언리얼 `[CoreRedirects]` PackageRedirects 의 접두 꼴. 함께 옮겨진 표의 `to` 는 새 팩 아래로 고친다.
  4. 팩 이름 바꾸기(`game/<옛>` → `game/<새>`)면 팩 이름이 든 이름도 함께: 골든 파일(`<Scenario name>.<이름>.<백엔드>…` — `AutomationGoldenUtil::makeGoldenID`)의
     `game.<옛>.` 접두, 글 파일(C++ · 파이썬 · CMake · JSON · XML) 속 시나리오 이름 `game.<옛>.`, 콘텐츠 경로 문자열 `game/<옛>/`, CTest 체인 이름
     `Scenario.<옛>`(`Test/Scenario/CMakeLists.txt` 가 팩 이름으로 짓는다 — 격리 표 · 로그 이름), 따옴표로 끝나는 도메인 `"game/<옛>"`(게임 프리셋 `_packRoot`), 팩 줄기 `game_<옛>`,
     씬 청크 이름 `scene_<옛>_`, 대소문자 · 구분자가 다른 경로 `Game/<Old>/` · `Game\\<Old>/`(옛 이름의 대소문자 꼴을 새 이름에 옮긴다).
     정확한 접두만 바꾼다(앞 글자가 낱말 글자면 다른 낱말이다) — 일반 낱말 `Empty` 는 건드리지 않는다. 리다이렉트 표는 옛 id 를 적으려고 있어 건너뛴다.
     팩 흡수(`game/<옛>` → `game/<새>/<하위>` — 폐기할 게임을 허브 팩의 미니게임 폴더로)면 콘텐츠 경로 `game/<옛>/` 와 도메인 `"game/<옛>"` 만 새 자리로 바꾸고,
     옛 도메인이 사라지므로 리다이렉트는 새 도메인 표에 접두 한 줄이다. 시나리오 이름 `game.<옛>.` · 체인 `Scenario.<옛>` 은 새 자리를 시나리오마다 고르므로 남은 목록에 찍는다.
     시나리오 파일 하나를 옮기면(`….scenario.xml` → `….scenario.xml`) 이름이 파일을 따라간다: `<Scenario name>` 의 끝 마디는 새 파일 이름, 팩 마디는 새 팩이고
     그 시나리오의 골든(`golden/<이름>.<골든>.<백엔드>…`)과 글 속 그 이름도 함께 바꾼다(골든은 이름만 — 그림은 다시 찍지 않는다).
  5. 작업 트리 전체를 검사해 끊어진 참조가 남았으면 알린다(실패 코드).
`--resume` 은 파일 옮기기를 건너뛰고 2–5 만 다시 돈다 — 규칙을 넓힌 뒤 이미 옮긴 자리에 적용한다(바꾸기는 모두 정확한 접두라 두 번 돌아도 같다).
임포트 산출물은 커밋하지 않으므로 원본을 옮긴 뒤에는 `App --import-models` 등으로 다시 임포트한다.
"""

from __future__ import annotations

import argparse
import re
import time
from dataclasses import dataclass
from pathlib import Path

from common.exit_code import ExitCode
from common.git import run_git
from common.paths import get_project_root
from lint.asset_identity import META_EXTENSION, create_guid, make_meta_text, parse_meta, write_header, write_keeping_identity
from lint.asset_references import (FieldConfig, add_redirects, check_working_tree, extract_references, is_referrer, CONTENT_FOLDER, REDIRECT_FILE_NAME, list_index_files, load_config,
                                  make_redirect_table_id)

NAME_CHARACTERS = set("abcdefghijklmnopqrstuvwxyz0123456789_.-/")
# 옮긴 뒤 옛 경로가 칸 밖에 남았는지 훑는 글 파일 — 경로 칸의 글 에셋 · 문서 · 출처 표 · 테스트 CMake.
LEFTOVER_SUFFIX = (".xml", ".json", ".material", ".md", ".txt", ".cpp", ".h", ".py", ".cmake", ".js")
# 팩 이름 바꾸기에서 팩 이름 문자열(시나리오 이름 · 콘텐츠 경로)을 고치는 글 파일 — 문서(.md)는 사람이 읽고 고친다(남은 목록에 찍힌다).
# 번역 팩(.po 의 `#:` 출처 줄) · 셰이더 · 머티리얼 · 셸 스크립트 · .gitignore 예외 줄도 팩 경로를 적는다.
# `Tools/` 의 우리 확장(launch-args · comment-fold)은 자바스크립트다.
PACK_STRING_SUFFIX = (".cpp", ".h", ".hpp", ".inl", ".py", ".cmake", ".json", ".xml", ".po", ".hlsl", ".hlsli", ".material", ".sh", ".js")
PACK_STRING_FILE_NAME = ("CMakeLists.txt", ".gitignore")
GOLDEN_FOLDER = "golden"
SCENARIO_EXTENSION = ".scenario.xml"


@dataclass(frozen=True)
class PackRename:
    """팩 이름 바꾸기(`game/<옛>` → `game/<새>`)의 이름 접두들."""
    old_pack: str
    new_pack: str

    @property
    def old_scenario_prefix(self) -> str:
        return f"game.{self.old_pack}."

    @property
    def new_scenario_prefix(self) -> str:
        return f"game.{self.new_pack}."


@dataclass
class PackStringCount:
    scenario_name: int = 0   # `<Scenario name="game.<옛>.…">`
    other_name: int = 0      # 그 밖의 `game.<옛>.` (시나리오 목록 · 체인 · 주석)
    path: int = 0            # `game/<옛>/`
    chain: int = 0           # CTest 체인 이름 `Scenario.<옛>`(뒤에 `.<하위>` 가 붙어도)
    domain: int = 0          # 따옴표 속 도메인 `"game/<옛>"` · 팩 줄기 `game_<옛>`


def get_pack_rename(old_id: str, new_id: str) -> PackRename | None:
    """두 id 가 모두 팩 뿌리(`game/<팩>`)면 팩 이름 바꾸기다."""
    old_parts, new_parts = old_id.split("/"), new_id.split("/")
    if len(old_parts) == 2 and len(new_parts) == 2 and old_parts[0] == new_parts[0] == "game":
        return PackRename(old_parts[1], new_parts[1])
    return None


@dataclass(frozen=True)
class PackAbsorb:
    """팩 흡수(`game/<옛>` → `game/<새>/<하위>`)의 옛 팩 이름과 새 자리."""
    old_pack: str
    new_id: str


def get_pack_absorb(old_id: str, new_id: str) -> PackAbsorb | None:
    """옛 id 가 팩 뿌리이고 새 id 가 다른 팩 아래 폴더면 팩 흡수다."""
    old_parts, new_parts = old_id.split("/"), new_id.split("/")
    if len(old_parts) == 2 and len(new_parts) > 2 and old_parts[0] == new_parts[0] == "game" and old_parts[1] != new_parts[1]:
        return PackAbsorb(old_parts[1], new_id)
    return None


def rewrite_absorbed_strings(text: str, absorb: PackAbsorb) -> tuple[str, int]:
    """글 속 `game/<옛>/…` 경로와 따옴표로 끝나는 도메인 `"game/<옛>"` 를 새 자리로. 앞 글자가 낱말 글자(`mygame/<옛>/`)거나 더 긴 이름이면 두지 않는다."""
    pattern = re.compile(r"(?<![A-Za-z0-9_.])game/" + re.escape(absorb.old_pack) + r"(?=[/\"'`])")
    return pattern.subn(absorb.new_id, text)


@dataclass(frozen=True)
class ScenarioRename:
    """시나리오 파일 하나를 옮길 때 바뀌는 시나리오 이름과 골든 폴더."""
    old_name: str
    new_name: str
    old_golden_folder: str
    new_golden_folder: str


def get_scenario_rename(old_id: str, new_id: str, text: str) -> ScenarioRename | None:
    """두 id 가 모두 시나리오 파일이고 이름의 끝 마디가 파일 이름이면 새 이름을 짓는다. 팩 마디(`game.<팩>.`)는 새 팩을 따르고 그 밖의 앞 마디(`editor.` · `engine.`)는 그대로다."""
    if not old_id.endswith(SCENARIO_EXTENSION) or not new_id.endswith(SCENARIO_EXTENSION):
        return None
    match = re.search(r"<Scenario\b[^>]*?\bname\s*=\s*[\"']([^\"']+)[\"']", text)
    old_folder, _, old_file = old_id.rpartition("/")
    new_folder, _, new_file = new_id.rpartition("/")
    old_stem, new_stem = old_file[:-len(SCENARIO_EXTENSION)], new_file[:-len(SCENARIO_EXTENSION)]
    if match is None or not match.group(1).endswith("." + old_stem):
        return None
    list_part = match.group(1)[:-len(old_stem) - 1].split(".")
    new_parts = new_id.split("/")
    if len(list_part) > 1 and list_part[0] == new_parts[0] == "game":
        list_part[1] = new_parts[1]
    new_name = ".".join([*list_part, new_stem])
    return ScenarioRename(match.group(1), new_name, f"{CONTENT_FOLDER}/{old_folder}/{GOLDEN_FOLDER}", f"{CONTENT_FOLDER}/{new_folder}/{GOLDEN_FOLDER}")


def list_scenario_golden_renames(list_index: list[str], rename: ScenarioRename) -> list[tuple[str, str]]:
    """그 시나리오의 골든 파일(그림 · 기록 메타)마다 (옛 경로, 새 경로)."""
    old_prefix = f"{rename.old_golden_folder}/{rename.old_name}."
    return [(path, f"{rename.new_golden_folder}/{rename.new_name}.{path[len(old_prefix):]}") for path in list_index if path.startswith(old_prefix)]


def rewrite_scenario_name(text: str, rename: ScenarioRename) -> tuple[str, int]:
    """글 속 시나리오 이름(골든 id 접두 포함)을 새 이름으로. 더 긴 이름(`game.x.mountain`)은 두지 않는다."""
    pattern = re.compile(r"(?<![A-Za-z0-9_.])" + re.escape(rename.old_name) + r"(?![A-Za-z0-9_])")
    return pattern.subn(rename.new_name, text)


def list_leftover_patterns(old_id: str, absorb: PackAbsorb | None) -> list[str]:
    """옮긴 뒤 칸 밖에 남았는지 훑을 옛 글(소문자) — 흡수면 바꾸지 않은 시나리오 이름 · 체인 이름도."""
    list_pattern = [old_id.lower()]
    if absorb is not None:
        list_pattern += [f"game.{absorb.old_pack}.", f"scenario.{absorb.old_pack}"]
    return list_pattern


def is_domain_root(asset_id: str) -> bool:
    """도메인 뿌리(`game/<팩>` · `dlc/<묶음>` · `common` …) — 그 아래 표 하나를 런타임 검색 순서가 읽는다."""
    return make_redirect_table_id(asset_id) == f"{asset_id}/{REDIRECT_FILE_NAME}"


def rewrite_pack_strings(text: str, rename: PackRename) -> tuple[str, PackStringCount]:
    """글 속 `game.<옛>.` 시나리오 이름과 `game/<옛>/` 경로 접두를 새 팩 이름으로. 앞 글자가 낱말 글자(`mygame/empty/`)면 다른 낱말이라 두지 않는다."""
    count = PackStringCount()
    old_name, new_name = rename.old_scenario_prefix, rename.new_scenario_prefix
    scenario_pattern = re.compile(r"(<Scenario\b[^>]*?\bname\s*=\s*[\"'])" + re.escape(old_name))
    count.scenario_name = len(scenario_pattern.findall(text))
    name_pattern = re.compile(r"(?<![A-Za-z0-9_.])" + re.escape(old_name))
    text, name_total = name_pattern.subn(new_name, text)
    count.other_name = name_total - count.scenario_name
    path_pattern = re.compile(r"(?<![A-Za-z0-9_.])" + re.escape(f"game/{rename.old_pack}/"))
    text, count.path = path_pattern.subn(f"game/{rename.new_pack}/", text)
    chain_pattern = re.compile(r"(?<![A-Za-z0-9_.])Scenario\." + re.escape(rename.old_pack) + r"(?![A-Za-z0-9_])")
    text, count.chain = chain_pattern.subn(f"Scenario.{rename.new_pack}", text)
    domain_pattern = re.compile(r"(?<![A-Za-z0-9_.])game/" + re.escape(rename.old_pack) + r"(?=[\"'`])")   # `"game/<옛>"` · `".../Content/game/<옛>"`
    text, domain_count = domain_pattern.subn(f"game/{rename.new_pack}", text)
    stem_pattern = re.compile(r"(?<![A-Za-z0-9_])game_" + re.escape(rename.old_pack) + r"(?![A-Za-z0-9_])")
    text, stem_count = stem_pattern.subn(f"game_{rename.new_pack}", text)
    chunk_pattern = re.compile(r"(?<![A-Za-z0-9_])scene_" + re.escape(rename.old_pack) + "_")   # 씬 청크 이름 `scene_<팩>_<씬>`(`PackSceneChunkPlan::makeSceneChunkName`)
    text, chunk_count = chunk_pattern.subn(f"scene_{rename.new_pack}_", text)
    count.domain = domain_count + stem_count + chunk_count
    # 대소문자 · 구분자가 다른 경로(`Game/Old/Maps` · `Game\\Old/` — 경로를 맞춤 꼴로 접는 테스트 자료)는 옛 이름의 대소문자 꼴을 새 이름에 옮긴다.
    folded_pattern = re.compile(r"(?<![A-Za-z0-9_.])((?i:game)[/\\]+)(" + "".join(f"[{c.lower()}{c.upper()}]" if c.isalpha() else re.escape(c) for c in rename.old_pack)
                                + r")(?=[/\\\"'`])")
    text, folded_count = folded_pattern.subn(lambda match: match.group(1) + match_case(match.group(2), rename.new_pack), text)
    count.path += folded_count
    return text, count


def match_case(sample: str, name: str) -> str:
    """@p name 을 @p sample 의 대소문자 꼴(모두 소문자 · 모두 대문자 · 첫 글자만 대문자)로."""
    if sample.isupper():
        return name.upper()
    if sample[:1].isupper():
        return name[:1].upper() + name[1:]
    return name


def is_pack_string_file(relative_path: str) -> bool:
    name = relative_path.rsplit("/", 1)[-1]
    return (relative_path.lower().endswith(PACK_STRING_SUFFIX) or name in PACK_STRING_FILE_NAME) and name != REDIRECT_FILE_NAME   # 표는 옛 id 를 적는 글이다


def list_golden_renames(list_index: list[str], rename: PackRename) -> list[tuple[str, str]]:
    """골든 폴더의 `game.<옛>.…` 파일(그림 · 기록 메타)마다 (옛 경로, 새 경로) — 팩 폴더 밖 골든도 이름에 팩이 들면 함께."""
    list_rename = []
    for path in list_index:
        folder, _, name = path.rpartition("/")
        if folder.rsplit("/", 1)[-1] == GOLDEN_FOLDER and name.startswith(rename.old_scenario_prefix):
            list_rename.append((path, f"{folder}/{rename.new_scenario_prefix}{name[len(rename.old_scenario_prefix):]}"))
    return list_rename


def rewrite_table_targets(text: str, list_rule: list[tuple[str, str, bool]]) -> str:
    """함께 옮겨진 표의 `to` 를 새 자리로 — `from` 은 옛 id 라 그대로 둔다(정확한 줄이 접두 줄보다 먼저 풀린다)."""
    return re.sub(r'(\bto=")([^"]*)(")', lambda match: match.group(1) + (rewrite_path(match.group(2), list_rule) or match.group(2)) + match.group(3), text)


def to_asset_id(argument: str) -> str:
    normalized = argument.replace("\\", "/").strip("/")
    if normalized.lower().startswith(CONTENT_FOLDER.lower() + "/"):
        normalized = normalized[len(CONTENT_FOLDER) + 1:]
    return normalized


def make_rules(old_id: str, new_id: str, is_folder: bool, config: FieldConfig) -> list[tuple[str, str, bool]]:
    """(옛 접두, 새 접두, 폴더인가) 규칙들 — 옮긴 것 자신과, 원본이면 그 임포트 산출물 자리."""
    list_rule = [(old_id.lower(), new_id.lower(), is_folder)]
    for imported in config.list_imported:
        marker = "/" + imported.source_folder + "/"
        if marker not in "/" + old_id or marker not in "/" + new_id:
            continue
        old_imported = ("/" + old_id).replace(marker, "/" + imported.folder + "/", 1)[1:]
        new_imported = ("/" + new_id).replace(marker, "/" + imported.folder + "/", 1)[1:]
        if not is_folder:
            if not old_id.lower().endswith(imported.list_source_extension) or not new_id.lower().endswith(imported.list_source_extension):
                continue
            old_imported = old_imported.rsplit(".", 1)[0]
            new_imported = new_imported.rsplit(".", 1)[0]
        list_rule.append((old_imported.lower(), new_imported.lower(), True))   # 산출물은 `<이름>.mesh` · `<이름>/…` — 접두로 바꾼다
    return list_rule


def rewrite_path(path: str, list_rule: list[tuple[str, str, bool]]) -> str | None:
    for old_prefix, new_prefix, is_prefix in list_rule:
        if path == old_prefix:
            return new_prefix
        if is_prefix and (path.startswith(old_prefix + "/") or path.startswith(old_prefix + ".")):
            return new_prefix + path[len(old_prefix):]
    return None


def rewrite_references(relative_path: str, text: str, list_rule: list[tuple[str, str, bool]], config: FieldConfig) -> tuple[str, int]:
    """참조 칸의 값 가운데 규칙에 맞는 낱말만 바꿉니다. 뒤에서부터 바꿔 앞 자리가 밀리지 않게 한다."""
    list_edit = []
    for reference in extract_references(relative_path, text, config):
        if (rewritten := rewrite_path(reference.path, list_rule)) is not None:
            list_edit.append((reference.start, reference.end, rewritten))
    for start, end, rewritten in sorted(list_edit, reverse=True):
        text = text[:start] + rewritten + text[end:]
    return text, len(list_edit)


def git_move(root: Path, old_path: str, new_path: str) -> None:
    (root / new_path).parent.mkdir(parents=True, exist_ok=True)
    for _ in range(20):  # IDE 의 git 이 잠깐 잡는 index.lock 은 기다리면 풀린다 — 반쯤 옮긴 채 멈추지 않게
        result = run_git(["mv", old_path, new_path], cwd=root)
        if result.succeeded or "index.lock" not in result.stderr:
            break
        time.sleep(0.5)
    if not result.succeeded:
        raise RuntimeError(f"git mv {old_path} {new_path} 실패: {result.stderr.strip()}")
    folder = (root / old_path).parent
    content = root / CONTENT_FOLDER
    while folder != content and folder.is_dir() and not any(folder.iterdir()):   # 비워진 옛 폴더를 남기지 않는다
        folder.rmdir()
        folder = folder.parent


def list_moved_files(root: Path, old_id: str, new_id: str, is_folder: bool) -> list[tuple[str, str]]:
    """옮긴 파일마다 (옛 id, 새 id) — 폴더면 옮긴 뒤 그 아래 추적 파일 전부. 표 파일 자신은 리다이렉트할 것이 아니다."""
    if not is_folder:
        return [(old_id, new_id)]
    prefix = f"{CONTENT_FOLDER}/{new_id}/"
    return [(old_id + "/" + path[len(prefix):], path[len(CONTENT_FOLDER) + 1:]) for path in list_index_files(root)
            if path.startswith(prefix) and not path.endswith("/" + REDIRECT_FILE_NAME) and not path.endswith(META_EXTENSION)]   # .meta 는 원본과 함께 간다 — 따로 가리킬 것이 아니다


def move_sidecar(root: Path, old_path: str, new_path: str) -> None:
    """원본 곁의 `.meta` 를 원본과 함께 옮기고 원본 이름 칸을 새 이름으로 쓴다(GUID 는 그대로)."""
    old_meta, new_meta = old_path + META_EXTENSION, new_path + META_EXTENSION
    if not (root / old_meta).is_file():
        return
    meta = parse_meta((root / old_meta).read_text(encoding="utf-8"))
    if meta is None:
        raise RuntimeError(f"{old_meta} 를 읽지 못했다 — 손으로 고친 뒤 다시 옮긴다")
    git_move(root, old_meta, new_meta)
    (root / new_meta).write_bytes(make_meta_text(new_path, meta[0], meta[2]).encode("utf-8"))
    result = run_git(["add", new_meta], cwd=root)
    if not result.succeeded:
        raise RuntimeError(f"git add {new_meta} 실패: {result.stderr.strip()}")


def write_redirects(root: Path, list_move: list[tuple[str, str]], prefix_move: tuple[str, str] | None = None) -> list[str]:
    """옛 id 도메인의 표마다 줄을 더해 쓰고 `git add` 한다. 쓴 표 경로 목록을 돌려준다.
    @p prefix_move 가 있으면(도메인 뿌리 옮기기) 파일마다 줄 대신 새 도메인 표에 접두 한 줄 — 옛 도메인 폴더는 사라져 검색 순서가 읽지 않는다."""
    map_move_by_table: dict[str, tuple[list[tuple[str, str]], list[tuple[str, str]]]] = {}
    for move in list_move:
        map_move_by_table.setdefault(make_redirect_table_id(move[0]), ([], []))[0].append(move)
    if prefix_move is not None:
        map_move_by_table.setdefault(make_redirect_table_id(prefix_move[1]), ([], []))[1].append(prefix_move)
    list_written = []
    for table_id, (list_table_move, list_prefix_move) in map_move_by_table.items():
        table_path = root / CONTENT_FOLDER / table_id
        text = add_redirects(table_path.read_text(encoding="utf-8") if table_path.is_file() else None, list_table_move, list_prefix_move)
        if not table_path.is_file():
            text = write_header(text, create_guid())   # 새 표도 우리 형식 글이라 정체성 머리를 단다 — 있던 표는 아래가 머리를 옮겨 쓴다
        write_keeping_identity(table_path, text)
        result = run_git(["add", f"{CONTENT_FOLDER}/{table_id}"], cwd=root)
        if not result.succeeded:
            raise RuntimeError(f"git add {CONTENT_FOLDER}/{table_id} 실패: {result.stderr.strip()}")
        list_written.append(f"{CONTENT_FOLDER}/{table_id}")
    return list_written


def report_leftovers(root: Path, list_pattern: list[str]) -> None:
    """칸 밖에 옛 경로 · 이름(@p list_pattern, 소문자)이 남은 추적 글 파일(주석 · 출처 표 · 문서)을 알립니다. 고치지 않는다 — 경로인지 사람이 읽고 정한다."""
    list_leftover = []
    for relative_path in list_index_files(root):
        if not relative_path.lower().endswith(LEFTOVER_SUFFIX) or relative_path.endswith("/" + REDIRECT_FILE_NAME) or not (root / relative_path).is_file():
            continue   # 리다이렉트 표는 옛 id 를 적으려고 있다
        lowered_text = (root / relative_path).read_bytes().decode("utf-8", "replace").lower()
        if any(pattern_text in lowered_text for pattern_text in list_pattern):
            list_leftover.append(relative_path)
    for relative_path in list_leftover:
        print(f"  [남은 옛 경로 — 칸 밖] {relative_path}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts move-asset", description="에셋을 옮기고 경로 칸의 참조를 고친다")
    parser.add_argument("id", nargs="+", help="옛 리소스 id(파일 또는 폴더)와 새 리소스 id — 여러 쌍이면 차례로 옮긴다")
    parser.add_argument("--dry-run", action="store_true", help="고칠 파일만 찍는다")
    parser.add_argument("--resume", action="store_true", help="파일은 이미 옮겼다 — 이름 · 참조 · 리다이렉트 고치기만 다시 돈다(도구 규칙을 넓힌 뒤 같은 옮기기에 적용)")
    args = parser.parse_args(argv)

    if len(args.id) % 2 != 0:
        parser.error("옛 id 와 새 id 는 짝이다")

    root = get_project_root()
    config = load_config(root)
    for old_argument, new_argument in zip(args.id[0::2], args.id[1::2]):
        exit_code = move_one(root, config, to_asset_id(old_argument), to_asset_id(new_argument), args.dry_run, args.resume)
        if exit_code != ExitCode.PASSED:
            return exit_code
    if args.dry_run:
        return ExitCode.PASSED

    list_problem = check_working_tree(root)
    for problem in list_problem:
        print(f"  - {problem}")
    if list_problem:
        print(f"[move-asset] 끊어진 참조 {len(list_problem)}건이 남았습니다 — 목록에 없는 칸이면 그 칸을 읽는 구조체에 PROPERTY( AssetPath ) 로 선언한다(Config/Dev/AssetReferenceFields.generated.json)")
        return ExitCode.VIOLATION
    return ExitCode.PASSED


def move_one(root: Path, config: FieldConfig, old_id: str, new_id: str, dry_run: bool, resume: bool) -> ExitCode:
    """한 쌍을 옮기고 참조 · 이름 · 리다이렉트를 고칩니다(끊어진 참조 검사는 부르는 쪽이 끝에 한 번). 옮길 수 없으면 까닭을 찍고 CHECK_INVALID."""
    old_path, new_path = f"{CONTENT_FOLDER}/{old_id}", f"{CONTENT_FOLDER}/{new_id}"
    if not set(new_id) <= NAME_CHARACTERS:
        print(f"[move-asset] '{new_id}' 는 리소스 이름 규칙(소문자 · 숫자 · '_' · '-' · '.' · '/')에 맞지 않습니다")
        return ExitCode.CHECK_INVALID
    moved_path = new_path if resume else old_path   # 다시 돌기면 옮긴 자리가 있고 옛 자리가 없어야 한다
    if not (root / moved_path).exists() or (resume and (root / old_path).exists()):
        print(f"[move-asset] '{moved_path}' 가 없습니다" + (f"(또는 옛 자리 '{old_path}' 가 아직 있습니다)" if resume else "") + " — 아무것도 바꾸지 않았습니다")
        return ExitCode.CHECK_INVALID
    if not resume and ((root / new_path).exists() or (root / (new_path + META_EXTENSION)).exists()):
        print(f"[move-asset] '{new_path}'(또는 그 .meta)가 이미 있습니다 — 아무것도 옮기지 않았습니다")
        return ExitCode.CHECK_INVALID
    is_folder = (root / moved_path).is_dir()
    list_rule = make_rules(old_id, new_id, is_folder, config)
    rename = get_pack_rename(old_id, new_id)
    absorb = get_pack_absorb(old_id, new_id)
    scenario = None if is_folder else get_scenario_rename(old_id, new_id, (root / moved_path).read_bytes().decode("utf-8", "surrogateescape"))
    # 옛 도메인 뿌리를 통째로 옮기면 옛 도메인 폴더가 사라진다 — 새 자리가 다른 도메인의 하위 폴더여도(흡수) 접두 한 줄이 새 도메인 표에 간다.
    is_root_move = is_folder and is_domain_root(old_id)
    if is_root_move and not is_domain_root(new_id) and (root / moved_path / REDIRECT_FILE_NAME).is_file():
        print(f"[move-asset] '{moved_path}/{REDIRECT_FILE_NAME}' 는 옮긴 뒤 아무 검색 순서도 읽지 않는다 — 그 줄을 {make_redirect_table_id(new_id)} 에 먼저 합친 뒤 지우고 다시 옮긴다")
        return ExitCode.CHECK_INVALID

    if not dry_run and not resume:
        git_move(root, old_path, new_path)
        if not is_folder:
            move_sidecar(root, old_path, new_path)   # 폴더는 안의 .meta 가 함께 간다
    list_golden = list_golden_renames(list_index_files(root), rename) if rename is not None else []
    if scenario is not None:
        list_golden = list_scenario_golden_renames(list_index_files(root), scenario)
        print(f"  [시나리오 이름] {scenario.old_name} -> {scenario.new_name}")
    for old_golden, new_golden in list_golden:
        print(f"  [골든] {old_golden} -> {new_golden.rsplit('/', 1)[-1]}")
        if not dry_run:
            git_move(root, old_golden, new_golden)
    fixed_count, string_file_count, string_count = 0, 0, PackStringCount()
    for relative_path in list_index_files(root):
        if not (root / relative_path).is_file():
            continue
        is_field_file = is_referrer(relative_path, config)
        is_string_file = (rename is not None or absorb is not None or scenario is not None) and is_pack_string_file(relative_path)
        if not is_field_file and not is_string_file:
            continue
        text = (root / relative_path).read_bytes().decode("utf-8", "surrogateescape")
        rewritten, edit_count = rewrite_references(relative_path, text, list_rule, config) if is_field_file else (text, 0)
        if is_string_file and absorb is not None:
            rewritten, absorbed_count = rewrite_absorbed_strings(rewritten, absorb)
            if absorbed_count:
                string_file_count += 1
                string_count.path += absorbed_count
                print(f"  [팩 흡수 경로] {relative_path}: {absorbed_count}")
        elif is_string_file and scenario is not None:
            rewritten, name_count = rewrite_scenario_name(rewritten, scenario)
            if name_count:
                print(f"  [시나리오 이름] {relative_path}: {name_count}")
        elif is_string_file:
            rewritten, count = rewrite_pack_strings(rewritten, rename)
            if count.scenario_name + count.other_name + count.path + count.chain + count.domain:
                string_file_count += 1
                string_count.scenario_name += count.scenario_name
                string_count.other_name += count.other_name
                string_count.path += count.path
                string_count.chain += count.chain
                string_count.domain += count.domain
                print(f"  [팩 이름 문자열] {relative_path}: 시나리오 이름 {count.scenario_name} · 그 밖 이름 {count.other_name} · 경로 {count.path} · "
                      f"체인 {count.chain} · 도메인 {count.domain}")
        if edit_count:
            fixed_count += 1
            print(f"  {relative_path}: {edit_count}")
        if rewritten != text and not dry_run:
            (root / relative_path).write_bytes(rewritten.encode("utf-8", "surrogateescape"))
    print(f"[move-asset] {old_id} -> {new_id}: 참조 파일 {fixed_count} 개{' (dry run — 아무것도 바꾸지 않았다)' if dry_run else ''}")
    if rename is not None:
        print(f"[move-asset] 팩 이름 {rename.old_pack} -> {rename.new_pack}: 골든 {len(list_golden)} 개, 문자열 파일 {string_file_count} 개 — "
              f"시나리오 이름 {string_count.scenario_name} · 그 밖 이름 {string_count.other_name} · 경로 {string_count.path} · 체인 {string_count.chain} · 도메인 {string_count.domain}")
    if absorb is not None:
        print(f"[move-asset] 팩 흡수 {absorb.old_pack} -> {absorb.new_id}: 문자열 파일 {string_file_count} 개 — 경로 {string_count.path}"
              f"(시나리오 이름 game.{absorb.old_pack}. · 체인 Scenario.{absorb.old_pack} 은 남은 목록을 보고 고친다)")
    if is_root_move:
        print(f"[move-asset] 도메인 뿌리 옮기기 — 리다이렉트는 {make_redirect_table_id(new_id)} 에 접두 한 줄(<PrefixRedirect from=\"{old_id}\" to=\"{new_id}\" />)")
    if not dry_run:
        moved_table = root / CONTENT_FOLDER / make_redirect_table_id(new_id)
        if is_root_move and moved_table.is_file():   # 옛 팩의 표가 함께 옮겨졌다 — 가리키던 자리(to)가 새 팩 아래다
            moved_table.write_text(rewrite_table_targets(moved_table.read_text(encoding="utf-8"), list_rule), encoding="utf-8", newline="\n")
        list_move = [] if is_root_move else list_moved_files(root, old_id, new_id, is_folder)
        for table_path in write_redirects(root, list_move, (old_id, new_id) if is_root_move else None):
            print(f"  [리다이렉트] {table_path}")
        report_leftovers(root, list_leftover_patterns(old_id, absorb) + ([scenario.old_name.lower()] if scenario is not None else []))
    return ExitCode.PASSED
