#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""RunDocStyle — 나열 · 괄호 중첩 · 긴 문장 · 긴 표 칸 · 대시 이음 · 폴더 트리 · 조어를 세는지, 코드 안은 세지 않는지(임시 저장소로)."""

from __future__ import annotations

import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts" / "lint"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts" / "lint" / "report"))

import RunDocStyle  # noqa: E402

_kBadDocument = """# 제목

이 모듈은 생성 · 조회 · 순회 · 파괴를 맡는다.
핸들은 매니저(씬(활성))에게 풉니다.
첫째 — 둘째 — 셋째.
이 문장은 아주 길어서 한 문장에 한 가지 생각만 담으라는 규칙을 어기고 있으며 쉼표로 계속 이어 가다가 결국 백이십 자를 넘겨 버리는 그런 문장이고 끝까지 마침표 없이 이어지다가 읽는 사람이 앞부분을 잊어버릴 만큼 길어진 뒤에야 마침내 끝납니다.

| 이름 | 설명 |
|---|---|
| 칸 | 여기에는 짧은 값이 아니라 문장이 들어 있어서 표 안의 글자 수 상한인 육십 자를 넘겨 버립니다. 문장이 둘입니다. |

```text
Source/
├── A.h
└── B.h
```

```cpp
// 코드 안의 a · b · c 와 (중첩(괄호)) 는 세지 않는다
```

`a · b · c` 인라인 코드 안도 세지 않는다.
"""

_kGoodDocument = """# 제목

이 모듈은 오브젝트를 만들고 찾습니다. 핸들은 그 핸들을 만든 씬에서만 대상을 찾을 수 있습니다.
"""


class DocStyleTest(unittest.TestCase):
    def testCountsEveryShapeOutsideCode(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "Bad.md").write_text(_kBadDocument, encoding="utf-8")
            stats = RunDocStyle.measureDocument(root, root / "Bad.md", RunDocStyle.kDefaultMaxSentenceLength,
                                                RunDocStyle.kDefaultMaxCellLength)

        self.assertEqual(stats.dotChainCount, 1)
        self.assertEqual(stats.nestedParenCount, 1)
        self.assertEqual(stats.dashChainCount, 1)
        self.assertEqual(stats.treeBlockCount, 1)
        self.assertEqual(stats.longCellCount, 1)
        self.assertEqual(stats.longSentenceCount, 1)
        self.assertEqual(stats.mapCoinedTerm["칸"], 1)
        self.assertEqual(stats.getScore(), 1 * 2 + 1 * 2 + 1 + 1 + 1 + 1 * 5)

    def testCleanDocumentScoresZero(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "Good.md").write_text(_kGoodDocument, encoding="utf-8")
            stats = RunDocStyle.measureDocument(root, root / "Good.md", RunDocStyle.kDefaultMaxSentenceLength,
                                                RunDocStyle.kDefaultMaxCellLength)

        self.assertEqual(stats.getScore(), 0)
        self.assertEqual(stats.coinedTermCount, 0)

    def testSkipsGeneratedRuleAndBuildDocuments(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for relativePath in ("README.md", "docs/Config/Generated.md", "docs/10_WritingDocs.md", "AGENTS.md",
                                 "Resource/credits.md", "build/Ninja-Debug/Note.md", "ThirdParty/Lib/README.md",
                                 "Source/Engine/README.md"):
                (root / relativePath).parent.mkdir(parents=True, exist_ok=True)
                (root / relativePath).write_text("# x\n", encoding="utf-8")

            listRelativePath = [path.relative_to(root).as_posix() for path in RunDocStyle.collectDocuments(root, [])]

        self.assertEqual(sorted(listRelativePath), ["README.md", "Source/Engine/README.md"])

    def testReportAlwaysReturnsZero(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "Bad.md").write_text(_kBadDocument, encoding="utf-8")
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                returnCode = RunDocStyle.main(["--root", str(root), "--files", "Bad.md", "--detail", "--terms"])

        self.assertEqual(returnCode, 0)
        self.assertIn("Bad.md", output.getvalue())
        self.assertIn("괄호 중첩", output.getvalue())


if __name__ == "__main__":
    unittest.main()
