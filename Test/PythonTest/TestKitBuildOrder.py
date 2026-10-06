#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
키트 폴더를 들어가는 순서(`sw_getModuleDirectoriesOfKind` — 매니페스트 의존 순서, 동점은 이름 순).

서버 · 클라이언트 키트는 같은 기능의 공유 키트 타깃을 `sw_linkSharedKit` 으로 링크한다 — 공유 키트 폴더가 먼저 들어가야 그 타깃이 있다.
목록 파일이 없으므로 순서는 CMake 함수가 정한다. 그 함수를 `cmake -P` 로 진짜 매니페스트에 돌려 `sw_linkSharedKit( A B )` 마다 B 가 A 보다
앞인지, 그리고 합성 매니페스트로 정렬 규칙(의존 먼저 · 이름 순 · 순환이면 실패)을 본다.
"""

from __future__ import annotations

import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
kKitRoot = kRepositoryRoot / "Source" / "GameFramework" / "Kits"
_kLinkSharedKitRe = re.compile(r"sw_linkSharedKit\(\s*(\w+)\s+(\w+)\s*\)")


def runOrderScriptInternal(listManifest: list[Path], outputPath: Path) -> subprocess.CompletedProcess:
    """매니페스트들을 읽고 Kit 폴더 순서를 `outputPath` 에 한 줄씩 쓰는 CMake 스크립트를 돌립니다."""
    scriptPath = outputPath.with_suffix(".cmake")
    listLine = [f'include("{(kRepositoryRoot / "cmake/Engine/ModuleManifest.cmake").as_posix()}")']
    listLine += [f'sw_readModuleManifest("{manifest.as_posix()}")' for manifest in listManifest]
    listLine += ["sw_getModuleDirectoriesOfKind(Kit listDirectory)",
                 'list(JOIN listDirectory "\\n" text)',
                 f'file(WRITE "{outputPath.as_posix()}" "${{text}}")']
    scriptPath.write_text("\n".join(listLine) + "\n", encoding="utf-8")
    return subprocess.run([shutil.which("cmake") or "cmake", "-P", str(scriptPath)], capture_output=True, encoding="utf-8",
                          errors="replace", check=False)


def writeManifestInternal(folder: Path, name: str, listDependency: list[str]) -> Path:
    manifest = {"_name": name, "_version": "1.0.0", "_kind": "Kit", "_listDependency": [{"_name": item} for item in listDependency],
                "_listPlatform": ["Windows", "Linux"], "_listConfiguration": ["Dev", "Shipping"], "_listTarget": ["Client", "Server"]}
    path = folder / name / f"{name}.module.json"
    path.parent.mkdir(parents=True)
    path.write_text(json.dumps(manifest), encoding="utf-8")
    return path


@unittest.skipIf(shutil.which("cmake") is None, "cmake 가 PATH 에 없다")
class KitBuildOrderTest(unittest.TestCase):
    def testSharedKitComesBeforeEveryKitThatLinksIt(self) -> None:
        listManifest = sorted(kKitRoot.rglob("*.module.json"))
        with tempfile.TemporaryDirectory() as folder:
            outputPath = Path(folder) / "order.txt"
            completed = runOrderScriptInternal(listManifest, outputPath)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            listDirectory = [Path(line).resolve() for line in outputPath.read_text(encoding="utf-8").splitlines() if line]
        self.assertEqual(len(listDirectory), len(listManifest))

        mapIndex = {directory: index for index, directory in enumerate(listDirectory)}
        mapDirectoryOfTarget = {manifest.stem.removesuffix(".module"): manifest.parent.resolve() for manifest in listManifest}
        listPair = []
        for cmakeLists in kKitRoot.rglob("CMakeLists.txt"):
            for kitName, sharedKitName in _kLinkSharedKitRe.findall(cmakeLists.read_text(encoding="utf-8")):
                listPair.append((kitName, sharedKitName))
                self.assertLess(mapIndex[mapDirectoryOfTarget[sharedKitName]], mapIndex[mapDirectoryOfTarget[kitName]],
                                f"{sharedKitName} 폴더가 {kitName} 보다 뒤에 들어간다")
        self.assertTrue(listPair, "sw_linkSharedKit 을 부르는 키트가 하나도 없다 — 정규식이 낡았다")

    def testDependencyFirstThenNameOrderAndCycleFails(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            listManifest = [writeManifestInternal(root, "GF_Server_Alpha", ["GameFramework", "GF_Zeta"]),
                            writeManifestInternal(root, "GF_Zeta", ["GameFramework"]),
                            writeManifestInternal(root, "GF_Beta", [])]
            outputPath = root / "order.txt"
            completed = runOrderScriptInternal(listManifest, outputPath)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            listName = [Path(line).name for line in outputPath.read_text(encoding="utf-8").splitlines() if line]
            self.assertEqual(listName, ["GF_Beta", "GF_Zeta", "GF_Server_Alpha"])

            cycleRoot = root / "cycle"
            listCycle = [writeManifestInternal(cycleRoot, "GF_A", ["GF_B"]), writeManifestInternal(cycleRoot, "GF_B", ["GF_A"])]
            completed = runOrderScriptInternal(listCycle, root / "cycle.txt")
            self.assertNotEqual(completed.returncode, 0)
            self.assertIn("dependency cycle", completed.stderr)


if __name__ == "__main__":
    sys.exit(unittest.main())
