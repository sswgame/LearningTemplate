"""
Android APK 패키징 값 검사 — 매니페스트 틀의 자리가 모두 채워지고, 게임 덮기 · 프리셋 최소 API 를 읽고, 틀린 방향 · 판 순서 · 모르는 자리는 멈춘다.
도구(aapt2 · zipalign · apksigner)는 부르지 않는다 — 값 계산만 본다.

실행: python -X utf8 Scripts/package/tests/test_android_apk.py (CTest `Scripts.AndroidApk`).
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from package.android_apk import ApkError, MANIFEST_TEMPLATE_PATH, read_preset_android, render_manifest, resolve_settings  # noqa: E402

PROJECT_ROOT = Path(__file__).resolve().parents[3]
CONFIG = {
    "application_id_prefix": "com.swengine.",
    "lib_name": "App",
    "label": "SW Engine",
    "version_code": 3,
    "version_name": "0.3",
    "screen_orientation": "sensorLandscape",
    "vulkan_version": "0x402000",
    "page_size_kb": 16,
    "debug_keystore": "build/AndroidDebug/debug.keystore",
    "games": {"Puzzle": {"screen_orientation": "portrait", "label": "Puzzle"}},
}


class AndroidApkTest(unittest.TestCase):
    def test_manifest_template_is_fully_filled(self) -> None:
        settings = resolve_settings(CONFIG, "Empty", 29, 36, "arm64-v8a", PROJECT_ROOT)
        text = render_manifest((PROJECT_ROOT / MANIFEST_TEMPLATE_PATH).read_text(encoding="utf-8"), settings)
        self.assertNotIn("${", text)
        self.assertIn('package="com.swengine.empty"', text)
        self.assertIn('android:screenOrientation="sensorLandscape"', text)
        self.assertIn('android:value="App"', text)
        self.assertIn('android:version="0x402000"', text)

    def test_game_override_wins(self) -> None:
        settings = resolve_settings(CONFIG, "Puzzle", 29, 36, "arm64-v8a", PROJECT_ROOT)
        self.assertEqual("portrait", settings.screen_orientation)
        self.assertEqual("Puzzle", settings.label)
        self.assertEqual("App", settings.lib_name)

    def test_invalid_values_stop(self) -> None:
        with self.assertRaises(ApkError):
            resolve_settings(dict(CONFIG, screen_orientation="sideways"), "Empty", 29, 36, "arm64-v8a", PROJECT_ROOT)
        with self.assertRaises(ApkError):
            resolve_settings(CONFIG, "Empty", 30, 29, "arm64-v8a", PROJECT_ROOT)
        with self.assertRaises(ApkError):
            resolve_settings({key: value for key, value in CONFIG.items() if key != "label"}, "Empty", 29, 36, "arm64-v8a", PROJECT_ROOT)
        settings = resolve_settings(CONFIG, "Empty", 29, 36, "arm64-v8a", PROJECT_ROOT)
        with self.assertRaises(ApkError):
            render_manifest("<manifest package=\"${unknownName}\"/>", settings)

    def test_preset_gives_minimum_api_and_abi(self) -> None:
        list_preset = [{"name": "Android-Shipping", "cacheVariables": {"ANDROID_PLATFORM": "android-29", "ANDROID_ABI": "arm64-v8a"}}]
        self.assertEqual((29, "arm64-v8a"), read_preset_android(list_preset))
        with self.assertRaises(ApkError):
            read_preset_android([{"name": "Android-Shipping", "cacheVariables": {"ANDROID_PLATFORM": "29", "ANDROID_ABI": "arm64-v8a"}}])


if __name__ == "__main__":
    unittest.main()
