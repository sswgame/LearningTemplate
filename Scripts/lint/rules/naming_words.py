"""
Scripts/lint/rules/naming_words.py

이름이 업계 용어(언리얼 · 유니티 · Godot · 일반 프로그래밍)를 쓰는지 표(`Config/Rules/Lint/NamingWords.json`)와 대조한다.

왜 린트인가. 이름이 업계 용어인지는 형식이 말할 수 없다. 표 없이 눈에 띈 이름만 고치면 `drain` · `Since` 처럼 같은 일에 새 낱말이
계속 들어온다(2026-11-07). 예전 검사는 함수 첫 단어와 형식 끝 단어만 봐서, 이름 가운데의 금지어(`hasPendingWork` · `XMLFields::readProperties`)와
3인칭 문장형 술어(`shouldDrawBatchInPass`) · 동사 없는 게터(`materialGroupFor`)가 그대로 통과했다(2026-11-12 Source 전체 점검 약 470 건).

규칙:
- verb — 함수 첫 단어가 금지 동사(`_banned`)면 실패한다. 파이썬 · 셸 · CMake 함수도 본다(CMake 는 `sw_` 다음 낱말, `_cmakeVerbs` 의 동사는 cmake-verb 가 본다).
- term: 금지 명사(`_bannedTerms`)는 모든 식별자(함수 · 형식 · 멤버 · 지역)의 모든 단어에서 본다. 표준 합성어(`WorkArea`, Win32 `SPI_GETWORKAREA`)는 `_keepTerms`.
  스크립트 선언(파이썬 · 셸 · CMake)과 파일 · 폴더 이름도 본다 — C++ 만 보던 사이 `fingerprint` · `tier` 가 스크립트 · CMake 에 남았다(2026-11-27 N11).
- phrase — 여러 낱말 금지(`_bannedPhrases`, `LiveReload` · `FixedLayout`)는 낱말이 이어 나오는 이름을 term 과 같은 자리에서 막는다. 낱말 하나로는 `live` · `fixed` 가 옳은 뜻도 있다.
- predicate — `bool` 을 돌려주는 함수의 첫 단어가 3인칭 동사(끝이 s)면 `_predicateWords`(언리얼 `Is · Has · Can · Should · Uses · Needs · Requires · Supports …`)여야 한다.
- lookup: 끝 단어가 `For` · `Of` 인 함수는 조회 동사(`_lookupWords`: get · find · compute …)로 시작해야 한다(`materialGroupFor` 는 "찾거나 만든다" 를 숨겼다).
- kind: 값의 갈래를 고르는 열거형(`enum class`)은 끝이 `Kind` 다. `…Type` 은 C++ · 리플렉션 형식을 뜻한다(NamingCpp). 프로토콜 · 외부 API 의 자기 용어(`HTTP2FrameType`)는 `_keep`.
  낱말 표는 낱말 하나씩만 봐서 열거형 끝말 모양을 막지 못했다 — 2026-11-19 24 개를 전수 개명한 뒤 새로 들어오지 않게 둔다.
- component — `…Component` 를 이어받은 형식(정의의 첫 기반 이름이 `Component` 로 끝난다)은 이름도 `Component` 로 끝나고, 컴포넌트가 아닌 형식은 `Component` 로
  끝나지 않는다(언리얼 `U…Component` · 유니티 `Component` 파생 관례 — docs/agents/NamingCpp.md "컴포넌트 이름"). 둘을 함께 보면 "끝이 Component 인 기반 = 컴포넌트" 가
  귀납으로 성립해 파일 하나만 보고 판정한다. 접미가 빠진 컴포넌트(옛 말 따라가기 AI 조종자)가 있었다(TypeNamingAudit 2026-11-21). `Component` 로 시작하는 이름(`ComponentHandle`)은 해당 없음.
- suffix — 형식 끝말의 모양(`_suffixRules`): 기반 끝말이 이름을 정하거나(`baseNeedsName` — Subsystem), 이름 끝말이 기반을 정하거나(`nameNeedsBase` — System),
  멤버 짝이 있어야 하거나(`members` — Codec 은 write/read · encode/decode), 템플릿 · 코드 상수 표에 쓰지 않거나(`notTemplate` · `notConstantTable` — Def 는 Desc),
  같은 헤더의 주인 형식 옆에 두지 않는다(`notBesideOwner` — `TUtil` 은 `T` 의 멤버). 낱말 표는 끝말이 무엇과 짝인지 보지 못한다.
- interface — `REFLECT( Interface )` 형식(상태 없는 리플렉션 인터페이스)은 `I` + 대문자로 시작한다(NamingCpp 인터페이스 규칙). 상태가 붙는 것은 생성 코드의 `static_assert` 가 막고,
  이름은 형식이 말하지 못해 여기서 본다 — 역할을 베이스로 만들어 `I` 없는 계약(옛 체력 원천 베이스)이 생겼던 자리다.
- word — 모든 선언 이름(형식 · 열거자 · 별칭 · 함수 · 멤버 · 상수 · 변수 · 매크로)과 `Source` 의 파일 · 폴더 이름, 스크립트(`Scripts` · `.claude/hooks` ·
  `.claude/skills` · `cmake` 의 함수 · 클래스 · 상수 · CLI 명령 · 규칙 id · CMake 함수 · 옵션 · 파일 · 폴더 이름)의 모든 낱말이 낱말 표
  (`_words` 의 a 업계 · b 게임)에 있어야 한다. 앞 규칙들은 함수 첫 단어 · 형식 끝 단어만 허용 표로 봐서, 멤버 · 파일 이름 · 이름 가운데의 조어
  (`SceneWalkKey` · `aimAtMark` · `parkHistory`)가 들어왔다(2026-11-13). 함수 첫 단어 · 형식 끝 단어 허용 표(`_functionWords` 1100 · `_typeSuffixes` 1240)는
  2026-11-18 지웠다 — 본 낱말을 모두 올린 허용 목록(`two` · `from` · `days`)이 되어 동사 · 접미 규율을 더는 지키지 못했고, 선언의 첫 · 끝 낱말은 이 규칙이 본다. 문맥에 따라 맞기도 틀리기도 한 낱말은 `_scopedWords` 가 쓸 경로(`paths`)나
  쓰지 않을 경로(`exceptPaths`, 그 안에서도 받는 합성어 `keepCompounds`)를 정한다(게임의 walk 는 맞고 엔진의 walk 는 순회다). 바깥 철자(`PFN_` · `VK_` · ImGui 깃발)는 `_externalPatterns`, 받아 온 스킬은 `_externalPaths` 가 뺀다.
  파이썬 이름의 PEP 8 꼴(모듈 · 함수 snake_case, 클래스 PascalCase, 상수 UPPER_CASE — docs/agents/NamingCpp.md)도 여기서 막는다.
  CMake 함수는 `sw_snake_case` 꼴만 받는다(내장 명령 꼴 + 우리 접두 — Qt qt_add_executable · KDE ecm_add_test). CMake 함수 꼴은 cmake 밖
  (Source · IntegrationTests · ThirdParty 감싸기 · 뿌리 CMakeLists, vcpkg 포트 제외)의 정의도 본다. 전수 집계는 `py -3 -m Scripts naming-census`.
- exception — 예외를 잇는 파이썬 클래스(기반이 `…Error` · `…Exception`)는 이름이 `Error` 로 끝난다(PEP 8). 멈춤 신호(`RunStop`)는 `_keep`.
- cmake-verb — CMake 함수 `sw_<동사>_…` 의 본문 모양(`_cmakeVerbs`): `require` 는 FATAL_ERROR 로 멈추고, `check` 는 PARENT_SCOPE 로 결과를 돌려주고 멈추지 않는다
  (find_package(REQUIRED) · CheckXXX). 이름이 하는 일을 말해야 부르는 쪽이 결과를 받을지 정한다.
- timing — 배포에 컴파일되는 헤더(`Source/Core` · `Engine` · `GameFramework` 의 `DevTools` · `Editor` · `Tests` · `Dev` 밖)에 `get…(Nanoseconds|Microseconds|Milliseconds)`
  꼴 접근자를 선언하면 실패한다 — 감시 값 재료로 프레임마다 시계를 읽어 칸에 쌓고 공개 접근자로 읽는 모양이 배포에 남았다(2026-11-21 `GameModeComponent::getLastStepMicroseconds`).
  시간은 프로파일러 구간(`SW_PROFILE_SCOPE`)으로 재고 감시 값이 그 구간을 읽는다. 시계 형식 자체 · 계산한 예산처럼 측정이 아닌 이름은 `_timingAccessorKeep` 에 까닭과 함께.
- 이름 하나씩의 예외는 `_keep`, 전수 개명한 옛 이름은 이름 이력 표(`Config/Rules/Lint/NamingWordsBannedNames.json` 의 `_bannedNames`, C++ 는 주석 · 문자열 밖 코드, 스크립트는 선언 · 파일 · CLI 명령 이름을 본다).
- 개념표(`_concepts` — 개념 → 고른 낱말 · 동의어)는 판정을 따로 두지 않는다. 동의어마다 막는 줄(`_banned` · `_bannedTerms` · `_bannedPhrases` · `_scopedWords`)이 있어야 하고,
  `naming-census` 가 개념마다 동의어 수를 낸다.

기준선(`Config/Rules/Lint/NamingWordsBaseline.json`): 파일 → 판정 종류(`term:Tier` · `word:spec` 처럼 판정과 막은 낱말) → 이름 수. 판정을 넓히거나 낱말을 막을 때 지금 이름은
기준선에 두고 새 이름만 멈춘다(magic-literals 와 같은 래칫). 기준선에 없는 파일 · 종류는 0 이라, 이미 0 인 판정은 예전처럼 모두 실패다. 줄면 `--ratchet`, 올리는 것은 통합의 `--accept` 뿐이다.

`py -3 -m Scripts lint naming-words [--self-check]` 로 돌리고, 커밋 훅이 돌린다.
"""

from __future__ import annotations

import collections
import functools
import re
from typing import Iterable, Mapping, NamedTuple, Sequence

from lint.acronyms import find_bad_words
from lint.cpp_code import match_brace, strip_comments_and_strings
from lint.lint_rule import LintRule, SourceFile, inspect_files
from lint.lint_scope import LintScope, is_under, make_file_scope, make_whole_scope, read_staged_renames
from lint.name_collection import (
    CMAKE_FUNCTION_SHAPE, ENUM_CLASS_DECLARATION, EXTENSIONS, PYTHON_SHAPES, collect_declarations, collect_path_names, collect_script_declarations, declares_names,
    is_cmake_path, is_external_name, list_file_names, list_path_names, split_lower_words, split_words,
)
from lint.rule_tables import Baseline, ExceptionTable, get_table_path

CONFIG_PATH = get_table_path("naming-words")   # 안내 글이 가리키는 낱말 표
NAME_RULES = ("verb", "term", "phrase", "predicate", "lookup", "word", "kind", "component", "suffix", "interface", "exception", "cmake-verb", "timing")   # 이름마다 한 줄씩 모으는 판정(같은 이름은 처음 자리 하나만)
KEEP_SECTIONS = ("_keep", "_acronymKeep", "_timingAccessorKeep")   # 이름 하나씩의 예외 — 까닭은 형식이 보고, 낡은 줄은 세지 않는다
RATCHET_COMMAND = "py -3 -m Scripts lint naming-words --ratchet"
BASELINE_COMMENT = ("naming-words 의 기준선 — 파일 → 판정 종류(판정:막은 낱말) → 이름 수. 판정을 넓히거나 낱말을 막을 때 지금 이름만 둔다. "
                    "줄면 `py -3 -m Scripts lint naming-words --ratchet`, 올리는 것은 통합의 `--accept <파일 · 폴더>` 뿐이다(손으로 고치지 않는다).")
FUNCTION = re.compile(r"\b_?([a-z][a-z0-9]*)[A-Z]\w*\s*\(")
BOOL_FUNCTION = re.compile(r"\bbool\s+(?:\w+::)*(_?[a-z][A-Za-z0-9]*)\s*\(")
IDENTIFIER = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
WORD_RUN = re.compile(r"\w+")   # `\b` 사이의 글자 덩어리 — 옛 이름이 맞을 수 있는 자리는 그 이름의 첫 덩어리와 같은 덩어리에서만 시작한다
#: 기반 목록이 있는 형식 정의(`class X : public Y {`) — 전방 선언(`class X;`)은 `{` 가 없어 빠진다. 기반 목록은 둘째 무리(없으면 None)
TYPE_DEFINITION = re.compile(r"\b(?:class|struct)\s+(?:(?:SW_\w+|alignas\s*\([^)]*\)|\[\[[^\]]*\]\])\s+)*([A-Za-z_]\w*)\s*(?:final\s*)?(?::(?!:)\s*([^{;()]*))?\{")
TEMPLATE_TYPE = re.compile(r"\btemplate\s*<[^;{]*?>\s*(?:class|struct)\s+(?:SW_\w+\s+)*([A-Za-z_]\w*)\s*(?:final\s*)?[:{]")
CONSTANT_TABLE = re.compile(r"\bconstexpr\s+(?:const\s+)?(?:\w+::)*([A-Za-z_]\w*)\s+[A-Za-z_]\w*\s*\[")   # 코드 상수 표 `constexpr XDef kTable[] =`
OWNER_DECLARATION = re.compile(r"\bnamespace\s+([A-Za-z_]\w*)\s*\{")
REFLECT_INTERFACE_TYPE = re.compile(r"\bREFLECT\(\s*Interface\b[^)]*\)\s*(?:class|struct)\s+(?:SW_\w+\s+)*([A-Za-z_]\w*)")
TIMING_ACCESSOR = re.compile(r"(?<![\w.>:])(get[A-Z]\w*(?:Nanoseconds|Microseconds|Milliseconds))\s*\(")   # 선언 자리만 — `x.get…(` · `p->get…(` · `A::get…(` 부르기는 뺀다
PYTHON_CLASS_HEAD = re.compile(r"^[ \t]*class[ \t]+([A-Za-z_]\w*)[ \t]*\(([^)]*)\)", re.M)
CMAKE_FUNCTION_BODY = re.compile(r"^[ \t]*function\s*\(\s*(sw_[a-z0-9_]+)(.*?)^[ \t]*endfunction\s*\(", re.M | re.S | re.I)
SHIPPING_HEADER_ROOTS = ("Source/Core/", "Source/Engine/", "Source/GameFramework/")
SHIPPING_HEADER_SKIP_PARTS = ("/DevTools/", "/Editor/", "/Tests/", "/Dev/")   # 배포에서 빠지는 모듈 · 폴더(`SW_DEV_ONLY_SOURCE_PATTERNS`)
COMPONENT_SUFFIX = "Component"


class Row(NamedTuple):
    rule: str   # 판정(`NAME_RULES` · banned · acronym)
    name: str
    hint: str   # 경로로 시작하는 안내
    path: str
    key: str = ""   # 기준선 종류의 둘째 마디 — 막은 낱말 · 구절 · 끝말 · 동사(없으면 판정 이름만)

    @property
    def kind(self) -> str:
        return f"{self.rule}:{self.key}" if self.key else self.rule


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
        self.banned_phrases = table.get("_bannedPhrases", {})
        self.phrase_words = {phrase: [word.lower() for word in split_words(phrase)] for phrase in self.banned_phrases}
        self.keep_terms = table.get("_keepTerms", [])
        self.predicate_words = set(table.get("_predicateWords", []))
        self.lookup_words = set(table.get("_lookupWords", []))
        self.keep = table["_keep"]
        self.timing_keep = table.get("_timingAccessorKeep", {})
        self.suffix_rules = table.get("_suffixRules", {})
        self.cmake_verbs = table.get("_cmakeVerbs", {})
        self.concepts = table.get("_concepts", {})
        self.banned_names = table.get("_bannedNames", {})
        self.map_first_run = {name: (match.group(0) if (match := WORD_RUN.match(name)) else None) for name in self.banned_names}
        self.map_term: dict[str, tuple[str, str] | None] = {}   # 이름 → (판정, 금지 낱말 · 구절) — 같은 식별자가 파일마다 수백 번 나와 `_keepTerms` 지우기를 이름마다 한 번만 한다
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

    def find_phrase(self, name: str) -> str | None:
        """@p name 의 낱말이 금지 구절(`_bannedPhrases`)의 낱말을 차례대로 이어 담으면 그 구절."""
        if not self.phrase_words:
            return None
        squashed = name.lower().replace("_", "")
        list_word: list[str] | None = None
        for phrase, phrase_words in self.phrase_words.items():
            if "".join(phrase_words) not in squashed:   # 낱말 쪼개기 전에 거르는 빠른 길
                continue
            list_word = list_word if list_word is not None else [word.lower() for word in split_words(name)]
            count = len(phrase_words)
            if any(list_word[index:index + count] == phrase_words for index in range(len(list_word) - count + 1)):
                return phrase
        return None

    def find_term(self, name: str) -> tuple[str, str] | None:
        """(term · phrase, 막은 낱말 · 구절) — 이름마다 한 번 계산한다."""
        if name not in self.map_term:
            term = find_term_word(name, self.banned_terms, self.keep_terms) if self.banned_terms else None
            phrase = None if term is not None else self.find_phrase(name)
            self.map_term[name] = ("term", term) if term is not None else ("phrase", phrase) if phrase is not None else None
        return self.map_term[name]

    def describe_term(self, name: str, found: tuple[str, str]) -> str:
        rule, word = found
        if rule == "term":
            return f"'{name}' 에 금지 명사 '{word}' 가 있다 — {self.banned_terms[word]}"
        return f"'{name}' 에 금지 구절 '{word}' 가 있다 — {self.banned_phrases[word]}"

    def classify_suffixes(self, path: str, code: str) -> list[Row]:
        """suffix 판정 — 형식 끝말과 기반 · 멤버 · 꼴의 짝(`_suffixRules`)."""
        list_row: list[Row] = []

        def record(suffix: str, name: str, problem: str) -> None:
            if name not in self.keep:
                list_row.append(Row("suffix", name, f"{path}: '{name}' {problem} — {self.suffix_rules[suffix]['hint']}", path, suffix))

        set_defined: set[str] = set()
        for match in TYPE_DEFINITION.finditer(code):
            name, first_base = match.group(1), find_first_base(match.group(2))
            set_defined.add(name)
            for suffix, rule in self.suffix_rules.items():
                if rule.get("baseNeedsName") and first_base.endswith(suffix) and not name.endswith(suffix):
                    record(suffix, name, f"의 기반 {first_base} 가 {suffix} 인데 이름이 {suffix} 로 끝나지 않는다")
                if (base := rule.get("nameNeedsBase")) and name.endswith(suffix) and name != suffix and not first_base.endswith(base):
                    record(suffix, name, f"는 {suffix} 로 끝나는데 첫 기반({first_base or '없음'})이 {base} 가 아니다")
                if (pairs := rule.get("members")) and name.endswith(suffix) and name != suffix:
                    body = code[match.end() - 1:match_brace(code, match.end() - 1) + 1]
                    if not any(all(re.search(rf"\b{member}\w*\s*\(", body) for member in pair) for pair in pairs):
                        record(suffix, name, f"에 {' · '.join('/'.join(pair) for pair in pairs)} 멤버 짝이 없다")
        for suffix, rule in self.suffix_rules.items():
            if rule.get("notTemplate"):
                for match in TEMPLATE_TYPE.finditer(code):
                    if match.group(1).endswith(suffix):
                        record(suffix, match.group(1), f"는 템플릿인데 끝말이 {suffix} 다")
            if rule.get("notConstantTable"):
                for match in CONSTANT_TABLE.finditer(code):
                    if match.group(1).endswith(suffix):
                        record(suffix, match.group(1), f"가 코드 상수 표의 칸인데 끝말이 {suffix} 다")
            if rule.get("notBesideOwner") and path.endswith(".h"):
                set_owner = set_defined | set(OWNER_DECLARATION.findall(code))
                for name in sorted(set_owner):
                    if name.endswith(suffix) and name[:-len(suffix)] in set_defined:
                        record(suffix, name, f"가 같은 헤더의 주인 형식 {name[:-len(suffix)]} 옆에 있다")
        return list_row

    def classify_code(self, path: str, code: str) -> list[Row]:
        """C++ 글 하나(주석 · 문자열을 지운 것)의 위반 — word 밖의 판정."""
        list_row: list[Row] = []

        def record(rule: str, name: str, hint: str, key: str = "") -> None:
            if name not in self.keep:
                list_row.append(Row(rule, name, f"{path}: {hint}", path, key))

        banned_names = self.banned_names
        if banned_names:
            for name in self.find_banned_names(code):
                list_row.append(Row("banned", name, f"{path}: 옛 이름 '{name}' — {banned_names[name]}", path))
        for word, name in collect_words(code):
            if name in self.keep:
                continue
            if word in self.banned_verbs:
                record("verb", name, f"함수 앞 단어 '{word}'({name}) 는 금지 동사다 — {self.banned_verbs[word]}", word)
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
        if self.suffix_rules:
            list_row += self.classify_suffixes(path, code)
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
        if self.banned_terms or self.banned_phrases:
            set_term: set[str] = set()
            for match in IDENTIFIER.finditer(code):
                name = match.group(0)
                if name in self.keep or name in set_term:
                    continue
                found = self.find_term(name)
                if found is not None:
                    set_term.add(name)
                    record(found[0], name, self.describe_term(name, found), found[1])
        return list_row

    def classify_script(self, path: str, text: str, list_name: Iterable[tuple[str, str]]) -> list[Row]:
        """스크립트(파이썬 · 셸 · CMake) 선언의 verb · term · phrase, 파이썬 예외 이름, CMake 동사의 본문 모양."""
        list_row = self.classify_terms(path, list_name)
        cmake = is_cmake_path(path)
        for kind, name in dict.fromkeys(list_name):
            if kind != "function" or name in self.keep:
                continue
            words = split_lower_words(name, kind)
            if cmake and words[:1] == ["sw"]:
                words = words[1:]
            if words and words[0] in self.banned_verbs and not (cmake and words[0] in self.cmake_verbs):
                list_row.append(Row("verb", name, f"{path}: 함수 앞 단어 '{words[0]}'({name}) 는 금지 동사다 — {self.banned_verbs[words[0]]}", path, words[0]))
        if path.endswith(".py"):
            for match in PYTHON_CLASS_HEAD.finditer(text):
                name, bases = match.group(1), [base.strip().split(".")[-1] for base in match.group(2).split(",")]
                if any(base.endswith(("Error", "Exception")) for base in bases) and not name.endswith("Error") and name not in self.keep:
                    list_row.append(Row("exception", name, f"{path}: 예외 클래스 '{name}' 는 Error 로 끝난다(PEP 8 · docs/agents/NamingCpp.md Python). 오류가 아닌 멈춤 신호면 _keep 에 까닭과 함께", path))
        if cmake and self.cmake_verbs:
            for match in CMAKE_FUNCTION_BODY.finditer(text):
                name, body = match.group(1), match.group(2)
                verb = name.split("_")[1] if name.count("_") >= 1 else ""
                rule = self.cmake_verbs.get(verb)
                if rule is None or name in self.keep:
                    continue
                list_missing = [token for token in rule.get("requires", ()) if token not in body]
                list_forbidden = [token for token in rule.get("forbids", ()) if token in body]
                if list_missing or list_forbidden:
                    detail = " · ".join([*(f"{token} 가 없다" for token in list_missing), *(f"{token} 가 있다" for token in list_forbidden)])
                    list_row.append(Row("cmake-verb", name, f"{path}: CMake 함수 '{name}' 의 본문에 {detail} — {rule['hint']}", path, verb))
        return list_row

    def classify_terms(self, path: str, list_name: Iterable[tuple[str, str]]) -> list[Row]:
        """C++ 밖의 이름(스크립트 선언 · 파일 · 폴더 이름)의 금지 명사 · 구절 — C++ 코드는 `classify_code` 가 모든 식별자에서 본다."""
        list_row: list[Row] = []
        if not (self.banned_terms or self.banned_phrases) or is_under(path, self.external_paths):
            return list_row
        for _, name in dict.fromkeys(list_name):
            if name not in self.keep and (found := self.find_term(name)) is not None:
                list_row.append(Row(found[0], name, f"{path}: {self.describe_term(name, found)}", path, found[1]))
        return list_row

    def find_word_problem(self, kind: str, name: str, path: str) -> tuple[str, str] | None:
        """word 판정 — 이름의 모든 낱말이 낱말 표(`_words` 의 a 업계 · b 게임)에 있는가. 어긋나면 (안내, 범위 낱말 — 표에 없는 낱말이면 빈 글)이다.
        `_scopedWords` 의 낱말은 `paths` 아래에서만 받거나 `exceptPaths` 아래에서 받지 않는다(게임의 walk 는 맞고 엔진의 walk 는 순회다). 표준 합성어(`_keepTerms`) · 바깥 이름(`_externalPatterns`) · `_keep` 은 뺀다."""
        if name in self.keep or is_external_name(name, self.externals) or is_under(path, self.external_paths):
            return None
        if name in self.banned_names and not path.startswith("Source/"):   # C++ 의 옛 이름은 classify_code 가 코드 전체에서 본다
            return f"{path}: 옛 이름 '{name}' — {self.banned_names[name]}", ""
        if kind == "function" and is_cmake_path(path) and CMAKE_FUNCTION_SHAPE.fullmatch(name) is None:
            return f"{path}: CMake 함수 '{name}' 는 sw_snake_case 여야 한다(docs/agents/NamingCpp.md — 내장 명령 꼴 + 우리 접두, 약어도 소문자)", ""
        shape = PYTHON_SHAPES.get(kind) if path.endswith(".py") else None
        if shape is not None and shape[0].fullmatch(name) is None:
            return f"{path}: 파이썬 이름 '{name}' 가 PEP 8 꼴이 아니다 — {shape[1]}(docs/agents/NamingCpp.md). 바깥 덮어쓰기(setUp · do_POST)는 _keep", ""
        stripped = name
        for term in self.keep_term_patterns:
            stripped = term.sub("_", stripped)
        for word in split_lower_words(stripped, kind):
            if word in self.known:
                continue
            if word not in self.scoped:
                return (f"{path}: '{name}' 의 낱말 '{word}' 가 낱말 표에 없다 — 언리얼 · 유니티 · Godot · 일반 용어와 대조한 뒤 {CONFIG_PATH} 의 "
                        f"_words(a 업계 · b 게임)에 올리거나 업계 낱말로 바꾼다"), ""
            scope = self.scoped[word]
            if "paths" in scope and not is_under(path, scope["paths"]):
                return f"{path}: '{name}' 의 낱말 '{word}' 는 {' · '.join(scope['paths'])} 에서만 쓴다 — {scope['hint']}", word
            if is_under(path, scope.get("exceptPaths", ())) and not any(compound in name for compound in scope.get("keepCompounds", ())):
                return f"{path}: '{name}' 의 낱말 '{word}' 는 {' · '.join(scope['exceptPaths'])} 에서 쓰지 않는다 — {scope['hint']}", word
        return None

    def classify_words(self, path: str, list_name: Iterable[tuple[str, str]]) -> list[Row]:
        return [Row("word", name, found[0], path, found[1]) for kind, name in dict.fromkeys(list_name) if (found := self.find_word_problem(kind, name, path)) is not None]

    def classify_acronyms(self, path: str, code: str) -> list[Row]:
        """약어를 첫 글자만 대문자로 쓴 이름 — 파일 · 폴더 이름과 C++ 식별자."""
        list_row: list[Row] = []
        for segment in path.rsplit(".", 1)[0].split("/")[1:]:
            name = segment.split(".")[0]
            if name not in self.acronym_keep and (list_bad := find_bad_words(name, self.acronyms)):
                list_row.append(Row("acronym", name, f"{path}: 경로 낱말 '{name}' 의 약어는 모두 대문자({', '.join(list_bad)}) — NamingCpp Acronyms", path))
        for name in sorted(set(IDENTIFIER.findall(code)) - self.acronym_keep.keys()):
            if list_bad := find_bad_words(name, self.acronyms):
                list_row.append(Row("acronym", name, f"{path}: '{name}' 의 약어는 모두 대문자({', '.join(list_bad)}) — 바깥 이름 · 데이터 철자면 {CONFIG_PATH} 의 _acronymKeep 에 까닭과 함께", path))
        return list_row

    def classify_path(self, path: str) -> list[Row]:
        """경로 이름(파일 · 폴더)의 word · term · phrase."""
        list_name = list_path_names(path)
        return self.classify_words(path, list_name) + self.classify_terms(path, list_name)

    def find_concept_problems(self) -> list[str]:
        """개념표의 동의어마다 그것을 막는 줄이 있는가 — 개념표는 판정을 따로 두지 않는다(같은 층에 막는 모델은 하나)."""
        set_term = {term.lower() for term in self.banned_terms}
        list_problem: list[str] = []
        for concept, entry in self.concepts.items():
            for synonym in entry["synonyms"]:
                if synonym.lower() not in set_term and synonym not in self.banned_phrases and synonym.lower() not in self.scoped and synonym.lower() not in self.banned_verbs:
                    list_problem.append(f"{CONFIG_PATH}: _concepts '{concept}' 의 동의어 '{synonym}' 를 막는 줄이 없다 — _bannedTerms · _bannedPhrases · _scopedWords · _banned 의 하나에 올린다")
        return list_problem


@functools.lru_cache(maxsize=256)
def compile_banned_names(list_name: tuple[str, ...]) -> re.Pattern:
    return re.compile(r"\b(" + "|".join(map(re.escape, list_name)) + r")\b")


def list_problems(rows: Iterable[Row]) -> list[str]:
    """판정마다 이름 하나에 한 줄(처음 자리) — 줄 머리는 판정 이름(`[verb]`), 옛 이름 · 약어는 머리가 없다."""
    map_hint: dict[tuple[str, str], str] = {}
    for row in rows:
        map_hint.setdefault((row.rule, row.name), row.hint)
    return [f"[{rule}] {hint}" if rule in NAME_RULES else hint for (rule, _), hint in sorted(map_hint.items(), key=lambda item: item[0])]


def count_rows(rows: Iterable[Row]) -> dict[str, dict[str, int]]:
    """파일 → 판정 종류 → 이름 수(기준선이 세는 것)."""
    map_name: dict[str, dict[str, set[str]]] = collections.defaultdict(lambda: collections.defaultdict(set))
    for row in rows:
        map_name[row.path][row.kind].add(row.name)
    return {path: {kind: len(names) for kind, names in sorted(kinds.items())} for path, kinds in map_name.items()}


def compare_rows(rows: Sequence[Row], baseline: Baseline, list_path: Iterable[str], map_old: Mapping[str, str]) -> tuple[list[Row], list[str]]:
    """기준선을 넘은 (파일, 종류)의 줄과 줄어든 종류의 안내 — 기준선에 없는 파일 · 종류는 0 이다."""
    map_count = count_rows(rows)
    set_over = {(path, kind) for path, kinds in map_count.items() for kind, count in kinds.items() if count > baseline.get_limits(path, map_old).get(kind, 0)}
    list_lowered = [f"{path}: {kind} {limit} -> {map_count.get(path, {}).get(kind, 0)} — 줄었다: `{RATCHET_COMMAND}` 로 기준선을 낮추고 함께 커밋한다"
                    for path in sorted(set(list_path)) for kind, limit in sorted(baseline.get_limits(path, map_old).items()) if map_count.get(path, {}).get(kind, 0) < limit]
    return [row for row in rows if (row.path, row.kind) in set_over], list_lowered


class NamingWordsRule(LintRule):
    id = "naming-words"
    group = "이름"
    blocks = "표에 없는 낱말(모든 이름 · 파일 · 폴더의 모든 낱말) · 금지 동사 · 금지 명사 · 금지 구절 · 끝말 모양 · 금지 이름 · 약어 표기(모두 대문자)"
    why_not_type = "업계 용어인지는 형식이 말하지 못한다"
    tables = ("", "BannedNames", "Baseline")

    @functools.cached_property
    def names(self) -> NameTable:
        return NameTable({**self.read_table(), "_bannedNames": self.read_table("BannedNames")["_bannedNames"]})

    def selects(self, path: str) -> bool:
        return declares_names(path)

    def inspect(self, file: SourceFile) -> list[Row]:
        if not file.path.startswith("Source/") or not file.path.endswith(EXTENSIONS):
            list_name = list_file_names(file.path, file.text)
            return self.names.classify_words(file.path, list_name) + self.names.classify_script(file.path, file.text, list_name)
        code = file.code_with_macros
        return (self.names.classify_code(file.path, code) + self.names.classify_words(file.path, list_file_names(file.path, code))
                + self.names.classify_acronyms(file.path, code))

    def collect_rows(self, scope: LintScope, found: Mapping[str, list[Row]]) -> list[Row]:
        list_row = [row for rows in found.values() for row in rows]
        for path in scope.list_file:
            list_row += self.names.classify_path(path)
        return list_row

    def evaluate(self, scope: LintScope, found: Mapping[str, list[Row]]) -> list[str]:
        list_row = self.collect_rows(scope, found)
        baseline = Baseline.load(self.root, self.id)
        map_old = read_staged_renames(self.root) if not scope.whole_repo and scope.list_file else {}
        list_over, list_lowered = compare_rows(list_row, baseline, scope.list_file, map_old)
        table = ExceptionTable(CONFIG_PATH, self.read_table(), KEEP_SECTIONS)
        list_missing = baseline.list_missing(set(scope.list_file)) if scope.whole_repo else []
        return list_problems(list_over) + list_lowered + list_missing + self.names.find_concept_problems() + table.list_problems(scope.whole_repo, stale_sections=())

    def update_baseline(self, list_accept: Sequence[str] | None) -> None:
        scope = make_whole_scope(self.root) if list_accept is None else make_file_scope(self.root, list_accept)
        found, _ = inspect_files(self.root, {self: scope})
        Baseline.load(self.root, self.id, BASELINE_COMMENT).update(self.root, count_rows(self.collect_rows(scope, found[self.id])), list_accept)

    def self_check(self) -> None:
        def find_code_problems(code: str, table: Mapping, path: str = "a.h") -> dict[str, set[str]]:
            map_name: dict[str, set[str]] = {rule: set() for rule in (*NAME_RULES, "banned")}
            for row in NameTable(table).classify_code(path, strip_comments_and_strings(code)):
                map_name[row.rule].add(row.name)
            return map_name

        def find_word_problems(list_name: Iterable[tuple[str, str, str]], table: Mapping) -> dict[str, str]:
            names = NameTable({"_banned": {}, **table})
            return {name: found[0] for kind, name, path in list_name if (found := names.find_word_problem(kind, name, path)) is not None}

        table = {"_banned": {"drain": "flush 또는 poll"},
                 "_bannedTerms": {"Verdict": "Result", "Work": "Job"}, "_keepTerms": ["WorkArea"], "_predicateWords": ["is", "has", "uses", "starts"], "_lookupWords": ["get", "find"],
                 "_keep": {"MailboxServiceResult": "이유"}}
        clean = 'struct FooComponent : Component {}; int getValue( int x ); struct MailboxServiceResult {}; // drainEvents() hasWork\nconst char* s = "drainEvents() Verdict"; bool hasValue(); bool setAxisTags(); bool focusPanel(); bool startsWith(); RECT readWorkArea; int getSlotFor( int x );'
        found = find_code_problems(clean, table)
        assert not any(found.values()), found
        bad = 'void drainEvents( int x ); struct FooVerdict {}; bool hasPendingWork(); bool drawsBatch(); int materialGroupFor( int x ); int _verdictCount;'
        found = find_code_problems(bad, table)
        assert found["verb"] == {"drainEvents"}, found
        assert found["term"] == {"FooVerdict", "hasPendingWork", "_verdictCount"}, found
        assert found["predicate"] == {"drawsBatch"}, found
        assert found["lookup"] == {"materialGroupFor"}, found
        assert find_code_problems("enum class PacketType : uint8 { A }; enum class PacketKind { A }; enum OldType { B }; struct ValueType {};", table)["kind"] == {"PacketType"}   # enum class 만 본다 — 구조체 · 옛 enum 은 다른 규칙 몫
        shape_code = ("class SW_API FooComponent : public SceneComponent {}; struct BarMover final : public sw::Component {}; struct BazComponent : public Widget<Component> {};"
                                "class ComponentHandle {}; class SW_API Component {}; struct QuxComponent; REFLECT( Interface ) struct IGood { }; REFLECT( Interface ) struct Bad { };")
        shape_found = find_code_problems(shape_code, table)
        assert shape_found["component"] == {"BarMover", "BazComponent"}, shape_found   # 양성: 접미 없는 컴포넌트 · 컴포넌트가 아닌 Component 끝말. 음성: FooComponent · ComponentHandle · 기반 Component · 전방 선언
        assert shape_found["interface"] == {"Bad"}, shape_found   # 양성: I 없는 인터페이스. 음성: IGood
        timing_table = dict(table, _timingAccessorKeep={"getElapsedMicroseconds": "시계 형식"})
        timing_code = ("int64 getLastStepMicroseconds() const { return watch.getElapsedMicroseconds(); } int64 getElapsedMicroseconds() const; uint64 Foo::getPeakNanoseconds();")
        assert find_code_problems(timing_code, timing_table, "Source/GameFramework/a.h")["timing"] == {"getLastStepMicroseconds"}   # 양성: 배포 헤더의 선언. 음성: 부르기 · 예외 표 · `A::` 정의
        for path in ("Source/Editor/a.h", "Source/Engine/Tests/a.h", "Source/DevTools/a.h", "Source/Engine/a.cpp"):
            assert find_code_problems(timing_code, timing_table, path)["timing"] == set(), path   # 음성: 배포에 없는 모듈 · 테스트 · .cpp
        assert find_first_base("public sw::Map<Key, Value>, private Other") == "Map" and find_first_base(None) == ""
        name_problems = list_problems(NameTable(table).classify_code("a.h", strip_comments_and_strings(bad)))
        assert len(name_problems) == 6 and "[verb] a.h: " in "".join(name_problems) and any("materialGroupFor" in line for line in name_problems), name_problems
        table_names = dict(table, _bannedNames={"CombatHealth": "HealthStateMachine"})
        assert find_code_problems("// CombatHealth\nconst char* s = \"CombatHealth\";", table_names)["banned"] == set(), "주석 · 문자열의 옛 이름은 본다고 하지 않는다"
        assert find_code_problems("CombatHealth health; int getCombatHealthRatio();", table_names)["banned"] == {"CombatHealth"}, "통째 이름만 막는다"
        overlap = dict(table, _bannedNames={"YawUtil": "a", "YawUtil::fromWorld": "b", "Gone": "c", "new-names": "d"})
        assert NameTable(overlap).find_banned_names("x = YawUtil::fromWorld( v ); new-names;") == ["YawUtil", "new-names"], "표 차례로 엮은 것과 같다 — 앞 이름(YawUtil)이 이기고, 글에 없는 이름은 엮지 않는다"

        # phrase — 낱말이 이어 나올 때만(양성 liveReload · LiveReloadTarget, 음성 liveOps · reloadLive · oliveReload)
        phrase_table = dict(table, _bannedPhrases={"LiveReload": "HotReload"})
        assert find_code_problems("void liveReload(); struct LiveReloadTarget {}; int liveOps; void reloadLive(); int oliveReload;", phrase_table)["phrase"] == {"liveReload", "LiveReloadTarget"}
        assert find_code_problems("int liveOps;", phrase_table)["phrase"] == set()   # 음성: 구절을 빼면 통과

        # suffix — 끝말과 기반 · 멤버 · 꼴의 짝
        suffix_table = dict(table, _suffixRules={"Subsystem": {"baseNeedsName": True, "hint": "h"}, "System": {"nameNeedsBase": "System", "hint": "h"},
                                                  "Codec": {"members": [["write", "read"], ["encode", "decode"]], "hint": "h"},
                                                  "Def": {"notTemplate": True, "notConstantTable": True, "hint": "h"}, "Util": {"notBesideOwner": True, "hint": "h"}})
        suffix_code = ("class AudioSubsystem : public EngineSubsystem {}; class AudioHub : public EngineSubsystem {}; class MoveSystem : public ISceneFrameSystem {};"
                       "class LoneSystem {}; struct PoseCodec { static void write(); static void read(); }; struct BadCodec { static void pack(); };"
                       "template <typename T> struct TableDef {}; struct ItemDef {}; constexpr ColumnDef kColumns[] = {}; struct Yaw {}; struct YawUtil {}; namespace PitchUtil {}")
        found = find_code_problems(suffix_code, suffix_table)["suffix"]
        assert found == {"AudioHub", "LoneSystem", "BadCodec", "TableDef", "ColumnDef", "YawUtil"}, found   # 음성: AudioSubsystem · MoveSystem · PoseCodec · ItemDef · 주인 없는 PitchUtil
        assert find_code_problems(suffix_code, suffix_table, "a.cpp")["suffix"] == found - {"YawUtil"}   # Util 짝은 헤더만

        code = strip_comments_and_strings("#define SW_SCENE_WATCH 1\nenum class Mode { Idle, Walk };\nusing Alias = int;\nstruct SceneWalkKey {};\n"
                         "void hoverMark( int outbChanged, const Foo& inoutListCrumb ) { float _walkSpeed = 1; const int kMaxCount = 2; }")
        declared = set(collect_declarations(code))
        for item in (("macro", "SW_SCENE_WATCH"), ("enumerator", "Walk"), ("alias", "Alias"), ("type", "SceneWalkKey"), ("function", "hoverMark"),
                     ("variable", "outbChanged"), ("variable", "inoutListCrumb"), ("member", "_walkSpeed"), ("constant", "kMaxCount")):
            assert item in declared, (item, declared)
        assert split_lower_words("outbChanged", "variable") == ["changed"] and split_lower_words("inoutListCrumb", "variable") == ["crumb"]
        assert split_lower_words("D3D12Vector3D", "type") == ["vector"] and split_lower_words("setAxisTags", "function") == ["set", "axis", "tags"]
        assert collect_path_names("Source/Games/Walk/WalkPath.h") == [("folder", "Games"), ("folder", "Walk"), ("file", "WalkPath")]
        table_words = {"_words": {"a": ["scene", "key", "speed", "area", "mark", "hover", "rc", "at", "panel", "project"], "b": ["ghost"]}, "_keep": {},
                      "_scopedWords": {"walk": {"paths": ["Source/Games"], "hint": "순회는 iterate"}, "settings": {"exceptPaths": ["Source/Editor"], "keepCompounds": ["ProjectSettings"], "hint": "Preferences"}},
                      "_externalPatterns": [r"^PFN_"], "_externalPaths": [".claude/skills/ponytail"], "_keepTerms": ["WorkArea"]}
        found = find_word_problems([("type", "SceneWalkKey", "Source/Engine/a.h"), ("member", "_walkSpeed", "Source/Games/b.h"), ("member", "_ghostKey", "Source/a.h"),
                               ("variable", "rcWorkArea", "Source/a.h"), ("alias", "PFN_vkWeirdo", "Source/a.h"), ("function", "aimAtMark", "Source/a.h"),
                               ("folder", "Walk", "Source/Games/Walk/x.h")], table_words)
        assert set(found) == {"SceneWalkKey", "aimAtMark"} and "iterate" in found["SceneWalkKey"] and "'aim'" in found["aimAtMark"], found
        found = find_word_problems([("type", "SettingsPanel", "Source/Editor/a.h"), ("type", "SettingsPanel", "Source/Engine/a.h"), ("type", "ProjectSettingsPanel", "Source/Editor/a.h")], table_words)
        assert list(found) == ["SettingsPanel"] and "Source/Editor/a.h" in found["SettingsPanel"], found   # exceptPaths 양성: 에디터의 settings. 음성: 엔진 · 표준 합성어
        script = "MAX_SCENE = 1\nkSceneKey = 2\nclass SceneKey:\n    def hover_mark(self) -> None: ...\n    def hoverScene(self) -> None: ...\n"
        found = find_word_problems([(kind, name, "Scripts/lint/scene_key.py") for kind, name in collect_script_declarations("Scripts/lint/scene_key.py", script)]
                               + [("file", "SceneKey", "Scripts/lint/SceneKey.py"), ("file", "SceneKey", "cmake/SceneKey.cmake")], table_words | {"_words": {"a": ["max", "scene", "key", "hover", "mark"], "b": []}})
        assert set(found) == {"kSceneKey", "hoverScene", "SceneKey"} and "Scripts/lint/SceneKey.py" in found["SceneKey"], found   # PEP 8 꼴 — CMake 파일 이름은 보지 않는다
        found = find_word_problems([("file", "scene_key", "Scripts/verify/scene_key.py"), ("command", "scene-key", "Scripts/__main__.py")],
                               table_words | {"_words": {"a": ["scene", "key"], "b": []}, "_bannedNames": {"scene_key": "key_scene", "scene-key": "key-scene"}})
        assert set(found) == {"scene_key", "scene-key"} and "key_scene" in found["scene_key"], found   # 스크립트의 옛 이름
        found = find_word_problems([("function", "sw_scene_key", "cmake/a.cmake"), ("function", "sw_sceneKey", "cmake/a.cmake"), ("function", "scene_key", "Source/CMakeLists.txt")],
                               table_words | {"_words": {"a": ["sw", "scene", "key"], "b": []}})
        assert set(found) == {"sw_sceneKey", "scene_key"} and all("sw_snake_case" in hint for hint in found.values()), found   # CMake 함수 꼴

        # 스크립트의 verb · term · phrase · exception · cmake-verb
        script_table = dict(table, _bannedTerms={"Tier": "layer"}, _bannedPhrases={"LiveReload": "HotReload"}, _keep={"RunStop": "멈춤 신호"},
                            _cmakeVerbs={"require": {"requires": ["FATAL_ERROR"], "hint": "h"}, "check": {"requires": ["PARENT_SCOPE"], "forbids": ["FATAL_ERROR"], "hint": "h"}})
        names = NameTable(script_table)
        python = ("def drain_queue() -> None: ...\ndef flush_queue() -> None: ...\nTIER_TABLE = 1\nclass LiveReloadWatch:\n    pass\n"
                  "class StageFailed(Exception):\n    pass\nclass CacheBrokenError(RuntimeError):\n    pass\nclass RunStop(Exception):\n    pass\nclass Plain:\n    pass\n")
        rows = names.classify_script("Scripts/a/b.py", python, collect_script_declarations("Scripts/a/b.py", python))
        assert {(row.rule, row.name) for row in rows} == {("verb", "drain_queue"), ("term", "TIER_TABLE"), ("phrase", "LiveReloadWatch"), ("exception", "StageFailed")}, rows
        cmake = ("function(sw_require_tools)\n  message(FATAL_ERROR \"x\")\nendfunction()\nfunction(sw_require_soft)\n  message(WARNING \"x\")\nendfunction()\n"
                 "function(sw_check_cache out)\n  set(${out} 1 PARENT_SCOPE)\nendfunction()\nfunction(sw_check_loud out)\n  message(FATAL_ERROR \"x\")\nendfunction()\n"
                 "function(sw_drain_jobs)\nendfunction()\n")
        rows = names.classify_script("cmake/a.cmake", cmake, collect_script_declarations("cmake/a.cmake", cmake))
        assert {(row.rule, row.name) for row in rows} == {("cmake-verb", "sw_require_soft"), ("cmake-verb", "sw_check_loud"), ("verb", "sw_drain_jobs")}, rows
        assert [row.name for row in names.classify_path("Scripts/verify/tier_table.py") if row.rule == "term"] == ["tier_table"]   # 파일 이름의 금지 명사

        # 기준선 — 지금 수까지는 통과, 하나 늘면 그 종류의 줄, 줄면 래칫 안내
        rows_now = [Row("term", "tierA", "a.py: x", "a.py", "Tier"), Row("term", "tierB", "a.py: y", "a.py", "Tier"), Row("word", "fooSpec", "b.h: z", "b.h", "spec")]
        baseline = Baseline("t.json", {"_comment": "t", "_files": {"a.py": {"term:Tier": 2}, "b.h": {"word:spec": 1}}})
        assert compare_rows(rows_now, baseline, ["a.py", "b.h"], {}) == ([], [])
        over, lowered = compare_rows([*rows_now, Row("term", "tierC", "a.py: w", "a.py", "Tier"), Row("verb", "drainX", "c.py: v", "c.py", "drain")], baseline, ["a.py", "b.h", "c.py"], {})
        assert {row.name for row in over} == {"tierA", "tierB", "tierC", "drainX"} and lowered == [], over   # 늘어난 종류는 그 종류 이름을 모두 보인다, 기준선 밖은 0
        over, lowered = compare_rows(rows_now[:1], baseline, ["a.py", "b.h"], {})
        assert over == [] and len(lowered) == 2 and "term:Tier 2 -> 1" in lowered[0], lowered
        assert compare_rows([Row("term", "tierA", "n.py: x", "n.py", "Tier")], Baseline("t.json", {"_comment": "t", "_files": {"o.py": {"term:Tier": 1}}}), ["n.py"], {"n.py": "o.py"}) == ([], [])   # 옮긴 파일은 옛 기준선을 잇는다

        concept_table = dict(table, _concepts={"Result": {"word": "Result", "synonyms": ["Verdict"]}, "GUID": {"word": "GUID", "synonyms": ["UUID"]}})
        assert NameTable(concept_table).find_concept_problems() == [f"{CONFIG_PATH}: _concepts 'GUID' 의 동의어 'UUID' 를 막는 줄이 없다 — _bannedTerms · _bannedPhrases · _scopedWords · _banned 의 하나에 올린다"]

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
