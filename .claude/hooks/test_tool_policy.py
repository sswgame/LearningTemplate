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
        self.post("Skill", {"skill": "humanizer"})
        self.assertIn("taste-skill", self.pre("Edit", target)["hookSpecificOutput"]["permissionDecisionReason"])
        self.post("Skill", {"skill": "taste-skill"})
        self.assertIsNone(self.pre("Edit", target))

    def test_comments_and_commits_need_the_writing_skill(self):
        self.post("Skill", {"skill": "ponytail"})
        self.assertIn("humanizer", self.pre("Edit", {"file_path": "D:/P/Source/Core/A.h"})["hookSpecificOutput"]["permissionDecisionReason"])
        self.assertIsNotNone(self.pre("Bash", {"command": "git commit -q -F msg.txt"}))
        self.assertIsNotNone(self.pre("Bash", {"command": "git -C D:/P/LT-wt/x commit -m a"}))
        self.assertIsNone(self.pre("Bash", {"command": "git commit-tree abc"}))
        self.assertIn("humanizer", self.pre("Edit", {"file_path": "D:/P/docs/NEXT.md"})["hookSpecificOutput"]["permissionDecisionReason"])
        self.post("Skill", {"skill": "humanizer"})
        self.assertIsNone(self.pre("Edit", {"file_path": "D:/P/Source/Core/A.h"}))
        self.assertIsNone(self.pre("Bash", {"command": "git commit -q -F msg.txt"}))

    def test_push_needs_preflight_and_other_commands_pass(self):
        self.assertIsNotNone(self.pre("Bash", {"command": "git push origin abc:main"}))
        self.assertIsNotNone(self.pre("Bash", {"command": "bash Scripts/verify/preflight.sh --dry-run"}))
        self.assertIsNone(self.pre("Bash", {"command": "git status"}))
        self.post("Skill", {"skill": "humanizer"})
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
        for command in ("py -3 -m Scripts linux-verify all", "cd x && py -3 Scripts/verify/linux_verify.py chain a", "python3 Scripts/verify/linux_verify.py build"):
            self.assertIn("linux-verify-before-linux", self.pre("Bash", {"command": command})["hookSpecificOutput"]["permissionDecisionReason"])
        for command in ("git -C D:/P push origin abc:main", "cd x && bash Scripts/verify/preflight.sh --no-linux"):
            self.assertIn("preflight-before-push", self.pre("Bash", {"command": command})["hookSpecificOutput"]["permissionDecisionReason"])
        for command in ("git grep -n run_in_wsl Scripts/verify/linux_verify.py", "git show HEAD:Scripts/verify/preflight.sh", "grep -n push Scripts/verify/preflight.sh"):
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
        self.post("Skill", {"skill": "humanizer"})
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
            "no-hand-edit-lint-baseline": [("Edit", {"file_path": "D:/P/Config/Rules/Lint/MagicLiteralsBaseline.json"}),
                                           ("Write", {"file_path": "D:/P/Config/Rules/Lint/NamingWordsBaseline.json"})],
            "no-banned-header-name": [("Write", {"file_path": "D:/P/Source/Engine/Audio/AudioTypes.h"}), ("Write", {"file_path": "D:/P/Source/Engine/X/LiveReloadWatch.h"})],
            "no-per-folder-constants-header": [("Write", {"file_path": "D:/P/Source/Engine/Audio/AudioConstants.h"})],
        }
        self.post("Skill", {"skill": "ponytail"})
        self.post("Skill", {"skill": "humanizer"})
        for name, calls in refused.items():
            for tool, tool_input in calls:
                self.assertIn(name, self.pre(tool, tool_input)["hookSpecificOutput"]["permissionDecisionReason"], (tool, tool_input))
        self.post("Skill", {"skill": "security-and-hardening"})   # 온라인 키트 경로의 다른 규칙을 채운다
        for tool, tool_input in (("Bash", {"command": "git log --grep stash"}), ("Bash", {"command": "git mv docs/a.md docs/b.md"}),
                                 ("Edit", {"file_path": "D:/P/Content/game/playground/materials/matgraph/layered_stone.matgraph.xml"}),
                                 ("Edit", {"file_path": "D:/P/Config/Dev/AssetReferenceFields.json"}),
                                 ("Write", {"file_path": "D:/P/Source/Core/Foundation/Constants.h"}),
                                 ("Edit", {"file_path": "D:/P/Source/Engine/Audio/AudioConstants.h"}),
                                 ("Write", {"file_path": "D:/P/Source/Engine/Audio/TypeRegistry.h"}), ("Write", {"file_path": "D:/P/Source/Engine/X/LiveOpsReload.h"}),
                                 ("Write", {"file_path": "D:/P/Source/GameFramework/Kits/Feature/Online/Chat/Runtime/ChatTypes.h"}),
                                 ("Write", {"file_path": os.path.join(tool_policy.PROJECT_ROOT, "Source", "Core", "Foundation", "Types.h")}),
                                 ("Edit", {"file_path": "D:/P/Source/Engine/Audio/AudioTypes.h"})):
            self.assertIsNone(self.pre(tool, tool_input), (tool, tool_input))


class AgentDispatchTest(unittest.TestCase):
    """agent_prompt_check: 작업 종류 표에 맞는 종류 · 모델 · effort 만 받는다."""

    def setUp(self):
        import agent_prompt_check
        self.check = agent_prompt_check.check
        with open(agent_prompt_check.RULES_PATH, encoding="utf-8") as file:
            self.rules = json.load(file)["agentPrompt"]

    def test_read_only_kind_needs_only_matching_choice(self):
        call = {"subagent_type": "Explore", "model": "haiku", "effort": "low", "prompt": "작업 종류: find\nX 를 찾는다"}
        self.assertIsNone(self.check(call, self.rules))
        self.assertIn("model", self.check({**call, "model": "opus"}, self.rules))
        self.assertIn("effort", self.check({**call, "effort": None}, self.rules))

    def test_missing_or_unknown_kind_is_refused(self):
        self.assertIn("작업 종류", self.check({"subagent_type": "Explore", "prompt": "찾는다"}, self.rules))
        self.assertIn("작업 종류", self.check({"prompt": "작업 종류: whatever"}, self.rules))

    def test_editing_kind_also_needs_the_required_text(self):
        call = {"model": "opus", "effort": "high", "prompt": "작업 종류: implement\n고친다"}
        self.assertIn("빠진 것", self.check(call, self.rules))

    def test_script_run_kind_needs_its_own_text(self):
        common = ("C++17 만 쓴다. 빌드 금지. 새 린트 규칙 금지. 한국어. 커밋은 원인/조치/결과. "
                  "docs 에는 계획 · 할 일 · 규칙만. NEXT: … 끝.")
        call = {"model": "haiku", "effort": "low", "prompt": "작업 종류: script-run\n" + common}
        self.assertIn("돌릴 명령", self.check(call, self.rules))
        own = " 돌릴 명령 목록과 차례는 아래와 같다. 기대와 다르면 멈추고 보고한다. 손 편집 금지."
        self.assertIsNone(self.check({**call, "prompt": call["prompt"] + own}, self.rules))
        self.assertIn("model", self.check({**call, "model": "sonnet", "prompt": call["prompt"] + own}, self.rules))

    def test_inherited_call_and_refused_isolation(self):
        self.assertIsNone(self.check({"subagent_type": "fork", "prompt": "이어서"}, self.rules))
        self.assertIsNotNone(self.check({"subagent_type": "fork", "isolation": "worktree", "prompt": "x"}, self.rules))


if __name__ == "__main__":
    unittest.main()
