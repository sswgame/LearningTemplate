#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
쿠킹 표(`Config/Engine/CookContract.json`)를 **읽은 결과** — RHI 백엔드 표와 쿡 접미사 표.

C++ 은 같은 파일에서 생성한 X-macro(`sw/config/CookContract.gen.h`, `GenerateCookContract.py`)로 읽고, 쿠커
(`CookAssets.py`)는 이 객체에 묻는다. 표를 읽고 검증하는 일은 여기 한 곳이다 — 헤더 생성기와 쿠커가 같은 객체를 쓴다.
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
    listAlias: tuple[str, ...]


@dataclass(frozen=True)
class CookSuffixSpec:
    """쿡 접미사 한 줄 — 소스 접미사 → 쿠킹본 접미사와 에셋 종류."""

    source: str
    cooked: str
    kind: str
    bAuthoringSource: bool


class CookContractSpec:
    """
    계약 파일 전체. `load()` 는 같은 프로젝트 루트에 대해 한 번만 읽는다.

    읽을 때 표의 규칙(이름 철자 · 별칭과 폴더의 중복 · 접미사 순서)을 검사해 어긋나면 `ValueError` 를 던진다 —
    configure 단계의 헤더 생성이 그 자리에서 멈춘다.
    """

    _mapCached: dict[Path, CookContractSpec] = {}

    def __init__(self, spec: dict) -> None:
        self.listBackend: list[RhiBackendSpec] = [makeBackendInternal(row) for row in spec["rhi_backends"]]
        self.listCookSuffix: list[CookSuffixSpec] = [makeCookSuffixInternal(row) for row in spec["cook_suffixes"]]
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

    @property
    def listShaderFolder(self) -> list[str]:
        """셰이더 바이너리 폴더 이름 전부(`shaders/bin/<폴더>`)."""
        return [backend.shaderFolder for backend in self.listBackend]

    @property
    def listCookedSuffix(self) -> list[str]:
        """쿠커가 만드는 산출물 접미사(중복 없이, 표 순서)."""
        listSuffix: list[str] = []
        for suffix in self.listCookSuffix:
            if suffix.cooked not in listSuffix:
                listSuffix.append(suffix.cooked)
        return listSuffix

    def findBackend(self, text: str) -> RhiBackendSpec | None:
        """별칭 · 백엔드 이름(대소문자 무시)으로 줄을 찾습니다. 모르면 None."""
        key = text.strip().lower()
        for backend in self.listBackend:
            if key == backend.name.lower() or key in backend.listAlias:
                return backend
        return None


def makeBackendInternal(row: dict) -> RhiBackendSpec:
    backend = RhiBackendSpec(
        name=str(row["name"]),
        shaderFolder=str(row["shader_folder"]),
        shaderTarget=str(row["shader_target"]),
        commandLineArgument=str(row["command_line_argument"]),
        listAlias=tuple(str(alias) for alias in row["aliases"]),
    )
    for identifier in (backend.name, backend.shaderTarget, backend.commandLineArgument):
        if not _kIdentifierPattern.match(identifier):
            raise ValueError(f"rhi_backends '{backend.name}': '{identifier}' 는 C++ 식별자가 아닙니다")
    for lowerName in (backend.shaderFolder, *backend.listAlias):
        if not _kLowerNamePattern.match(lowerName):
            raise ValueError(f"rhi_backends '{backend.name}': '{lowerName}' 는 소문자 이름이 아닙니다")
    if backend.shaderFolder not in backend.listAlias:
        raise ValueError(f"rhi_backends '{backend.name}': 별칭에 셰이더 폴더 '{backend.shaderFolder}' 가 없습니다")
    return backend


def makeCookSuffixInternal(row: dict) -> CookSuffixSpec:
    suffix = CookSuffixSpec(
        source=str(row["source"]),
        cooked=str(row["cooked"]),
        kind=str(row["kind"]),
        bAuthoringSource=bool(row["is_authoring_source"]),
    )
    for text in (suffix.source, suffix.cooked):
        if not text.startswith(".") or '"' in text or "\\" in text:
            raise ValueError(f"cook_suffixes: '{text}' 는 '.' 으로 시작하는 접미사가 아닙니다")
    if not _kIdentifierPattern.match(suffix.kind):
        raise ValueError(f"cook_suffixes '{suffix.source}': kind '{suffix.kind}' 는 C++ 식별자가 아닙니다")
    return suffix


def validateInternal(spec: CookContractSpec) -> None:
    """표 전체 규칙 — 이름 · 폴더 · 별칭이 백엔드끼리 겹치지 않고, 긴 접미사가 짧은 것보다 먼저 온다."""
    mapOwnerByName: dict[str, str] = {}
    for backend in spec.listBackend:
        for name in {backend.name.lower(), backend.shaderFolder, *backend.listAlias}:
            owner = mapOwnerByName.setdefault(name, backend.name)
            if owner != backend.name:
                raise ValueError(f"rhi_backends: '{name}' 를 '{owner}' 와 '{backend.name}' 가 같이 씁니다")

    for laterIndex, later in enumerate(spec.listCookSuffix):
        for earlier in spec.listCookSuffix[:laterIndex]:
            if later.source == earlier.source:
                raise ValueError(f"cook_suffixes: '{later.source}' 가 두 번 있습니다")
            if later.source.endswith(earlier.source):
                raise ValueError(f"cook_suffixes: '{later.source}' 가 그것의 끝인 '{earlier.source}' 보다 뒤에 있어 맞지 않습니다")
