"""
Scripts/dev/delegate_macros.py

델리게이트에 함수를 붙이는 맨 API(`X::create<&f>()` · `X::create<&T::m>( p )` · `X::create( 람다 )`)를 `Core/Delegate/Delegate.h` 끝의 매크로로 바꾼다.
맨 API 는 종류마다 함수를 템플릿 인자로 주는지 생성자 인자로 주는지가 달라 모양을 외워야 한다 — 매크로는 종류 이름 + 같은 인자 순서로 덮는다(사용자 2026-11-20).

  py -3 -m Scripts delegate-macros [경로…]          # 바꾼다(경로 없으면 Source)
  py -3 -m Scripts delegate-macros --check [경로…]  # 남은 맨 create 를 찍고, 있으면 실패

- `X::create<&M>( inst )` → `SW_DELEGATE_METHOD( X, &M, inst )`, `X::create<&F>()` → `SW_DELEGATE_FUNCTION( X, &F )`,
  `X::create( 람다 · 함수 객체 )` → `SW_DELEGATE_LAMBDA( X, … )`. `X::template create<…>`(의존 형식)도 같다 — 매크로가 `template` 을 붙인다.
- 대상은 X 의 마지막 이름이 델리게이트 계열(`Delegate`, 그것을 가리키는 `using` · `SW_DECLARE_DELEGATE` 별칭 — 정의를 따라 닫는다)인 것만이다.
  같은 이름의 팩토리(`Material::create` 등)는 건드리지 않는다.
- 주석 · 문자열 · 전처리 줄(매크로 정의)은 보지 않는다(`lint.cpp_shape.strip_code`). `Core/Delegate/` (구현 · 그 테스트)는 맨 API 의 자리라 뺀다.
- 매크로 인자가 쉼표로 갈라지는 자리(`Foo<A, B>::Delegate` · `&T::f<A, B>` · 괄호 밖 쉼표가 든 인스턴스)는 바꾸지 않고 알린다 — 별칭을 두고 다시 돌린다.
- 병합 뒤 충돌 파일에 다시 돌린다(이미 바꾼 자리는 그대로).

린트 `cpp-shape` 가 `find_sites` 로 같은 판정을 한다.
"""

from __future__ import annotations

import argparse
import functools
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.exit_code import ExitCode  # noqa: E402
from common.git import run_git_or_raise  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from lint.cpp_shape import strip_code  # noqa: E402

TAG = "[delegate-macros]"
EXCLUDED_FOLDER = "Source/Core/Delegate/"   # 맨 API 의 정의 · 구현 · 그 테스트
SKIP_PARTS = ("ThirdParty", "Generated", ".gen.")
CPP_SUFFIX = (".h", ".cpp", ".inl")
DELEGATE_ROOT = "Delegate"

CREATE_PATTERN = re.compile(r"::\s*(?:template\s+)?create\s*(?=[<(])")
DECLARE_PATTERN = re.compile(r"SW_DECLARE_DELEGATE\s*\(\s*[^,]+,\s*(\w+)")
ALIAS_PATTERN = re.compile(r"\busing\s+(\w+)\s*=\s*([^;]+);")
ANGLE_GROUP = re.compile(r"<[^<>]*>")
LAST_NAME = re.compile(r"(\w+)\s*$")


@dataclass(frozen=True)
class Site:
    """맨 create 자리 하나. `start`/`end` 는 바꿀 글의 구간, `replacement` 는 매크로 글(바꿀 수 없으면 None 과 `problem`)."""

    start: int
    end: int
    line: int
    kind: str
    replacement: str | None
    problem: str | None


# ------------------------------------------------------------------------------
# 델리게이트 계열 이름
# ------------------------------------------------------------------------------
def get_last_name(type_text: str) -> str | None:
    """`a::B<C>::D<E>` → `D`. 템플릿 인자 묶음을 지운 뒤 마지막 식별자."""
    text = type_text
    while True:
        reduced = ANGLE_GROUP.sub("", text)
        if reduced == text:
            break
        text = reduced
    text = re.sub(r"\b(?:typename|const|volatile|struct|class)\b", " ", text)
    match = LAST_NAME.search(text)
    return match.group(1) if match else None


def collect_delegate_names(list_line: Iterable[str]) -> frozenset[str]:
    """`SW_DECLARE_DELEGATE` · `using X = …Delegate…` 별칭을 따라 닫은 델리게이트 계열 이름(MulticastDelegate 는 create 가 없어 들지 않는다)."""
    names = {DELEGATE_ROOT}
    aliases: dict[str, set[str]] = {}
    for line in list_line:
        for match in DECLARE_PATTERN.finditer(line):
            names.add(match.group(1))
        for match in ALIAS_PATTERN.finditer(line):
            target = get_last_name(match.group(2))
            if target:
                aliases.setdefault(match.group(1), set()).add(target)
    changed = True
    while changed:
        changed = False
        for name, targets in aliases.items():
            if name not in names and targets & names:
                names.add(name)
                changed = True
    return frozenset(names)


@functools.lru_cache(maxsize=4)
def load_delegate_names(project_root: str) -> frozenset[str]:
    """작업 트리 Source 의 별칭 줄을 git grep 으로 모아 델리게이트 계열 이름을 낸다(프로세스마다 한 번)."""
    output = run_git_or_raise(["grep", "-h", "-E", r"SW_DECLARE_DELEGATE|using\s+\w+\s*=", "--", "Source/*.h", "Source/*.cpp", "Source/*.inl"], cwd=Path(project_root))
    return collect_delegate_names(output.splitlines())


# ------------------------------------------------------------------------------
# 자리 찾기
# ------------------------------------------------------------------------------
def skip_space_back(code: str, index: int) -> int:
    while index > 0 and code[index - 1].isspace():
        index -= 1
    return index


def scan_type_back(code: str, end: int) -> int:
    """`end` 바로 앞에서 끝나는 한정 이름(`a::B<C>::D`)의 시작. 꺾쇠 · 괄호 짝을 거꾸로 맞춘다."""
    index = skip_space_back(code, end)
    while True:
        if index > 0 and code[index - 1] == ">":
            depth = 0
            while index > 0:
                index -= 1
                char = code[index]
                if char in ">)":
                    depth += 1
                elif char in "<(":
                    depth -= 1
                    if depth == 0:
                        break
            index = skip_space_back(code, index)
        name_end = index
        while index > 0 and (code[index - 1].isalnum() or code[index - 1] == "_"):
            index -= 1
        if index == name_end:
            return name_end
        before = skip_space_back(code, index)
        if code[before - 2:before] == "::":
            index = skip_space_back(code, before - 2)
            if index == 0 or not (code[index - 1].isalnum() or code[index - 1] in "_>"):
                return before - 2   # 전역 한정 `::sw::…`
            continue
        return index


def match_forward(code: str, start: int, opener: str, closer: str) -> int:
    """`code[start]` 가 opener 일 때 짝 closer 의 위치. 꺾쇠 안의 괄호 · `->` 를 건넌다. 못 찾으면 -1."""
    depth = 0
    index = start
    while index < len(code):
        char = code[index]
        if opener == "<" and char == ">" and code[index - 1] == "-":
            pass
        elif char == opener or (opener == "<" and char == "("):
            depth += 1
        elif char == closer or (opener == "<" and char == ")"):
            depth -= 1
            if depth == 0:
                return index
        index += 1
    return -1


def has_split_comma(code_piece: str) -> bool:
    """전처리기는 괄호만 쉼표를 감싼다 — 괄호 밖 쉼표가 있으면 매크로 인자가 갈라진다."""
    depth = 0
    for char in code_piece:
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
        elif char == "," and depth == 0:
            return True
    return False


def find_sites(raw: str, code: str, names: frozenset[str]) -> list[Site]:
    """`code` 는 `strip_code(raw)`. 델리게이트 계열 형식의 맨 create 자리를 모두 낸다(겹친 자리 포함 — 바깥이 먼저)."""
    list_site: list[Site] = []
    for match in CREATE_PATTERN.finditer(code):
        type_start = scan_type_back(code, match.start())
        type_code = code[type_start:match.start()]
        if get_last_name(type_code) not in names:
            continue
        cursor = match.end()
        template_argument: str | None = None
        template_code = ""
        if code[cursor] == "<":
            close = match_forward(code, cursor, "<", ">")
            if close < 0:
                continue
            template_code = code[cursor + 1:close]
            template_argument = raw[cursor + 1:close].strip()
            cursor = close + 1
            while cursor < len(code) and code[cursor].isspace():
                cursor += 1
            if cursor >= len(code) or code[cursor] != "(":
                continue   # `&X::create<&f>` 처럼 부르지 않는 자리 — 매크로로 덮을 모양이 아니다
        close = match_forward(code, cursor, "(", ")")
        if close < 0:
            continue
        argument = raw[cursor + 1:close].strip()
        argument_code = code[cursor + 1:close]
        type_text = " ".join(raw[type_start:match.start()].split())
        line = raw.count("\n", 0, type_start) + 1
        if template_argument is not None:
            kind = "FUNCTION" if argument == "" else "METHOD"
            pieces = [type_text, template_argument] + ([argument] if argument else [])
            piece_codes = [type_code, template_code] + ([argument_code] if argument else [])
        elif argument:
            kind = "LAMBDA"
            pieces = [type_text, argument]
            piece_codes = [type_code]   # 람다는 가변 인자(`...`)라 쉼표가 갈라도 된다
        else:
            continue
        problem = None
        if any(has_split_comma(piece) for piece in piece_codes):
            problem = "매크로 인자가 괄호 밖 쉼표로 갈라진다 — 델리게이트형 별칭(SW_DECLARE_DELEGATE · using)을 두고 다시 돌린다"
        replacement = None if problem else f"SW_DELEGATE_{kind}( {', '.join(pieces)} )"
        list_site.append(Site(type_start, close + 1, line, kind, replacement, problem))
    return list_site


def rewrite_text(raw: str, names: frozenset[str]) -> tuple[str, list[Site]]:
    """안쪽 자리부터 바꾼다(람다 안의 create) — 겹치지 않는 가장 안쪽만 한 바퀴에 바꾸고 다시 찾는다. 바꾼 자리와 남긴 자리를 돌려준다."""
    list_done: list[Site] = []
    while True:
        list_site = [site for site in find_sites(raw, strip_code(raw), names) if site.replacement is not None]
        innermost = [site for site in list_site if not any(other is not site and site.start <= other.start and other.end <= site.end for other in list_site)]
        if not innermost:
            break
        for site in sorted(innermost, key=lambda s: s.start, reverse=True):
            raw = raw[:site.start] + site.replacement + raw[site.end:]
        list_done += innermost
    return raw, list_done + [site for site in find_sites(raw, strip_code(raw), names) if site.replacement is None]


def is_target(relative: str) -> bool:
    return relative.startswith("Source/") and relative.endswith(CPP_SUFFIX) and not relative.startswith(EXCLUDED_FOLDER) and not any(part in relative for part in SKIP_PARTS)


def list_files(project_root: Path, list_path: Sequence[str]) -> list[Path]:
    output = run_git_or_raise(["ls-files", "--", *(list_path or ["Source"])], cwd=project_root)
    return [project_root / relative for relative in output.splitlines() if is_target(relative)]


def run_self_check() -> int:
    names = collect_delegate_names(["SW_DECLARE_DELEGATE( void, DoneDelegate, int32 );", "using ReplyDelegate = DoneDelegate;", "using Mesh = MeshBase;"])
    failures = 0
    cases = {
        "auto d = DoneDelegate::create<&Foo::bar>( this );": "auto d = SW_DELEGATE_METHOD( DoneDelegate, &Foo::bar, this );",
        "auto d = sw::Delegate<void( int, int )>::create<&f>();": "auto d = SW_DELEGATE_FUNCTION( sw::Delegate<void( int, int )>, &f );",
        "x( A<T>::ReplyDelegate::create( [a, b]( int v ) { g( \"::create(\" ); } ) );": "x( SW_DELEGATE_LAMBDA( A<T>::ReplyDelegate, [a, b]( int v ) { g( \"::create(\" ); } ) );",
        "r = X<T>::ReplyDelegate::template create<&T::f>( p );": "r = SW_DELEGATE_METHOD( X<T>::ReplyDelegate, &T::f, p );",
        "m = Mesh::create( desc ); // DoneDelegate::create<&f>()": "m = Mesh::create( desc ); // DoneDelegate::create<&f>()",
        "o = DoneDelegate::create( [this] { q( DoneDelegate::create<&g>() ); } );": "o = SW_DELEGATE_LAMBDA( DoneDelegate, [this] { q( SW_DELEGATE_FUNCTION( DoneDelegate, &g ) ); } );",
        "o = P<A, B>::DoneDelegate::create<&g>();": "o = P<A, B>::DoneDelegate::create<&g>();",
    }
    for source, want in cases.items():
        got, _ = rewrite_text(source, names)
        if got != want:
            print(f"{TAG} self-check: {source!r}\n  got  {got!r}\n  want {want!r}")
            failures += 1
    print(f"{TAG} self-check {'OK' if failures == 0 else 'FAIL'}")
    return ExitCode.PASSED if failures == 0 else ExitCode.VIOLATION


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts delegate-macros", description="델리게이트의 맨 create 를 SW_DELEGATE_* 매크로로 바꾼다")
    parser.add_argument("paths", nargs="*", help="볼 경로(저장소 기준, 없으면 Source)")
    parser.add_argument("--check", action="store_true", help="바꾸지 않고 남은 자리를 찍는다 — 있으면 실패")
    parser.add_argument("--self-check", action="store_true", help="변환 자체 테스트")
    args = parser.parse_args(argv)
    if args.self_check:
        return run_self_check()
    project_root = get_project_root()
    names = load_delegate_names(str(project_root))
    counts: dict[str, int] = {}
    list_problem: list[str] = []
    changed_files = 0
    for path in list_files(project_root, args.paths):
        with path.open(encoding="utf-8", newline="") as file:
            raw = file.read()
        relative = path.relative_to(project_root).as_posix()
        if args.check:
            sites = find_sites(raw, strip_code(raw), names)
            new_raw = raw
        else:
            new_raw, sites = rewrite_text(raw, names)
        for site in sites:
            if site.replacement is None or args.check:
                list_problem.append(f"{relative}:{site.line}: {site.problem or '델리게이트는 SW_DELEGATE_' + site.kind + ' 로 붙인다'}")
            else:
                counts[site.kind] = counts.get(site.kind, 0) + 1
        if new_raw != raw:
            with path.open("w", encoding="utf-8", newline="") as file:
                file.write(new_raw)
            changed_files += 1
    for line in list_problem:
        print(line)
    if not args.check:
        print(f"{TAG} 바꾼 파일 {changed_files} · " + " · ".join(f"{kind} {count}" for kind, count in sorted(counts.items())))
    print(f"{TAG} 남은 자리 {len(list_problem)}")
    return ExitCode.VIOLATION if list_problem else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
