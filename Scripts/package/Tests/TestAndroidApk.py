"""
Android APK 패키징 값 검사 — 매니페스트 틀의 자리가 모두 채워지고, 게임 덮기 · 프리셋 최소 API 를 읽고, 틀린 방향 · 판 순서 · 모르는 자리는 멈춘다.
도구(aapt2 · zipalign · apksigner)는 부르지 않는다 — 값 계산만 본다.

실행: python -X utf8 Scripts/package/Tests/TestAndroidApk.py (CTest `Scripts.AndroidApk`).
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from package.AndroidApk import ApkError, kManifestTemplatePath, readPresetAndroid, renderManifest, resolveSettings  # noqa: E402

kProjectRoot = Path(__file__).resolve().parents[3]
kConfig = {
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
    def testManifestTemplateIsFullyFilled(self) -> None:
        settings = resolveSettings(kConfig, "Empty", 29, 36, "arm64-v8a", kProjectRoot)
        text = renderManifest((kProjectRoot / kManifestTemplatePath).read_text(encoding="utf-8"), settings)
        self.assertNotIn("${", text)
        self.assertIn('package="com.swengine.empty"', text)
        self.assertIn('android:screenOrientation="sensorLandscape"', text)
        self.assertIn('android:value="App"', text)
        self.assertIn('android:version="0x402000"', text)

    def testGameOverrideWins(self) -> None:
        settings = resolveSettings(kConfig, "Puzzle", 29, 36, "arm64-v8a", kProjectRoot)
        self.assertEqual("portrait", settings.screenOrientation)
        self.assertEqual("Puzzle", settings.label)
        self.assertEqual("App", settings.libName)

    def testInvalidValuesStop(self) -> None:
        with self.assertRaises(ApkError):
            resolveSettings(dict(kConfig, screen_orientation="sideways"), "Empty", 29, 36, "arm64-v8a", kProjectRoot)
        with self.assertRaises(ApkError):
            resolveSettings(kConfig, "Empty", 30, 29, "arm64-v8a", kProjectRoot)
        with self.assertRaises(ApkError):
            resolveSettings({key: value for key, value in kConfig.items() if key != "label"}, "Empty", 29, 36, "arm64-v8a", kProjectRoot)
        settings = resolveSettings(kConfig, "Empty", 29, 36, "arm64-v8a", kProjectRoot)
        with self.assertRaises(ApkError):
            renderManifest("<manifest package=\"${unknownName}\"/>", settings)

    def testPresetGivesMinimumApiAndAbi(self) -> None:
        listPreset = [{"name": "Android-Shipping", "cacheVariables": {"ANDROID_PLATFORM": "android-29", "ANDROID_ABI": "arm64-v8a"}}]
        self.assertEqual((29, "arm64-v8a"), readPresetAndroid(listPreset))
        with self.assertRaises(ApkError):
            readPresetAndroid([{"name": "Android-Shipping", "cacheVariables": {"ANDROID_PLATFORM": "29", "ANDROID_ABI": "arm64-v8a"}}])


if __name__ == "__main__":
    unittest.main()
