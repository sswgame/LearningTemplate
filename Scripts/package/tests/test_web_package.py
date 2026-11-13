"""
웹 패키징 검사 — 빌드 산출물이 없으면 멈추고, 페이지가 캔버스 · 로더 · App.js 를 그 순서로 싣고, 서버가 교차 출처 격리 헤더를 준다.
웹 확인(web-check)의 확인 페이지가 보고 스크립트를 Module 뒤 · App.js 앞에 싣고, 판정이 오류 줄 · 빠진 보고를 실패로 센다.
file_packager · 브라우저는 부르지 않는다.

실행: python -X utf8 Scripts/package/tests/test_web_package.py (CTest `Scripts.WebPackage`).
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from package.web_check import RunReport, evaluate_run, AUDIO_STARTED_LINE, PROBE_PATH, write_check_page  # noqa: E402
from package.web_package import IsolatedRequestHandler, WebPackageError, INDEX_HTML, PACK_LOADER_NAME, make_web_package  # noqa: E402

PROJECT_ROOT = Path(__file__).resolve().parents[3]


class WebPackageTest(unittest.TestCase):
    def test_missing_build_output_stops(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with self.assertRaises(WebPackageError) as context:
                make_web_package("Empty", root / "build", root / "packs", root / "out", PROJECT_ROOT)
            self.assertIn("App.js", str(context.exception))
            self.assertFalse((root / "out").exists())  # 멈춘 묶음은 폴더를 남기지 않는다

    def test_page_loads_canvas_then_loader_then_app(self) -> None:
        page = INDEX_HTML.format(title="Empty", loader=PACK_LOADER_NAME, reporter="")
        self.assertIn('<canvas id="canvas"', page)  # WebWindow::kCanvasSelector
        self.assertLess(page.index("var Module"), page.index(PACK_LOADER_NAME))
        self.assertLess(page.index(PACK_LOADER_NAME), page.index('src="App.js"'))

    def test_server_sends_isolation_headers(self) -> None:
        sent: list[tuple[str, str]] = []
        handler = IsolatedRequestHandler.__new__(IsolatedRequestHandler)
        handler.send_header = lambda key, value: sent.append((key, value))  # type: ignore[method-assign]
        handler._headers_buffer = []  # type: ignore[attr-defined]
        handler.request_version = "HTTP/1.1"
        handler.wfile = type("Sink", (), {"write": lambda self, data: None, "flush": lambda self: None})()
        handler.end_headers()
        self.assertIn(("Cross-Origin-Opener-Policy", "same-origin"), sent)
        self.assertIn(("Cross-Origin-Embedder-Policy", "require-corp"), sent)

    def test_check_page_reports_between_module_and_app(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            write_check_page(Path(folder), "Empty", "abc123", 5)
            page = (Path(folder) / "check.html").read_text(encoding="utf-8")
        self.assertLess(page.index("var Module"), page.index("__swcheck/"))
        self.assertLess(page.index("__swcheck/"), page.index('src="App.js"'))
        self.assertIn('"abc123"', page)
        self.assertIn(PROBE_PATH, page)

    def test_evaluate_counts_errors_and_missing_reports(self) -> None:
        report = RunReport(list_line=["O booted", AUDIO_STARTED_LINE], gpu="yes")
        report.done.set()
        self.assertEqual([], evaluate_run("run1", report))
        report.list_line.append("E WebGPU: device lost")
        report.exit = "abort oom"
        self.assertEqual(2, len(evaluate_run("run1", report)))
        self.assertEqual(3, len(evaluate_run("run2", RunReport())))  # 끝나지 않았다 + GPU 보고 없음 + 오디오 시작 줄 없음
        silent = RunReport(list_line=["O booted"], gpu="yes")
        silent.done.set()
        self.assertEqual(1, len(evaluate_run("run3", silent)))  # 워클릿이 링을 당기지 않으면 진다


if __name__ == "__main__":
    unittest.main()
