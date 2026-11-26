"""
Scripts/lint/naming_words.py

이름이 업계 용어(언리얼 · 유니티 · Godot · 일반 프로그래밍)를 쓰는지 표(`Config/Rules/Lint/NamingWords.json`)와 대조한다.

왜 린트인가. 이름이 업계 용어인지는 형식이 말할 수 없다. 표 없이 눈에 띈 이름만 고치면 `drain` · `Since` 처럼 같은 일에 새 낱말이
계속 들어온다(2026-11-07). 예전 검사는 함수 첫 단어와 형식 끝 단어만 봐서, 이름 가운데의 금지어(`hasPendingWork` · `XMLFields::readProperties`)와
3인칭 문장형 술어(`shouldDrawBatchInPass`) · 동사 없는 게터(`materialGroupFor`)가 그대로 통과했다(2026-11-12 Source 전체 점검 약 470 건).

규칙:
- verb — 함수 첫 단어가 금지 동사(`_banned`)면 실패한다.
- term: 금지 명사(`_bannedTerms`)는 모든 식별자(함수 · 형식 · 멤버 · 지역)의 모든 단어에서 본다. 표준 합성어(`WorkArea`, Win32 `SPI_GETWORKAREA`)는 `_keepTerms`.
- predicate — `bool` 을 돌려주는 함수의 첫 단어가 3인칭 동사(끝이 s)면 `_predicateWords`(언리얼 `Is · Has · Can · Should · Uses · Needs · Requires · Supports …`)여야 한다.
- lookup: 끝 단어가 `For` · `Of` 인 함수는 조회 동사(`_lookupWords`: get · find · compute …)로 시작해야 한다(`materialGroupFor` 는 "찾거나 만든다" 를 숨겼다).
- kind: 값의 갈래를 고르는 열거형(`enum class`)은 끝이 `Kind` 다. `…Type` 은 C++ · 리플렉션 형식을 뜻한다(NamingCpp). 프로토콜 · 외부 API 의 자기 용어(`HTTP2FrameType`)는 `_keep`.
  낱말 표는 낱말 하나씩만 봐서 열거형 끝말 모양을 막지 못했다 — 2026-11-19 24 개를 전수 개명한 뒤 새로 들어오지 않게 둔다.
- component — `…Component` 를 이어받은 형식(정의의 첫 기반 이름이 `Component` 로 끝난다)은 이름도 `Component` 로 끝나고, 컴포넌트가 아닌 형식은 `Component` 로
  끝나지 않는다(언리얼 `U…Component` · 유니티 `Component` 파생 관례 — docs/agents/NamingCpp.md "컴포넌트 이름"). 둘을 함께 보면 "끝이 Component 인 기반 = 컴포넌트" 가
  귀납으로 성립해 파일 하나만 보고 판정한다. 접미가 빠진 컴포넌트(옛 말 따라가기 AI 조종자)가 있었다(TypeNamingAudit 2026-11-21). `Component` 로 시작하는 이름(`ComponentHandle`)은 해당 없음.
- interface — `REFLECT( Interface )` 형식(상태 없는 리플렉션 인터페이스)은 `I` + 대문자로 시작한다(NamingCpp 인터페이스 규칙). 상태가 붙는 것은 생성 코드의 `static_assert` 가 막고,
  이름은 형식이 말하지 못해 여기서 본다 — 역할을 베이스로 만들어 `I` 없는 계약(옛 체력 원천 베이스)이 생겼던 자리다.
- word — 모든 선언 이름(형식 · 열거자 · 별칭 · 함수 · 멤버 · 상수 · 변수 · 매크로)과 `Source` 의 파일 · 폴더 이름, 스크립트(`Scripts` · `.claude/hooks` ·
  `.claude/skills` · `cmake` 의 함수 · 클래스 · 상수 · CLI 명령 · 규칙 id · CMake 함수 · 옵션 · 파일 · 폴더 이름)의 모든 낱말이 낱말 표
  (`_words` 의 a 업계 · b 게임)에 있어야 한다. 앞 규칙들은 함수 첫 단어 · 형식 끝 단어만 허용 표로 봐서, 멤버 · 파일 이름 · 이름 가운데의 조어
  (`SceneWalkKey` · `aimAtMark` · `parkHistory`)가 들어왔다(2026-11-13). 함수 첫 단어 · 형식 끝 단어 허용 표(`_functionWords` 1100 · `_typeSuffixes` 1240)는
  2026-11-18 지웠다 — 본 낱말을 모두 올린 허용 목록(`two` · `from` · `days`)이 되어 동사 · 접미 규율을 더는 지키지 못했고, 선언의 첫 · 끝 낱말은 이 규칙이 본다. 문맥에 따라 맞기도 틀리기도 한 낱말은 `_scopedWords` 가 쓸 경로를 정한다
  (게임의 walk 는 맞고 엔진의 walk 는 순회다). 바깥 철자(`PFN_` · `VK_` · ImGui 깃발)는 `_externalPatterns`, 받아 온 스킬은 `_externalPaths` 가 뺀다.
  파이썬 이름의 PEP 8 꼴(모듈 · 함수 snake_case, 클래스 PascalCase, 상수 UPPER_CASE — docs/agents/NamingCpp.md)도 여기서 막는다.
  CMake 함수는 `sw_snake_case` 꼴만 받는다(내장 명령 꼴 + 우리 접두 — Qt qt_add_executable · KDE ecm_add_test). CMake 함수 꼴은 cmake 밖
  (Source · Test · ThirdParty 감싸기 · 뿌리 CMakeLists, vcpkg 포트 제외)의 정의도 본다. 전수 집계는 `py -3 -m Scripts naming-census`.
- timing — 배포에 컴파일되는 헤더(`Source/Core` · `Engine` · `GameFramework` 의 `DevTools` · `Editor` · `Tests` · `Dev` 밖)에 `get…(Nanoseconds|Microseconds|Milliseconds)`
  꼴 접근자를 선언하면 실패한다 — 감시 값 재료로 프레임마다 시계를 읽어 칸에 쌓고 공개 접근자로 읽는 모양이 배포에 남았다(2026-11-21 `GameModeComponent::getLastStepMicroseconds`).
  시간은 프로파일러 구간(`SW_PROFILE_SCOPE`)으로 재고 감시 값이 그 구간을 읽는다. 시계 형식 자체 · 계산한 예산처럼 측정이 아닌 이름은 `_timingAccessorKeep` 에 까닭과 함께.
- 이름 하나씩의 예외는 `_keep`, 전수 개명한 옛 이름은 이름 이력 표(`Config/Rules/Lint/NamingWordsBannedNames.json` 의 `_bannedNames`, C++ 는 주석 · 문자열 밖 코드, 스크립트는 선언 · 파일 · CLI 명령 이름을 본다).

verb · term · predicate · lookup · word · kind 의 위반은 모두 실패다. 예전에는 이름 기준선(남은 위반 이름 목록, 줄기만 하는 래칫)을 두었으나 영역별 이전이
0 에 닿아 은퇴했다(2026-11-21) — 이름 하나의 예외는 `_keep` 에 까닭과 함께 둔다.

`py -3 -m Scripts lint naming-words [--self-check]` 로 돌리고, 커밋 훅이 돌린다.
"""

from __future__ import annotations

import functools
import re
from typing import Iterable, Mapping, Sequence

from lint.acronyms import find_bad_words
from lint.cpp_code import strip_comments_and_strings
from lint.lint_rule import LintRule, SourceFile
from lint.lint_scope import LintScope, is_under
from lint.name_collection import (
    CMAKE_FUNCTION_SHAPE, ENUM_CLASS_DECLARATION, EXTENSIONS, PYTHON_SHAPES, collect_declarations, collect_path_names, collect_script_declarations, declares_names,
    is_cmake_path, is_external_name, list_file_names, list_path_names, split_lower_words, split_words,
)
from lint.rule_tables import ExceptionTable, get_table_path

CONFIG_PATH = get_table_path("naming-words")   # 안내 글이 가리키는 낱말 표
NAME_RULES = ("verb", "term", "predicate", "lookup", "word", "kind", "component", "interface", "timing")   # 이름마다 한 줄씩 모으는 판정(같은 이름은 처음 자리 하나만)
KEEP_SECTIONS = ("_keep", "_acronymKeep", "_timingAccessorKeep")   # 이름 하나씩의 예외 — 까닭은 형식이 보고, 낡은 줄은 세지 않는다
Row = tuple[str, str, str]   # 판정(`NAME_RULES` · banned · acronym) · 이름 · 안내
FUNCTION = re.compile(r"\b_?([a-z][a-z0-9]*)[A-Z]\w*\s*\(")
BOOL_FUNCTION = re.compile(r"\bbool\s+(?:\w+::)*(_?[a-z][A-Za-z0-9]*)\s*\(")
IDENTIFIER = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
WORD_RUN = re.compile(r"\w+")   # `\b` 사이의 글자 덩어리 — 옛 이름이 맞을 수 있는 자리는 그 이름의 첫 덩어리와 같은 덩어리에서만 시작한다
#: 기반 목록이 있는 형식 정의(`class X : public Y {`) — 전방 선언(`class X;`)은 `{` 가 없어 빠진다. 기반 목록은 둘째 무리(없으면 None)
TYPE_DEFINITION = re.compile(r"\b(?:class|struct)\s+(?:(?:SW_\w+|alignas\s*\([^)]*\)|\[\[[^\]]*\]\])\s+)*([A-Za-z_]\w*)\s*(?:final\s*)?(?::(?!:)\s*([^{;()]*))?\{")
REFLECT_INTERFACE_TYPE = re.compile(r"\bREFLECT\(\s*Interface\b[^)]*\)\s*(?:class|struct)\s+(?:SW_\w+\s+)*([A-Za-z_]\w*)")
TIMING_ACCESSOR = re.compile(r"(?<![\w.>:])(get[A-Z]\w*(?:Nanoseconds|Microseconds|Milliseconds))\s*\(")   # 선언 자리만 — `x.get…(` · `p->get…(` · `A::get…(` 부르기는 뺀다
SHIPPING_HEADER_ROOTS = ("Source/Core/", "Source/Engine/", "Source/GameFramework/")
SHIPPING_HEADER_SKIP_PARTS = ("/DevTools/", "/Editor/", "/Tests/", "/Dev/")   # 배포에서 빠지는 모듈 · 폴더(`SW_DEV_ONLY_SOURCE_PATTERNS`)
COMPONENT_SUFFIX = "Component"


def find_first_base(bases: str | None) -> str:
    """기반 목록 글(`public sw::Foo<T>, private Bar`)의 첫 기반의 짧은 이름(`Foo`)입니다. 없으면 빈 글입니다."""
    if not bases:
        return ""
    text = bases
    while True:   # 템플릿 인자를 안쪽부터 지운다 — 인자의 쉼표가 기반 구분으로 읽히지 않게
        stripped = re.sub(r"<[^<>]*>", "", text)
        if stripped == text:
            break
        text = stripped
    words = [word for word in re.findall(r"[A-Za-z_]\w*", text.split(",")[0]) if word not in ("public", "protected", "private", "virtual")]
    return words[-1] if words else ""


def find_component_problem(name: str, first_base: str) -> str | None:
    """component 규칙 — 이름과 첫 기반의 `Component` 접미가 어긋나면 그 안내입니다."""
    base_is_component = first_base.endswith(COMPONENT_SUFFIX)
    if base_is_component and not name.endswith(COMPONENT_SUFFIX):
        return f"컴포넌트 '{name}'(기반 {first_base})의 이름이 Component 로 끝나지 않는다 — [수식어][기전]Component 로 바꾼다(docs/agents/NamingCpp.md 컴포넌트 이름)"
    if name.endswith(COMPONENT_SUFFIX) and name != COMPONENT_SUFFIX and not base_is_component:
        return f"'{name}' 는 컴포넌트가 아닌데(첫 기반 {first_base or '없음'}) 이름이 Component 로 끝난다 — 하는 일 이름으로 바꾸거나 Component 를 잇는다"
    return None


def is_third_person_verb(word: str) -> bool:
    """3인칭 단수 동사 꼴(끝이 s)이면 true — s 로 끝나는 명사 · 명령형(`focus` · `axis` · `atlas` · `class`)은 뺀다."""
    return word.endswith("s") and not word.endswith(("ss", "us", "is", "as", "os"))


def collect_words(code: str) -> list[tuple[str, str]]:
    """코드(주석 · 문자열을 뺀 글)의 (함수 앞 단어, 이름) — 선언과 호출 모두."""
    return [(match.group(1), match.group(0).split("(")[0].strip()) for match in FUNCTION.finditer(code)]


def find_term_word(name: str, banned_terms: dict, keep_terms: Sequence[str]) -> str | None:
    """@p name 의 단어 가운데 금지 명사가 있으면 그 단어(표 철자)입니다. 표준 합성어(`_keepTerms`)는 먼저 지운다."""
    stripped = name
    for term in keep_terms:
        stripped = re.sub(re.escape(term), "", stripped, flags=re.I)
    lowered = {term.lower(): term for term in banned_terms}
    for word in split_words(stripped):
        if word.lower() in lowered:
            return lowered[word.lower()]
    return None


def is_shipping_header(path: str) -> bool:
    """배포 구성이 짓는 엔진 층 헤더인가 — timing 판정의 범위."""
    return path.endswith((".h", ".inl")) and path.startswith(SHIPPING_HEADER_ROOTS) and not any(part in path for part in SHIPPING_HEADER_SKIP_PARTS)


class NameTable:
    """낱말 표(`NamingWords.json`)와 이름 이력(`_bannedNames`)을 판정이 묻기 좋게 편 것."""

    def __init__(self, table: Mapping) -> None:
        self.banned_verbs = table["_banned"]
        self.banned_terms = table.get("_bannedTerms", {})
        self.keep_terms = table.get("_keepTerms", [])
        self.predicate_words = set(table.get("_predicateWords", []))
        self.lookup_words = set(table.get("_lookupWords", []))
        self.keep = table["_keep"]
        self.timing_keep = table.get("_timingAccessorKeep", {})
        self.banned_names = table.get("_bannedNames", {})
        self.map_first_run = {name: (match.group(0) if (match := WORD_RUN.match(name)) else None) for name in self.banned_names}
        self.map_term: dict[str, str | None] = {}   # 이름 → 금지 명사 — 같은 식별자가 파일마다 수백 번 나와 `_keepTerms` 지우기를 이름마다 한 번만 한다
        words = table.get("_words", {"a": (), "b": ()})
        self.known = {*words["a"], *words["b"]}
        self.scoped = table.get("_scopedWords", {})
        self.externals = [re.compile(pattern) for pattern in table.get("_externalPatterns", ())]
        self.external_paths = table.get("_externalPaths", ())
        # 파이썬 snake_case 이름도 같은 합성어로 본다(`RequiredNameFields` 는 `required_name_fields` 도 덮는다)
        self.keep_term_patterns = [re.compile(re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_?", re.escape(term)), re.I) for term in self.keep_terms]
        self.acronyms = {item.upper() for item in table.get("_acronyms", ())}
        self.acronym_keep = table.get("_acronymKeep", {})

    def find_banned_names(self, code: str) -> list[str]:
        """
        @p code 의 옛 이름(`\\b(이름|…)\\b` 의 차례대로) — 옛 이름 천여 개를 엮은 정규식을 파일마다 모든 자리에서 돌리지 않고, 글의 덩어리(`WORD_RUN`)에 첫 덩어리가
        있는 이름만 엮어 돈다. 이름이 맞는 자리는 늘 그 이름의 첫 덩어리와 같은 덩어리이므로(`\\b` 로 시작한다) 뺀 이름은 어디서도 맞지 않고, 표 차례를 지켜
        엮어 겹친 이름(`YawUtil` · `YawUtil::fromWorld`)도 전체를 엮은 것과 같은 것을 고른다. 첫 글자가 덩어리 글자가 아닌 이름(첫 덩어리 None)은 늘 엮는다.
        """
        set_run = set(WORD_RUN.findall(code))
        list_candidate = tuple(name for name in self.banned_names if self.map_first_run[name] is None or self.map_first_run[name] in set_run)
        return [match.group(1) for match in compile_banned_names(list_candidate).finditer(code)] if list_candidate else []

    def classify_code(self, path: str, code: str) -> list[Row]:
        """C++ 글 하나(주석 · 문자열을 지운 것)의 위반 — word 밖의 판정."""
        list_row: list[Row] = []

        def record(rule: str, name: str, hint: str) -> None:
            if name not in self.keep:
                list_row.append((rule, name, f"{path}: {hint}"))

        banned_names = self.banned_names
        if banned_names:
            for name in self.find_banned_names(code):
                list_row.append(("banned", name, f"{path}: 옛 이름 '{name}' — {banned_names[name]}"))
        for word, name in collect_words(code):
            if name in self.keep:
                continue
            if word in self.banned_verbs:
                record("verb", name, f"함수 앞 단어 '{word}'({name}) 는 금지 동사다 — {self.banned_verbs[word]}")
            words = split_words(name, function=True)
            if len(words) > 1 and words[-1] in ("For", "Of") and words[0] not in self.lookup_words:
                record("lookup", name, f"'{name}' 는 동사 없는 For · Of 게터다 — get · find · findOrAdd · compute 로 시작한다(_lookupWords)")
        for match in ENUM_CLASS_DECLARATION.finditer(code):
            name = match.group(1)
            if name.endswith("Type"):
                record("kind", name, f"열거형 '{name}' 의 끝이 Type 이다 — 값의 갈래는 …Kind(NamingCpp, 언리얼 E…Kind · 유니티 …Kind). 프로토콜 · 외부 API 의 자기 용어면 _keep 에 이유와 함께")
        for match in TYPE_DEFINITION.finditer(code):
            name = match.group(1)
            problem = find_component_problem(name, find_first_base(match.group(2)))
            if problem is not None:
                record("component", name, problem)
        for match in REFLECT_INTERFACE_TYPE.finditer(code):
            name = match.group(1)
            if re.fullmatch(r"I[A-Z]\w*", name) is None:
                record("interface", name, f"REFLECT( Interface ) 형식 '{name}' 는 I + 대문자로 시작한다 — 상태 없는 계약의 이름 규칙(NamingCpp 인터페이스)")
        if is_shipping_header(path):
            for match in TIMING_ACCESSOR.finditer(code):
                name = match.group(1)
                if name not in self.timing_keep:
                    record("timing", name, f"배포 헤더의 시간 접근자 '{name}' — 감시 값 재료면 프로파일러 구간(SW_PROFILE_SCOPE)으로 재고 감시 값이 구간을 읽는다"
                                                 f"(SceneTickScheduler::kComponentTickScopeName). 측정이 아니면 {CONFIG_PATH} 의 _timingAccessorKeep 에 까닭과 함께")
        for match in BOOL_FUNCTION.finditer(code):
            name = match.group(1)
            words = split_words(name, function=True)
            first = words[0] if words else ""
            if is_third_person_verb(first) and first not in self.predicate_words:
                record("predicate", name, f"bool 함수 '{name}' 가 3인칭 문장형('{first}')이다 — is · has · can · should · uses · needs · requires · supports 로(_predicateWords, 언리얼 Is/Has/Uses/Requires…)")
        if self.banned_terms:
            set_term: set[str] = set()
            for match in IDENTIFIER.finditer(code):
                name = match.group(0)
                if name in self.keep or name in set_term:
                    continue
                if name not in self.map_term:
                    self.map_term[name] = find_term_word(name, self.banned_terms, self.keep_terms)
                term = self.map_term[name]
                if term is not None:
                    set_term.add(name)
                    record("term", name, f"'{name}' 에 금지 명사 '{term}' 가 있다 — {self.banned_terms[term]}")
        return list_row

    def find_word_problem(self, kind: str, name: str, path: str) -> str | None:
        """word 판정 — 이름의 모든 낱말이 낱말 표(`_words` 의 a 업계 · b 게임)에 있는가. 어긋나면 안내다.
        `_scopedWords` 의 낱말은 그 경로 아래에서만 받는다(게임의 walk 는 맞고 엔진의 walk 는 순회다). 표준 합성어(`_keepTerms`) · 바깥 이름(`_externalPatterns`) · `_keep` 은 뺀다."""
        if name in self.keep or is_external_name(name, self.externals) or is_under(path, self.external_paths):
            return None
        if name in self.banned_names and not path.startswith("Source/"):   # C++ 의 옛 이름은 classify_code 가 코드 전체에서 본다
            return f"{path}: 옛 이름 '{name}' — {self.banned_names[name]}"
        if kind == "function" and is_cmake_path(path) and CMAKE_FUNCTION_SHAPE.fullmatch(name) is None:
            return f"{path}: CMake 함수 '{name}' 는 sw_snake_case 여야 한다(docs/agents/NamingCpp.md — 내장 명령 꼴 + 우리 접두, 약어도 소문자)"
        shape = PYTHON_SHAPES.get(kind) if path.endswith(".py") else None
        if shape is not None and shape[0].fullmatch(name) is None:
            return f"{path}: 파이썬 이름 '{name}' 가 PEP 8 꼴이 아니다 — {shape[1]}(docs/agents/NamingCpp.md). 바깥 덮어쓰기(setUp · do_POST)는 _keep"
        stripped = name
        for term in self.keep_term_patterns:
            stripped = term.sub("_", stripped)
        for word in split_lower_words(stripped, kind):
            if word in self.known:
                continue
            if word not in self.scoped:
                return (f"{path}: '{name}' 의 낱말 '{word}' 가 낱말 표에 없다 — 언리얼 · 유니티 · Godot · 일반 용어와 대조한 뒤 {CONFIG_PATH} 의 "
                        f"_words(a 업계 · b 게임)에 올리거나 업계 낱말로 바꾼다")
            if not is_under(path, self.scoped[word]["paths"]):
                return f"{path}: '{name}' 의 낱말 '{word}' 는 {' · '.join(self.scoped[word]['paths'])} 에서만 쓴다 — {self.scoped[word]['hint']}"
        return None

    def classify_words(self, path: str, list_name: Iterable[tuple[str, str]]) -> list[Row]:
        return [("word", name, problem) for kind, name in dict.fromkeys(list_name) if (problem := self.find_word_problem(kind, name, path)) is not None]

    def classify_acronyms(self, path: str, code: str) -> list[Row]:
        """약어를 첫 글자만 대문자로 쓴 이름 — 파일 · 폴더 이름과 C++ 식별자."""
        list_row: list[Row] = []
        for segment in path.rsplit(".", 1)[0].split("/")[1:]:
            name = segment.split(".")[0]
            if name not in self.acronym_keep and (list_bad := find_bad_words(name, self.acronyms)):
                list_row.append(("acronym", name, f"{path}: 경로 낱말 '{name}' 의 약어는 모두 대문자({', '.join(list_bad)}) — NamingCpp Acronyms"))
        for name in sorted(set(IDENTIFIER.findall(code)) - self.acronym_keep.keys()):
            if list_bad := find_bad_words(name, self.acronyms):
                list_row.append(("acronym", name, f"{path}: '{name}' 의 약어는 모두 대문자({', '.join(list_bad)}) — 바깥 이름 · 데이터 철자면 {CONFIG_PATH} 의 _acronymKeep 에 까닭과 함께"))
        return list_row


@functools.lru_cache(maxsize=256)
def compile_banned_names(list_name: tuple[str, ...]) -> re.Pattern:
    return re.compile(r"\b(" + "|".join(map(re.escape, list_name)) + r")\b")


def list_problems(rows: Iterable[Row]) -> list[str]:
    """판정마다 이름 하나에 한 줄(처음 자리) — 줄 머리는 판정 이름(`[verb]`), 옛 이름 · 약어는 머리가 없다."""
    map_hint: dict[tuple[str, str], str] = {}
    for rule, name, hint in rows:
        map_hint.setdefault((rule, name), hint)
    return [f"[{rule}] {hint}" if rule in NAME_RULES else hint for (rule, _), hint in sorted(map_hint.items(), key=lambda item: item[0])]


class NamingWordsRule(LintRule):
    id = "naming-words"
    group = "이름"
    blocks = "표에 없는 낱말(모든 이름 · 파일 · 폴더의 모든 낱말) · 금지 동사 · 금지 명사 · 금지 이름 · 약어 표기(모두 대문자)"
    why_not_type = "업계 용어인지는 형식이 말하지 못한다"
    tables = ("", "BannedNames")

    @functools.cached_property
    def names(self) -> NameTable:
        return NameTable({**self.read_table(), "_bannedNames": self.read_table("BannedNames")["_bannedNames"]})

    def selects(self, path: str) -> bool:
        return declares_names(path)

    def inspect(self, file: SourceFile) -> list[Row]:
        if not file.path.startswith("Source/") or not file.path.endswith(EXTENSIONS):
            return self.names.classify_words(file.path, list_file_names(file.path, file.text))
        code = file.code_with_macros
        return (self.names.classify_code(file.path, code) + self.names.classify_words(file.path, list_file_names(file.path, code))
                + self.names.classify_acronyms(file.path, code))

    def evaluate(self, scope: LintScope, found: Mapping[str, list[Row]]) -> list[str]:
        list_row = [row for rows in found.values() for row in rows]
        for path in scope.list_file:
            list_row += self.names.classify_words(path, list_path_names(path))
        table = ExceptionTable(CONFIG_PATH, self.read_table(), KEEP_SECTIONS)
        return list_problems(list_row) + table.list_problems(scope.whole_repo, stale_sections=())

    def self_check(self) -> None:
        def check_code(code: str, table: Mapping, path: str = "a.h") -> dict[str, set[str]]:
            map_name: dict[str, set[str]] = {rule: set() for rule in (*NAME_RULES, "banned")}
            for rule, name, _ in NameTable(table).classify_code(path, strip_comments_and_strings(code)):
                map_name[rule].add(name)
            return map_name

        def check_words(list_name: Iterable[tuple[str, str, str]], table: Mapping) -> dict[str, str]:
            names = NameTable({"_banned": {}, **table})
            return {name: problem for kind, name, path in list_name if (problem := names.find_word_problem(kind, name, path)) is not None}

        table = {"_banned": {"drain": "flush 또는 poll"},
                 "_bannedTerms": {"Verdict": "Result", "Work": "Job"}, "_keepTerms": ["WorkArea"], "_predicateWords": ["is", "has", "uses", "starts"], "_lookupWords": ["get", "find"],
                 "_keep": {"MailboxServiceResult": "이유"}}
        clean = 'struct FooComponent : Component {}; int getValue( int x ); struct MailboxServiceResult {}; // drainEvents() hasWork\nconst char* s = "drainEvents() Verdict"; bool hasValue(); bool setAxisTags(); bool focusPanel(); bool startsWith(); RECT readWorkArea; int getSlotFor( int x );'
        found = check_code(clean, table)
        assert not any(found.values()), found
        bad = 'void drainEvents( int x ); struct FooVerdict {}; bool hasPendingWork(); bool drawsBatch(); int materialGroupFor( int x ); int _verdictCount;'
        found = check_code(bad, table)
        assert found["verb"] == {"drainEvents"}, found
        assert found["term"] == {"FooVerdict", "hasPendingWork", "_verdictCount"}, found
        assert found["predicate"] == {"drawsBatch"}, found
        assert found["lookup"] == {"materialGroupFor"}, found
        assert check_code("enum class PacketType : uint8 { A }; enum class PacketKind { A }; enum OldType { B }; struct ValueType {};", table)["kind"] == {"PacketType"}   # enum class 만 본다 — 구조체 · 옛 enum 은 다른 규칙 몫
        shape_code = ("class SW_API FooComponent : public SceneComponent {}; struct BarMover final : public sw::Component {}; struct BazComponent : public Widget<Component> {};"
                                "class ComponentHandle {}; class SW_API Component {}; struct QuxComponent; REFLECT( Interface ) struct IGood { }; REFLECT( Interface ) struct Bad { };")
        shape_found = check_code(shape_code, table)
        assert shape_found["component"] == {"BarMover", "BazComponent"}, shape_found   # 양성: 접미 없는 컴포넌트 · 컴포넌트가 아닌 Component 끝말. 음성: FooComponent · ComponentHandle · 기반 Component · 전방 선언
        assert shape_found["interface"] == {"Bad"}, shape_found   # 양성: I 없는 인터페이스. 음성: IGood
        timing_table = dict(table, _timingAccessorKeep={"getElapsedMicroseconds": "시계 형식"})
        timing_code = ("int64 getLastStepMicroseconds() const { return watch.getElapsedMicroseconds(); } int64 getElapsedMicroseconds() const; uint64 Foo::getPeakNanoseconds();")
        assert check_code(timing_code, timing_table, "Source/GameFramework/a.h")["timing"] == {"getLastStepMicroseconds"}   # 양성: 배포 헤더의 선언. 음성: 부르기 · 예외 표 · `A::` 정의
        for path in ("Source/Editor/a.h", "Source/Engine/Tests/a.h", "Source/DevTools/a.h", "Source/Engine/a.cpp"):
            assert check_code(timing_code, timing_table, path)["timing"] == set(), path   # 음성: 배포에 없는 모듈 · 테스트 · .cpp
        assert find_first_base("public sw::Map<Key, Value>, private Other") == "Map" and find_first_base(None) == ""
        name_problems = list_problems(NameTable(table).classify_code("a.h", strip_comments_and_strings(bad)))
        assert len(name_problems) == 6 and "[verb] a.h: " in "".join(name_problems) and any("materialGroupFor" in line for line in name_problems), name_problems
        table_names = dict(table, _bannedNames={"CombatHealth": "HealthStateMachine"})
        assert check_code("// CombatHealth\nconst char* s = \"CombatHealth\";", table_names)["banned"] == set(), "주석 · 문자열의 옛 이름은 본다고 하지 않는다"
        assert check_code("CombatHealth health; int getCombatHealthRatio();", table_names)["banned"] == {"CombatHealth"}, "통째 이름만 막는다"
        overlap = dict(table, _bannedNames={"YawUtil": "a", "YawUtil::fromWorld": "b", "Gone": "c", "new-names": "d"})
        assert NameTable(overlap).find_banned_names("x = YawUtil::fromWorld( v ); new-names;") == ["YawUtil", "new-names"], "표 차례로 엮은 것과 같다 — 앞 이름(YawUtil)이 이기고, 글에 없는 이름은 엮지 않는다"

        code = strip_comments_and_strings("#define SW_SCENE_WATCH 1\nenum class Mode { Idle, Walk };\nusing Alias = int;\nstruct SceneWalkKey {};\n"
                         "void hoverMark( int outbChanged, const Foo& inoutListCrumb ) { float _walkSpeed = 1; const int kMaxCount = 2; }")
        declared = set(collect_declarations(code))
        for item in (("macro", "SW_SCENE_WATCH"), ("enumerator", "Walk"), ("alias", "Alias"), ("type", "SceneWalkKey"), ("function", "hoverMark"),
                     ("variable", "outbChanged"), ("variable", "inoutListCrumb"), ("member", "_walkSpeed"), ("constant", "kMaxCount")):
            assert item in declared, (item, declared)
        assert split_lower_words("outbChanged", "variable") == ["changed"] and split_lower_words("inoutListCrumb", "variable") == ["crumb"]
        assert split_lower_words("D3D12Vector3D", "type") == ["vector"] and split_lower_words("setAxisTags", "function") == ["set", "axis", "tags"]
        assert collect_path_names("Source/Games/Walk/WalkPath.h") == [("folder", "Games"), ("folder", "Walk"), ("file", "WalkPath")]
        table_words = {"_words": {"a": ["scene", "key", "speed", "area", "mark", "hover", "rc", "at"], "b": ["ghost"]}, "_keep": {},
                      "_scopedWords": {"walk": {"paths": ["Source/Games"], "hint": "순회는 iterate"}}, "_externalPatterns": [r"^PFN_"], "_externalPaths": [".claude/skills/ponytail"], "_keepTerms": ["WorkArea"]}
        found = check_words([("type", "SceneWalkKey", "Source/Engine/a.h"), ("member", "_walkSpeed", "Source/Games/b.h"), ("member", "_ghostKey", "Source/a.h"),
                               ("variable", "rcWorkArea", "Source/a.h"), ("alias", "PFN_vkWeirdo", "Source/a.h"), ("function", "aimAtMark", "Source/a.h"),
                               ("folder", "Walk", "Source/Games/Walk/x.h")], table_words)
        assert set(found) == {"SceneWalkKey", "aimAtMark"} and "iterate" in found["SceneWalkKey"] and "'aim'" in found["aimAtMark"], found
        script = "MAX_SCENE = 1\nkSceneKey = 2\nclass SceneKey:\n    def hover_mark(self) -> None: ...\n    def hoverScene(self) -> None: ...\n"
        found = check_words([(kind, name, "Scripts/lint/scene_key.py") for kind, name in collect_script_declarations("Scripts/lint/scene_key.py", script)]
                               + [("file", "SceneKey", "Scripts/lint/SceneKey.py"), ("file", "SceneKey", "cmake/SceneKey.cmake")], table_words | {"_words": {"a": ["max", "scene", "key", "hover", "mark"], "b": []}})
        assert set(found) == {"kSceneKey", "hoverScene", "SceneKey"} and "Scripts/lint/SceneKey.py" in found["SceneKey"], found   # PEP 8 꼴 — CMake 파일 이름은 보지 않는다
        found = check_words([("file", "scene_key", "Scripts/dev/scene_key.py"), ("command", "scene-key", "Scripts/__main__.py")],
                               table_words | {"_words": {"a": ["scene", "key"], "b": []}, "_bannedNames": {"scene_key": "key_scene", "scene-key": "key-scene"}})
        assert set(found) == {"scene_key", "scene-key"} and "key_scene" in found["scene_key"], found   # 스크립트의 옛 이름
        found = check_words([("function", "sw_scene_key", "cmake/a.cmake"), ("function", "sw_sceneKey", "cmake/a.cmake"), ("function", "scene_key", "Source/CMakeLists.txt")],
                               table_words | {"_words": {"a": ["sw", "scene", "key"], "b": []}})
        assert set(found) == {"sw_sceneKey", "scene_key"} and all("sw_snake_case" in hint for hint in found.values()), found   # CMake 함수 꼴
        acronyms = {"ID", "JSON", "UV", "PSO", "UI"}
        assert find_bad_words("AssetId", acronyms) == ["Id"]
        assert find_bad_words("jsonText", acronyms) == []
        assert find_bad_words("JsonRoundTrip", acronyms) == ["Json"]
        assert find_bad_words("countMaterialPSOs", acronyms) == []
        assert find_bad_words("Uint32", acronyms) == []
        assert find_bad_words("Idle", acronyms) == []
        assert find_bad_words("_pUiSystem", acronyms) == ["Ui"]
        assert find_bad_words("gv_editorUiScale", acronyms) == []
        assert find_bad_words("kArrUv2", acronyms) == ["Uv2"]
