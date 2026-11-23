"""
Pack 플랫폼 검사 — 플랫폼마다 프리셋과 패키지 폴더가 정해지고, 그 플랫폼이 짓지 않는 타깃 · 백엔드는 빌드 전에 실패한다.
빌드 · SDK 는 부르지 않는다.

실행: python -X utf8 Scripts/package/tests/test_pack_platform.py (CTest `Scripts.PackPlatform`).
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from package.pack import (  # noqa: E402
    ANDROID_PLATFORM, DESKTOP_PLATFORM, WEB_PLATFORM, PackageError, PackageRequest, find_preset, make_staging_dir, stage_package, verify_package,
)


def make_request(platform: str, target: str = "Client", rhi: str = "") -> PackageRequest:
    return PackageRequest(target=target, game="Playground", rhi=rhi, output_root=Path("out"), platform=platform)


class PackPlatformTest(unittest.TestCase):
    def test_preset_and_folder_of_each_platform(self) -> None:
        root = Path("out")
        self.assertEqual("Ninja-Shipping-Server", find_preset(make_request(DESKTOP_PLATFORM, "Server")))
        self.assertEqual(root / "Playground-Server", make_staging_dir(make_request(DESKTOP_PLATFORM, "Server"), root))
        self.assertEqual("Web-Shipping", find_preset(make_request(WEB_PLATFORM)))
        self.assertEqual(root / "Playground-Web", make_staging_dir(make_request(WEB_PLATFORM), root))
        self.assertEqual("Android-Shipping", find_preset(make_request(ANDROID_PLATFORM)))
        self.assertEqual(root / "Playground-Android", make_staging_dir(make_request(ANDROID_PLATFORM), root))

    def test_unsupported_target_or_backend_stops(self) -> None:
        for platform in (WEB_PLATFORM, ANDROID_PLATFORM):
            with self.assertRaises(PackageError):
                find_preset(make_request(platform, "Server"))
            with self.assertRaises(PackageError):
                find_preset(make_request(platform, rhi="Vulkan"))

    def test_platform_tool_failure_is_a_package_error(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            request = make_request(WEB_PLATFORM)
            with self.assertRaises(PackageError) as context:
                stage_package(request, root, root / "build" / "Bin", root / "out")
            self.assertIn("App.js", str(context.exception))

    def test_verify_looks_for_the_platform_package(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            staging = Path(folder)
            with self.assertRaises(PackageError):
                verify_package(make_request(WEB_PLATFORM), staging)
            (staging / "index.html").write_text("<html/>", encoding="utf-8")
            verify_package(make_request(WEB_PLATFORM), staging)
            with self.assertRaises(PackageError):
                verify_package(make_request(ANDROID_PLATFORM), staging)
            (staging / "playground.apk").write_bytes(b"apk")
            verify_package(make_request(ANDROID_PLATFORM), staging)


if __name__ == "__main__":
    unittest.main()
