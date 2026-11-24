"""ToolPolicy 판정 테스트: py -3 .claude/hooks/test_tool_policy.py"""
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tool_policy  # noqa: E402


class ToolPolicyTest(unittest.TestCase):
    def setUp(self):
        tool_policy.STATE_DIR = tempfile.mkdtemp()

    @staticmethod
    def pre(tool, tool_input):
        return tool_policy.ToolPolicy({"hook_event_name": "PreToolUse", "session_id": "t", "tool_name": tool, "tool_input": tool_input}).on_pre_tool_use()

    @staticmethod
    def post(tool, tool_input):
        tool_policy.ToolPolicy({"hook_event_name": "PostToolUse", "session_id": "t", "tool_name": tool, "tool_input": tool_input}).on_post_tool_use()

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

    def test_push_needs_preflight_and_other_commands_pass(self):
        self.assertIsNotNone(self.pre("Bash", {"command": "git push origin abc:main"}))
        self.assertIsNotNone(self.pre("Bash", {"command": "bash Scripts/dev/preflight.sh --dry-run"}))
        self.assertIsNone(self.pre("Bash", {"command": "git status"}))
        self.assertIsNone(self.pre("Edit", {"file_path": "D:/P/docs/NEXT.md"}))

    def test_hook_call_latency_is_reported(self):
        """호출 하나의 지연(빈 입력 20 번의 p95)을 찍고 예산(Config/Dev/Verify.json 의 _hookBudgetMilliseconds)을 넘으면 경고한다 — 기계 · 부하마다 달라
        판정은 바꾸지 않는다. 넘으면 `py -3 -I -S`(site 를 읽지 않는다)와 PostToolUse 매처 좁히기를 본다(docs/NEXT.md 측정 Scripts)."""
        here = os.path.dirname(os.path.abspath(__file__))
        with open(os.path.join(here, "..", "..", "Config", "Dev", "Verify.json"), encoding="utf-8") as stream:
            budget = json.load(stream)["_hookBudgetMilliseconds"]
        list_milliseconds = []
        for _ in range(20):
            started = time.perf_counter()
            subprocess.run([sys.executable, "-I", "-S", os.path.join(here, "tool_policy.py")], input=b"{}", capture_output=True, check=True)
            list_milliseconds.append((time.perf_counter() - started) * 1000.0)
        p95 = sorted(list_milliseconds)[18]
        print(f"\n[tool-policy] 훅 호출 p95 {p95:.0f} ms (예산 {budget} ms){' - 경고: 예산을 넘었다' if p95 > budget else ''}")
        self.assertGreater(p95, 0.0)

    def test_tool_rules_anchor_on_the_command_position(self):
        for command in ("py -3 -m Scripts linux-scenarios all", "cd x && py -3 Scripts/dev/linux_scenarios.py chain a", "python3 Scripts/dev/linux_scenarios.py build"):
            self.assertIn("linux-verify-before-linux", self.pre("Bash", {"command": command})["hookSpecificOutput"]["permissionDecisionReason"])
        for command in ("git -C D:/P push origin abc:main", "cd x && bash Scripts/dev/preflight.sh --no-linux"):
            self.assertIn("preflight-before-push", self.pre("Bash", {"command": command})["hookSpecificOutput"]["permissionDecisionReason"])
        for command in ("git grep -n run_in_wsl Scripts/dev/linux_scenarios.py", "git show HEAD:Scripts/dev/preflight.sh", "grep -n push Scripts/dev/preflight.sh"):
            self.assertIsNone(self.pre("Bash", {"command": command}))

    def test_polling_loop_is_refused(self):
        for command in ('until grep -q "x" f; do sleep 5; done; cat f', "until ! tasklist | grep -q git; do :; done", "while true; do sleep 1; done"):
            self.assertIn("no-polling-wait", self.pre("Bash", {"command": command})["hookSpecificOutput"]["permissionDecisionReason"])

    def test_orphan_commands_are_refused(self):
        for tool, command, rule in (("Monitor", "tail -n +1 -F a.log | grep --line-buffered x", "no-endless-follow"), ("Bash", "tail -f a.log", "no-endless-follow"),
                                    ("PowerShell", "Get-Content a.log -Wait", "no-endless-follow"), ("Bash", "(timeout 115 py -3 leaf.py | tail -2)", "no-timeout-python"),
                                    ("Bash", "py -3 a.py; cat > /dev/null; grep x f", "no-stdin-wait")):
            self.assertIn(rule, self.pre(tool, {"command": command})["hookSpecificOutput"]["permissionDecisionReason"])
        for command in ("tail -60 a.log | cut -c1-300", "tail -n 5 Full.log", "x | tail -2; rm -f b", "timeout 60 ./App.exe", "echo x | cat > /dev/null", "cat a > /dev/null"):
            self.assertIsNone(self.pre("Bash", {"command": command}), command)

    def test_build_only_in_main_tree(self):
        for command in ("cd D:/P/.claude/worktrees/work-e10 && cmake --build --preset Ninja-Debug", "cmake --preset Ninja-Debug -S .claude/worktrees/x",
                        "cd D:/Projects/Personal/LT-wt/agentcfg && cmake --build --preset Ninja-Release"):
            self.assertIn("one-build-in-main-tree", self.pre("Bash", {"command": command})["hookSpecificOutput"]["permissionDecisionReason"])
        for command in ("cmake --build --preset Ninja-Debug", "git -C .claude/worktrees/x log", "cd build/Ninja-Debug && ctest -R foo"):
            self.assertIsNone(self.pre("Bash", {"command": command}))

    def test_any_of_requirement(self):
        target = {"file_path": "D:/P/Source/Engine/Graphics/RHI/Vulkan/V.cpp"}
        self.post("Skill", {"skill": "ponytail"})
        self.assertIsNotNone(self.pre("Edit", target))
        self.post("mcp__plugin_context7_context7__query-docs", {})
        self.assertIsNotNone(self.pre("Edit", target))
        self.post("Skill", {"skill": "source-driven-development"})
        self.assertIsNone(self.pre("Edit", target))

    def test_forbidden_commands_and_generated_files(self):
        refused = {
            "no-git-stash": [("Bash", {"command": "git stash"}), ("Bash", {"command": "cd x && git -C D:/P stash pop"}), ("PowerShell", {"command": "git stash push -m a"})],
            "move-with-tools": [("Bash", {"command": "git mv Source/Engine/A.h Source/Engine/B/A.h"}), ("Bash", {"command": "git mv Content/game/a.png Content/game/b.png"})],
            "no-hand-edit-generated": [("Edit", {"file_path": "D:/P/Content/game/playground/materials/matgraph/layered_stone.gen.material"}),
                                       ("Write", {"file_path": "D:/P/Content/shaders/matgraph/x_1a2b.hlsl"}),
                                       ("Edit", {"file_path": r"D:\P\Config\Dev\AssetReferenceFields.generated.json"})],
            "no-hand-raise-magic-baseline": [("Edit", {"file_path": "D:/P/Config/Rules/Lint/MagicLiteralsBaseline.json"})],
            "no-per-folder-constants-header": [("Write", {"file_path": "D:/P/Source/Engine/Audio/AudioConstants.h"})],
        }
        self.post("Skill", {"skill": "ponytail"})
        for name, calls in refused.items():
            for tool, tool_input in calls:
                self.assertIn(name, self.pre(tool, tool_input)["hookSpecificOutput"]["permissionDecisionReason"], (tool, tool_input))
        for tool, tool_input in (("Bash", {"command": "git log --grep stash"}), ("Bash", {"command": "git mv docs/a.md docs/b.md"}),
                                 ("Edit", {"file_path": "D:/P/Content/game/playground/materials/matgraph/layered_stone.matgraph.xml"}),
                                 ("Edit", {"file_path": "D:/P/Config/Dev/AssetReferenceFields.json"}),
                                 ("Write", {"file_path": "D:/P/Source/Core/Foundation/Constants.h"}),
                                 ("Edit", {"file_path": "D:/P/Source/Engine/Audio/AudioConstants.h"})):
            self.assertIsNone(self.pre(tool, tool_input), (tool, tool_input))


if __name__ == "__main__":
    unittest.main()
