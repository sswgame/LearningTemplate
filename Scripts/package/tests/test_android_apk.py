"""
Android APK 패키징 값 검사 — 매니페스트 틀의 자리가 모두 채워지고, 게임 덮기 · 프리셋 최소 API 를 읽고, 틀린 방향 · 판 순서 · 모르는 자리는 멈춘다.
dex 단계: 엔진 창이 찾는 Java 클래스의 소스가 있고, Java 소스가 없으면 멈추며, javac · d8 명령이 android.jar · 최소 API 를 쓴다.
도구(javac · d8 · aapt2 · zipalign · apksigner)는 부르지 않는다 — 값 계산만 본다.

실행: python -X utf8 Scripts/package/tests/test_android_apk.py (CTest `Scripts.AndroidApk`).
"""

from __future__ import annotations

import re
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from package.android_apk import (  # noqa: E402
    ApkError, JAVA_SOURCE_ROOT, MANIFEST_TEMPLATE_PATH, collect_java_sources, make_d8_command, make_javac_command, read_preset_android, render_manifest,
    resolve_settings,
)

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
        settings = resolve_settings(CONFIG, "Playground", 29, 36, "arm64-v8a", PROJECT_ROOT)
        text = render_manifest((PROJECT_ROOT / MANIFEST_TEMPLATE_PATH).read_text(encoding="utf-8"), settings)
        self.assertNotIn("${", text)
        self.assertIn('package="com.swengine.playground"', text)
        self.assertIn('android:screenOrientation="sensorLandscape"', text)
        self.assertIn('android:value="App"', text)
        self.assertIn('android:version="0x402000"', text)
        self.assertIn('android:hasCode="true"', text)   # classes.dex 의 입력 뷰를 런타임이 연다

    def test_game_override_wins(self) -> None:
        settings = resolve_settings(CONFIG, "Puzzle", 29, 36, "arm64-v8a", PROJECT_ROOT)
        self.assertEqual("portrait", settings.screen_orientation)
        self.assertEqual("Puzzle", settings.label)
        self.assertEqual("App", settings.lib_name)

    def test_invalid_values_stop(self) -> None:
        with self.assertRaises(ApkError):
            resolve_settings(dict(CONFIG, screen_orientation="sideways"), "Playground", 29, 36, "arm64-v8a", PROJECT_ROOT)
        with self.assertRaises(ApkError):
            resolve_settings(CONFIG, "Playground", 30, 29, "arm64-v8a", PROJECT_ROOT)
        with self.assertRaises(ApkError):
            resolve_settings({key: value for key, value in CONFIG.items() if key != "label"}, "Playground", 29, 36, "arm64-v8a", PROJECT_ROOT)
        settings = resolve_settings(CONFIG, "Playground", 29, 36, "arm64-v8a", PROJECT_ROOT)
        with self.assertRaises(ApkError):
            render_manifest("<manifest package=\"${unknownName}\"/>", settings)

    def test_preset_gives_minimum_api_and_abi(self) -> None:
        list_preset = [{"name": "Android-Shipping", "cacheVariables": {"ANDROID_PLATFORM": "android-29", "ANDROID_ABI": "arm64-v8a"}}]
        self.assertEqual((29, "arm64-v8a"), read_preset_android(list_preset, "Android-Shipping"))
        with self.assertRaises(ApkError):
            read_preset_android([{"name": "Android-Shipping", "cacheVariables": {"ANDROID_PLATFORM": "29", "ANDROID_ABI": "arm64-v8a"}}], "Android-Shipping")

    def test_development_preset_inherits_api_and_abi(self) -> None:
        """개발 구성(Android-Release)은 Android-Shipping 을 상속해 API · ABI 를 적지 않는다 — 상속 사슬을 따라 읽고, 자식 값이 이긴다."""
        list_preset = [
            {"name": "Android-Shipping", "cacheVariables": {"ANDROID_PLATFORM": "android-29", "ANDROID_ABI": "arm64-v8a"}},
            {"name": "Android-Release", "inherits": "Android-Shipping", "cacheVariables": {"SW_STATIC_MODULES": "ON"}},
            {"name": "Android-Other", "inherits": ["Android-Release"], "cacheVariables": {"ANDROID_PLATFORM": "android-31"}},
        ]
        self.assertEqual((29, "arm64-v8a"), read_preset_android(list_preset, "Android-Release"))
        self.assertEqual((31, "arm64-v8a"), read_preset_android(list_preset, "Android-Other"))
        with self.assertRaises(ApkError):
            read_preset_android(list_preset, "Android-Missing")
        with self.assertRaises(ApkError):
            read_preset_android(list_preset + [{"name": "Android-Loop", "inherits": "Android-Loop"}], "Android-Loop")

    def test_window_java_class_has_source(self) -> None:
        """C++ 창이 클래스 로더로 찾는 이름(`AndroidInputView::kJavaClassName`)의 소스가 dex 에 담길 뿌리 아래 있다 — 이름 · 경로가 어긋나면 기기에서 창이 서지 않는다."""
        header = (PROJECT_ROOT / "Source/Engine/Window/Android/AndroidInputView.h").read_text(encoding="utf-8")
        match = re.search(r'kJavaClassName = "([A-Za-z0-9_.]+)"', header)
        self.assertIsNotNone(match)
        source = PROJECT_ROOT / JAVA_SOURCE_ROOT / (match.group(1).replace(".", "/") + ".java")
        self.assertIn(source, collect_java_sources(PROJECT_ROOT))

    def test_missing_java_sources_stop(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(ApkError):
                collect_java_sources(Path(folder))

    def test_java_and_dex_commands(self) -> None:
        android_jar = Path("sdk/platforms/android-36/android.jar")
        javac = make_javac_command(Path("javac"), android_jar, Path("classes"), [Path("A.java")])
        self.assertEqual(android_jar, javac[javac.index("-bootclasspath") + 1])   # java.* 도 기기의 것(android.jar)으로
        self.assertEqual("8", javac[javac.index("-target") + 1])
        self.assertEqual(Path("A.java"), javac[-1])
        d8 = make_d8_command(Path("d8"), android_jar, 29, Path("dex"), [Path("A.class")])
        self.assertEqual("29", d8[d8.index("--min-api") + 1])
        self.assertEqual(android_jar, d8[d8.index("--lib") + 1])
        self.assertIn("--release", d8)


if __name__ == "__main__":
    unittest.main()
