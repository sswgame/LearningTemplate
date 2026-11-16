#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
쿠킹 표(`Config/Engine/CookContract.json`)를 **읽은 결과** — RHI 백엔드 표 · 쿡 접미사 표 · 빌드 타깃별 제외 에셋 종류 표.

C++(쿠커 `PackCooker` · `ShaderCooker` 포함)은 같은 파일에서 생성한 X-macro(`sw/config/CookContract.gen.h`, `cook_contract.py`)로 읽고,
파이썬 도구(`pack.py` 등)는 이 객체에 묻는다. 파이썬에서 표를 읽고 검증하는 일은 여기 한 곳이다 — 헤더 생성기와 도구가 같은 객체를 쓴다.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path

from .paths import get_project_root

#: 계약 파일 (저장소 기준 경로).
COOK_CONTRACT_CONFIG_RELATIVE = "Config/Engine/CookContract.json"

#: 열거자 · 명령줄 인자 이름으로 쓸 수 있는 철자(C++ 식별자).
_IDENTIFIER_PATTERN = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
#: 별칭 · 폴더 이름의 철자(소문자 — 쿠커와 서브폴더 비교가 소문자로 한다).
_LOWER_NAME_PATTERN = re.compile(r"^[a-z0-9_]+$")


@dataclass(frozen=True)
class RhiBackendSpec:
    """RHI 백엔드 한 줄. 줄 순서가 `RHIBackend` 열거값이다."""

    name: str
    shader_folder: str
    shader_target: str
    command_line_argument: str
    command_line_name: str
    module_name: str
    source_folder: str
    graphics_libs: str
    shipping_define: str
    #: 셰이더 단계 하나가 쓸 수 있는 텍스처 단위 수. None 은 텍스처가 고정 칸이 아닌 백엔드(bindless 배열 · 기기마다 다른 한도)다 — 칸은 반드시 적는다.
    stage_texture_unit_limit: int | None


#: 빌드 타깃 이름(CMake `SW_TARGET_TYPE`).
LIST_BUILD_TARGET = ("Game", "Client", "Server")


class CookContractSpec:
    """
    계약 파일 전체. `load()` 는 같은 프로젝트 루트에 대해 한 번만 읽는다.

    읽을 때 표의 규칙(이름 철자 · 별칭과 폴더의 중복 · 접미사 순서)을 검사해 어긋나면 `ValueError` 를 던진다 —
    configure 단계의 헤더 생성이 그 자리에서 멈춘다.
    """

    _map_cached: dict[Path, CookContractSpec] = {}

    def __init__(self, spec: dict) -> None:
        self.list_backend: list[RhiBackendSpec] = [_make_backend(row) for row in spec["rhi_backends"]]
        #: 빌드 타깃 → 그 타깃이 빼는 제외 묶음 이름(표 순서, 엔진 `AssetTypeDef::_excludeGroup`).
        self.map_excluded_kind_by_target: dict[str, tuple[str, ...]] = {
            str(row["target"]): tuple(str(kind) for kind in row["kinds"]) for row in spec["target_excluded_asset_kinds"]
        }
        #: 플랫폼 → 만들 수 있는 백엔드(건너갈 순서, 맨 앞이 기본).
        self.map_backend_order_by_platform: dict[str, tuple[str, ...]] = {
            str(row["platform"]): tuple(str(name) for name in row["backends"]) for row in spec["platform_rhi_backends"]
        }
        #: 플랫폼 → 검증 체인(`-scenario-backends=all`)이 묶음마다 도는 백엔드 — 만들 수 있는 것의 부분집합. 새 백엔드는 골든을 갖춘 뒤 여기에 든다
        #: (데스크톱 WebGPU 는 만들 수 있지만 골든이 없어 아직 아니다 — 언리얼 자동화도 플랫폼 기본 RHI 로 돌고 다른 RHI 는 따로 지정한다).
        self.map_scenario_backends_by_platform: dict[str, tuple[str, ...]] = {
            str(row["platform"]): tuple(str(name) for name in row["scenario_backends"]) for row in spec["platform_rhi_backends"]
        }
        backend_names = {backend.name for backend in self.list_backend}
        for platform, list_name in self.map_backend_order_by_platform.items():
            if not list_name or any(name not in backend_names for name in list_name):
                raise ValueError(f"platform_rhi_backends '{platform}': 비었거나 rhi_backends 에 없는 이름이 있습니다 {list_name}")
            list_scenario = self.map_scenario_backends_by_platform[platform]
            if not list_scenario or any(name not in list_name for name in list_scenario):
                raise ValueError(f"platform_rhi_backends '{platform}': scenario_backends 는 그 플랫폼 backends 의 비지 않은 부분집합이어야 합니다 {list_scenario}")
        default_name = str(spec["default_rhi_backend"])
        self.default_backend: RhiBackendSpec = next(
            (backend for backend in self.list_backend if backend.name == default_name), None
        )
        if self.default_backend is None:
            raise ValueError(f"default_rhi_backend '{default_name}' 는 rhi_backends 에 없습니다")
        _validate(self)

    @classmethod
    def load(cls, project_root: Path | None = None) -> CookContractSpec:
        root = (project_root or get_project_root()).resolve()
        if root not in cls._map_cached:
            config_path = root / COOK_CONTRACT_CONFIG_RELATIVE
            if not config_path.is_file():
                raise FileNotFoundError(f"Cook contract not found: {config_path}")
            cls._map_cached[root] = cls(json.loads(config_path.read_text(encoding="utf-8")))
        return cls._map_cached[root]

    # --- 값 --------------------------------------------------------------------

    def find_backend(self, text: str) -> RhiBackendSpec | None:
        """백엔드 이름(대소문자 무시 — EngineConfig · CMake 값) 또는 명령줄 이름으로 줄을 찾습니다. 모르면 None."""
        key = text.strip().lower()
        for backend in self.list_backend:
            if key in (backend.name.lower(), backend.command_line_name):
                return backend
        return None


def _make_backend(row: dict) -> RhiBackendSpec:
    backend = RhiBackendSpec(
        name=str(row["name"]),
        shader_folder=str(row["shader_folder"]),
        shader_target=str(row["shader_target"]),
        command_line_argument=str(row["command_line_argument"]),
        command_line_name=str(row["command_line_name"]),
        module_name=str(row["module"]),
        source_folder=str(row["source_folder"]),
        graphics_libs=str(row["graphics_libs"]),
        shipping_define=str(row["shipping_define"]),
        stage_texture_unit_limit=row["stage_texture_unit_limit"],
    )
    limit = backend.stage_texture_unit_limit
    if limit is not None and (isinstance(limit, bool) or not isinstance(limit, int) or limit <= 0):
        raise ValueError(f"rhi_backends '{backend.name}': stage_texture_unit_limit 는 양의 정수이거나 null 이어야 합니다 ({limit!r})")
    for identifier in (backend.name, backend.shader_target, backend.command_line_argument, backend.module_name,
                       backend.graphics_libs, backend.shipping_define):
        if not _IDENTIFIER_PATTERN.match(identifier):
            raise ValueError(f"rhi_backends '{backend.name}': '{identifier}' 는 C++ 식별자가 아닙니다")
    # 소스 폴더는 `Source/Engine/Graphics/RHI` 아래 경로다(공용 코드를 묶은 `DX/DX12` 처럼 한 단계 더 들어갈 수 있다) — 마디마다 식별자다.
    if not all(_IDENTIFIER_PATTERN.match(segment) for segment in backend.source_folder.split("/")):
        raise ValueError(f"rhi_backends '{backend.name}': '{backend.source_folder}' 는 식별자를 '/' 로 이은 폴더 경로가 아닙니다")
    for lower_name in (backend.shader_folder, backend.command_line_name):
        if not _LOWER_NAME_PATTERN.match(lower_name):
            raise ValueError(f"rhi_backends '{backend.name}': '{lower_name}' 는 소문자 이름이 아닙니다")
    return backend


def _validate(spec: CookContractSpec) -> None:
    """표 전체 규칙 — 이름 · 폴더 · 명령줄 이름이 백엔드끼리 겹치지 않고, 타깃 제외가 아는 종류만 가리킨다. 쿡 접미사는 엔진의 에셋 종류 표(`AssetTypeRegistry`)가 가진다."""
    map_owner_by_name: dict[str, str] = {}
    for backend in spec.list_backend:
        for name in {backend.name.lower(), backend.shader_folder, backend.command_line_name, backend.module_name, backend.source_folder,
                     backend.graphics_libs, backend.shipping_define}:
            owner = map_owner_by_name.setdefault(name, backend.name)
            if owner != backend.name:
                raise ValueError(f"rhi_backends: '{name}' 를 '{owner}' 와 '{backend.name}' 가 같이 씁니다")

    for target, list_kind in spec.map_excluded_kind_by_target.items():
        if target not in LIST_BUILD_TARGET:
            raise ValueError(f"target_excluded_asset_kinds: 모르는 빌드 타깃 '{target}' ({' · '.join(LIST_BUILD_TARGET)})")
        if target == "Game":
            raise ValueError("target_excluded_asset_kinds: Game 타깃은 클라이언트 · 서버를 다 담으므로 아무것도 빼지 않습니다")
        for kind_name in list_kind:
            if not _IDENTIFIER_PATTERN.match(kind_name):
                raise ValueError(f"target_excluded_asset_kinds '{target}': '{kind_name}' 는 C++ 식별자가 아닙니다")
