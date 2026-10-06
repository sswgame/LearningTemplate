#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
configure 가 부르는 생성기 — 임시 폴더로 다시 돌린 출력이 그 빌드 폴더의 산출물과 **같은 바이트**인지(줄끝 포함).

빌드 폴더는 CTest 가 넘기는 `SW_BUILD_DIR`(그 시험을 등록한 빌드), 없으면 `.clangd` 가 가리키는 트리. 산출물이 없으면 건너뛴다.
생성기의 공통부(`writeGeneratedFile` · `runGenerator`)를 고치면 이 시험이 출력이 그대로인지 본다. 다르면 — 고친 것이 출력을 바꿨거나,
그 빌드 폴더가 소스보다 오래됐다(다시 configure).
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from common import BuildTree  # noqa: E402


def findBuildTreeInternal() -> BuildTree | None:
    buildDir = os.environ.get("SW_BUILD_DIR")
    tree = BuildTree(Path(buildDir)) if buildDir else BuildTree.findDefault(kRepositoryRoot)
    return tree if (tree.path / "generated/sw/config/ConfigVars.cmake").is_file() else None


#: (생성기, 그 빌드의 산출물 이름들, 출력 앞에 붙는 인자를 만드는 함수) — configure 가 부르는 모양 그대로(cmake/Config · Engine · Environment).
def listGeneratorInternal(tree: BuildTree) -> list[tuple[str, list[str], list[str]]]:
    gameConfig = f"Config/Game/{tree.readCacheValue('SW_ACTIVE_GAME') or 'Empty'}.json"
    return [
        ("GenerateCMakeConstants.py", ["ConfigVars.cmake"], []),
        ("GeneratePackFormat.py", ["PackFormat.gen.h"], []),
        ("GenerateCookContract.py", ["CookContract.gen.h", "CookContract.cmake"], []),
        ("GenerateShippingHostDefaults.py", ["ShippingHostDefaults.h"], [gameConfig]),
        ("GenerateToolchainCMake.py", ["ToolchainVars.cmake"], []),
        ("GenerateLintTargets.py", ["LintTargets.cmake"], []),
    ]


class GeneratorsTest(unittest.TestCase):
    def testOutputMatchesConfiguredTree(self) -> None:
        tree = findBuildTreeInternal()
        if tree is None:
            self.skipTest("구성된 빌드 폴더가 없다")
        generatedDir = tree.path / "generated/sw/config"
        with tempfile.TemporaryDirectory() as tempDir:
            for scriptName, listOutput, listExtra in listGeneratorInternal(tree):
                with self.subTest(generator=scriptName):
                    listOutPath = [Path(tempDir) / name for name in listOutput]
                    result = subprocess.run([sys.executable, str(kRepositoryRoot / "Scripts/generate" / scriptName),
                                             *(str(path) for path in listOutPath), *listExtra],
                                            cwd=kRepositoryRoot, capture_output=True, text=True, encoding="utf-8", errors="replace")
                    self.assertEqual(result.returncode, 0, result.stderr)
                    for name, outPath in zip(listOutput, listOutPath):
                        expected = generatedDir / name
                        if not expected.is_file():
                            continue
                        self.assertEqual(outPath.read_bytes(), expected.read_bytes(),
                                         f"{name}: {tree.name} 의 산출물과 다르다(출력이 바뀌었거나 그 빌드 폴더가 오래됐다 — 다시 configure)")

    def testCombinedRunMatchesEachGenerator(self) -> None:
        # configure 는 다섯을 GenerateConfigureFiles 한 프로세스로 부른다 — 단독 실행과 같은 바이트여야 한다.
        tree = findBuildTreeInternal()
        if tree is None:
            self.skipTest("구성된 빌드 폴더가 없다")
        gameName = tree.readCacheValue("SW_ACTIVE_GAME") or "Empty"
        with tempfile.TemporaryDirectory() as tempDir:
            result = subprocess.run([sys.executable, str(kRepositoryRoot / "Scripts/generate/GenerateConfigureFiles.py"), tempDir, "--game", gameName],
                                    cwd=kRepositoryRoot, capture_output=True, text=True, encoding="utf-8", errors="replace")
            self.assertEqual(result.returncode, 0, result.stderr)
            for name in ("ConfigVars.cmake", "PackFormat.gen.h", "CookContract.gen.h", "CookContract.cmake", "ShippingHostDefaults.h",
                         "LintTargets.cmake"):
                with self.subTest(output=name):
                    self.assertEqual((Path(tempDir) / name).read_bytes(), (tree.path / "generated/sw/config" / name).read_bytes())


if __name__ == "__main__":
    unittest.main()
