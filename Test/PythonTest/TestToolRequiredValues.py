#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""도구가 필수 값 · 읽기 실패를 기본값으로 덮지 않고 멈추거나 위반으로 알리는가 — 쿠킹 표 키, Git 설치 폴더 환경 변수, 규칙 검사의 파일 읽기.

팩 설정 · 팩 레이아웃의 필수 값은 엔진이 읽는다(`PackConfig` 의 `Required` 칸 — `ConfigFileSchemaTest`).
"""

from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from common import Host  # noqa: E402
from common.BuildTree import BuildTree  # noqa: E402
from common.CookContract import CookContractSpec, kCookContractConfigRelative  # noqa: E402


class ToolRequiredValuesTest(unittest.TestCase):
    @unittest.skipUnless(sys.platform == "win32", "Windows 설치 폴더 탐색")
    def testGitSearchWithoutProgramFilesStops(self) -> None:
        with mock.patch.object(Host.shutil, "which", return_value=None), mock.patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(RuntimeError, "ProgramFiles"):
                Host.resolveGitExecutable()

    def testCookContractWithoutTargetExclusionStops(self) -> None:
        spec = json.loads((kRepositoryRoot / kCookContractConfigRelative).read_text(encoding="utf-8"))
        del spec["target_excluded_asset_kinds"]
        with self.assertRaises(KeyError):
            CookContractSpec(spec)

    def testBuildTreesWithoutPresetsStop(self) -> None:
        with tempfile.TemporaryDirectory() as tempDir:
            with self.assertRaises(FileNotFoundError):
                list(BuildTree.iterConfigured(Path(tempDir)))


if __name__ == "__main__":
    unittest.main()
