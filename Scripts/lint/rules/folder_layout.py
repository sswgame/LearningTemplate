"""
Scripts/lint/rules/folder_layout.py

파일 자리 규칙(`docs/agents/FolderLayout.md` 4 절)을 경로로 막는다 — 표는 `Config/Rules/Lint/FolderLayout.json`.
  1. 잡동사니 폴더 이름(`Util` · `Utility` · `Helpers` · `Misc` …) 금지 — 하는 일(Encoding · Algorithm · Hash …)로 묶는다.
  2. 기능 폴더의 `Component/` 에는 컴포넌트(`*Component` · 허용 끝말)만 — 씬 서비스 · 질의 · 검사는 `<기능>/Scene/`.
  3. 잡동사니 파일 줄기(`_bannedFileStems` — 끝 낱말이 `Types` · `Support` · `Defines` · `Helpers` · `Misc`) 금지 — 파일 이름은 내용으로. 온라인 키트의 `<Kit>Types.h` 만 뼈대가 짓는 자리다.
  4. 파일 이름 = 주 형식: `X.h` 는 형식 · 이름공간 · 별칭 `X` 를 선언하고(인터페이스는 `IX.h`), 짝 헤더 없는 `X.cpp` 는 `X`(또는 `X` 의 앞머리 형식)의 멤버를 정의하거나
     진입점(`main` · 테스트 케이스)이다. `*Fwd.h` · `*Macros.h` · `pch.h` · 생성 파일 · `<Kit>Types.h` 는 뺀다. 여러 형식을 일부러 모은 헤더는 `_multiTypeHeaders` 에 까닭과 함께.
     파일 이름으로 형식을 찾는다(언리얼 · 유니티도 형식 하나에 파일 하나) — `ParserDefines` · `AppearanceTypes` 처럼 이름이 내용을 말하지 못한 파일이 쌓였다(2026-11-27 이름 점검).

왜 린트인가. 폴더 자리는 형식이 말하지 못한다. 기준표 없이 "처음 쓰는 쪽" · 이름 낱말로 자리를 정해 RegistrationList(String 폴더) · StringUtil(Container) 같은
어긋남이 쌓였다(FolderMoves 13 절, 2026-11-08 전수 조사). 1 · 2 · 3 은 경로만, 4 는 헤더 · .cpp 글을 본다.
3 · 4 의 지금 위반은 기준선(`Config/Rules/Lint/FolderLayoutBaseline.json`, 파일 → 종류 → 수)에 두고 새 파일만 멈춘다. 줄면 `--ratchet`, 올리는 것은 통합의 `--accept` 뿐이다.
`Dev/` 폴더 자리는 린트가 아니라 configure 가 막는다(`cmake/Modules/Build/ExcludedSources.cmake`, 표 `Config/Rules/Layers/DevFolders.json`) — 배포가 빼는 개발 폴더 목록과 한 표다.
"""

from __future__ import annotations

import functools
import re
from typing import Mapping, Sequence

from lint.lint_rule import LintRule, SourceFile, inspect_files
from lint.lint_scope import LintScope, make_file_scope, make_whole_scope, read_staged_renames
from lint.name_collection import split_words
from lint.rule_tables import Baseline, ExceptionTable

RATCHET_COMMAND = "py -3 -m Scripts lint folder-layout --ratchet"
BASELINE_COMMENT = ("folder-layout 의 기준선 — 파일 → 종류(main-type: 파일 이름 = 주 형식, stem:<낱말>: 잡동사니 파일 줄기) → 수. 새 파일만 멈춘다. "
                    "줄면 `py -3 -m Scripts lint folder-layout --ratchet`, 올리는 것은 통합의 `--accept <파일 · 폴더>` 뿐이다(손으로 고치지 않는다).")
CODE_SUFFIXES = (".h", ".cpp")
MAIN_TYPE_SKIP_SUFFIXES = ("Fwd", "Macros")
ONLINE_KIT_ROOT = "Source/GameFramework/Kits/Feature/Online/"
ENTRY_POINT = re.compile(r"\b(?:w?main|WinMain|wWinMain|android_main)\s*\(|\bSW_TEST_CASE(?:_\w+)?\s*\(")


def is_online_kit_types(path: str) -> bool:
    """온라인 키트 뼈대가 짓는 `<Kit>/…/<Kit>Types.h` 인가."""
    if not path.startswith(ONLINE_KIT_ROOT):
        return False
    kit = path[len(ONLINE_KIT_ROOT):].split("/")[0]
    return get_stem(path) == f"{kit}Types"


def get_stem(path: str) -> str:
    return path.rsplit("/", 1)[-1].split(".")[0]


def find_banned_stem(path: str, banned: Mapping[str, str]) -> str | None:
    """끝 낱말이 금지 줄기인 파일 이름이면 그 낱말."""
    words = split_words(get_stem(path))
    if words and words[-1] in banned and not is_online_kit_types(path):
        return words[-1]
    return None


def is_main_type_target(path: str) -> bool:
    """파일 이름 = 주 형식 판정을 받는 파일인가 — Source 의 .h · .cpp(생성 · pch · Fwd · Macros · 온라인 키트 Types 제외)."""
    name = path.rsplit("/", 1)[-1]
    stem = get_stem(path)
    return (path.startswith("Source/") and path.endswith(CODE_SUFFIXES) and name.count(".") == 1 and stem != "pch"
            and not stem.endswith(MAIN_TYPE_SKIP_SUFFIXES) and not is_online_kit_types(path))


def declares_main_type(stem: str, code: str) -> bool:
    """헤더 글이 형식 · 이름공간 · 별칭 @p stem 을 선언하는가."""
    name = re.escape(stem)
    return re.search(rf"\b(?:class|struct|union|enum(?:\s+class|\s+struct)?|namespace)\s+(?:(?:SW_\w+|alignas\s*\([^)]*\)|\[\[[^\]]*\]\])\s+)*(?:\w+::)*{name}\b"
                     rf"|\busing\s+{name}\s*=|\btypedef\b[^;]*\b{name}\s*;", code) is not None


def defines_owner_members(stem: str, code: str) -> bool:
    """짝 헤더 없는 .cpp 가 형식 @p stem 을 정의하거나, 그 앞머리 · 꼬리 형식(`CodeGenerator` ← `CodeGeneratorProperty.cpp`, `Process` ← `PosixProcess.cpp`)의 멤버를 정의하거나, 진입점인가."""
    if ENTRY_POINT.search(code) or declares_main_type(stem, code):
        return True
    words = split_words(stem)
    set_owner = {"".join(words[:count]) for count in range(1, len(words) + 1)} | {"".join(words[count:]) for count in range(len(words))}
    return any(re.search(rf"\b{re.escape(owner)}::", code) for owner in set_owner)


def find_main_type_problem(path: str, code: str, has_header: bool, registration_suffixes: Sequence[str] = ()) -> str | None:
    stem = get_stem(path)
    if stem.endswith(tuple(registration_suffixes)):   # 개발 전용 등록 파일(`<X>Watches` · `<X>DevCommands`)은 등록하는 것으로 이름 짓는다
        return None
    if path.endswith(".h") and not declares_main_type(stem, code):
        return f"{path}: 헤더 이름 '{stem}' 와 같은 형식 · 이름공간을 선언하지 않는다 — 파일을 주 형식 이름으로 바꾸거나 형식마다 파일을 나눈다(FolderLayout 4 절)"
    if path.endswith(".cpp") and not has_header and not defines_owner_members(stem, code):
        return f"{path}: 짝 헤더가 없는데 '{stem}'(또는 그 앞머리) 형식의 멤버도 진입점도 없다 — 정의하는 형식 이름으로 바꾼다(FolderLayout 4 절)"
    return None


def find_problems(list_path: list[str], table: dict) -> list[str]:
    banned = {name.lower(): reason for name, reason in table["_bannedFolderNames"].items()}
    component_roots = tuple(table["_componentFolderModel"])
    component_suffixes = tuple(table["_componentSuffixes"])
    list_problem: list[str] = []
    seen_folder: set[str] = set()
    for relative in list_path:
        parts = relative.split("/")
        for index, folder in enumerate(parts[:-1]):
            prefix = "/".join(parts[: index + 1]) + "/"
            if prefix in seen_folder:
                continue
            if folder.lower() in banned:
                seen_folder.add(prefix)
                list_problem.append(f"{prefix}: 잡동사니 폴더 '{folder}' — {banned[folder.lower()]}")
        if "Component" in parts[:-1] and "Tests" not in parts and not relative.startswith(component_roots):
            stem = parts[-1].split(".")[0]
            if not stem.endswith(component_suffixes):
                list_problem.append(f"{relative}: `Component/` 에는 컴포넌트만 — 씬 서비스 · 질의 · 검사는 `<기능>/Scene/`(FolderLayout 4 절)")
    return list_problem


def compare_rows(found: Mapping[str, list[tuple[str, str]]], baseline: Baseline, list_path: Sequence[str], map_old: Mapping[str, str]) -> list[str]:
    """(종류, 안내) 를 파일마다 기준선과 견준다 — 늘면 그 종류의 안내, 줄면 래칫 안내."""
    list_problem: list[str] = []
    for path in sorted(set(list_path)):
        rows = found.get(path, [])
        limits = baseline.get_limits(path, map_old)
        for kind in sorted({kind for kind, _ in rows} | set(limits)):
            hints = [hint for row_kind, hint in rows if row_kind == kind]
            if len(hints) > limits.get(kind, 0):
                list_problem += hints
            elif len(hints) < limits.get(kind, 0):
                list_problem.append(f"{path}: {kind} {limits[kind]} -> {len(hints)} — 줄었다: `{RATCHET_COMMAND}` 로 기준선을 낮추고 함께 커밋한다")
    return list_problem


def count_rows(found: Mapping[str, list[tuple[str, str]]]) -> dict[str, dict[str, int]]:
    map_count: dict[str, dict[str, int]] = {}
    for path, rows in found.items():
        for kind, _ in rows:
            map_count.setdefault(path, {}).setdefault(kind, 0)
            map_count[path][kind] += 1
    return map_count


class FolderLayoutRule(LintRule):
    id = "folder-layout"
    group = "경계"
    blocks = "잡동사니 폴더 · Component 폴더 · 잡동사니 파일 줄기 · 파일 이름 = 주 형식"
    why_not_type = "파일 자리 · 이름은 형식이 아니다"
    tables = ("", "Baseline")

    def selects(self, path: str) -> bool:
        return path.startswith("Source/") and path.endswith((*CODE_SUFFIXES, ".inl"))

    def inspect(self, file: SourceFile) -> list[tuple[str, str]]:
        """(기준선 종류, 안내) — 잡동사니 줄기와 주 형식."""
        table = self.table
        rows: list[tuple[str, str]] = []
        stem_word = find_banned_stem(file.path, table["_bannedFileStems"])
        if stem_word is not None:
            rows.append((f"stem:{stem_word}", f"{file.path}: 파일 줄기 끝 '{stem_word}' 는 잡동사니 이름이다 — {table['_bannedFileStems'][stem_word]}"))
        if is_main_type_target(file.path) and file.path not in table["_multiTypeHeaders"]:
            has_header = (self.root / (file.path[:-len(".cpp")] + ".h")).is_file() if file.path.endswith(".cpp") else True
            problem = find_main_type_problem(file.path, file.code_with_macros, has_header, table["_registrationFileSuffixes"])
            if problem is not None:
                rows.append(("main-type", problem))
        return rows

    @functools.cached_property
    def table(self) -> dict:
        return self.read_table()

    def evaluate(self, scope: LintScope, found: Mapping[str, list[tuple[str, str]]]) -> list[str]:
        table = self.read_table()
        exceptions = ExceptionTable(self.get_table(), table, ("_multiTypeHeaders",))
        for path in exceptions.get("_multiTypeHeaders"):
            if path in scope.list_file:
                exceptions.mark_used("_multiTypeHeaders", path)
        baseline = Baseline.load(self.root, self.id)
        list_path = [path for path in scope.list_file if self.selects(path)]
        map_old = read_staged_renames(self.root) if not scope.whole_repo and list_path else {}
        list_problem = find_problems(scope.select(("Source",)), table) + compare_rows(found, baseline, list_path, map_old)
        if scope.whole_repo:
            list_problem += baseline.list_missing(set(scope.list_file))
        return list_problem + exceptions.list_problems(scope.whole_repo)

    def update_baseline(self, list_accept: Sequence[str] | None) -> None:
        scope = make_whole_scope(self.root) if list_accept is None else make_file_scope(self.root, list_accept)
        found, _ = inspect_files(self.root, {self: scope})
        Baseline.load(self.root, self.id, BASELINE_COMMENT).update(self.root, count_rows(found[self.id]), list_accept)

    def self_check(self) -> None:
        table = {"_bannedFolderNames": {"Utils": "기능 이름으로"}, "_componentFolderModel": ["Source/GameFramework/Runtime/"], "_componentSuffixes": ["Component"]}
        list_problem = find_problems(["Source/Engine/Utils/A.h", "Source/Engine/utils/B.h", "Source/Engine/X/Component/FooComponent.h",
                                    "Source/Engine/X/Component/FooQuery.h", "Source/GameFramework/Runtime/Component/Any.h", "Source/Engine/X/Component/Tests/T.cpp"], table)
        assert list_problem == ["Source/Engine/Utils/: 잡동사니 폴더 'Utils' — 기능 이름으로", "Source/Engine/utils/: 잡동사니 폴더 'utils' — 기능 이름으로",
                               "Source/Engine/X/Component/FooQuery.h: `Component/` 에는 컴포넌트만 — 씬 서비스 · 질의 · 검사는 `<기능>/Scene/`(FolderLayout 4 절)"], list_problem

        banned = {"Types": "내용으로", "Defines": "내용으로"}
        assert find_banned_stem("Source/Engine/Gameplay/AppearanceTypes.h", banned) == "Types"   # 양성
        assert find_banned_stem("Source/GameFramework/Kits/Feature/Online/Chat/Runtime/ChatTypes.h", banned) is None   # 음성: 온라인 키트 뼈대 자리
        assert find_banned_stem("Source/Engine/Gameplay/TypeRegistry.h", banned) is None   # 음성: 끝 낱말이 아니다

        assert find_main_type_problem("Source/A/Foo.h", "namespace sw { class SW_API Foo final { }; }", True) is None
        assert find_main_type_problem("Source/A/IFoo.h", "struct IFoo { virtual void run() = 0; };", True) is None
        assert find_main_type_problem("Source/A/FooMath.h", "namespace sw::FooMath { float f(); }", True) is None   # 이름공간도 주인이다
        assert find_main_type_problem("Source/A/ParserDefines.h", "namespace sw { constexpr int kA = 1; struct Annotation {}; }", True) is not None   # 양성
        assert find_main_type_problem("Source/A/Foo.h", "class FooBar {};", True) is not None   # 양성: 앞머리만 같은 이름
        assert find_main_type_problem("Source/A/CodeGeneratorProperty.cpp", "void CodeGenerator::emitProperty() {}", False) is None   # 앞머리 형식의 멤버
        assert find_main_type_problem("Source/A/TestFoo.cpp", "SW_TEST_CASE( Foo, Bar ) {}", False) is None   # 진입점
        assert find_main_type_problem("Source/A/Loose.cpp", "static void helper() {}", False) is not None   # 양성: 주인 없는 .cpp
        assert find_main_type_problem("Source/A/Loose.cpp", "static void helper() {}", True) is None   # 짝 헤더가 있으면 헤더가 본다
        assert find_main_type_problem("Source/A/Posix/PosixProcess.cpp", "int Process::run() { return 0; }", False) is None   # 플랫폼 구현은 꼬리 형식의 멤버
        assert find_main_type_problem("Source/A/Bend.cpp", "class Bend final : public Modifier {};", False) is None   # .cpp 안에서 정의한 형식
        assert find_main_type_problem("Source/A/FooWatches.cpp", "SW_WATCH( a );", False, ["Watches"]) is None   # 등록 파일
        assert not is_main_type_target("Source/A/FooFwd.h") and not is_main_type_target("Source/A/Foo.gen.h") and not is_main_type_target("Source/A/pch.h")

        baseline = Baseline("t.json", {"_comment": "t", "_files": {"Source/A/B.h": {"main-type": 1}}})
        found = {"Source/A/B.h": [("main-type", "b")], "Source/A/C.h": [("stem:Types", "c")]}
        assert compare_rows(found, baseline, ["Source/A/B.h", "Source/A/C.h"], {}) == ["c"]   # 기준선 안은 통과, 새 파일은 멈춘다
        assert "줄었다" in compare_rows({}, baseline, ["Source/A/B.h"], {})[0]
