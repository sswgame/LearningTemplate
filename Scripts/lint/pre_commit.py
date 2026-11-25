"""
Scripts/lint/pre_commit.py

린트 입구 하나 — 규칙은 `Scripts/lint/*.py` 의 `LintRule` 하위 클래스다(`lint_rule.py`).

  py -3 -m Scripts lint                 # 커밋 훅: 모든 규칙이 스테이지된 파일만 본다(LintScope) + 포맷
  py -3 -m Scripts lint --all           # 묶음 검증: 모든 규칙이 저장소 전체를 본다 + 포맷(preflight.sh 첫 단계)
  py -3 -m Scripts lint --list          # 규칙 목록(갈래 · 막는 것 · 왜 린트인가 · 예외 파일)
  py -3 -m Scripts lint <id>            # 규칙 하나가 저장소 전체를 본다(--files: 그 파일만, --self-check: 자체 검사, --ratchet · --accept: 기준선)

커밋 훅은 저장소 크기에 비례하지 않는다: 규칙은 스테이지된 파일만 보고, 여러 파일을 보는 규칙은 내용 해시 색인(`index_cache`)을 바뀐 파일만
다시 계산해 쓴다. 규칙의 표 · 예외 · 모듈(그 규칙만 쓰는 도우미 포함)이 스테이지에 있으면 그 규칙만 저장소 전체를 보고(`LintRule.list_own_inputs`),
여러 규칙이 함께 쓰는 도우미만 바뀌었으면 그것을 쓰는 규칙의 자체 검사를 돈다(`LintRule.list_inputs`). `--all` 은 모든 규칙의 자체 검사도 돈다.
이 모양은 시간이 아니라 연 파일 수로 지킨다(`check_read_bound` — 린트 코드가 바뀐 커밋과 `--all` 이 돈다): 파일 하나를 스테이지하면 모든 규칙을 통틀어
그 파일을 한 번만 연다. 규칙은 파일 글을 `SourceFile` · `lint_scope.read_text` · `index_cache` 로만 얻는다.
**통과한 것은 찍지 않는다.** 통과는 한 줄, 실패만 상세를 찍는다. 고치는 것은 `py -3 -m Scripts format` 하나다(clang-format — include 순서 포함, vcpkg format-manifest).
"""

from __future__ import annotations

import argparse
import builtins
import io
import json
import subprocess
import time
from pathlib import Path
from typing import Sequence

from common.command import Command, ScriptContext
from common.exit_code import ExitCode
from lint.format_files import run_clang_format_batch, run_vcpkg_manifest_format, select_format_files, select_vcpkg_manifests
from lint.index_cache import CACHE_FOLDER
from lint.lint_rule import LintRule, find_orphan_tables, find_rules, format_rules, inspect_file, inspect_files
from lint.lint_scope import LintScope, clear_read_cache, list_staged_with_unstaged_changes, make_file_scope, make_staged_scope, make_whole_scope
from lint.module_imports import list_imported_module_files

def select_rule_scope(rule: LintRule, scope: LintScope) -> LintScope:
    """
    규칙 자신의 표 · 예외 · 모듈(그 규칙만 쓰는 도우미 포함)이 바뀌었으면 그 규칙만 저장소 전체 — 바뀌지 않은 파일의 판정도 달라진다.

    여러 규칙이 함께 쓰는 도우미(`lint_rule` · `cpp_code` · common …)만 바뀌었으면 전체로 돌리지 않는다 — 도우미 하나에 규칙 수십 개가 저장소 전체를 돌았다.
    그 도우미를 쓰는 규칙의 자체 검사가 보고, 실제 파일에서 판정이 달라졌는지는 묶음 검증 첫 단계 `lint --all` 이 본다.
    """
    return make_whole_scope(rule.root) if not scope.whole_repo and scope.touches(roots=rule.list_own_inputs()) else scope


#: 읽기 한도를 볼 스테이지 파일의 갈래(뿌리, 접미사) — 갈래마다 저장소의 첫 파일 하나를 스테이지된 것처럼 넘긴다.
PROBE_KINDS = (("Source/Engine/", ".cpp"), ("Source/Engine/", ".h"), ("Source/Editor/", ".cpp"), ("Source/Editor/", ".h"),
               ("Source/", "CMakeLists.txt"), ("Content/", ".xml"), ("Content/", ".hlsl"), ("Scripts/", ".py"))


def list_probe_files(project_root: Path) -> list[str]:
    list_repo = make_whole_scope(project_root).list_file
    return [next(path for path in list_repo if path.startswith(root) and path.endswith(suffix)) for root, suffix in PROBE_KINDS]


def list_blob_reads(list_argument: Sequence[str], input_data: bytes | str | None) -> list[tuple[str, str]]:
    """git 호출 하나가 내용을 읽는 블롭의 (판, 경로) — `cat-file --batch`(표준 입력의 `<판>:<경로>` 줄) · `show <판>:<경로>`. 판이 빈 글이면 인덱스(`:경로`)다.
    `--batch-check` 는 내용이 아니라 크기 · 종류만 읽어 세지 않는다."""
    if not list_argument or not Path(list_argument[0]).name.lower().startswith("git"):
        return []
    list_spec: list[str] = []
    if "cat-file" in list_argument and "--batch" in list_argument:
        text = input_data.decode("utf-8", "replace") if isinstance(input_data, bytes) else (input_data or "")
        list_spec = [line.strip() for line in text.splitlines()]
    elif "show" in list_argument:
        list_spec = [argument for argument in list_argument[list_argument.index("show") + 1:] if not argument.startswith("-")]
    return [(spec.partition(":")[0], spec.partition(":")[2]) for spec in list_spec if ":" in spec and spec.partition(":")[2]]


def check_read_bound(project_root: Path) -> list[str]:
    """
    커밋 훅이 저장소 크기에 비례하지 않는다는 것을 형식으로 지킨다(시간을 재지 않는다): 파일 하나를 스테이지한 범위로 **모든 규칙을 차례로** 돌 때
    연 파일이 그 파일 한 번뿐이어야 한다. 사실 캐시(`index_cache`)를 먼저 데우므로 캐시 미스는 0 이다 — 넘으면 어떤 규칙이 스테이지 밖 파일을 읽거나
    (색인 없이 저장소 · 폴더 · 짝을 읽는다) 같은 파일을 따로 읽은 것이다(`lint_scope.read_bytes` 를 거치지 않았다).

    사실 캐시 파일(`Saved/Lint/<저장소>/`)도 센다: 저장소마다 manifest 하나와 조각 하나까지 — 판정에 전체가 필요한 규칙은 요약(manifest)만 읽어야 한다.
    git 으로 읽는 블롭(`cat-file --batch` · `show`, `list_blob_reads`)도 센다 — 인덱스 블롭은 그 파일을 연 것과 같고, 다른 판(HEAD)의 블롭은 스테이지 파일 자신의 옛 판만 된다.
    세지 않는 것: 규칙의 표(`Config/Rules/`, `LintRule.list_tables`), 규칙 코드(Scripts 모듈 — 사실 캐시의 판), 그 밖의 `Saved/`, `.git/`.
    """
    map_rule = find_rules(project_root)
    set_code = {path for rule in map_rule.values() for path in list_imported_module_files(type(rule).__module__)}
    resolved_root = project_root.resolve()
    list_probe = list_probe_files(project_root)
    for rule in map_rule.values():
        rule.check(LintScope(tuple(list_probe), (), False))   # 사실 캐시 데우기
    list_problem: list[str] = []
    original_open = io.open
    original_run = subprocess.run
    for probe in list_probe:
        clear_read_cache()
        list_opened: list[tuple[str, str]] = []
        current = [""]

        def counting_open(file, *args, **kwargs):  # noqa: ANN001, ANN002, ANN003 — io.open 과 같은 인자
            try:
                relative = Path(file).resolve().relative_to(resolved_root).as_posix() if isinstance(file, (str, Path)) else ""
            except ValueError:
                relative = ""
            if relative and relative not in set_code and (relative.startswith(CACHE_FOLDER + "/") or not relative.startswith(("Config/Rules/", "Saved/", ".git/"))):
                list_opened.append((current[0], relative))
            return original_open(file, *args, **kwargs)

        def counting_run(list_argument, *args, **kwargs):  # noqa: ANN001, ANN002, ANN003 — subprocess.run 과 같은 인자
            if isinstance(list_argument, (list, tuple)):
                for revision, relative in list_blob_reads([str(part) for part in list_argument], kwargs.get("input")):
                    if relative not in set_code and not relative.startswith(("Config/Rules/", "Saved/", ".git/")):
                        list_opened.append((current[0], f"{revision}:{relative}" if revision else relative))
            return original_run(list_argument, *args, **kwargs)

        io.open = builtins.open = counting_open   # Path.read_text · read_bytes 는 io.open, 맨 open 은 builtins.open 을 부른다
        subprocess.run = counting_run              # git 은 `run_process` · `run_git_bytes` 둘 다 subprocess.run 을 부른다
        try:
            scope = LintScope((probe,), (), False)
            current[0] = "inspect"
            map_found, _ = inspect_file(project_root, probe, [rule for rule in map_rule.values() if rule.selects(probe)])
            for rule in map_rule.values():
                current[0] = rule.id
                rule.evaluate(scope, {probe: map_found[rule.id]} if rule.id in map_found else {})
        finally:
            io.open = builtins.open = original_open
            subprocess.run = original_run
        map_store_files: dict[tuple[str, str], list[str]] = {}
        for index, (rule_id, relative) in enumerate(list_opened):
            if relative.startswith(CACHE_FOLDER + "/"):
                store, _, file_name = relative[len(CACHE_FOLDER) + 1:].partition("/")
                if file_name.startswith("shard-"):
                    map_store_files.setdefault((rule_id, store), []).append(file_name)
                continue
            if any(relative == table or relative.startswith(table.rstrip("/") + "/") for table in (map_rule[rule_id].list_tables() if rule_id in map_rule else ())):
                continue
            revision, _, history_path = relative.partition(":")
            if history_path:   # 다른 판의 블롭 — 스테이지 파일 자신의 옛 판만 된다(같은 파일의 다른 내용이라 다시 읽기가 아니다)
                if history_path != probe:
                    list_problem.append(f"{rule_id}: 스테이지 '{probe}' 하나에 스테이지 밖 '{history_path}' 의 {revision} 판 블롭을 읽는다 — 여러 파일을 보는 판정은 `index_cache` 색인으로")
                continue
            if relative != probe:
                list_problem.append(f"{rule_id}: 스테이지 '{probe}' 하나에 스테이지 밖 '{relative}' 를 읽는다 — 여러 파일을 보는 판정은 `index_cache` 색인으로")
            elif any(other == relative for _, other in list_opened[:index]):
                list_problem.append(f"{rule_id}: 스테이지 '{probe}' 를 다른 규칙과 따로 다시 읽는다 — `lint_scope.read_text` · `readTexts` 로")
        for (rule_id, store), list_shard in sorted(map_store_files.items()):
            if len(list_shard) > 1:
                list_problem.append(f"{rule_id}: 스테이지 '{probe}' 하나에 사실 캐시 '{store}' 의 조각 {len(list_shard)} 개를 읽는다 — 판정은 요약(`contribute`)이나 "
                                   "함께 묻는 파일을 한 조각에(`shardOf`)")
    return list_problem


def run_self_check(rule: LintRule) -> str | None:
    """규칙의 자체 검사 — 실패면 까닭, 통과면 None."""
    try:
        rule.self_check()
    except Exception as error:   # noqa: BLE001 — 자체 검사의 assert · 오류는 모두 실패다
        return f"{type(error).__name__}: {error}"
    return None


VERIFY_CONFIG = "Config/Dev/Verify.json"   # `_hookBudgetMilliseconds` — 커밋 훅의 규칙 하나 · 도구 정책 훅 호출 하나의 시간 예산


def read_hook_budget_seconds(project_root: Path) -> float:
    """시간 예산(초) — 없으면 멈춘다(폴백 없음). 판정은 바꾸지 않고 경고만 낸다(시간은 기계 · 부하마다 달라 형식으로 지킬 것이 아니다)."""
    value = json.loads((project_root / VERIFY_CONFIG).read_text(encoding="utf-8")).get("_hookBudgetMilliseconds")
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        raise SystemExit(f"[lint] {VERIFY_CONFIG} 의 _hookBudgetMilliseconds 가 수가 아니다")
    return float(value) / 1000.0


def format_rule_times(list_time: Sequence[tuple[float, str]], whole_repo: bool, budget_seconds: float) -> str | None:
    """
    규칙별 시간 줄(ESLint `TIMING=1` 꼴) — `--all` 은 느린 다섯과 합을 늘 찍고(재지 않고 줄일 수 없다), 커밋 훅은 예산을 넘은 규칙만 경고한다(없으면 None).
    시간은 프로세스마다 잰 합이고(풀로 나눈 것은 벽시계보다 크다) 그 규칙의 자체 검사가 든다.
    """
    ordered = sorted(list_time, reverse=True)
    if whole_repo:
        return f"[lint-time] 느린 규칙: {' · '.join(f'{rule_id} {seconds:.2f}s' for seconds, rule_id in ordered[:5])} (합 {sum(seconds for seconds, _ in ordered):.1f}s)"
    list_slow = [(seconds, rule_id) for seconds, rule_id in ordered if seconds > budget_seconds]
    if not list_slow:
        return None
    return f"[lint-time] 경고: 규칙 하나가 {budget_seconds * 1000:.0f} ms 를 넘었다 — {' · '.join(f'{rule_id} {seconds * 1000:.0f} ms' for seconds, rule_id in list_slow)}"


def run_lint(project_root: Path, scope: LintScope) -> int:
    if not scope.list_file and not scope.list_removed:
        return ExitCode.PASSED
    has_errors = False
    map_rule = find_rules(project_root)
    map_scope = {rule: select_rule_scope(rule, scope) for rule in map_rule.values()}
    list_whole = [rule.id for rule, rule_scope in map_scope.items() if rule_scope.whole_repo and not scope.whole_repo]
    map_found, seconds = inspect_files(project_root, map_scope)
    for rule, rule_scope in map_scope.items():
        started = time.perf_counter()
        list_problem = rule.evaluate(rule_scope, map_found[rule.id])
        if scope.whole_repo or scope.touches(roots=rule.list_inputs()):
            failure = run_self_check(rule)
            list_problem = list_problem + ([f"자체 검사 실패 — {failure}"] if failure else [])
        seconds[rule.id] += time.perf_counter() - started
        if list_problem:
            has_errors = True
            where = f" — 예외 · 기준선: {rule.describe_tables()}" if rule.describe_tables() else ""
            print(f"[{rule.id}] FAIL — {rule.blocks} {len(list_problem)}건{where}")
            for line in list_problem:
                print(f"  - {line}")

    # 린트 코드(규칙 · 도우미 · 이 파일) · 린트 표 폴더가 바뀌면 커밋 훅의 모양(스테이지 비례 읽기)과 린트 표마다 읽는 규칙이 하나인지를 다시 본다 — 시간을 재지 않고 연 파일을 센다.
    if scope.whole_repo or scope.touches(roots=("Scripts/lint/", "Config/Rules/Lint/", *sorted({path for rule in map_rule.values() for path in rule.list_inputs() if path.startswith("Scripts/")}))):
        list_shape = check_read_bound(project_root) + find_orphan_tables(project_root)
        if list_shape:
            has_errors = True
            print(f"[lint-shape] FAIL — 커밋 훅의 모양 {len(list_shape)}건")
            for line in list_shape:
                print(f"  - {line}")

    # 포맷 설정(include 순서 포함)이 바뀌면 바뀌지 않은 파일의 판정도 바뀐다 — 전체를 본다.
    format_scope = make_whole_scope(project_root) if ".clang-format" in scope.list_file else scope
    list_format = [project_root / path for path in select_format_files(format_scope)]
    if list_format and run_clang_format_batch(list_format, check_only=True, cwd=project_root) != 0:
        has_errors = True
        print("[format] FAIL — 'py -3 -m Scripts format' 으로 고친 뒤 다시 git add 한다.")
    # vcpkg 실행 파일을 한 번 띄운다 — 매니페스트가 스테이지에 있을 때만(`--all` 은 모두).
    if run_vcpkg_manifest_format([project_root / path for path in select_vcpkg_manifests(scope)], check_only=True) != 0:
        has_errors = True
        print("[format] FAIL — 'py -3 -m Scripts format --files <vcpkg.json>' 으로 고친 뒤 다시 git add 한다.")

    time_line = format_rule_times([(value, rule_id) for rule_id, value in seconds.items()], scope.whole_repo, read_hook_budget_seconds(project_root))
    if time_line:
        print(time_line)
    if has_errors:
        print("\n[lint] FAIL — 고친 뒤 다시 git add 하고 커밋한다.")
        return ExitCode.VIOLATION
    what = "저장소 전체" if scope.whole_repo else f"스테이지 {len(scope.list_file) + len(scope.list_removed)}개"
    whole = f"(표가 바뀌어 전체: {', '.join(list_whole)})" if list_whole else ""
    print(f"[lint] OK — {what}, 규칙 {len(map_rule)}개{whole} · 포맷")
    return ExitCode.PASSED


class LintCommand(Command):
    name = "lint"
    help = "린트 — 인자 없음: 커밋 훅(스테이지된 파일), --all: 저장소 전체, --list: 규칙 목록, <규칙 id>: 규칙 하나"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        mode = parser.add_mutually_exclusive_group()
        mode.add_argument("--all", action="store_true", help="모든 규칙이 저장소 전체를 본다 + 포맷(묶음 검증)")
        mode.add_argument("--list", action="store_true", help="규칙 목록을 찍는다")
        mode.add_argument("--files", nargs="+", metavar="파일", help="규칙 하나가 이 파일만 본다")
        mode.add_argument("--self-check", action="store_true", help="규칙 하나의 자체 검사")
        mode.add_argument("--ratchet", action="store_true", help="규칙 하나의 기준선을 줄어든 만큼 낮춘다(올리지 않는다)")
        mode.add_argument("--accept", nargs="+", metavar="파일", help="규칙 하나의 기준선에서 그 파일들을 지금 수로 바꾼다(올리는 유일한 길 — 통합에서만)")
        parser.add_argument("rule", nargs="?", metavar="규칙", help="규칙 id(--list)")

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        if args.rule is not None:
            return self.run_rule(context, args)
        if args.files or args.self_check or args.ratchet or args.accept:
            self.parser.error("--files · --self-check · --ratchet · --accept 는 규칙 id 와 함께 준다")
        if args.list:
            print(format_rules(context.root))
            return ExitCode.PASSED
        if args.all:
            return run_lint(context.root, make_whole_scope(context.root))
        scope = make_staged_scope(context.root)
        list_partial = list_staged_with_unstaged_changes(context.root, scope)
        if list_partial:
            print(f"[lint] FAIL — 스테이지한 파일에 스테이지 안 된 변경이 더 있다(린트는 작업 폴더 글을 본다): {', '.join(list_partial)}\n"
                  "  남은 변경도 git add 하거나 되돌린 뒤 커밋한다(git stash 는 쓰지 않는다).")
            return ExitCode.VIOLATION
        return run_lint(context.root, scope)

    def run_rule(self, context: ScriptContext, args: argparse.Namespace) -> int:
        rule = find_rules(context.root).get(args.rule)
        if rule is None:
            self.parser.error(f"모르는 규칙 '{args.rule}' — `py -3 -m Scripts lint --list`")
        if args.ratchet or args.accept:
            rule.update_baseline(args.accept)
            return ExitCode.PASSED
        if args.self_check:
            failure = run_self_check(rule)
            list_problem = [f"자체 검사 실패 — {failure}"] if failure else []
        else:
            list_problem = rule.check(make_file_scope(context.root, args.files) if args.files else make_whole_scope(context.root))
        for problem in list_problem:
            print(f"  - {problem}")
        print(f"[{rule.id}] {'FAIL' if list_problem else 'OK'} ({len(list_problem)}건)")
        return ExitCode.VIOLATION if list_problem else ExitCode.PASSED
