#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckShaderStamps.py

커밋된 셰이더 바이너리가 지금 소스에서 나온 것인지 쿠킹 표(`Config/Engine/CookContract.json`)의 **모든 백엔드**에서 검사합니다.

이 저장소는 쿠킹된 바이너리까지 커밋한다. 바이너리를 만드는 `App --cook-shaders` 는 빌드가 있어야 돌기 때문에 커밋 훅은 App 이 없으면
셰이더 검증을 건너뛰고, 병합은 한쪽의 소스와 다른 쪽의 바이너리를 섞을 수 있다(되살아난 낡은 바이너리, 51f9dab91). 그러면 한 백엔드만
다른 그림을 내거나 Shipping 이 빈 화면을 내는데, CI 는 GPU 가 없어 그 백엔드로 그려 보지 않는다(C-41 2단계).

판정은 빌드 없이 한다. 백엔드 폴더마다 `cook.stamp` 의 소스 내용 해시가 지금 소스와 같은지, 스탬프에 빠진 · 남은 셰이더가 없는지,
매니페스트에 없는 바이너리가 없는지를 쿠커의 `verifyShaderCookInternal` 로 본다(Shipping 패키징이 쓰는 것과 같은 판정이다).
도구 지문(`@toolchain`)은 컴파일러를 아는 C++ 쿠커만 판정한다. 그래서 이 게이트는 Linux 린트 잡에서도 돈다.
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

#: 판정 함수를 가진 쿠커. 판정은 도구의 것이므로 `--root`(셀프 테스트의 임시 트리)가 아니라 이 저장소의 쿠커를 쓴다.
kCookerPath = Path(__file__).resolve().parents[2] / "generate" / "CookAssets.py"

#: 셀프 테스트 조각 — 소스는 있는데 dx12 하나만 쿠킹한 트리. 나머지 세 백엔드의 바이너리 폴더가 없다.
_kOneBackendStamp = "SWCOOK 4\n0000000000000000 probe.hlsl\n0000000000000000 @toolchain\n"


def loadCookerInternal() -> ModuleType:
    """쿠커를 파일 경로로 불러옵니다. 불러오지 못하면 검사가 성립하지 않는다."""
    moduleSpec = importlib.util.spec_from_file_location("CookAssetsUnderStampCheck", kCookerPath)
    if moduleSpec is None or moduleSpec.loader is None:
        raise GateError(f"쿠커를 불러올 수 없습니다: {kCookerPath}")
    module = importlib.util.module_from_spec(moduleSpec)
    moduleSpec.loader.exec_module(module)
    if not callable(getattr(module, "verifyShaderCookInternal", None)):
        raise GateError(f"{kCookerPath} 에 verifyShaderCookInternal() 가 없습니다 - 이 게이트가 부르는 판정 함수입니다")
    return module


class CheckShaderStampsGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 곳에 있다."""

    description = "커밋된 셰이더 바이너리가 지금 소스에서 나온 것인지 쿠킹 표의 모든 백엔드에서 검사"
    buildComment = "Checking that committed shader binaries match their sources for every RHI backend..."
    timeoutSeconds = 60
    preCommitPattern = ("Resource/*/shaders/*", "Resource/game/*/shaders/*", kCookContractConfigRelative, "Scripts/generate/CookAssets.py")
    preCommitFileArgument = ""
    violationHeader = "쿠킹하지 않았거나 낡은 셰이더 바이너리"
    hint = "  다시 굽는다:  build/<프리셋>/Bin/App(.exe) --cook-shaders  (바이너리 · cook.stamp · reflection.manifest 를 소스와 함께 커밋한다)"
    selfTestCases = [
        {
            "name": "dx12 만 쿠킹하고 나머지 백엔드를 빠뜨린 셰이더",
            "expectedViolation": "바이너리 폴더가 없습니다 (한 번도 쿠킹하지 않았습니다)",
            "repoFiles": [kCookContractConfigRelative],
            "files": {
                "Resource/engine/shaders/probe.hlsl": "float4 main() : SV_Target { return 0; }\n",
                "Resource/engine/shaders/bin/dx12/cook.stamp": _kOneBackendStamp,
            },
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        try:
            spec = CookContractSpec.load(repositoryRoot)
        except (OSError, KeyError, ValueError) as exception:
            raise GateError(f"{kCookContractConfigRelative} 를 읽을 수 없습니다: {exception}") from exception
        cooker = loadCookerInternal()
        listViolation: list[str] = []
        for shaderFolder in spec.listShaderFolder:
            listViolation += [f"[{shaderFolder}] {problem}" for problem in cooker.verifyShaderCookInternal(repositoryRoot, shaderFolder)]
        return GateResult(listViolation=listViolation, summary=f"{len(spec.listShaderFolder)} backends ({', '.join(spec.listShaderFolder)})")


main = CheckShaderStampsGate.run


if __name__ == "__main__":
    sys.exit(main())
