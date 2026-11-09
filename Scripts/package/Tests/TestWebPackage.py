"""
웹 패키징 검사 — 빌드 산출물이 없으면 멈추고, 페이지가 캔버스 · 로더 · App.js 를 그 순서로 싣고, 서버가 교차 출처 격리 헤더를 준다.
file_packager 는 부르지 않는다.

실행: python -X utf8 Scripts/package/Tests/TestWebPackage.py (CTest `Scripts.WebPackage`).
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from package.WebPackage import IsolatedRequestHandler, WebPackageError, kIndexHtml, kPackLoaderName, makeWebPackage  # noqa: E402

kProjectRoot = Path(__file__).resolve().parents[3]


class WebPackageTest(unittest.TestCase):
    def testMissingBuildOutputStops(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with self.assertRaises(WebPackageError) as context:
                makeWebPackage("Empty", root / "build", root / "packs", root / "out", kProjectRoot)
            self.assertIn("App.js", str(context.exception))
            self.assertFalse((root / "out").exists())  # 멈춘 묶음은 폴더를 남기지 않는다

    def testPageLoadsCanvasThenLoaderThenApp(self) -> None:
        page = kIndexHtml.format(title="Empty", loader=kPackLoaderName)
        self.assertIn('<canvas id="canvas"', page)  # WebWindow::kCanvasSelector
        self.assertLess(page.index("var Module"), page.index(kPackLoaderName))
        self.assertLess(page.index(kPackLoaderName), page.index('src="App.js"'))

    def testServerSendsIsolationHeaders(self) -> None:
        sent: list[tuple[str, str]] = []
        handler = IsolatedRequestHandler.__new__(IsolatedRequestHandler)
        handler.send_header = lambda key, value: sent.append((key, value))  # type: ignore[method-assign]
        handler._headers_buffer = []  # type: ignore[attr-defined]
        handler.request_version = "HTTP/1.1"
        handler.wfile = type("Sink", (), {"write": lambda self, data: None, "flush": lambda self: None})()
        handler.end_headers()
        self.assertIn(("Cross-Origin-Opener-Policy", "same-origin"), sent)
        self.assertIn(("Cross-Origin-Embedder-Policy", "require-corp"), sent)


if __name__ == "__main__":
    unittest.main()
