#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/target_layers.py

타깃 경계 껍데기를 만든다. `Source/` 가 모든 타깃의 include 루트라 Core 가 `Engine/...` 을, 키트가 매니페스트에 적지 않은 키트를 include 해도 헤더만 쓰면
컴파일 · 링크가 된다. 표(`Config/Rules/Layers/TargetLayers.toml`)의 게이트마다, 그리고 모듈(키트 · 에디터 확장 · 게임)마다 보면 안 되는 헤더와 같은 경로의 `#error`
파일을 `<빌드>/gates/<게이트>/` 에 놓고, CMake(`sw_apply_target_layer_guard`)가 그 폴더를 타깃의 include 자리 맨 앞에 둔다.
위반하면(헤더를 거쳐 닿는 전이 include 도) 컴파일이 멈춘다 — `module_layers.py` · `editor_layers.py` 와 같은 틀이고, 그쪽이 폴더 층을 보면 이쪽은 타깃 경계를 본다.

`[gatefolder]` 는 경로 접두어 대신 폴더 이름으로 막는다 — 서버 구성이 짓지 않는 `Client/`, 클라이언트 구성이 짓지 않는 `Server/` 가 모듈 · 게임마다 있어서다.
`serverbuild` 는 서버가 짓지 않는 엔진 폴더(`cmake/Modules/Build/BuildLayout.cmake` 의 `SW_EXCLUDED_ENGINE_FOLDERS`)를 모두 막아야 한다 — 대조해 빠지면 멈춘다.

파일 수가 많아(키트 60 여 개 × 헤더 수백) CMake 대신 여기서 쓴다. 게이트마다 목록 스탬프가 같으면 파일을 건드리지 않아 configure 가 파일 하나만 읽는다.

  python Scripts/generate/target_layers.py <출력 .cmake 파일>
"""

from __future__ import annotations

import hashlib
import json
import re
import shutil
import sys
import tomllib
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.file_collect import list_files_under  # noqa: E402
from common.host_platform import is_windows_host  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from generate.generator_output import GeneratorError, run_generator, write_generated_file  # noqa: E402

TAG = "TargetLayers"
RULE_FILE = Path(__file__).resolve().parents[2] / "Config" / "Rules" / "Layers" / "TargetLayers.toml"
HEADER_SUFFIX = {".h", ".hpp", ".inl", ".xxx"}
MODULE_KINDS = {"Kit", "EditorExtension", "Game"}   # 모듈별 게이트를 받는 종류
STUB_FOLDER_NAME = "gates"   # 빌드 루트 바로 밑 — 껍데기 경로가 키트 헤더의 깊은 경로를 그대로 이어 받아 워크트리에서 260 자를 넘었다
WINDOWS_MAX_PATH = 260   # LongPathsEnabled 가 꺼진 Windows 의 컴파일러는 이보다 긴 include 후보를 못 열어 껍데기를 건너뛴다(게이트가 조용히 풀린다)
QUOTED_INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
SERVER_GUARD_NAME = "serverbuild"
CLIENT_GUARD_NAME = "clientbuild"
EXCLUDED_FOLDER_LINE = re.compile(r'list\(APPEND SW_EXCLUDED_ENGINE_FOLDERS ([^)]*)\)')


def read_server_excluded_folders(project_root: Path) -> list[str]:
    """전용 서버가 짓지 않는 엔진 폴더(`Source/Engine/` 기준) — 정본은 `cmake/Modules/Build/BuildLayout.cmake` 의 `SW_EXCLUDED_ENGINE_FOLDERS` 한 곳이다."""
    text = (project_root / "cmake/Modules/Build/BuildLayout.cmake").read_text(encoding="utf-8")
    list_folder: list[str] = []
    for match in EXCLUDED_FOLDER_LINE.finditer(text):
        list_folder.extend(name.strip('"') for name in match.group(1).split())
    if not list_folder:
        raise GeneratorError("cmake/Modules/Build/BuildLayout.cmake: SW_EXCLUDED_ENGINE_FOLDERS 를 읽지 못했다")
    return list_folder


def check_server_guard_covers_excluded_folders(list_prefix: list[str], list_excluded_folder: list[str]) -> None:
    """서버 게이트가 서버가 짓지 않는 엔진 폴더를 모두 막는지 본다 — 빠지면 서버 구성이 짓지 않는 헤더를 읽어 링크에서야 드러난다."""
    list_missing = [f"Engine/{folder}" for folder in list_excluded_folder if f"Engine/{folder}" not in list_prefix]
    if list_missing:
        raise GeneratorError(f"[gate] {SERVER_GUARD_NAME} 에 서버가 짓지 않는 엔진 폴더가 빠졌다: {', '.join(list_missing)} — {RULE_FILE.name} 에 더하세요(SW_EXCLUDED_ENGINE_FOLDERS)")


def is_under_folder_name(header: str, set_folder_name: set[str]) -> bool:
    """@p header 의 폴더 마디 중 하나가 @p set_folder_name 에 있으면 참이다(파일 이름은 보지 않는다)."""
    return any(segment in set_folder_name for segment in header.split("/")[:-1])


class ModuleInfo:
    """매니페스트 하나 — 모듈 폴더 · 종류 · 직접 의존."""

    def __init__(self, gate_name: str, name: str, kind: str, folder: str, list_dependency: list[str]) -> None:
        self.gate_name = gate_name              # 모듈별 게이트 이름. 게임은 이름이 모두 SWGame 이라 폴더로 가른다
        self.name = name
        self.kind = kind
        self.folder = folder                  # `Source/` 기준 모듈 폴더(런타임 · 서버 · 확장이 나뉜 키트는 각자의 폴더)
        self.list_dependency = list_dependency


def make_game_guard_name(folder: str) -> str:
    """게임 모듈의 게이트 이름 — `Games/<폴더>` 의 폴더 이름으로 가른다."""
    return f"SWGame_{folder.rsplit('/', 1)[-1]}"


def read_modules(source_dir: Path) -> list[ModuleInfo]:
    """`Source/` 의 매니페스트를 모두 읽는다."""
    list_module: list[ModuleInfo] = []
    for path in (path for path in list_files_under(source_dir) if path.name.endswith(".module.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            name, kind = data["_name"], data["_kind"]
            list_dependency = [entry["_name"] for entry in data.get("_listDependency", [])]
        except (OSError, ValueError, KeyError, TypeError) as exception:
            raise GeneratorError(f"{path}: 매니페스트를 읽지 못했다 ({exception!r})") from exception
        folder = path.parent.relative_to(source_dir).as_posix()
        gate_name = make_game_guard_name(folder) if kind == "Game" else name
        list_module.append(ModuleInfo(gate_name, name, kind, folder, list_dependency))
    return list_module


def compute_closure(module: ModuleInfo, map_by_name: dict[str, ModuleInfo]) -> set[str]:
    """@p module 이 볼 수 있는 모듈 폴더 — 자기와 `_listDependency` 의 전이 폐쇄."""
    list_visible = [module.folder]
    set_seen = {module.name}
    list_pending = list(module.list_dependency)
    while list_pending:
        name = list_pending.pop()
        if name in set_seen:
            continue
        set_seen.add(name)
        dependency = map_by_name.get(name)
        if dependency is None:
            continue   # 없는 모듈은 매니페스트 해석(`sw_resolve_module_manifests`)이 멈춘다 — 여기서 같은 말을 또 하지 않는다
        list_visible.append(dependency.folder)
        list_pending.extend(dependency.list_dependency)
    return set(list_visible)


def find_owner(header: str, list_folder: list[str]) -> str | None:
    """헤더를 가진 모듈 폴더 — 가장 긴 접두어가 이긴다(`NetSimulation/Editor` 는 `NetSimulation` 안이지만 따로다). @p listFolder 는 긴 것부터 정렬돼 있다."""
    for folder in list_folder:
        if header.startswith(folder + "/"):
            return folder
    return None


def write_guard_stubs(stub_dir: Path, gate_name: str, list_header: list[str], message: str) -> None:
    """게이트 하나의 껍데기를 맞춘다. 스탬프(문구 해시 + 지난 헤더 목록)와 대조해 **바뀐 껍데기만** 더하고 지운다.

    헤더 하나가 늘거나 옮겨질 때마다 그 폴더를 막는 게이트 전부가 폴더째 지우고 다시 쓰면(게이트 101 개 · 껍데기 5 만여 개) 재구성 46 초 중 31 초가
    이 생성기였다. 문구가 바뀌었거나 스탬프 · 폴더가 없을 때만 통째로 다시 쓴다(사라진 헤더의 껍데기가 옮겨 온 헤더를 가리지 않게 지우는 것은 같다).
    """
    stub_root = stub_dir / gate_name
    stamp_path = stub_dir / f"{gate_name}.stamp"
    content = f"#error \"{message}\"\n"
    content_digest = hashlib.sha256(content.encode("utf-8")).hexdigest()
    set_previous: set[str] | None = None
    if stamp_path.is_file() and stub_root.is_dir():
        list_stamp_line = stamp_path.read_text(encoding="utf-8").splitlines()
        if list_stamp_line and list_stamp_line[0] == content_digest:
            set_previous = set(list_stamp_line[1:])
    if set_previous is None:
        if stub_root.exists():
            shutil.rmtree(stub_root)
        stub_root.mkdir(parents=True)
        set_previous = set()
    set_current = set(list_header)
    if is_windows_host():
        list_long = [header for header in set_current if len(str(stub_root / header)) >= WINDOWS_MAX_PATH]
        if list_long:
            raise GeneratorError(f"게이트 껍데기 경로가 {WINDOWS_MAX_PATH} 자를 넘는다({len(list_long)} 개, 예: {stub_root / max(list_long, key=len)}) — "
                                 "컴파일러가 못 열어 게이트가 풀린다. 빌드 폴더를 얕은 경로에 두거나 헤더 경로를 줄이세요")
    if set_current == set_previous:
        return
    for header in set_previous - set_current:
        (stub_root / header).unlink(missing_ok=True)
    set_folder: set[Path] = set()
    for header in sorted(set_current - set_previous):
        path = stub_root / header
        if path.parent not in set_folder:
            path.parent.mkdir(parents=True, exist_ok=True)
            set_folder.add(path.parent)
        path.write_text(content, encoding="utf-8")
    stamp_path.parent.mkdir(parents=True, exist_ok=True)
    stamp_path.write_text("\n".join([content_digest, *sorted(set_current)]) + "\n", encoding="utf-8")


def check_allow_closure(source_dir: Path, gate_name: str, list_allowed: list[str], set_denied: set[str]) -> None:
    """연 헤더가 막힌 헤더를 include 하면 멈춘다 — 컴파일 오류가 아니라 표를 고칠 자리로 알린다. 연 헤더마다 보므로 전이도 잡힌다."""
    list_problem: list[str] = []
    for header in list_allowed:
        for included in QUOTED_INCLUDE.findall((source_dir / header).read_text(encoding="utf-8")):
            if included in set_denied:
                list_problem.append(f"{header} -> {included}")
    if list_problem:
        raise GeneratorError(f"[allow] {gate_name}: 연 헤더가 막힌 헤더를 include 한다 — 그 헤더도 [allow] 에 더하거나 include 를 끊으세요: {'; '.join(list_problem)}")


def collect_guards(source_dir: Path, map_rule: dict[str, list[str]], map_allow: dict[str, list[str]], map_folder: dict[str, list[str]]) -> dict[str, tuple[list[str], str]]:
    """게이트 이름 → (막는 헤더, 오류 문구). 파일을 쓰지 않는다 — 표를 코드와 대조하는 쪽도 이 함수를 쓴다."""
    list_header = sorted(path.relative_to(source_dir).as_posix() for path in list_files_under(source_dir) if path.suffix in HEADER_SUFFIX)
    list_stale = [prefix for list_prefix in map_rule.values() for prefix in list_prefix if not (source_dir / prefix).exists()]
    if list_stale:
        raise GeneratorError(f"폴더가 없는 게이트 줄: {', '.join(sorted(set(list_stale)))} — {RULE_FILE.name} 에서 지우세요")
    list_unknown_allow = [gate_name for gate_name in map_allow if gate_name not in map_rule]
    if list_unknown_allow:
        raise GeneratorError(f"[allow] 에 [gate] 에 없는 게이트: {', '.join(sorted(list_unknown_allow))}")
    list_missing_allow = [header for list_allowed in map_allow.values() for header in list_allowed if not (source_dir / header).is_file()]
    if list_missing_allow:
        raise GeneratorError(f"[allow] 의 헤더가 없다: {', '.join(sorted(set(list_missing_allow)))} — {RULE_FILE.name} 를 고치세요")
    list_unknown_folder_gate = [gate_name for gate_name in map_folder if gate_name not in map_rule]
    if list_unknown_folder_gate:
        raise GeneratorError(f"[gatefolder] 에 [gate] 에 없는 게이트: {', '.join(sorted(list_unknown_folder_gate))}")
    set_present_folder = {segment for header in list_header for segment in header.split("/")[:-1]}
    list_stale_folder = [name for list_name in map_folder.values() for name in list_name if name not in set_present_folder]
    if list_stale_folder:
        raise GeneratorError(f"[gatefolder] 의 폴더 이름을 가진 헤더가 없다: {', '.join(sorted(set(list_stale_folder)))} — {RULE_FILE.name} 에서 지우세요")
    map_gate: dict[str, tuple[list[str], str]] = {}
    for gate_name, list_prefix in map_rule.items():
        set_allowed = set(map_allow.get(gate_name, []))
        set_folder_name = set(map_folder.get(gate_name, []))
        list_denied = [header for header in list_header
                      if (any(header.startswith(prefix + "/") for prefix in list_prefix) or is_under_folder_name(header, set_folder_name)) and header not in set_allowed]
        if set_allowed:
            check_allow_closure(source_dir, gate_name, sorted(set_allowed), set(list_denied))
        if gate_name == SERVER_GUARD_NAME:
            message = f"Target gate '{gate_name}' forbids this header (Config/Rules/Layers/TargetLayers.toml): a dedicated server does not build client-only code (Engine Audio/Renderer/UI, Client/ folders). Call it through a seam (GameSound, IHUDViewModelBinder, IGameClientPart, IEngineRenderDriver ...) or move the caller into a Client/ folder."
        elif gate_name == CLIENT_GUARD_NAME:
            message = f"Target gate '{gate_name}' forbids this header (Config/Rules/Layers/TargetLayers.toml): a client build does not build server-only code (Server/ folders). Call it through a seam (IOnlineLocalServerFactory ...), move a shared type out of the Server/ folder, or move the caller into a Server/ folder."
        else:
            message = f"Target gate '{gate_name}' forbids this header (Config/Rules/Layers/TargetLayers.toml): a lower target must not see an upper one. Move what you need down, or invert the dependency."
        map_gate[gate_name] = (list_denied, message)

    list_module = read_modules(source_dir)
    map_by_name = {module.name: module for module in list_module if module.kind != "Game"}
    list_gated = [module for module in list_module if module.kind in MODULE_KINDS]
    list_folder = sorted((module.folder for module in list_gated), key=len, reverse=True)
    map_folder_header: dict[str, list[str]] = {}
    for header in list_header:
        owner = find_owner(header, list_folder)
        if owner is not None:
            map_folder_header.setdefault(owner, []).append(header)
    for module in list_gated:
        set_visible = compute_closure(module, map_by_name)
        list_denied = sorted(header for folder, list_owned in map_folder_header.items() if folder not in set_visible for header in list_owned)
        map_gate[f"module/{module.gate_name}"] = (list_denied, f"Module '{module.name}' ({module.folder}) cannot include this header: its owner module is not in the _listDependency closure of {module.name}.module.json. Add the dependency, or move the shared type down.")
    return map_gate


def generate_target_layers(output_path: Path) -> None:
    try:
        table = tomllib.loads(RULE_FILE.read_text(encoding="utf-8"))
        map_rule: dict[str, list[str]] = table["gate"]
        map_allow: dict[str, list[str]] = table["allow"]
        map_folder: dict[str, list[str]] = table["gatefolder"]
        list_server_prefix = map_rule[SERVER_GUARD_NAME]
        if CLIENT_GUARD_NAME not in map_rule:   # CMake(`sw_apply_target_layer_guard`)가 Client 구성에 거는 게이트
            raise KeyError(CLIENT_GUARD_NAME)
    except (OSError, tomllib.TOMLDecodeError, KeyError) as exception:
        raise GeneratorError(f"{RULE_FILE.name}: 읽지 못했다 ({exception!r}) — [gate] · [gatefolder] · [allow] 표와 [gate] {SERVER_GUARD_NAME} · {CLIENT_GUARD_NAME} 가 있어야 한다") from exception
    project_root = get_project_root()
    check_server_guard_covers_excluded_folders(list_server_prefix, read_server_excluded_folders(project_root))
    source_dir = project_root / "Source"
    map_gate = collect_guards(source_dir, map_rule, map_allow, map_folder)
    stub_dir = output_path.parents[3] / STUB_FOLDER_NAME   # <빌드>/generated/sw/config/TargetLayers.cmake → <빌드>/gates
    for gate_name, (list_denied, message) in map_gate.items():
        write_guard_stubs(stub_dir, gate_name, list_denied, message)
    list_line = [
        "# AUTO-GENERATED by Scripts/generate/target_layers.py — do not edit. 쓰는 곳: sw_apply_target_layer_guard(cmake/Modules/Build/TargetCompileRules.cmake).",
        f'set(SW_TARGET_LAYER_STUB_DIR "{stub_dir.as_posix()}")',
        f'set(SW_TARGET_LAYER_GUARDS "{";".join(sorted(map_gate))}")',
        *(f'set(SW_TARGET_LAYER_ALLOW_{gate_name} "{";".join(sorted(list_allowed))}")' for gate_name, list_allowed in sorted(map_allow.items())),
        "",
    ]
    write_generated_file(output_path, "\n".join(list_line), tag=TAG, summary=f"{len(map_gate)} gates · {sum(len(entry[0]) for entry in map_gate.values())} stubs")


def main(argv: Sequence[str] | None = None) -> int:
    return run_generator(argv, tag=TAG, description="타깃 경계 표 → #error 껍데기",
                        add_arguments=lambda parser: parser.add_argument("output", type=Path, help="쓸 .cmake 파일(<빌드>/generated/sw/config/TargetLayers.cmake — 껍데기는 <빌드>/gates)"),
                        generate=lambda args: generate_target_layers(args.output))


if __name__ == "__main__":
    sys.exit(main())
