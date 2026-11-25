"""
Scripts/lint/lint_rule.py

린트 규칙의 기반 — 규칙 하나 = `LintRule` 하위 클래스 하나 = 파일 하나(+ 표 하나). 파일을 더하면 규칙이 생기고 지우면 사라진다(clang-tidy 의 check 등록과 같은 자리).

린트는 형식 · 빌드로 막지 못하는 틈에만 둔다(CLAUDE.md 원칙 1). 규칙을 더하기 전에 형식 · API · 빌드 경계(`Config/Rules/Layers/*.toml` 의 층 · 타깃 게이트
`#error` 껍데기)로 막을 수 없는지 먼저 본다 — `why_not_type` 을 한 줄로 채우지 못하면 린트가 아니다.

도는 길은 둘로 나뉜다(맵 · 리듀스):
- `inspect(file)` 은 **파일 하나**만 보고 찾은 것(피클할 수 있는 값)을 돌려준다. 파일마다 한 번 읽고 한 번 지운 글(`SourceFile.code`)을 고른 규칙 모두가 나눠 쓰고,
  저장소 전체면 프로세스 풀이 파일을 나눠 맡는다(`inspect_files`). 예전에는 규칙마다 저장소를 돌아, 글 캐시보다 파일이 많은 `--all` 은 같은 파일을 규칙 수만큼 다시 읽고 지웠다.
- `evaluate(scope, found)` 는 모은 것으로 위반 줄을 낸다 — 예외 표 · 기준선 · 여러 파일의 관계. `inspect` 를 쓰지 않는 규칙(경로만 보는 것, 색인 `index_cache` 로 저장소 전체를
  보는 것)은 `evaluate` 만 쓴다.
"""

from __future__ import annotations

import abc
import collections
import functools
import importlib
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence

from lint.cpp_code import blank_preprocessor, strip_comments_and_strings
from lint.lint_scope import LintScope, read_text
from lint.module_imports import get_module_file, list_imported_module_files
from lint.parallel import flat_map_in_processes
from lint.rule_tables import TABLE_FOLDER, ExceptionTable, get_table_path, read_rule_table

LINT_FOLDER = Path(__file__).resolve().parent
GROUPS = ("경계", "모양", "이름", "셰이더", "참조", "래칫")   # `--list` 와 도는 차례


@dataclass(frozen=True)
class SourceFile:
    """규칙이 보는 파일 하나 — 글과 지운 글은 처음 물을 때 한 번 만든다."""

    path: str   # 저장소 기준 posix 경로
    text: str   # 줄 끝 LF, BOM 없음

    @functools.cached_property
    def code_with_macros(self) -> str:
        """C++ 의 주석 · 문자열 · 문자 상수를 같은 길이의 공백으로 바꾼 글(전처리 줄은 둔다)."""
        return strip_comments_and_strings(self.text)

    @functools.cached_property
    def code(self) -> str:
        """`code_with_macros` 에서 전처리 줄도 지운 글 — 줄 번호 · 자리가 `text` 와 같다."""
        return blank_preprocessor(self.code_with_macros)

    @functools.cached_property
    def lines(self) -> list[str]:
        return self.text.split("\n")


class LintRule(abc.ABC):
    id: str             # `py -3 -m Scripts lint <id>`
    group: str          # `GROUPS` 의 하나
    blocks: str         # 무엇을 막는가
    why_not_type: str   # 왜 형식 · 빌드로 막지 못하는가
    tables: Sequence[str] = ()   # 규칙의 표 종류 — `""` 사람이 까닭을 적는 표(`Config/Rules/Lint/<id 의 PascalCase>.json`), `Baseline` 기계가 다시 쓰는 기준선, `BannedNames` 이름 이력
    exceptions: str = ""         # `tables` 밖의 예외 파일(층 표의 칸 등)
    inputs: Sequence[str] = ()   # 그 밖에 읽는 표(파일 · 폴더) — 바뀌면 바뀌지 않은 파일의 판정도 바뀐다

    def __init__(self, root: Path) -> None:
        self.root = root

    # --- 규칙이 채우는 것 ---------------------------------------------------------------------------------------------------

    def selects(self, path: str) -> bool:
        """이 파일을 `inspect` 할까 — 저장소 기준 posix 경로만 보고 정한다."""
        return False

    def inspect(self, file: SourceFile) -> Any:
        """파일 하나에서 찾은 것(없으면 거짓 값)."""
        raise NotImplementedError(f"{self.id}: selects 가 참이면 inspect 를 채운다")

    @abc.abstractmethod
    def evaluate(self, scope: LintScope, found: Mapping[str, Any]) -> list[str]:
        """위반 줄 — @p found 는 경로 순으로 모은 `inspect` 의 거짓이 아닌 결과다."""

    def self_check(self) -> None:
        """판정 규칙의 자체 검사(assert) — 저장소를 읽지 않는다."""

    def update_baseline(self, list_accept: Sequence[str] | None) -> None:
        """`--ratchet`(None) · `--accept <파일…>` — 기준선 표가 있는 규칙만 채운다."""
        raise SystemExit(f"[lint] {self.id} 는 기준선이 없다")

    # --- 기반이 맡는 것 -----------------------------------------------------------------------------------------------------

    def load_exceptions(self, *sections: str) -> ExceptionTable:
        return ExceptionTable.load(self.root, self.id, sections)

    def read_table(self, kind: str = "") -> dict:
        return read_rule_table(self.root, self.id, kind)

    def get_table(self, kind: str = "") -> str:
        return get_table_path(self.id, kind)

    def check(self, scope: LintScope) -> list[str]:
        """이 규칙 하나만 — 단독 실행의 길."""
        found, _ = inspect_files(self.root, {self: scope})
        return self.evaluate(scope, found[self.id])

    def describe_tables(self) -> str:
        """`--list` · 실패 줄이 찍는 표."""
        return " · ".join([*(self.get_table(kind) for kind in self.tables), *([self.exceptions] if self.exceptions else [])])

    def list_tables(self) -> list[str]:
        """규칙이 읽는 표 — 린트 표, 예외 파일과 입력 표(파일 · 폴더)."""
        return [*(self.get_table(kind) for kind in self.tables), *([self.exceptions.split()[0]] if self.exceptions else []), *self.inputs]

    def list_own_inputs(self) -> list[str]:
        """스테이지에 있으면 이 규칙을 저장소 전체로 돌릴 파일 · 폴더 — 규칙 모듈, 이 규칙만 import 하는 도우미, 표."""
        module = type(self).__module__
        map_user_count = count_module_users()
        return [*(path for path in list_imported_module_files(module) if path == get_module_file(module) or map_user_count[path] == 1), *self.list_tables()]

    def list_inputs(self) -> list[str]:
        """스테이지에 있으면 이 규칙의 자체 검사를 돌릴 파일 · 폴더 — 규칙 모듈과 그것이 import 하는 모든 모듈, 표."""
        return [*list_imported_module_files(type(self).__module__), *self.list_tables()]


@functools.cache
def find_rules(root: Path) -> dict[str, LintRule]:
    """id → 규칙. `Scripts/lint/*.py` 의 `LintRule` 하위 클래스를 모은다(차례는 갈래 · id). id 가 겹치거나 갈래가 표에 없으면 멈춘다."""
    for path in sorted(LINT_FOLDER.glob("*.py")):
        if "(LintRule):" in path.read_text(encoding="utf-8"):
            importlib.import_module(f"lint.{path.stem}")
    list_rule = [rule_class(root) for rule_class in LintRule.__subclasses__() if rule_class.__module__.startswith("lint.")]
    list_bad = [rule.id for rule in list_rule if rule.group not in GROUPS]
    list_bad += [rule_id for rule_id, count in collections.Counter(rule.id for rule in list_rule).items() if count > 1]
    if list_bad:
        sys.exit(f"[lint] 규칙 {', '.join(list_bad)}: 갈래가 {' · '.join(GROUPS)} 밖이거나 id 가 겹친다")
    return {rule.id: rule for rule in sorted(list_rule, key=lambda rule: (GROUPS.index(rule.group), rule.id))}


@functools.cache
def count_module_users() -> collections.Counter[str]:
    """모듈 파일마다 그것을 (거쳐서라도) import 하는 규칙 수 — 1 이면 그 규칙의 것, 2 이상이면 함께 쓰는 도우미다."""
    return collections.Counter(path for rule_class in LintRule.__subclasses__() for path in list_imported_module_files(rule_class.__module__))


def inspect_file(root: Path, path: str, list_rule: Iterable[LintRule]) -> tuple[dict[str, Any], dict[str, float]]:
    """파일 하나를 한 번 읽어 규칙마다 본다 — (규칙 → 찾은 것, 규칙 → 초). 지운 글을 처음 만든 시간은 그것을 처음 쓴 규칙에 든다."""
    text = read_text(root / path, encoding="utf-8-sig")
    if text is None:
        raise FileNotFoundError(f"{path}: 없거나 UTF-8 이 아니다")   # 범위의 파일은 있다 — 건너뛰면 검사 없이 지나간다
    file = SourceFile(path, text)
    map_found: dict[str, Any] = {}
    map_seconds: dict[str, float] = {}
    for rule in list_rule:
        started = time.perf_counter()
        if found := rule.inspect(file):
            map_found[rule.id] = found
        map_seconds[rule.id] = time.perf_counter() - started
    return map_found, map_seconds


def _inspect_job(root: Path, job: tuple[str, tuple[str, ...]]) -> list[tuple[str, dict[str, Any], dict[str, float]]]:
    """풀의 자식이 도는 일 하나(피클하려고 모듈 최상위에 둔다)."""
    path, rule_ids = job
    map_rule = find_rules(root)
    return [(path, *inspect_file(root, path, [map_rule[rule_id] for rule_id in rule_ids]))]


def inspect_files(root: Path, map_scope: Mapping[LintRule, LintScope]) -> tuple[dict[str, dict[str, Any]], collections.Counter[str]]:
    """규칙마다의 범위에서 고른 파일을 파일마다 한 번씩 본다 — (규칙 → 경로 순의 찾은 것, 규칙 → 초의 합)."""
    map_job: dict[str, list[str]] = {}
    for rule, scope in map_scope.items():
        for path in scope.list_file:
            if rule.selects(path):
                map_job.setdefault(path, []).append(rule.id)
    map_found: dict[str, dict[str, Any]] = {rule.id: {} for rule in map_scope}
    seconds: collections.Counter[str] = collections.Counter()
    for path, found, file_seconds in flat_map_in_processes(functools.partial(_inspect_job, root), [(path, tuple(map_job[path])) for path in sorted(map_job)]):
        for rule_id, value in found.items():
            map_found[rule_id][path] = value
        seconds.update(file_seconds)
    return map_found, seconds


def find_orphan_tables(root: Path) -> list[str]:
    """`Config/Rules/Lint/` 의 표마다 그것을 읽는 규칙이 꼭 하나인지 — 주인 없는 표는 아무도 읽지 않는 낡은 데이터이고, 둘이 읽으면 규칙 id 하나에 표 하나가 깨진다."""
    map_owner: dict[str, list[str]] = collections.defaultdict(list)
    for rule in find_rules(root).values():
        for table in rule.list_tables():
            map_owner[table].append(rule.id)
    list_problem: list[str] = []
    for path in sorted((root / TABLE_FOLDER).glob("*.json")):
        relative = path.relative_to(root).as_posix()
        owners = map_owner.get(relative, [])
        if len(owners) != 1:
            list_problem.append(f"{relative}: 이 표를 읽는 규칙이 {len(owners)} 개다({', '.join(owners) or '없음'}) — 규칙의 tables 로 하나에 붙이거나 지운다")
    list_problem += [f"{table}: 규칙 {', '.join(owners)} 의 표가 없다" for table, owners in sorted(map_owner.items()) if table.startswith(TABLE_FOLDER) and not (root / table).is_file()]
    return list_problem


def format_rules(root: Path) -> str:
    """`lint --list` 가 찍는 표 — 갈래 · id · 막는 것 · 예외 파일."""
    list_line: list[str] = []
    group = ""
    for rule in find_rules(root).values():
        if rule.group != group:
            group = rule.group
            list_line.append(f"\n[{group}]")
        suffix = f"  ({rule.describe_tables()})" if rule.describe_tables() else ""
        list_line.append(f"  {rule.id:<22} {rule.blocks}{suffix}")
        list_line.append(f"  {'':<22} 왜 린트: {rule.why_not_type}")
    return "\n".join(list_line)
