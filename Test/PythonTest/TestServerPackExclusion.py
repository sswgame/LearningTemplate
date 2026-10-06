#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
빌드 타깃별 제외 에셋 종류(`Config/Engine/CookContract.json` 의 `target_excluded_asset_kinds`)의 단위 시험.

전용 서버 패키지에는 텍스처 · 셰이더 바이너리 · 오디오가 **0 개**여야 하고(서버는 그 종류를 읽지 않는다 — `ResourceUtil::setHostTarget`),
메시 · 애니메이션은 남아야 한다(충돌 · 소켓 · 히트박스 · 루트 모션). 저장소의 실제 `Resource/` 를 쿠커가 팩에 넣을 파일 목록
(`CookAssets.collectPackFiles` — `cookPack` 이 쓰는 것과 같은 판단)으로 훑는다.
"""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from common import CookContractSpec, kFilePackConfig  # noqa: E402


def loadCookerInternal():
    """쿠커 스크립트를 모듈로 불러옵니다."""
    cookerPath = kRepositoryRoot / "Scripts" / "generate" / "CookAssets.py"
    moduleSpec = importlib.util.spec_from_file_location("CookAssetsUnderTest", cookerPath)
    module = importlib.util.module_from_spec(moduleSpec)
    moduleSpec.loader.exec_module(module)
    return module


def listDomainDirectoriesInternal() -> list[Path]:
    """팩 하나가 되는 도메인 폴더(engine · common · game/<게임>)입니다."""
    resourceDir = kRepositoryRoot / "Resource"
    listDirectory = [resourceDir / "engine", resourceDir / "common"]
    listDirectory += sorted(path for path in (resourceDir / "game").iterdir() if path.is_dir())
    return [directory for directory in listDirectory if directory.is_dir()]


class ServerPackExclusionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.cooker = loadCookerInternal()
        cls.spec = CookContractSpec.load(kRepositoryRoot)
        cls.packConfig = cls.cooker.readJsonDictInternal(kRepositoryRoot / kFilePackConfig, kFilePackConfig)
        cls.mapPackFilesByTarget: dict[str, list[str]] = {}
        for buildTarget in ("Server", "Client"):
            listRel: list[str] = []
            for domainDir in listDomainDirectoriesInternal():
                listPackFile, _ = cls.cooker.collectPackFiles(domainDir, cls.packConfig, targetRhi="dx12", buildTarget=buildTarget)
                listRel += [f"{domainDir.name}/{rel}" for rel, _ in listPackFile]
            cls.mapPackFilesByTarget[buildTarget] = listRel

    def testServerPackHasNoExcludedKind(self) -> None:
        """서버 패키지에 표가 빼는 종류(텍스처 · 셰이더 바이너리 · 오디오) 파일이 0 개다."""
        listKind = self.spec.mapExcludedKindByTarget.get("Server", ())
        self.assertEqual(set(listKind), {"Texture", "ShaderBinary", "Audio"})
        listLeaked = [rel for rel in self.mapPackFilesByTarget["Server"] if self.spec.findExcludedKind(rel, "Server") is not None]
        self.assertEqual([], listLeaked[:10], f"{len(listLeaked)} excluded-kind files in the server pack")
        listServerLower = [rel.lower() for rel in self.mapPackFilesByTarget["Server"]]
        for suffix in (".dds", ".ogg", ".wav", ".dxil", ".spv", ".dxbc"):
            self.assertFalse(any(rel.endswith(suffix) for rel in listServerLower), suffix)

    def testServerPackKeepsMeshesAndAnimation(self) -> None:
        """메시 · 애니메이션은 서버에 남는다 — 충돌 · 소켓 · 히트박스 · 루트 모션."""
        listServer = self.mapPackFilesByTarget["Server"]
        self.assertTrue(any(rel.endswith(".mesh") for rel in listServer))
        self.assertTrue(any(rel.endswith(".animclip") for rel in listServer))

    def testClientPackKeepsEveryKind(self) -> None:
        """클라이언트 패키지는 아무것도 빼지 않는다 — 같은 판단이 타깃마다 다른 답을 낸다(눈먼 필터가 아니다)."""
        self.assertEqual((), self.spec.mapExcludedKindByTarget.get("Client", ()))
        listClient = self.mapPackFilesByTarget["Client"]
        for kindName in ("Texture", "ShaderBinary", "Audio"):
            kind = next(assetKind for assetKind in self.spec.listAssetKind if assetKind.name == kindName)
            self.assertTrue(any(kind.matches(rel) for rel in listClient), kindName)
        self.assertGreater(len(listClient), len(self.mapPackFilesByTarget["Server"]))

    def testFolderPatternMatchesWholeSegmentsOnly(self) -> None:
        """폴더 조각은 경로 조각 경계에서만 맞는다(`shaders/bin` 이 `myshaders/binary` 에 맞지 않는다)."""
        kind = next(assetKind for assetKind in self.spec.listAssetKind if assetKind.name == "ShaderBinary")
        self.assertTrue(kind.matches("shaders/bin/dx12/x.dxil"))
        self.assertTrue(kind.matches("engine/shaders/bin/vulkan/x.spv"))
        self.assertFalse(kind.matches("myshaders/binary/x.txt"))
        self.assertFalse(kind.matches("shaders/x.hlsl"))


if __name__ == "__main__":
    unittest.main()
