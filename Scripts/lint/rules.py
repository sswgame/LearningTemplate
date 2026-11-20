#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/rules.py

커밋 린트 규칙 표 — **린트 규칙은 여기 한 곳에만 있다.** `py -3 -m Scripts lint --list` 가 이 표를 찍는다.

린트는 형식 · 빌드로 막지 못하는 틈에만 둔다(CLAUDE.md 원칙 1). 규칙을 더하기 전에 형식 · API · 빌드 경계(`Config/Rules/*.toml` 의 층 ·
타깃 게이트 `#error` 껍데기)로 막을 수 없는지 먼저 본다 — 표의 `why_not_type` 칸을 한 줄로 채우지 못하면 린트가 아니다.
언리얼 UBT 의 include 규칙 · clang-tidy 의 `.clang-tidy` 처럼 "무엇을 검사하는가" 를 한 목록으로 둔다.

칸:
- `id`         : `py -3 -m Scripts lint <id> [인자…]` — 그 규칙 모듈의 단독 실행(`--all` · `--self-check` 등은 모듈마다).
- `group`      : 갈래(형식 · 경계 · 모양 · 이름 · 셰이더 · 참조 · 래칫).
- `blocks`     : 무엇을 막는가.
- `why_not_type` : 왜 형식 · 빌드로 막지 못하는가.
- `check`      : `모듈:함수`. 모두 `(projectRoot, scope: LintScope)` 를 받아 위반 줄 목록을 돌려준다(`lint_scope.py`). 커밋 훅의 scope 는 스테이지된
                 파일, `--all` 은 저장소 전체다 — 규칙은 저장소를 스스로 걷지 않는다. 여러 파일의 관계를 보는 규칙은 `index_cache` 의 색인을 쓴다.
- `tables`     : 규칙의 린트 표 종류 — `""` 는 사람이 까닭을 적는 표 `Config/Rules/Lint/<id 의 PascalCase>.json`, `Baseline` 은 기계가 다시 쓰는 기준선,
                 `BannedNames` 는 개명 도구가 더하는 이력. 경로는 `rule_tables.get_table_path` 만 짓는다 — 규칙 id 하나에 표 하나라 파일 이름만 보고 규칙을 안다.
- `exceptions` : `tables` 밖의 예외 파일(층 표의 칸 등, 없으면 빈 글). 예외에는 까닭을 함께 적는다.
- `inputs`     : 그 밖에 규칙이 읽는 표(파일 · 폴더). 규칙 모듈 · 그 규칙만 쓰는 도우미 · 예외 파일 · 입력이 스테이지에 있으면 그 규칙은 커밋 훅에서도
                 저장소 전체를 본다(표가 바뀌면 바뀌지 않은 파일의 판정도 바뀐다 — 언리얼 UBT 가 규칙 파일이 바뀌면 다시 짓는 것과 같다). 여러 규칙이
                 함께 쓰는 도우미(LintScope · CppText · common …)가 바뀌면 그것을 쓰는 규칙의 자체 검사만 돈다(`Rule.listOwnInputs` · `list_inputs`).

규칙 데이터는 `Config/Rules/` 한 폴더다 — 빌드가 막는 층 · 타깃 경계 표는 `Config/Rules/Layers/`(CMake · 생성기 · 린트가 함께 읽는다),
린트의 예외 · 기준선 · 낱말 표는 `Config/Rules/Lint/`(형식은 `rule_tables.py`).
"""

from __future__ import annotations

import collections
import functools
from dataclasses import dataclass
from pathlib import Path

from lint.module_imports import get_module_file, list_imported_module_files
from lint.rule_tables import TABLE_FOLDER, get_table_path

LAYER_TABLES = ("Config/Rules/Layers/",)


@dataclass(frozen=True)
class Rule:
    id: str
    group: str
    blocks: str
    why_not_type: str
    check: str
    exceptions: str = ""
    inputs: tuple[str, ...] = ()
    tables: tuple[str, ...] = ()

    def get_module_name(self) -> str:
        return self.check.partition(":")[0]

    def get_table(self, kind: str = "") -> str:
        """이 규칙의 린트 표 경로 — @p kind 는 `tables` 의 한 종류."""
        if kind not in self.tables:
            raise KeyError(f"규칙 '{self.id}' 에 표 종류 '{kind}' 가 없다(rules.py 의 tables)")
        return get_table_path(self.id, kind)

    def describe_tables(self) -> str:
        """`--list` · 실패 줄이 찍는 표 — 린트 표와 그 밖의 예외 파일."""
        return " · ".join([*(get_table_path(self.id, kind) for kind in self.tables), *([self.exceptions] if self.exceptions else [])])

    def list_tables(self) -> list[str]:
        """규칙이 읽는 표 — 린트 표, 예외 파일과 입력 표(파일 · 폴더)."""
        return [*(get_table_path(self.id, kind) for kind in self.tables), *([self.exceptions.split()[0]] if self.exceptions else []), *self.inputs]

    def list_own_inputs(self) -> list[str]:
        """스테이지에 있으면 이 규칙을 저장소 전체로 돌릴 파일 · 폴더 — 규칙 모듈, 이 규칙만 import 하는 도우미, 표."""
        map_user_count = count_module_users()
        own_module = get_module_file(self.get_module_name())
        return [*(path for path in list_imported_module_files(self.get_module_name()) if path == own_module or map_user_count[path] == 1), *self.list_tables()]

    def list_inputs(self) -> list[str]:
        """스테이지에 있으면 이 규칙의 자체 검사를 돌릴 파일 · 폴더 — 규칙 모듈과 그것이 import 하는 모든 모듈(여러 규칙이 함께 쓰는 도우미 포함), 표."""
        return [*list_imported_module_files(self.get_module_name()), *self.list_tables()]


RULES: tuple[Rule, ...] = (
    # --- 경계(빌드 게이트가 닿지 않는 자리) ------------------------------------------------------------------------------------
    Rule("layer-includes", "경계", "아래 층 파일이 위 층 헤더를 include(Core · Engine · GameFramework · Editor 층 표)", "빌드의 #error 껍데기가 막지만 그 부분을 컴파일할 때(Debug 빌드)에야 드러나고, 아무도 include 하지 않는 헤더는 보지 않는다 — 같은 표로 커밋 때 먼저 본다", "lint.layer_includes:check_layer_includes", inputs=LAYER_TABLES),
    Rule("layer-cycles", "경계", "같은 엔진 층의 폴더끼리 서로 include(층 안 순환)", "같은 층은 한 부분이라 #error 껍데기가 없다 — 폴더마다 부분을 나누면 층 번호가 폴더 순서를 흉내 낼 뿐이다", "lint.layer_cycles:check_layer_cycles", exceptions="Config/Rules/Layers/EngineLayers.toml [sameTierCycle]", inputs=LAYER_TABLES),
    Rule("thread-owners", "경계", "표준 · OS 함수로 스레드를 바로 만드는 파일", "엔진 스레드는 열쇠(ThreadCreationKey)가 막지만 표준 · OS 이름은 우리가 정의하지 않아 형식으로 막지 못한다", "lint.thread_owners:check_thread_owners", tables=("",)),
    Rule("cmake-path-case", "경계", "대소문자만 다른 CMake include · add_subdirectory 경로", "CMake 경로는 컴파일러가 보지 않는다(Linux 구성만 선다)", "lint.cmake_path_case:check_cmake_path_case"),
    Rule("folder-layout", "경계", "잡동사니 폴더 · Component 폴더", "파일 자리는 형식이 아니다", "lint.folder_layout:check_folder_layout", tables=("",)),
    Rule("platform-chains", "경계", "빠진 플랫폼을 컴파일 오류로 내지 않는 #if 사슬", "전처리 갈래는 형식이 막지 못한다", "lint.platform_chains:check_platform_chains", tables=("",)),
    # --- 모양(공통 틀이 있는데 손으로 다시 적은 것) ----------------------------------------------------------------------------
    Rule("cpp-shape", "모양", "선언 순서 · 깃발 칸 · 익명 네임스페이스 · 주인 우선 · 기반 하나", "합법 C++ 의 배치 규칙이다", "lint.cpp_shape:check_cpp_shape", tables=("",)),
    Rule("common-shapes", "모양", "공통 기반을 두고 손으로 적은 모양(열거 이름 표 · id 를 버리는 입구 · 손으로 짝지은 writeState · readState( Archive& ) · 프레임 함수 안의 리터럴 hashed_string …)", "칸 이름 · switch 글자 · 부른 함수는 형식이 말하지 못한다", "lint.common_shapes:check_common_shapes", tables=("",)),
    Rule("mutable-statics", "모양", "주인 객체 밖의 가변 정적 저장소(감춘 싱글턴)", "함수 정적 · 이름공간 범위 변수는 합법 C++ 이다", "lint.mutable_statics:check_mutable_statics", tables=("",)),
    # --- 이름 -------------------------------------------------------------------------------------------------------------
    Rule("naming-words", "이름", "표에 없는 낱말(모든 이름 · 파일 · 폴더의 모든 낱말) · 금지 동사 · 금지 명사 · 금지 이름 · 약어 표기(모두 대문자)", "업계 용어인지는 형식이 말하지 못한다", "lint.naming_words:check_naming_words", tables=("", "BannedNames")),
    # --- 셰이더 -----------------------------------------------------------------------------------------------------------
    Rule("shader-kill-order", "셰이더", "픽셀 버리기(clip · discard) 뒤의 미분 읽기", "HLSL 에 미분을 쓰는 함수를 적을 형식이 없다", "lint.shader_kill_order:check_shader_kill_order"),
    Rule("shader-branch-shape", "셰이더", "`if ( a && b ) … else if ( a )` 사슬(NVIDIA GL 오컴파일)", "분기 모양은 형식이 아니다", "lint.shader_branch_shape:check_shader_branch_shape"),
    # --- 참조 · 생성물 ----------------------------------------------------------------------------------------------------
    Rule("feature-refs", "참조", "참조 0 인 컴포넌트 · 감시 값 · 개발 명령, 정의 없는 기대 로그 id", "쓰는 곳 · 내는 곳이 있는지는 형식이 말하지 못한다", "lint.feature_references:check_feature_references", tables=("",)),
    Rule("asset-refs", "참조", "끊어진 에셋 경로 · 옮긴 에셋의 옛 id(지우거나 옮긴 에셋을 가리키던 다른 파일도)", "에셋은 글 경로로 가리킨다 — 있는지는 형식이 말하지 못한다(고치는 것은 `py -3 -m Scripts move-asset`)", "lint.asset_references:check_asset_references", inputs=("Config/Dev/AssetReferenceFields.json", "Config/Dev/AssetReferenceFields.generated.json")),
    Rule("asset-ids", "참조", "에셋 정체성 결함 — 머리 `<?asset guid?>` 없음 · 깨짐, 짝 없는 .meta · .meta 없는 원본, GUID 겹침, HEAD 경로의 GUID 바뀜", "정체성은 파일 글(머리 · 곁 .meta)이라 형식이 말하지 못한다(고치는 것은 `py -3 -m Scripts asset-ids --fix`)", "lint.asset_identity:check_asset_identity", inputs=("Config/Dev/AssetIdentity.json",)),
    # --- 래칫(늘지 않게만, 줄이면 `--accept`) -----------------------------------------------------------------------------
    Rule("magic-literals", "래칫", "일반 코드의 이름 없는 숫자 리터럴이 파일마다 늘어남", "리터럴이 매직인지는 형식이 가르지 못한다", "lint.magic_literals:check_magic_literals", tables=("Baseline",)),
    Rule("result-types", "모양", "std::optional · string& outError 모양(어디서나 0 — 예외는 까닭과 함께 표에)", "언어가 주는 모양이다", "lint.result_types:check_result_types", tables=("",)),
    Rule("header-include-limits", "래칫", "다이어트한 헤더가 pch 밖에서 끌어오는 헤더 수가 상한 표보다 늘어남(줄면 표를 낮춘다)", "포함 사슬이 다시 길어지는 것을 막는 형식이 C++17 에 없다 — 측정 숫자(`header-fanin`)의 문턱이다", "lint.header_include_limits:check_header_include_limits", tables=("",)),
)

RULE_BY_ID: dict[str, Rule] = {rule.id: rule for rule in RULES}


@functools.cache
def count_module_users() -> collections.Counter[str]:
    """모듈 파일마다 그것을 (거쳐서라도) import 하는 규칙 수 — 1 이면 그 규칙의 것, 2 이상이면 함께 쓰는 도우미다."""
    return collections.Counter(path for rule in RULES for path in list_imported_module_files(rule.get_module_name()))


def find_orphan_tables(project_root: Path) -> list[str]:
    """`Config/Rules/Lint/` 의 표마다 그것을 읽는 규칙이 꼭 하나인지 — 주인 없는 표는 아무도 읽지 않는 낡은 데이터이고, 둘이 읽으면 규칙 id 하나에 표 하나가 깨진다."""
    map_owner: dict[str, list[str]] = collections.defaultdict(list)
    for rule in RULES:
        for table in rule.list_tables():
            map_owner[table].append(rule.id)
    list_problem: list[str] = []
    for path in sorted((project_root / TABLE_FOLDER).glob("*.json")):
        relative = path.relative_to(project_root).as_posix()
        owners = map_owner.get(relative, [])
        if len(owners) != 1:
            list_problem.append(f"{relative}: 이 표를 읽는 규칙이 {len(owners)} 개다({', '.join(owners) or '없음'}) — rules.py 의 tables 로 규칙 하나에 붙이거나 지운다")
    list_problem += [f"{table}: 규칙 {', '.join(owners)} 의 표가 없다" for table, owners in sorted(map_owner.items()) if table.startswith(TABLE_FOLDER) and not (project_root / table).is_file()]
    return list_problem


def format_table() -> str:
    """`lint --list` 가 찍는 표 — 갈래 · id · 막는 것 · 예외 파일."""
    list_line: list[str] = []
    group = ""
    for rule in RULES:
        if rule.group != group:
            group = rule.group
            list_line.append(f"\n[{group}]")
        suffix = f"  ({rule.describe_tables()})" if rule.describe_tables() else ""
        list_line.append(f"  {rule.id:<22} {rule.blocks}{suffix}")
        list_line.append(f"  {'':<22} 왜 린트: {rule.why_not_type}")
    return "\n".join(list_line)
