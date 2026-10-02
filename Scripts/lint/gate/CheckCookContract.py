#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckCookContract.py

쿠커(`Scripts/generate/CookAssets.py`)가 쿠킹 표(`Config/Engine/CookContract.json`)대로 고르는지 검사합니다.

C++ 은 같은 표를 생성 헤더의 X-macro 로 읽어 컴파일 때 따라오지만, 쿠커는 파이썬이라 표를 어기는 판단(손으로 적은 폴더
목록 · 별칭 목록 · 산출물 이름 규칙)을 해도 아무것도 깨지지 않는다 — 팩에 다른 백엔드의 셰이더 바이너리가 실려도 빌드와
실행은 멀쩡하다. 그래서 쿠커의 판단 함수 셋을 표의 줄마다 불러 답을 대조한다:

  - `shouldIncludeFile`  : `shaders/bin/<폴더>/` 는 타깃 백엔드의 폴더만 팩에 들어간다(표의 모든 폴더에 대해).
  - `resolveTargetRhi`   : 표의 백엔드 이름 · 별칭 · 셰이더 폴더가 그 백엔드의 폴더로 풀린다. 아무것도 없으면 기본 백엔드다.
  - `isCookedArtifact`   : 표의 쿠킹본 접미사는 산출물이고, 저작 소스 접미사는 산출물이 아니다.

쿠커는 `--root` 아래의 `Scripts/generate/CookAssets.py` 를 파일 경로로 불러온다(게이트 셀프 테스트가 임시 트리의 조각을 쓴다).
"""

from __future__ import annotations

import argparse
import importlib.util
import sys
from pathlib import Path
from types import ModuleType

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import CookContractSpec, kCookContractConfigRelative  # noqa: E402
from LintGate import GateError, GateResult, LintGate  # noqa: E402

#: 검사하는 쿠커 (저장소 기준 경로).
kCookerRelative = "Scripts/generate/CookAssets.py"

#: 쿠커 조각 — 셰이더 폴더를 손으로 적어 GL 바이너리를 늘 싣던 필터. 셀프 테스트가 이것을 잡아야 한다.
_kBrokenCookerFixture = '''
def shouldIncludeFile(relPath, config, targetRhi="dx12"):
    parts = relPath.lower().split("/")
    if "shaders/bin" in relPath.lower():
        for rhiFolder in ("dx12", "vulkan", "dx11"):
            if rhiFolder in parts[:-1] and rhiFolder != targetRhi:
                return False
    return True


def resolveTargetRhi(config, cliRhi="", projectRoot=None):
    text = (cliRhi or config.get("target_rhi", "")).strip().lower()
    if text in ("opengl", "gl"):
        return "opengl"
    return "dx12"


def isCookedArtifact(relPath):
    return relPath.lower().endswith(".scene.bin")
'''

#: 셀프 테스트의 표 — 백엔드 둘 · 접미사 한 줄이면 위 필터의 결함이 드러난다.
_kContractFixture = '''{
    "default_rhi_backend": "DirectX12",
    "rhi_backends": [
        { "name": "DirectX12", "shader_folder": "dx12", "shader_target": "DXIL_D3D12", "command_line_argument": "DIRECTX_12", "aliases": [ "dx12" ] },
        { "name": "OpenGL", "shader_folder": "opengl", "shader_target": "SPIRV_OpenGL", "command_line_argument": "OPENGL", "aliases": [ "gl", "opengl" ] }
    ],
    "cook_suffixes": [
        { "source": ".scene.xml", "cooked": ".scene.bin", "kind": "Scene", "is_authoring_source": true }
    ]
}
'''


def loadCookerInternal(cookerPath: Path) -> ModuleType:
    """쿠커를 파일 경로로 불러옵니다. 불러오지 못하면 검사가 성립하지 않는다."""
    if not cookerPath.is_file():
        raise GateError(f"쿠커가 없습니다: {cookerPath}")
    moduleSpec = importlib.util.spec_from_file_location("CookAssetsUnderContractCheck", cookerPath)
    if moduleSpec is None or moduleSpec.loader is None:
        raise GateError(f"쿠커를 불러올 수 없습니다: {cookerPath}")
    module = importlib.util.module_from_spec(moduleSpec)
    moduleSpec.loader.exec_module(module)
    for functionName in ("shouldIncludeFile", "resolveTargetRhi", "isCookedArtifact"):
        if not callable(getattr(module, functionName, None)):
            raise GateError(f"{kCookerRelative} 에 {functionName}() 가 없습니다 - 이 게이트가 부르는 판단 함수입니다")
    return module


def checkShaderFolderFilterInternal(cooker: ModuleType, spec: CookContractSpec) -> list[str]:
    """타깃 백엔드마다, 표의 모든 셰이더 폴더 중 그 백엔드의 것만 팩에 들어가는지."""
    listViolation: list[str] = []
    for target in spec.listBackend:
        for backend in spec.listBackend:
            relPath = f"shaders/bin/{backend.shaderFolder}/probe.vs.bin"
            bExpected = backend is target
            bActual = bool(cooker.shouldIncludeFile(relPath, {}, target.shaderFolder))
            if bActual != bExpected:
                verdict = "넣는다" if bActual else "뺀다"
                listViolation.append(f"shouldIncludeFile('{relPath}', targetRhi='{target.shaderFolder}') 가 {verdict} - 표는 반대다")
    return listViolation


def checkTargetResolutionInternal(cooker: ModuleType, spec: CookContractSpec) -> list[str]:
    """표의 이름 · 별칭 · 폴더가 그 백엔드의 폴더로 풀리고, 아무것도 주지 않으면 기본 백엔드인지."""
    listViolation: list[str] = []
    for backend in spec.listBackend:
        for text in (backend.name, backend.shaderFolder, *backend.listAlias, backend.listAlias[0].upper()):
            resolvedFromCli = cooker.resolveTargetRhi({}, cliRhi=text)
            resolvedFromConfig = cooker.resolveTargetRhi({"target_rhi": text})
            for origin, resolved in (("cliRhi", resolvedFromCli), ("target_rhi", resolvedFromConfig)):
                if resolved != backend.shaderFolder:
                    listViolation.append(f"resolveTargetRhi({origin}='{text}') = '{resolved}' - 표는 '{backend.shaderFolder}'")
    resolvedDefault = cooker.resolveTargetRhi({})
    if resolvedDefault != spec.defaultBackend.shaderFolder:
        listViolation.append(f"resolveTargetRhi() 기본값 = '{resolvedDefault}' - 표는 '{spec.defaultBackend.shaderFolder}'")
    return listViolation


def checkCookedArtifactInternal(cooker: ModuleType, spec: CookContractSpec) -> list[str]:
    """쿠킹본 접미사는 산출물, 저작 소스 접미사는 산출물이 아닌지."""
    listViolation: list[str] = []
    for suffix in spec.listCookSuffix:
        cookedPath = f"maps/probe{suffix.cooked}"
        if not cooker.isCookedArtifact(cookedPath):
            listViolation.append(f"isCookedArtifact('{cookedPath}') 가 False - 표의 쿠킹본 접미사 '{suffix.cooked}' 다")
        if suffix.bAuthoringSource:
            sourcePath = f"maps/probe{suffix.source}"
            if cooker.isCookedArtifact(sourcePath):
                listViolation.append(f"isCookedArtifact('{sourcePath}') 가 True - 표의 저작 소스 접미사 '{suffix.source}' 다")
    return listViolation


class CheckCookContractGate(LintGate):
    """쿠커가 쿠킹 표를 따르는지 — 파이썬 쪽은 컴파일이 대신 막아 주지 않는다."""

    description = f"{kCookerRelative} 의 판단이 {kCookContractConfigRelative} 와 같은지 검사"
    buildComment = "Checking that CookAssets.py follows Config/Engine/CookContract.json..."
    timeoutSeconds = 30
    preCommitPattern = (kCookContractConfigRelative, kCookerRelative, "Scripts/common/CookContract.py")
    violationHeader = "쿠킹 표와 다른 쿠커 판단"
    hint = f"  쿠커는 목록을 손으로 들지 말고 CookContractSpec({kCookContractConfigRelative})에 물어야 합니다."
    selfTestCases = [
        {
            "name": "셰이더 폴더 필터가 opengl 을 빠뜨린 쿠커",
            "files": {
                kCookContractConfigRelative: _kContractFixture,
                kCookerRelative: _kBrokenCookerFixture,
            },
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        try:
            spec = CookContractSpec.load(repositoryRoot)
        except (OSError, KeyError, ValueError) as exception:
            raise GateError(f"{kCookContractConfigRelative} 를 읽을 수 없습니다: {exception}") from exception
        cooker = loadCookerInternal(repositoryRoot / kCookerRelative)

        listViolation = checkShaderFolderFilterInternal(cooker, spec)
        listViolation += checkTargetResolutionInternal(cooker, spec)
        listViolation += checkCookedArtifactInternal(cooker, spec)
        return GateResult(
            listViolation=listViolation,
            summary=f"{len(spec.listBackend)} backends, {len(spec.listCookSuffix)} cook suffixes",
        )


main = CheckCookContractGate.run


if __name__ == "__main__":
    sys.exit(main())
