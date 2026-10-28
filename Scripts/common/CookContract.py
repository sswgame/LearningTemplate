#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
쿠킹 표(`Config/Engine/CookContract.json`)를 **읽은 결과** — RHI 백엔드 표 · 쿡 접미사 표 · 빌드 타깃별 제외 에셋 종류 표.

C++(쿠커 `PackCooker` · `ShaderCooker` 포함)은 같은 파일에서 생성한 X-macro(`sw/config/CookContract.gen.h`, `CookContract.py`)로 읽고,
파이썬 도구(`Pack.py` 등)는 이 객체에 묻는다. 파이썬에서 표를 읽고 검증하는 일은 여기 한 곳이다 — 헤더 생성기와 도구가 같은 객체를 쓴다.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path

from .Paths import getProjectRoot

#: 계약 파일 (저장소 기준 경로).
kCookContractConfigRelative = "Config/Engine/CookContract.json"

#: 열거자 · 명령줄 인자 이름으로 쓸 수 있는 철자(C++ 식별자).
_kIdentifierPattern = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
#: 별칭 · 폴더 이름의 철자(소문자 — 쿠커와 서브폴더 비교가 소문자로 한다).
_kLowerNamePattern = re.compile(r"^[a-z0-9_]+$")


@dataclass(frozen=True)
class RhiBackendSpec:
    """RHI 백엔드 한 줄. 줄 순서가 `RHIBackend` 열거값이다."""

    name: str
    shaderFolder: str
    shaderTarget: str
    commandLineArgument: str
    commandLineName: str
    moduleName: str
    sourceFolder: str
    graphicsLibs: str
    shippingDefine: str


#: 빌드 타깃 이름(CMake `SW_TARGET_TYPE`).
kListBuildTarget = ("Game", "Client", "Server")


class CookContractSpec:
    """
    계약 파일 전체. `load()` 는 같은 프로젝트 루트에 대해 한 번만 읽는다.

    읽을 때 표의 규칙(이름 철자 · 별칭과 폴더의 중복 · 접미사 순서)을 검사해 어긋나면 `ValueError` 를 던진다 —
    configure 단계의 헤더 생성이 그 자리에서 멈춘다.
    """

    _mapCached: dict[Path, CookContractSpec] = {}

    def __init__(self, spec: dict) -> None:
        self.listBackend: list[RhiBackendSpec] = [makeBackendInternal(row) for row in spec["rhi_backends"]]
        #: 빌드 타깃 → 그 타깃이 빼는 제외 묶음 이름(표 순서, 엔진 `AssetTypeDef::_excludeGroup`).
        self.mapExcludedKindByTarget: dict[str, tuple[str, ...]] = {
            str(row["target"]): tuple(str(kind) for kind in row["kinds"]) for row in spec["target_excluded_asset_kinds"]
        }
        defaultName = str(spec["default_rhi_backend"])
        self.defaultBackend: RhiBackendSpec = next(
            (backend for backend in self.listBackend if backend.name == defaultName), None
        )
        if self.defaultBackend is None:
            raise ValueError(f"default_rhi_backend '{defaultName}' 는 rhi_backends 에 없습니다")
        validateInternal(self)

    @classmethod
    def load(cls, projectRoot: Path | None = None) -> CookContractSpec:
        root = (projectRoot or getProjectRoot()).resolve()
        if root not in cls._mapCached:
            configPath = root / kCookContractConfigRelative
            if not configPath.is_file():
                raise FileNotFoundError(f"Cook contract not found: {configPath}")
            cls._mapCached[root] = cls(json.loads(configPath.read_text(encoding="utf-8")))
        return cls._mapCached[root]

    # --- 값 --------------------------------------------------------------------

    def findBackend(self, text: str) -> RhiBackendSpec | None:
        """백엔드 이름(대소문자 무시 — EngineConfig · CMake 값) 또는 명령줄 이름으로 줄을 찾습니다. 모르면 None."""
        key = text.strip().lower()
        for backend in self.listBackend:
            if key in (backend.name.lower(), backend.commandLineName):
                return backend
        return None


def makeBackendInternal(row: dict) -> RhiBackendSpec:
    backend = RhiBackendSpec(
        name=str(row["name"]),
        shaderFolder=str(row["shader_folder"]),
        shaderTarget=str(row["shader_target"]),
        commandLineArgument=str(row["command_line_argument"]),
        commandLineName=str(row["command_line_name"]),
        moduleName=str(row["module"]),
        sourceFolder=str(row["source_folder"]),
        graphicsLibs=str(row["graphics_libs"]),
        shippingDefine=str(row["shipping_define"]),
    )
    for identifier in (backend.name, backend.shaderTarget, backend.commandLineArgument, backend.moduleName, backend.sourceFolder,
                       backend.graphicsLibs, backend.shippingDefine):
        if not _kIdentifierPattern.match(identifier):
            raise ValueError(f"rhi_backends '{backend.name}': '{identifier}' 는 C++ 식별자가 아닙니다")
    for lowerName in (backend.shaderFolder, backend.commandLineName):
        if not _kLowerNamePattern.match(lowerName):
            raise ValueError(f"rhi_backends '{backend.name}': '{lowerName}' 는 소문자 이름이 아닙니다")
    return backend


def validateInternal(spec: CookContractSpec) -> None:
    """표 전체 규칙 — 이름 · 폴더 · 명령줄 이름이 백엔드끼리 겹치지 않고, 타깃 제외가 아는 종류만 가리킨다. 쿡 접미사는 엔진의 에셋 종류 표(`AssetTypeRegistry`)가 가진다."""
    mapOwnerByName: dict[str, str] = {}
    for backend in spec.listBackend:
        for name in {backend.name.lower(), backend.shaderFolder, backend.commandLineName, backend.moduleName, backend.sourceFolder,
                     backend.graphicsLibs, backend.shippingDefine}:
            owner = mapOwnerByName.setdefault(name, backend.name)
            if owner != backend.name:
                raise ValueError(f"rhi_backends: '{name}' 를 '{owner}' 와 '{backend.name}' 가 같이 씁니다")

    for target, listKind in spec.mapExcludedKindByTarget.items():
        if target not in kListBuildTarget:
            raise ValueError(f"target_excluded_asset_kinds: 모르는 빌드 타깃 '{target}' ({' · '.join(kListBuildTarget)})")
        if target == "Game":
            raise ValueError("target_excluded_asset_kinds: Game 타깃은 클라이언트 · 서버를 다 담으므로 아무것도 빼지 않습니다")
        for kindName in listKind:
            if not _kIdentifierPattern.match(kindName):
                raise ValueError(f"target_excluded_asset_kinds '{target}': '{kindName}' 는 C++ 식별자가 아닙니다")
