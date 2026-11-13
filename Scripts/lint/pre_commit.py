#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/pre_commit.py

린트 입구 하나 — 규칙은 `rules.py` 의 표 한 곳에 있다.

  py -3 -m Scripts lint                 # 커밋 훅: 모든 규칙이 스테이지된 파일만 본다(LintScope) + clang-format
  py -3 -m Scripts lint --all           # 묶음 검증: 모든 규칙이 저장소 전체를 본다 + clang-format(VerifyBundle.sh 첫 단계)
  py -3 -m Scripts lint --list          # 규칙 표(갈래 · 막는 것 · 왜 린트인가 · 예외 파일)
  py -3 -m Scripts lint <id> [인자…]    # 규칙 하나의 단독 실행(`--all` · `--self-check` 등은 그 모듈의 인자)

커밋 훅은 저장소 크기에 비례하지 않는다: 규칙은 스테이지된 파일만 보고, 여러 파일을 보는 규칙은 내용 해시 색인(`FactCache`)을 바뀐 파일만
다시 계산해 쓴다. 규칙의 표 · 예외 · 모듈(그 규칙만 쓰는 도우미 포함)이 스테이지에 있으면 그 규칙만 저장소 전체를 보고(`Rule.listOwnInputs`),
여러 규칙이 함께 쓰는 도우미만 바뀌었으면 그것을 쓰는 규칙의 자체 검사를 돈다(`Rule.listInputs`). `--all` 은 모든 규칙의 자체 검사도 돈다.
이 모양은 시간이 아니라 연 파일 수로 지킨다(`check_read_bound` — 린트 코드가 바뀐 커밋과 `--all` 이 돈다): 파일 하나를 스테이지하면 모든 규칙을 통틀어
그 파일을 한 번만 연다. 규칙은 파일 글을 `lint_scope.read_text` · `read_texts` 나 `FactCache` 로만 얻는다.
**통과한 것은 찍지 않는다.** 통과는 한 줄, 실패만 상세를 찍는다. 고치는 것은 `py -3 -m Scripts format` 하나다(include 순서 + clang-format).
"""

from __future__ import annotations

import builtins
import collections
import contextlib
import dataclasses
import functools
import importlib
import io
import os
import sys
import tempfile
import types
from pathlib import Path
from typing import Callable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.project_constants import CPP_ALL_EXTENSIONS, LINT_TARGET_REL_DIRS, NOT_OUR_DIR_NAMES, SHADER_SOURCE_EXTENSIONS  # noqa: E402
from common.exit_code import ExitCode  # noqa: E402
from common.git import run_git, run_git_in_hook_repository  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from lint.format import run_clang_format_batch  # noqa: E402
from lint.lint_scope import LintScope, make_staged_scope, make_whole_scope, _read_bytes  # noqa: E402
from lint.index_cache import CACHE_FOLDER  # noqa: E402
from lint.module_imports import list_imported_module_files  # noqa: E402
from lint.rules import Rule, format_table, RULE_BY_ID, RULES  # noqa: E402

CPP_SUFFIXES = tuple(sorted(CPP_ALL_EXTENSIONS))
SHADER_SUFFIXES = tuple(sorted(SHADER_SOURCE_EXTENSIONS))


def select_format_files(scope: LintScope) -> list[str]:
    """clang-format 이 볼 파일 — `format --all` 과 같은 대상(C++ 은 `LINT_TARGET_REL_DIRS`, 셰이더는 남의 코드 · 산출물 폴더 밖). 받아 온 남의 헤더는 고치지 않는다."""
    list_cpp = scope.select(LINT_TARGET_REL_DIRS, CPP_SUFFIXES)
    list_shader = [path for path in scope.select(suffixes=SHADER_SUFFIXES) if not any(part in NOT_OUR_DIR_NAMES for part in path.split("/")[:-1])]
    return list_cpp + list_shader


def run_rule(rule: Rule, project_root: Path, scope: LintScope) -> list[str]:
    module_name, _, function_name = rule.check.partition(":")
    return getattr(importlib.import_module(module_name), function_name)(project_root, scope)


def select_rule_scope(rule: Rule, project_root: Path, scope: LintScope) -> LintScope:
    """
    규칙 자신의 표 · 예외 · 모듈(그 규칙만 쓰는 도우미 포함)이 바뀌었으면 그 규칙만 저장소 전체 — 바뀌지 않은 파일의 판정도 달라진다.

    여러 규칙이 함께 쓰는 도우미(LintScope · CppText · FunctionSpans · common …)만 바뀌었으면 전체로 돌리지 않는다 — 도우미 하나에 규칙 수십 개가
    저장소 전체를 돌았다. 그 도우미를 쓰는 규칙의 자체 검사가 보고(`run_self_check`), 실제 파일에서 판정이 달라졌는지는 묶음 검증 첫 단계 `lint --all` 이 본다.
    """
    if scope.whole_repo or not (scope.touches(roots=rule.list_own_inputs()) or rule.id in list_changed_rule_rows(project_root, scope)):
        return scope
    return make_whole_scope(project_root)


RULES_FILE = "Scripts/lint/rules.py"


def list_changed_rule_rows(project_root: Path, scope: LintScope) -> frozenset[str]:
    """표(`rules.py`)가 스테이지에 있으면 줄이 바뀐(새로 생긴) 규칙 id — 그 규칙만 저장소 전체로 돈다(예외 · 입력 표 · 검사 함수를 바꾼 줄)."""
    if RULES_FILE not in scope.list_file:
        return frozenset()
    return find_changed_rule_rows(project_root)


@functools.cache
def find_changed_rule_rows(project_root: Path) -> frozenset[str]:
    result = run_git_in_hook_repository(["show", f"HEAD:{RULES_FILE}"], cwd=project_root)
    if result.return_code != 0:
        return frozenset(rule.id for rule in RULES)   # HEAD 에 표가 없다(첫 커밋 · 새 표) — 모두 새 줄이다
    return diff_rule_rows(result.stdout, RULES)


def diff_rule_rows(old_table_text: str, new_rules: Sequence[Rule]) -> frozenset[str]:
    """옛 표 글(`rules.py` 전체)을 읽어 칸이 다르거나 새로 생긴 규칙 id."""
    name = "lint.RulesAtHead"
    module = types.ModuleType(name)
    sys.modules[name] = module   # dataclass 가 모듈을 sys.modules 에서 찾는다
    module.__file__ = str(Path(__file__).with_name("rules.py"))   # 표가 자기 자리로 다른 파일을 찾는다(HEAD 판도 같은 자리)
    try:
        exec(compile(old_table_text, f"HEAD:{RULES_FILE}", "exec"), module.__dict__)   # noqa: S102 — 이 저장소의 커밋된 표다
    finally:
        del sys.modules[name]
    map_old = {rule.id: dataclasses.astuple(rule) for rule in module.RULES}
    return frozenset(rule.id for rule in new_rules if map_old.get(rule.id) != dataclasses.astuple(rule))


#: 읽기 한도를 볼 스테이지 파일의 갈래(뿌리, 접미사) — 갈래마다 저장소의 첫 파일 하나를 스테이지된 것처럼 넘긴다.
PROBE_KINDS = (("Source/Engine/", ".cpp"), ("Source/Engine/", ".h"), ("Source/Editor/", ".cpp"), ("Source/Editor/", ".h"),
               ("Source/", "CMakeLists.txt"), ("Content/", ".xml"), ("Content/", ".hlsl"), ("Scripts/", ".py"))


def list_probe_files(project_root: Path) -> list[str]:
    list_repo = make_whole_scope(project_root).list_file
    return [next(path for path in list_repo if path.startswith(root) and path.endswith(suffix)) for root, suffix in PROBE_KINDS]


def check_read_bound(project_root: Path) -> list[str]:
    """
    커밋 훅이 저장소 크기에 비례하지 않는다는 것을 형식으로 지킨다(시간을 재지 않는다): 파일 하나를 스테이지한 범위로 **모든 규칙을 차례로** 돌 때
    연 파일이 그 파일 한 번뿐이어야 한다. 사실 캐시(`FactCache`)를 먼저 데우므로 캐시 미스는 0 이다 — 넘으면 어떤 규칙이 스테이지 밖 파일을 읽거나
    (색인 없이 저장소 · 폴더 · 짝을 읽는다) 같은 파일을 따로 읽은 것이다(`lint_scope.read_bytes` 를 거치지 않았다).

    사실 캐시 파일(`Saved/Lint/<저장소>/`)도 센다: 저장소마다 manifest 하나와 조각 하나까지 — 판정에 전체가 필요한 규칙은 요약(manifest)만 읽어야 한다.
    세지 않는 것: 규칙의 표(`Config/Rules/`, `Rule.listTables`), 규칙 코드(Scripts 모듈 — 사실 캐시의 판), 그 밖의 `Saved/`, `.git/`.
    shortcut: `io.open` · `open` 만 센다 — git 으로 읽는 블롭(`git cat-file`)은 세지 않는다, 그런 규칙이 늘면 git 호출도 센다.
    """
    set_code = {path for rule in RULES for path in list_imported_module_files(rule.get_module_name())}
    resolved_root = project_root.resolve()
    list_probe = list_probe_files(project_root)
    for rule in RULES:
        run_rule(rule, project_root, LintScope(tuple(list_probe), (), False))   # 사실 캐시 데우기
    list_problem: list[str] = []
    original_open = io.open
    for probe in list_probe:
        _read_bytes.cache_clear()
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

        io.open = builtins.open = counting_open   # Path.read_text · read_bytes 는 io.open, 맨 open 은 builtins.open 을 부른다
        try:
            for rule in RULES:
                current[0] = rule.id
                run_rule(rule, project_root, LintScope((probe,), (), False))
        finally:
            io.open = builtins.open = original_open
        map_store_files: dict[tuple[str, str], list[str]] = {}
        for index, (rule_id, relative) in enumerate(list_opened):
            rule = RULE_BY_ID[rule_id]
            if relative.startswith(CACHE_FOLDER + "/"):
                store, _, file_name = relative[len(CACHE_FOLDER) + 1:].partition("/")
                if file_name.startswith("shard-"):
                    map_store_files.setdefault((rule_id, store), []).append(file_name)
                continue
            if any(relative == table or relative.startswith(table.rstrip("/") + "/") for table in rule.list_tables()):
                continue
            if relative != probe:
                list_problem.append(f"{rule_id}: 스테이지 '{probe}' 하나에 스테이지 밖 '{relative}' 를 읽는다 — 여러 파일을 보는 판정은 `FactCache` 색인으로")
            elif any(other == relative for _, other in list_opened[:index]):
                list_problem.append(f"{rule_id}: 스테이지 '{probe}' 를 다른 규칙과 따로 다시 읽는다 — `lint_scope.read_text` · `readTexts` 로")
        for (rule_id, store), list_shard in sorted(map_store_files.items()):
            if len(list_shard) > 1:
                list_problem.append(f"{rule_id}: 스테이지 '{probe}' 하나에 사실 캐시 '{store}' 의 조각 {len(list_shard)} 개를 읽는다 — 판정은 요약(`contribute`)이나 "
                                   "함께 묻는 파일을 한 조각에(`shardOf`)")
    return list_problem


def check_merge_scope() -> list[str]:
    """병합 중 범위(`make_staged_scope`) — 임시 저장소에서 양쪽이 고친 파일 · 한쪽만 고친 파일 · 한쪽이 지운 파일 · 새 파일을 섞어 병합하고, 손으로 푼 충돌만 남는지 본다."""
    def git(root: Path, *list_argument: str) -> None:
        result = run_git(["-c", "user.name=lint", "-c", "user.email=lint@localhost", "-c", "core.autocrlf=false", *list_argument], cwd=root)
        if result.return_code != 0 and list_argument[0] != "merge":   # 충돌 난 병합은 0 이 아니다
            raise RuntimeError(f"git {' '.join(list_argument)}: {result.stderr.strip()}")

    # 커밋 훅 안에서는 git 이 GIT_DIR · GIT_INDEX_FILE 을 넘겨 준다 — 그대로 두면 임시 저장소의 git 이 이 저장소를 건드린다(core.bare 를 켰다).
    map_saved = {name: os.environ.pop(name) for name in list(os.environ) if name.startswith("GIT_")}
    try:
        return _check_merge_scope(git)
    finally:
        os.environ.update(map_saved)


def _check_merge_scope(git: Callable[..., None]) -> list[str]:
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        git(root, "init", "-q", "-b", "main")
        for name in ("a", "b", "c"):
            (root / f"{name}.txt").write_text(f"{name}\n", encoding="utf-8")
        git(root, "add", ".")
        git(root, "commit", "-qm", "base")
        git(root, "checkout", "-qb", "side")
        (root / "b.txt").write_text("b side\n", encoding="utf-8")
        (root / "c.txt").write_text("c side\n", encoding="utf-8")
        (root / "n.txt").write_text("n\n", encoding="utf-8")
        git(root, "add", ".")
        git(root, "commit", "-qm", "side")
        git(root, "checkout", "-q", "main")
        (root / "c.txt").write_text("c main\n", encoding="utf-8")
        git(root, "rm", "-q", "a.txt")
        git(root, "commit", "-qam", "main")
        git(root, "merge", "-q", "side")
        (root / "c.txt").write_text("c merged\n", encoding="utf-8")
        git(root, "add", "c.txt")
        scope = make_staged_scope(root)
    return [] if (scope.list_file, scope.list_removed) == (("c.txt",), ()) else [f"병합 범위가 손으로 푼 c.txt 하나가 아니다: {scope.list_file} · 지움 {scope.list_removed}"]


def check_fix_command(project_root: Path) -> list[str]:
    """include-order 위반 안내가 시키는 명령(`include_order.FIX_COMMAND`)을 `Scripts/__main__.py` 의 하위 명령 표로 풀어 그대로 부르고, 파일의 include 가
    실제로 고쳐졌는지 본다 — 안내가 아무것도 하지 않는 명령(진입점 없는 `format.py`)이나 clang-format 만 도는 명령을 가리키던 것을 막는다."""
    import importlib.util
    from lint.include_order import fix_text, FIX_COMMAND
    list_token = FIX_COMMAND.split()
    if list_token[:4] != ["py", "-3", "-m", "Scripts"] or list_token[-1] != "--files":
        return [f"안내 명령 '{FIX_COMMAND}' 이 `py -3 -m Scripts <하위 명령> … --files` 꼴이 아니다"]
    spec = importlib.util.spec_from_file_location("ScriptsEntry", Path(__file__).resolve().parents[1] / "__main__.py")
    entry = sys.modules.setdefault("ScriptsEntry", importlib.util.module_from_spec(spec))   # dataclass 가 모듈을 sys.modules 에서 찾는다
    spec.loader.exec_module(entry)
    command = entry._SUBCOMMAND_BY_NAME.get(list_token[4])
    if command is None:
        return [f"안내 명령의 하위 명령 '{list_token[4]}' 가 Scripts/__main__.py 표에 없다"]
    folder = project_root / "Saved" / "Lint" / "FixCommandProbe"
    folder.mkdir(parents=True, exist_ok=True)
    probe = folder / "Probe.cpp"
    probe.write_text('#include "pch.h"\n#include "Core/Foundation/Types.h"\n#include <vector>\n#include <algorithm>\n\nint x;\n', encoding="utf-8", newline="\n")
    relative = probe.relative_to(project_root).as_posix()
    try:
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            command.run([*list_token[5:], relative])
        fixed = not fix_text(probe.read_text(encoding="utf-8"), relative)[1]
    finally:
        probe.unlink(missing_ok=True)
    return [] if fixed else [f"안내 명령 '{FIX_COMMAND} <파일>' 이 include 순서를 고치지 않는다"]


def check_fact_store() -> list[str]:
    """사실 저장소(`index_cache.open_facts`) — 임시 폴더에서 짓고, 한 파일 고치기 · 지우기 · 더하기 뒤 요약이 처음부터 접은 것과 같은지, 물은 사실이 맞는지,
    조각을 망가뜨리면 다시 지어 같은 답을 내는지 본다."""
    from lint.index_cache import open_facts
    list_problem: list[str] = []
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)

        def run(map_text: dict[str, str], want: list[str]) -> tuple[dict, collections.Counter[str]]:
            facts = open_facts(root, "probe", [], sorted(map_text), lambda _, text: text.split(), map_key={path: f"k{hash(text)}" for path, text in map_text.items()},
                              read_texts=lambda list_path: {path: map_text.get(path) for path in list_path}, want=want, contribute=lambda _, list_word: list_word)
            return facts.map_fact, facts.summary

        def expect(map_text: dict[str, str], want: list[str], what: str) -> None:
            map_fact, summary = run(map_text, want)
            if summary != collections.Counter(word for text in map_text.values() for word in text.split()):
                list_problem.append(f"사실 저장소: {what} 뒤 요약이 처음부터 접은 것과 다르다")
            if map_fact != {path: map_text[path].split() for path in sorted(want) if path in map_text}:
                list_problem.append(f"사실 저장소: {what} 뒤 물은 사실이 다르다")

        map_text = {f"d{index % 7}/f{index}.txt": f"a b w{index % 3}" for index in range(40)}
        expect(map_text, [], "처음 짓기")
        map_text["d1/f1.txt"] = "a c"
        del map_text["d2/f2.txt"]
        map_text["d9/new.txt"] = "z z"
        expect(map_text, ["d1/f1.txt", "d9/new.txt"], "고치기 · 지우기 · 더하기")
        for shard in (root / CACHE_FOLDER / "probe").glob("shard-*.json"):
            shard.write_text("{}", encoding="utf-8")
            break
        expect(map_text, sorted(map_text), "조각을 망가뜨린")
    return list_problem


def check_rule_row_diff() -> list[str]:
    """표 줄 비교(`diff_rule_rows`) — 지금 표 글과 같으면 바뀐 줄이 없고, 한 규칙의 `blocks` 를 바꾼 글이면 그 규칙 하나다."""
    text = (Path(__file__).resolve().parent / "rules.py").read_text(encoding="utf-8")
    target = RULES[1]
    edited = text.replace(f'Rule("{target.id}", "{target.group}", "{target.blocks}"', f'Rule("{target.id}", "{target.group}", "{target.blocks} 바꿈"', 1)
    list_problem = [] if diff_rule_rows(text, RULES) == frozenset() else ["지금 표와 같은 글에서 바뀐 규칙 줄이 나온다"]
    return list_problem + ([] if edited != text and diff_rule_rows(edited, RULES) == {target.id} else [f"'{target.id}' 줄만 바꾼 표에서 그 id 하나가 나오지 않는다"])


def run_self_check(rule: Rule) -> str | None:
    """규칙 모듈의 자체 검사(`run_self_check` 가 있을 때) — 실패면 까닭, 통과면 None. 통과 줄은 찍지 않는다."""
    module = importlib.import_module(rule.get_module_name())
    if not hasattr(module, "run_self_check"):
        return None
    buffer = io.StringIO()
    try:
        with contextlib.redirect_stdout(buffer):
            result = module.run_self_check()
    except (Exception, SystemExit) as error:   # noqa: BLE001 — 자체 검사의 assert · 오류는 모두 실패다
        return f"{type(error).__name__}: {error} {buffer.getvalue().strip()}".strip()
    if result is False or (type(result) is int and result != 0):   # 모듈마다 None · 0 · True 가 통과다
        return buffer.getvalue().strip() or f"돌려준 값 {result}"
    return None


def run_lint(project_root: Path, scope: LintScope) -> int:
    if not scope.list_file and not scope.list_removed:
        return ExitCode.PASSED
    has_errors = False
    list_whole: list[str] = []
    for rule in RULES:
        rule_scope = select_rule_scope(rule, project_root, scope)
        if rule_scope.whole_repo and not scope.whole_repo:
            list_whole.append(rule.id)
        list_problem = run_rule(rule, project_root, rule_scope)
        if scope.whole_repo or scope.touches(roots=rule.list_inputs()):
            failure = run_self_check(rule)
            list_problem = list_problem + ([f"자체 검사 실패 — {failure}"] if failure else [])
        if list_problem:
            has_errors = True
            where = f" — 예외 · 기준선: {rule.exceptions}" if rule.exceptions else ""
            print(f"[{rule.id}] FAIL — {rule.blocks} {len(list_problem)}건{where}")
            for line in list_problem:
                print(f"  - {line}")

    # 린트 코드(규칙 · 도우미 · 이 파일 · 하위 명령 표)가 바뀌면 커밋 훅의 모양(스테이지 비례 읽기 · 병합 범위)과 안내가 시키는 고치는 명령을 다시 본다 — 시간을 재지 않고 연 파일을 센다.
    if scope.whole_repo or scope.touches(roots=("Scripts/lint/", "Scripts/__main__.py", *sorted({path for rule in RULES for path in rule.list_inputs() if path.startswith("Scripts/")}))):
        list_shape = check_read_bound(project_root) + check_merge_scope() + check_fix_command(project_root) + check_rule_row_diff() + check_fact_store()
        if list_shape:
            has_errors = True
            print(f"[lint-shape] FAIL — 커밋 훅 · 고치는 명령의 모양 {len(list_shape)}건")
            for line in list_shape:
                print(f"  - {line}")

    list_format =[project_root / path for path in select_format_files(scope)]
    if list_format and run_clang_format_batch(list_format, check_only=True, cwd=project_root) != 0:
        has_errors = True
        print("[format] FAIL — 'py -3 -m Scripts format' 으로 고친 뒤 다시 git add 한다.")

    if has_errors:
        print("\n[lint] FAIL — 고친 뒤 다시 git add 하고 커밋한다.")
        return ExitCode.VIOLATION
    what = "저장소 전체" if scope.whole_repo else f"스테이지 {len(scope.list_file) + len(scope.list_removed)}개"
    whole = f"(표가 바뀌어 전체: {', '.join(list_whole)})" if list_whole else ""
    print(f"[lint] OK — {what}, 규칙 {len(RULES)}개{whole} · 포맷")
    return ExitCode.PASSED


def main(argv: Sequence[str] | None = None) -> int:
    list_argument = list(sys.argv[1:] if argv is None else argv)
    if not list_argument:
        return run_lint(get_project_root(), make_staged_scope(get_project_root()))
    if list_argument[0] == "--all":
        return run_lint(get_project_root(), make_whole_scope(get_project_root()))
    if list_argument[0] == "--list":
        print(format_table())
        return ExitCode.PASSED
    rule = RULE_BY_ID.get(list_argument[0])
    if rule is None:
        print(f"[lint] 모르는 규칙 '{list_argument[0]}' — `py -3 -m Scripts lint --list`", file=sys.stderr)
        return ExitCode.CHECK_INVALID
    module = importlib.import_module(rule.get_module_name())
    if hasattr(module, "main") is False:
        print(f"[lint] {rule.id} 는 커밋 훅으로만 돈다(단독 실행 없음)", file=sys.stderr)
        return ExitCode.CHECK_INVALID
    return module.main(list_argument[1:])


if __name__ == "__main__":
    sys.exit(main())
