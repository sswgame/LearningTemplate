"""ToolGate 판정 테스트: py -3 .claude/hooks/TestToolGate.py"""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ToolGate  # noqa: E402


class ToolGateTest(unittest.TestCase):
    def setUp(self):
        ToolGate.kStateDir = tempfile.mkdtemp()

    @staticmethod
    def pre(tool, toolInput):
        return ToolGate.ToolGate({"hook_event_name": "PreToolUse", "session_id": "t", "tool_name": tool, "tool_input": toolInput}).onPreToolUse()

    @staticmethod
    def post(tool, toolInput):
        ToolGate.ToolGate({"hook_event_name": "PostToolUse", "session_id": "t", "tool_name": tool, "tool_input": toolInput}).onPostToolUse()

    def testReadingSourceNeedsGraph(self):
        self.assertIsNotNone(self.pre("Read", {"file_path": "D:\\P\\Source\\Engine\\A.h"}))
        self.assertIsNotNone(self.pre("Bash", {"command": "sed -n 1,5p Source/Engine/A.h"}))
        self.post("Bash", {"command": 'graphify query "x"'})
        self.assertIsNone(self.pre("Read", {"file_path": "D:/P/Source/Engine/A.h"}))

    def testEditorUiNeedsPonytailThenTaste(self):
        target = {"file_path": "D:/P/Source/Editor/Panels/X.cpp"}
        self.assertIn("ponytail", self.pre("Edit", target)["hookSpecificOutput"]["permissionDecisionReason"])
        self.post("Skill", {"skill": "ponytail"})
        self.assertIn("taste-skill", self.pre("Edit", target)["hookSpecificOutput"]["permissionDecisionReason"])
        self.post("Skill", {"skill": "taste-skill"})
        self.assertIsNone(self.pre("Edit", target))

    def testPushNeedsBundleVerifyAndOtherCommandsPass(self):
        self.assertIsNotNone(self.pre("Bash", {"command": "git push origin abc:main"}))
        self.assertIsNone(self.pre("Bash", {"command": "git status"}))
        self.assertIsNone(self.pre("Edit", {"file_path": "D:/P/docs/NEXT.md"}))

    def testPollingLoopIsRefused(self):
        for command in ('until grep -q "x" f; do sleep 5; done; cat f', "until ! tasklist | grep -q git; do :; done", "while true; do sleep 1; done"):
            self.assertIn("no-polling-wait", self.pre("Bash", {"command": command})["hookSpecificOutput"]["permissionDecisionReason"])

    def testBuildOnlyInMainTree(self):
        for command in ("cd D:/P/.claude/worktrees/work-e10 && cmake --build --preset Ninja-Debug", "cmake --preset Ninja-Debug -S .claude/worktrees/x"):
            self.assertIn("one-build-in-main-tree", self.pre("Bash", {"command": command})["hookSpecificOutput"]["permissionDecisionReason"])
        for command in ("cmake --build --preset Ninja-Debug", "git -C .claude/worktrees/x log", "cd build/Ninja-Debug && ctest -R foo"):
            self.assertIsNone(self.pre("Bash", {"command": command}))

    def testAnyOfRequirement(self):
        target = {"file_path": "D:/P/Source/Engine/Graphics/RHI/Vulkan/V.cpp"}
        self.post("Skill", {"skill": "ponytail"})
        self.assertIsNotNone(self.pre("Edit", target))
        self.post("mcp__plugin_context7_context7__query-docs", {})
        self.assertIsNone(self.pre("Edit", target))


if __name__ == "__main__":
    unittest.main()
