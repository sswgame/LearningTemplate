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
                 파일, `--all` 은 저장소 전체다 — 규칙은 저장소를 스스로 걷지 않는다. 여러 파일의 관계를 보는 규칙은 `FactCache` 의 색인을 쓴다.
- `exceptions` : 예외 · 기준선 파일(없으면 빈 글). 예외에는 까닭을 함께 적는다.
- `inputs`     : 그 밖에 규칙이 읽는 표(파일 · 폴더). 규칙 모듈 · 그 규칙만 쓰는 도우미 · 예외 파일 · 입력이 스테이지에 있으면 그 규칙은 커밋 훅에서도
                 저장소 전체를 본다(표가 바뀌면 바뀌지 않은 파일의 판정도 바뀐다 — 언리얼 UBT 가 규칙 파일이 바뀌면 다시 짓는 것과 같다). 여러 규칙이
                 함께 쓰는 도우미(LintScope · CppText · common …)가 바뀌면 그것을 쓰는 규칙의 자체 검사만 돈다(`Rule.listOwnInputs` · `list_inputs`).

규칙 데이터는 `Config/Rules/` 한 폴더다 — 빌드가 막는 층 · 타깃 경계 표는 `Config/Rules/Layers/`(CMake · 생성기 · 린트가 함께 읽는다),
린트의 예외 · 기준선 · 낱말 표는 `Config/Rules/Lint/`.
"""

from __future__ import annotations

import collections
import functools
from dataclasses import dataclass

from lint.module_imports import get_module_file, list_imported_module_files

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

    def get_module_name(self) -> str:
        return self.check.partition(":")[0]

    def list_tables(self) -> list[str]:
        """규칙이 읽는 표 — 예외 파일과 입력 표(파일 · 폴더)."""
        return [*([self.exceptions.split()[0]] if self.exceptions else []), *self.inputs]

    def list_own_inputs(self) -> list[str]:
        """스테이지에 있으면 이 규칙을 저장소 전체로 돌릴 파일 · 폴더 — 규칙 모듈, 이 규칙만 import 하는 도우미, 표."""
        map_user_count = count_module_users()
        own_module = get_module_file(self.get_module_name())
        return [*(path for path in list_imported_module_files(self.get_module_name()) if path == own_module or map_user_count[path] == 1), *self.list_tables()]

    def list_inputs(self) -> list[str]:
        """스테이지에 있으면 이 규칙의 자체 검사를 돌릴 파일 · 폴더 — 규칙 모듈과 그것이 import 하는 모든 모듈(여러 규칙이 함께 쓰는 도우미 포함), 표."""
        return [*list_imported_module_files(self.get_module_name()), *self.list_tables()]


RULES: tuple[Rule, ...] = (
    # --- 형식(고치는 것은 `format` 하나 — include 순서 + clang-format) -------------------------------------------------------------------------
    Rule("include-order", "형식", "include 순서 · 묶음", "include 줄의 순서는 컴파일러가 보지 않는다", "lint.include_order:check_include_order_scope"),
    # --- 경계(빌드 게이트가 닿지 않는 자리) ------------------------------------------------------------------------------------
    Rule("layer-includes", "경계", "아래 층 파일이 위 층 헤더를 include(Core · Engine · GameFramework · Editor 층 표)", "빌드의 #error 껍데기가 막지만 그 부분을 컴파일할 때(Debug 빌드)에야 드러나고, 아무도 include 하지 않는 헤더는 보지 않는다 — 같은 표로 커밋 때 먼저 본다", "lint.layer_includes:check_layer_includes", inputs=LAYER_TABLES),
    Rule("layer-cycles", "경계", "같은 엔진 층의 폴더끼리 서로 include(층 안 순환)", "같은 층은 한 부분이라 #error 껍데기가 없다 — 폴더마다 부분을 나누면 층 번호가 폴더 순서를 흉내 낼 뿐이다", "lint.layer_cycles:check_layer_cycles", exceptions="Config/Rules/Layers/EngineLayers.toml [sameTierCycle]", inputs=LAYER_TABLES),
    Rule("thread-owners", "경계", "표준 · OS 함수로 스레드를 바로 만드는 파일", "엔진 스레드는 열쇠(ThreadCreationKey)가 막지만 표준 · OS 이름은 우리가 정의하지 않아 형식으로 막지 못한다", "lint.thread_owners:check_thread_owners", exceptions="Config/Rules/Lint/ThreadOwners.json"),
    Rule("cmake-path-case", "경계", "대소문자만 다른 CMake include · add_subdirectory 경로", "CMake 경로는 컴파일러가 보지 않는다(Linux 구성만 선다)", "lint.cmake_path_case:check_cmake_path_case"),
    Rule("folder-layout", "경계", "잡동사니 폴더 · Component 폴더", "파일 자리는 형식이 아니다", "lint.folder_layout:check_folder_layout", exceptions="Config/Rules/Lint/FolderLayout.json"),
    Rule("platform-chains", "경계", "빠진 플랫폼을 컴파일 오류로 내지 않는 #if 사슬", "전처리 갈래는 형식이 막지 못한다", "lint.platform_chains:check_platform_chains", exceptions="Config/Rules/Lint/PlatformChains.json"),
    # --- 모양(공통 틀이 있는데 손으로 다시 적은 것) ----------------------------------------------------------------------------
    Rule("cpp-shape", "모양", "선언 순서 · 깃발 칸 · 익명 네임스페이스 · 주인 우선 · 기반 하나", "합법 C++ 의 배치 규칙이다", "lint.cpp_shape:check_cpp_shape", exceptions="Config/Rules/Lint/CppShapeExceptions.json"),
    Rule("state-shape", "모양", "손으로 짝지은 writeState · readState( Archive& )", "스냅샷(ComponentStateStore)은 serializeState 만 받지만, Archive 는 바이트 그릇이라 그것을 받는 멤버 짝을 새로 짓는 것은 형식으로 막지 못한다", "lint.state_shape:check_state_shape"),
    Rule("byte-read", "모양", "파일에서 읽은 개수를 믿고 할당 · LittleEndianUtil 직접 읽기", "vector::resize 는 어떤 수든 받는다", "lint.byte_read_shape:check_byte_read_shape", exceptions="Config/Rules/Lint/ByteReadRawFiles.json"),
    Rule("common-shapes", "모양", "공통 기반을 두고 손으로 적은 모양(열거 이름 표 · 다시 읽기 대조 · id 를 버리는 입구 …)", "칸 이름 · switch 글자 · 부른 함수는 형식이 말하지 못한다", "lint.common_shapes:check_common_shapes", exceptions="Config/Rules/Lint/CommonShapeExceptions.json"),
    Rule("mutable-statics", "모양", "주인 객체 밖의 가변 정적 저장소(감춘 싱글턴)", "함수 정적 · 이름공간 범위 변수는 합법 C++ 이다", "lint.mutable_statics:check_mutable_statics", exceptions="Config/Rules/Lint/MutableStaticExceptions.json"),
    Rule("frame-intern", "모양", "틱 · 갱신 · 칠하기 함수 안의 리터럴 hashed_string", "프레임 밖에서는 정상 코드다", "lint.frame_intern:check_frame_intern", exceptions="Config/Rules/Lint/FrameInternExceptions.json"),
    Rule("disk-path-case", "모양", "경로 포함 · 같음 판정(isPathAtOrUnderRoot · startsWithPathComponent …)에 키로 꺼낸 접은 글(normalizePath(…).str() 을 담은 지역 변수) · toLower 의 글을 넘김", "normalizePath 의 결과(PathKey)는 형식이 막지만, 키 · id 로 꺼낸 str() 과 범용 toLower 는 string 이라 출처를 잃는다", "lint.disk_path_case:check_disk_path_case", exceptions="Config/Rules/Lint/DiskPathCase.json"),
    Rule("optional-minus-one", "모양", "optional · expected 자리(반환 · 대입 · 초기값 · 기본 인자 · 인자)에 리터럴 -1 을 값으로", "표지 상수는 형식(InvalidIndex)이 막지만 리터럴 -1 은 정수라 optional<int32> 의 값으로 합법이다", "lint.optional_minus_one:check_optional_minus_one"),
    Rule("required-name-fields", "모양", "이름 · 경로 PROPERTY 칸(뼈 · 소켓 · 액션 · 태그 …)의 C++ 문자열 기본값 — 데이터가 안 적으면 맞지 않는 이름이 조용히 쓰인다(Required 또는 예외)", "리터럴 초기값은 합법 C++ 이고, 그 값이 형식뿐인 빈 값인지 데이터가 덮을 기본값인지 형식이 말하지 못한다(이름 전용 형식은 생성기 설계 W-R3 와 겹친다)", "lint.required_name_fields:check_required_name_fields", exceptions="Config/Rules/Lint/RequiredNameFieldExceptions.json"),
    # --- 이름 -------------------------------------------------------------------------------------------------------------
    Rule("naming-words", "이름", "표에 없는 함수 앞 단어 · 형식 끝 단어 · 낱말(모든 이름 · 파일 · 폴더의 모든 낱말) · 금지 이름", "업계 용어인지는 형식이 말하지 못한다", "lint.naming_words:check_naming_words", exceptions="Config/Rules/Lint/NamingWords.json", inputs=("Config/Rules/Lint/NamingBaseline.json",)),
    Rule("acronyms", "이름", "식별자 · 파일 · 폴더의 약어 표기(모두 대문자)", "이름은 형식이 막지 못한다", "lint.acronyms:check_acronyms", exceptions="Config/Rules/Lint/Acronyms.json"),
    # --- 셰이더 -----------------------------------------------------------------------------------------------------------
    Rule("shader-kill-order", "셰이더", "픽셀 버리기(clip · discard) 뒤의 미분 읽기", "HLSL 에 미분을 쓰는 함수를 적을 형식이 없다", "lint.shader_kill_order:check_shader_kill_order"),
    Rule("shader-branch-shape", "셰이더", "`if ( a && b ) … else if ( a )` 사슬(NVIDIA GL 오컴파일)", "분기 모양은 형식이 아니다", "lint.shader_branch_shape:check_shader_branch_shape"),
    # --- 참조 · 생성물 ----------------------------------------------------------------------------------------------------
    Rule("feature-refs", "참조", "참조 0 인 컴포넌트 · 감시 값 · 개발 명령", "쓰는 곳이 있는지는 형식이 말하지 못한다", "lint.feature_references:check_feature_references", exceptions="Config/Rules/Lint/FeatureReferenceExceptions.json"),
    Rule("client-only-types", "참조", "클라이언트 전용 형식 이름 표(Config/Engine/ClientOnlyTypes.generated.json)가 소스와 어긋남", "서버가 런타임에 읽는 생성 표다 — 고치는 것은 `py -3 -m Scripts client-only-types`", "generate.client_only_types:check_client_only_types", inputs=("Config/Engine/ClientOnlyTypes.generated.json", "cmake/Engine/BuildLayout.cmake")),
    Rule("asset-refs", "참조", "끊어진 에셋 경로 · 옮긴 에셋의 옛 id · 인덱스의 .meta(지우거나 옮긴 에셋을 가리키던 다른 파일도)", "에셋은 글 경로로 가리킨다 — 있는지는 형식이 말하지 못한다(고치는 것은 `py -3 -m Scripts move-asset`)", "lint.asset_references:check_asset_references", inputs=("Config/Dev/AssetReferenceFields.json", "Config/Dev/AssetReferenceFields.generated.json")),
    # --- 래칫(늘지 않게만, 줄이면 `--accept`) -----------------------------------------------------------------------------
    Rule("magic-literals", "래칫", "일반 코드의 이름 없는 숫자 리터럴이 파일마다 늘어남", "리터럴이 매직인지는 형식이 가르지 못한다", "lint.magic_literals:check_magic_literals", exceptions="Config/Rules/Lint/MagicLiteralBaseline.json"),
    Rule("result-types", "래칫", "std::optional · string& outError 모양(Core 0, 그 밖은 늘지 않게)", "언어가 주는 모양이다", "lint.result_types:check_result_types", exceptions="Config/Rules/Lint/ResultTypeBaseline.json"),
)

RULE_BY_ID: dict[str, Rule] = {rule.id: rule for rule in RULES}


@functools.cache
def count_module_users() -> collections.Counter[str]:
    """모듈 파일마다 그것을 (거쳐서라도) import 하는 규칙 수 — 1 이면 그 규칙의 것, 2 이상이면 함께 쓰는 도우미다."""
    return collections.Counter(path for rule in RULES for path in list_imported_module_files(rule.get_module_name()))


def format_table() -> str:
    """`lint --list` 가 찍는 표 — 갈래 · id · 막는 것 · 예외 파일."""
    list_line: list[str] = []
    group = ""
    for rule in RULES:
        if rule.group != group:
            group = rule.group
            list_line.append(f"\n[{group}]")
        suffix = f"  ({rule.exceptions})" if rule.exceptions else ""
        list_line.append(f"  {rule.id:<22} {rule.blocks}{suffix}")
        list_line.append(f"  {'':<22} 왜 린트: {rule.why_not_type}")
    return "\n".join(list_line)
