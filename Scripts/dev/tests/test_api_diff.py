"""
api-diff 회귀 검사 — diff 글에서 새 함수 · 멤버 · 등록 이름 · Config 키를 모으고, 두 갈래가 각자 더한 같은 이름을 겹침으로 낸다(D3-4).

실행: python -X utf8 Scripts/dev/tests/test_api_diff.py (CTest `Scripts.ApiDiff`). 저장소를 읽지 않는다.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from dev.api_diff import NewName, find_overlaps, parse_new_names  # noqa: E402

BRANCH_A = """diff --git a/Source/Engine/Renderer/RenderStability.h b/Source/Engine/Renderer/RenderStability.h
--- a/Source/Engine/Renderer/RenderStability.h
+++ b/Source/Engine/Renderer/RenderStability.h
@@ -1,3 +1,5 @@
+        [[nodiscard]] static uint32 countSettledFrames( const FrameViewTarget& target );
+        uint32 _settledFrameCount{ 0 };
diff --git a/Source/Engine/Renderer/RenderWatches.cpp b/Source/Engine/Renderer/RenderWatches.cpp
--- /dev/null
+++ b/Source/Engine/Renderer/RenderWatches.cpp
@@ -0,0 +1,2 @@
+    SW_AUTOMATION_WATCH( renderSettled, "Render.Settled", "Frames settled", &readSettled );
+    #define SW_AUTOMATION_WATCH( id, pName, pDescription, pRead ) "ignored"
diff --git a/Config/Engine/EngineConfig.json b/Config/Engine/EngineConfig.json
--- a/Config/Engine/EngineConfig.json
+++ b/Config/Engine/EngineConfig.json
@@ -1,2 +1,3 @@
+    "_renderStability": { "_frames": 4 },
"""

BRANCH_B = """diff --git a/Source/Engine/Renderer/RenderStability.h b/Source/Engine/Renderer/RenderStability.h
--- a/Source/Engine/Renderer/RenderStability.h
+++ b/Source/Engine/Renderer/RenderStability.h
@@ -1,3 +1,4 @@
+        uint32 _settledFrameCount = 0;
diff --git a/Source/Engine/Audio/AudioWatches.cpp b/Source/Engine/Audio/AudioWatches.cpp
--- a/Source/Engine/Audio/AudioWatches.cpp
+++ b/Source/Engine/Audio/AudioWatches.cpp
@@ -1,2 +1,3 @@
+    SW_AUTOMATION_WALL_WATCH( audioSettled, "Render.Settled", "Voices settled", &readVoices );
diff --git a/Source/Engine/Audio/AudioMixer.h b/Source/Engine/Audio/AudioMixer.h
--- a/Source/Engine/Audio/AudioMixer.h
+++ b/Source/Engine/Audio/AudioMixer.h
@@ -1,2 +1,3 @@
+        uint32 _settledFrameCount{ 0 };
-        [[nodiscard]] uint32 countSettledFrames() const;
+        [[nodiscard]] uint32 countSettledFrames( uint32 bus ) const;
diff --git a/Config/Engine/EngineConfig.json b/Config/Engine/EngineConfig.json
--- a/Config/Engine/EngineConfig.json
+++ b/Config/Engine/EngineConfig.json
@@ -1,2 +1,3 @@
+    "_renderStability": { "_frames": 8 },
"""


class ApiDiffTest(unittest.TestCase):
    def test_collect_each_kind(self) -> None:
        names = {(n._kind, n._name) for n in parse_new_names(BRANCH_A)}
        self.assertEqual(names, {("function", "countSettledFrames"), ("member", "_settledFrameCount"), ("registration", "Render.Settled"), ("config", "_renderStability")})

    def test_name_on_removed_line_is_not_new(self) -> None:
        self.assertNotIn("countSettledFrames", {n._name for n in parse_new_names(BRANCH_B)})

    def test_overlap_across_refs(self) -> None:
        overlaps = find_overlaps({"w-a": parse_new_names(BRANCH_A), "w-b": parse_new_names(BRANCH_B)})
        self.assertEqual(set(overlaps), {("member", "_settledFrameCount", "Source/Engine/Renderer/RenderStability.h"), ("registration", "Render.Settled", ""),
                                         ("config", "_renderStability", "Config/Engine/EngineConfig.json")})
        # 다른 파일의 같은 멤버 이름(AudioMixer.h)은 겹침이 아니다.
        self.assertEqual(overlaps[("member", "_settledFrameCount", "Source/Engine/Renderer/RenderStability.h")],
                         [("w-a", "Source/Engine/Renderer/RenderStability.h"), ("w-b", "Source/Engine/Renderer/RenderStability.h")])

    def test_one_branch_alone_has_no_overlap(self) -> None:
        self.assertEqual(find_overlaps({"w-a": parse_new_names(BRANCH_A)}), {})
        self.assertEqual(find_overlaps({"w-a": [NewName("function", "f", "a.h")], "w-b": [NewName("function", "g", "a.h")]}), {})


if __name__ == "__main__":
    unittest.main()
