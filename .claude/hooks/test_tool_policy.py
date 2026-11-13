"""ToolGate 판정 테스트: py -3 .claude/hooks/test_tool_policy.py"""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tool_policy  # noqa: E402


class ToolGateTest(unittest.TestCase):
    def setUp(self):
        tool_policy.STATE_DIR = tempfile.mkdtemp()

    @staticmethod
    def pre(tool, tool_input):
        return tool_policy.ToolGate({"hook_event_name": "PreToolUse", "session_id": "t", "tool_name": tool, "tool_input": tool_input}).on_pre_tool_use()

    @staticmethod
    def post(tool, tool_input):
        tool_policy.ToolGate({"hook_event_name": "PostToolUse", "session_id": "t", "tool_name": tool, "tool_input": tool_input}).on_post_tool_use()

    def test_reading_source_needs_graph(self):
        self.assertIsNotNone(self.pre("Read", {"file_path": "D:\\P\\Source\\Engine\\A.h"}))
        self.assertIsNotNone(self.pre("Bash", {"command": "sed -n 1,5p Source/Engine/A.h"}))
        self.post("Bash", {"command": 'graphify query "x"'})
        self.assertIsNone(self.pre("Read", {"file_path": "D:/P/Source/Engine/A.h"}))

    def test_editor_ui_needs_ponytail_then_taste(self):
        target = {"file_path": "D:/P/Source/Editor/Panels/X.cpp"}
        self.assertIn("ponytail", self.pre("Edit", target)["hookSpecificOutput"]["permissionDecisionReason"])
        self.post("Skill", {"skill": "ponytail"})
        self.assertIn("taste-skill", self.pre("Edit", target)["hookSpecificOutput"]["permissionDecisionReason"])
        self.post("Skill", {"skill": "taste-skill"})
        self.assertIsNone(self.pre("Edit", target))

    def test_push_needs_bundle_verify_and_other_commands_pass(self):
        self.assertIsNotNone(self.pre("Bash", {"command": "git push origin abc:main"}))
        self.assertIsNone(self.pre("Bash", {"command": "git status"}))
        self.assertIsNone(self.pre("Edit", {"file_path": "D:/P/docs/NEXT.md"}))

    def test_polling_loop_is_refused(self):
        for command in ('until grep -q "x" f; do sleep 5; done; cat f', "until ! tasklist | grep -q git; do :; done", "while true; do sleep 1; done"):
            self.assertIn("no-polling-wait", self.pre("Bash", {"command": command})["hookSpecificOutput"]["permissionDecisionReason"])

    def test_build_only_in_main_tree(self):
        for command in ("cd D:/P/.claude/worktrees/work-e10 && cmake --build --preset Ninja-Debug", "cmake --preset Ninja-Debug -S .claude/worktrees/x"):
            self.assertIn("one-build-in-main-tree", self.pre("Bash", {"command": command})["hookSpecificOutput"]["permissionDecisionReason"])
        for command in ("cmake --build --preset Ninja-Debug", "git -C .claude/worktrees/x log", "cd build/Ninja-Debug && ctest -R foo"):
            self.assertIsNone(self.pre("Bash", {"command": command}))

    def test_any_of_requirement(self):
        target = {"file_path": "D:/P/Source/Engine/Graphics/RHI/Vulkan/V.cpp"}
        self.post("Skill", {"skill": "ponytail"})
        self.assertIsNotNone(self.pre("Edit", target))
        self.post("mcp__plugin_context7_context7__query-docs", {})
        self.assertIsNone(self.pre("Edit", target))


if __name__ == "__main__":
    unittest.main()
