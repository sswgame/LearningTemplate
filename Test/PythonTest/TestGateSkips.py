#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
게이트가 검사하지 못한 것을 통과로 내지 않는지 — 필수 입력(빌드 폴더 · 컴파일 DB)이 없으면 불성립(5), 있으면 실제로 대조한다.
기반 규칙(`LintGate.judgeOutcome`)은 `TestLintGate.py`, 종류 약속은 `TestGateVerdict.py` 가 본다.
"""

from __future__ import annotations

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from common.ExitCode import ExitCode  # noqa: E402
from lint.gate import CheckSourceGlob  # noqa: E402


class CheckSourceGlobOutcomeTest(unittest.TestCase):
    def setUp(self) -> None:
        self._temp = tempfile.TemporaryDirectory()
        self.root = Path(self._temp.name).resolve()
        for name in ("A", "B"):
            path = self.root / "Source" / "Engine" / f"{name}.cpp"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("// probe\n", encoding="utf-8")
        (self.root / "build").mkdir()

    def tearDown(self) -> None:
        self._temp.cleanup()

    def runGate(self) -> tuple[int, str]:
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer), contextlib.redirect_stderr(buffer):
            code = CheckSourceGlob.main(["--root", str(self.root), "--build-dir", "build"])
        return code, buffer.getvalue()

    def writeDatabase(self, listEntry: list[dict]) -> None:
        (self.root / "build" / "compile_commands.json").write_text(json.dumps(listEntry), encoding="utf-8")

    def entryOf(self, relative: str) -> dict:
        return {"directory": str(self.root), "file": (self.root / relative).as_posix()}

    def testNoCompileDatabaseIsUnavailableNotOk(self) -> None:
        code, output = self.runGate()
        self.assertEqual(code, ExitCode.PrerequisiteMissing)
        self.assertIn("UNAVAILABLE (컴파일 DB 없음", output)
        self.assertNotIn("OK", output)

    def testPlainDatabaseComparesSources(self) -> None:
        self.writeDatabase([self.entryOf("Source/Engine/A.cpp")])
        code, output = self.runGate()
        self.assertEqual(code, ExitCode.Violation)
        self.assertIn("compile_commands 에 없음: Source/Engine/B.cpp", output)

    def testUnityBundleIsExpandedInsteadOfSkipped(self) -> None:
        # 전에는 `unity_` 항목이 하나라도 있으면 "건너뜁니다" 로 통과했다 — CI 프리셋이 전부 유니티라 CI 에서는 이 게이트가 아무것도 보지 않았다.
        bundle = self.root / "build" / "Source" / "Engine" / "CMakeFiles" / "Engine.dir" / "Unity" / "unity_0_cxx.cxx"
        bundle.parent.mkdir(parents=True)
        bundle.write_text('#include "' + (self.root / "Source/Engine/A.cpp").as_posix() + '"\n', encoding="utf-8")
        self.writeDatabase([{"directory": str(self.root), "file": bundle.as_posix()}])
        code, output = self.runGate()
        self.assertEqual(code, ExitCode.Violation)
        self.assertIn("compile_commands 에 없음: Source/Engine/B.cpp", output)
        self.assertNotIn("compile_commands 에 없음: Source/Engine/A.cpp", output)

    def testUnityBundleCoveringEverythingPasses(self) -> None:
        bundle = self.root / "build" / "unity_0_cxx.cxx"
        bundle.write_text("".join(f'#include "{(self.root / "Source/Engine" / (name + ".cpp")).as_posix()}"\n' for name in ("A", "B")), encoding="utf-8")
        self.writeDatabase([{"directory": str(self.root), "file": bundle.as_posix()}])
        code, output = self.runGate()
        self.assertEqual(code, ExitCode.Passed, output)

    def testNoSourcesAtAllIsUnavailableNotOk(self) -> None:
        # --root 가 저장소가 아니면 소스가 하나도 없다 — "0 개를 대조하고 위반 없음" 은 통과가 아니다.
        for path in (self.root / "Source" / "Engine").glob("*.cpp"):
            path.unlink()
        self.writeDatabase([])
        code, output = self.runGate()
        self.assertEqual(code, ExitCode.PrerequisiteMissing)
        self.assertIn("UNAVAILABLE (훑을 파일 없음", output)


if __name__ == "__main__":
    unittest.main()
