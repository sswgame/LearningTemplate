#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/platform_chains.py

플랫폼 `#if` 사슬의 모양을 본다.

**왜 린트인가.** 전처리 갈래는 형식으로 막을 수 없다. `#if Windows #else` 는 "Windows 가 아니면 POSIX" 를 가정하고, `#if Windows #elif Linux` 뒤에 `#else` 가 없으면 새 플랫폼에서 몸이 조용히
비어, 새 플랫폼이 컴파일은 되고 틀린 가지를 타며 찾는 길이 런타임 실패뿐이다(2026-11-07 조사 — 92 + 24 곳). 언리얼 `PLATFORM_*` 처럼 빠진 플랫폼은 컴파일 오류로 낸다.

규칙:
1. 첫 조건이 플랫폼 매크로뿐인 사슬에 갈래가 둘 이상이면 끝은 `#else` + `#error` 다. 몸이 하나뿐인 `#if`(한 플랫폼만의 include · 파일 전체 가드)는 괜찮다.
2. `!defined( SW_PLATFORM_* )` 를 쓰지 않는다. 부정은 새 플랫폼까지 삼킨다. Windows 가 아닌 곳은 `SW_PLATFORM_POSIX` 다.

첫 조건에 다른 매크로가 섞인 사슬(`SW_PLATFORM_LINUX && SW_WITH_WAYLAND`, 내장 함수 고르기)은 기능 검사라 보지 않는다.
예외는 `Config/Rules/Lint/PlatformChains.json` 에 이유와 함께 적는다(데스크톱 전용 폴더 · 플랫폼 폴더 · 매크로 검사 파일).

CMake(`*.cmake` · `CMakeLists.txt`, ThirdParty 제외)도 같은 규칙이다 — `if(WIN32) … else()` 의 `else()` 가 몸을 가지면 Android · Emscripten 이 Linux 가지를
조용히 탄다. 플랫폼 검사(`WIN32` · `UNIX` · `CMAKE_SYSTEM_NAME STREQUAL` · `sw_platform_name STREQUAL` · 호스트 `CMAKE_HOST_*`)만으로 된 첫 조건의
사슬은 `else()` + `message(FATAL_ERROR …)` 로 끝나고, 부정(`NOT WIN32`)만으로 된 조건은 `return()` · `FATAL_ERROR` 만 거는 가드일 때만 쓴다.
POSIX 계열은 `UNIX` · `CMAKE_HOST_UNIX` 로 이름을 적는다. 다른 변수가 섞인 조건(`WIN32 AND NOT SW_SHIPPING_BUILD`)은 기능 검사라 보지 않는다.

스크립트(`Scripts/**.py` · `.claude/hooks/*.py`)는 git 을 `common/git.py` 입구로만, 셸(`Scripts/**.sh`)은 `common/git.sh` 의 sw_git 으로만 띄운다(물려받은
저장소 환경 변수를 뺀다). CMake 는 git 을 띄우지 않는다(`execute_process` · `find_package(Git)` — 파이썬 단계로).
스크립트는 호스트 OS 를 `common/host_platform.py` 의 표에서만 읽는다 — `sys.platform` · `os.name` · `platform.system()` 을
다른 곳에서 물으면 `else` 가 "Windows 아니면 Linux" 가 된다. 표는 모르는 호스트에서 들여올 때 멈춘다.

`py -3 -m Scripts lint platform-chains [--self-check]` 로 돌리고, 커밋 훅(`rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.exit_code import ExitCode  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from lint.lint_scope import LintScope, make_whole_scope, read_texts  # noqa: E402

CONFIG_PATH = "Config/Rules/Lint/PlatformChains.json"
EXTENSIONS = (".h", ".cpp", ".inl")
DIRECTIVE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")
PLATFORM_ONLY = re.compile(r"^!?defined\( SW_PLATFORM_\w+ \)(?: \|\| !?defined\( SW_PLATFORM_\w+ \))*$")
NEGATED = re.compile(r"!\s*defined\(\s*SW_PLATFORM_\w+\s*\)")


def find_problems_in_text(path: str, text: str) -> list[str]:
    """글 하나의 사슬 모양 위반을 돌려준다."""
    list_problem: list[str] = []
    lines = text.split("\n")
    stack: list[list[tuple[str, str, int]]] = []
    for line_number, line in enumerate(lines, 1):
        match = DIRECTIVE.match(line)
        if match is None:
            continue
        keyword = match.group(1)
        condition = match.group(2).split("//")[0].strip()
        if keyword in ("if", "ifdef", "ifndef", "elif") and NEGATED.search(condition):
            list_problem.append(f"{path}:{line_number}: `!defined( SW_PLATFORM_* )` 는 새 플랫폼까지 삼킨다 — SW_PLATFORM_POSIX 나 플랫폼을 이름으로 적는다")
        if keyword in ("if", "ifdef", "ifndef"):
            stack.append([(keyword, condition, line_number)])
        elif keyword in ("elif", "else"):
            if stack:
                stack[-1].append((keyword, condition, line_number))
        elif stack:
            chain = stack.pop()
            if len(chain) < 2 or PLATFORM_ONLY.match(chain[0][1]) is None:
                continue
            last = chain[-1]
            if last[0] != "else":
                list_problem.append(f"{path}:{chain[0][2]}: 플랫폼 사슬에 `#else` 가 없어 새 플랫폼에서 몸이 조용히 빈다 — `#else` + `#error` 로 끝낸다")
            elif not any(body.lstrip().startswith("#error") for body in lines[last[2]:line_number - 1]):
                list_problem.append(f"{path}:{last[2]}: 플랫폼 사슬의 `#else` 가 몸을 가진다(남은 플랫폼을 가정한다) — `#elif defined( SW_PLATFORM_POSIX )` 처럼 이름으로 적고 `#else` + `#error` 로 끝낸다")
    return list_problem


def is_exempt(path: str, table: dict) -> bool:
    if any(path.startswith(prefix) for prefix in table["_exemptPaths"]):
        return True
    return any(part in table["_platformFolders"] for part in path.split("/")[:-1])


def find_problems(list_source: Iterable[tuple[str, str]], table: dict) -> list[str]:
    list_problem: list[str] = []
    for path, text in list_source:
        if not is_exempt(path, table):
            list_problem.extend(find_problems_in_text(path, text))
    return list_problem


def read_sources(project_root: Path, scope: LintScope) -> list[tuple[str, str]]:
    return [(path, text) for path, text in read_texts(project_root, scope.select(("Source",), EXTENSIONS)) if "SW_PLATFORM_" in text]


# 여러 줄에 걸친 `if(` 도 사슬의 짝을 맞추려고 여는 줄만 본다 — 닫는 괄호가 없는 조건은 플랫폼 검사로 읽히지 않아 건너뛴다.
CMAKE_DIRECTIVE = re.compile(r"^\s*(if|elseif|else|endif)\s*\((?:(.*)\)\s*(?:#.*)?$|(.*)$)", re.I)
CMAKE_PLATFORM_ATOM = re.compile(
    r'^(?:NOT\s+)?(?:WIN32|UNIX|APPLE|ANDROID|EMSCRIPTEN|CMAKE_HOST_WIN32|CMAKE_HOST_UNIX|CMAKE_HOST_APPLE'
    r'|(?:CMAKE_SYSTEM_NAME|CMAKE_HOST_SYSTEM_NAME|sw_platform_name)\s+STREQUAL\s+"\w*")$')
CMAKE_GUARD_BODY = re.compile(r"^\s*(?:return\(\)|message\(\s*FATAL_ERROR\b.*|#.*|)\s*$")
CMAKE_FATAL = re.compile(r"^\s*message\(\s*FATAL_ERROR\b")
PYTHON_HOST_QUERY = re.compile(r"\bsys\.platform\b|\bos\.name\b|\bplatform\.system\(\)")
PYTHON_HOST_TABLE = "Scripts/common/host_platform.py"
# git 을 자식 프로세스로 직접 띄우는 줄(리스트 인자 · 셸 글) — 입구 `common/git.py`(run_git · run_git_in_hook_repository · run_git_bytes)만 띄운다.
PYTHON_DIRECT_GIT = re.compile(r"\b(?:subprocess\.(?:run|call|check_call|check_output|Popen)|Popen|shell)\s*\(\s*(?:\[\s*(?:[\"']git[\"']|gitExe\b|resolveGitExecutable\b)|f?[\"'][^\"']*\bgit\s)")
PYTHON_GIT_ENTRANCE = "Scripts/common/git.py"
# 셸: 명령 자리(줄 머리 · `;` `&` `|` `(` `$(` 뒤)의 `git` — 입구 `Scripts/common/git.sh` 의 sw_git 만 부른다.
SHELL_DIRECT_GIT = re.compile(r"(?:^|[;&|(]|\$\()\s*(?:command\s+)?git\s")
SHELL_GIT_ENTRANCE = "Scripts/common/git.sh"
# CMake: 구성 중 git 을 띄우는 자리(`execute_process` · `add_custom_command` · `add_custom_target` 의 git · GIT_EXECUTABLE, find_package(Git)).
# 지금 CMake 는 git 을 띄우지 않는다 — 필요해지면 환경을 비우는 함수 하나를 cmake 쪽에 두고 이 예외에 그 파일을 적는다.
CMAKE_DIRECT_GIT = re.compile(r"\b(?:execute_process|add_custom_command|add_custom_target)\s*\((?:[^()]|\([^()]*\))*?\b(?:git|GIT_EXECUTABLE)\b|\bfind_package\s*\(\s*Git\b", re.I | re.S)
GIT_HINT = "물려받은 GIT_DIR · GIT_INDEX_FILE 을 빼는 입구를 거친다 — 커밋 훅이 넘긴 환경을 물려받으면 다른 저장소를 가리킨 git 이 훅의 저장소를 건드린다"


def is_cmake_platform_only(condition: str) -> bool:
    """`OR` 로 이은 원자가 모두 플랫폼 검사면 True 다(다른 변수가 섞이면 기능 검사라 보지 않는다)."""
    list_atom = [atom.strip() for atom in re.split(r"\s+OR\s+", condition.strip())]
    return bool(list_atom) and all(CMAKE_PLATFORM_ATOM.match(atom) for atom in list_atom)


def find_cmake_problems_in_text(path: str, text: str) -> list[str]:
    """CMake 글 하나의 플랫폼 사슬 위반을 돌려준다."""
    list_problem: list[str] = []
    lines = text.split("\n")
    stack: list[list[tuple[str, str, int]]] = []
    for line_number, line in enumerate(lines, 1):
        match = CMAKE_DIRECTIVE.match(line)
        if match is None:
            continue
        keyword = match.group(1).lower()
        condition = match.group(2) if match.group(2) is not None else f"<multi-line> {match.group(3)}"
        if keyword == "if":
            stack.append([(keyword, condition, line_number)])
        elif keyword in ("elseif", "else"):
            if stack:
                stack[-1].append((keyword, condition, line_number))
        elif stack:
            chain = stack.pop()
            first = chain[0]
            if not is_cmake_platform_only(first[1]):
                continue
            if first[1].strip().startswith("NOT") and len(chain) == 1:
                if not all(CMAKE_GUARD_BODY.match(body) for body in lines[first[2]:line_number - 1]):
                    list_problem.append(f"{path}:{first[2]}: 플랫폼 부정(`if(NOT WIN32)`)이 몸을 가진다(남은 플랫폼을 가정한다) — 플랫폼을 이름으로 적고 `else()` + `message(FATAL_ERROR …)` 로 끝낸다")
                continue
            if len(chain) < 2:
                continue
            last = chain[-1]
            if last[0] != "else":
                list_problem.append(f"{path}:{first[2]}: 플랫폼 사슬에 `else()` 가 없어 새 플랫폼에서 몸이 조용히 빈다 — `else()` + `message(FATAL_ERROR …)` 로 끝낸다")
            elif not any(CMAKE_FATAL.match(body) for body in lines[last[2]:line_number - 1]):
                list_problem.append(f"{path}:{last[2]}: 플랫폼 사슬의 `else()` 가 몸을 가진다(남은 플랫폼을 가정한다) — `elseif(UNIX)` 처럼 이름으로 적고 `else()` + `message(FATAL_ERROR …)` 로 끝낸다")
    return list_problem


def find_python_problems_in_text(path: str, text: str) -> list[str]:
    """스크립트 하나가 호스트 OS 를 표 밖에서 묻는 줄을 돌려준다."""
    if path == "Scripts/lint/platform_chains.py":   # 이 린트(질의 낱말을 글로 든다)
        return []
    list_problem: list[str] = []
    for line_number, line in enumerate(text.split("\n"), 1):
        code = line.split("#")[0]
        if path != PYTHON_HOST_TABLE and PYTHON_HOST_QUERY.search(code):
            list_problem.append(f"{path}:{line_number}: 호스트 OS 를 직접 묻는다 — `common/host_platform.py`(isWindowsHost · kHost · executableName)를 쓴다")
        if path != PYTHON_GIT_ENTRANCE and PYTHON_DIRECT_GIT.search(code):
            list_problem.append(f"{path}:{line_number}: git 을 직접 띄운다 — `common/git.py` 의 runGit(물려받은 GIT_DIR · GIT_INDEX_FILE 을 뺀다)을 쓴다. 커밋 훅이 넘긴"
                               " 환경을 물려받으면 다른 저장소를 가리킨 명령이 훅의 저장소를 건드린다")
    return list_problem


def read_build_sources(project_root: Path, scope: LintScope) -> list[tuple[str, str]]:
    """CMake · 스크립트 글(뿌리 CMakeLists.txt, cmake/*.cmake, Source 의 CMakeLists.txt, Scripts 의 .py · .sh, .claude/hooks 의 .py)."""
    list_path = [path for path in scope.list_file
                if path == "CMakeLists.txt" or (path.startswith("cmake/") and path.endswith(".cmake"))
                or (path.startswith("Source/") and path.endswith("/CMakeLists.txt"))
                or (path.startswith(("Scripts/", ".claude/hooks/")) and path.endswith(".py") and "/__pycache__/" not in path)
                or (path.startswith("Scripts/") and path.endswith(".sh"))]
    return read_texts(project_root, list_path)


def find_shell_problems_in_text(path: str, text: str) -> list[str]:
    """셸 스크립트 하나가 git 을 입구 밖에서 띄우는 줄."""
    if path == SHELL_GIT_ENTRANCE:
        return []
    return [f"{path}:{line_number}: git 을 직접 띄운다 — `source Scripts/common/git.sh` 뒤 sw_git 으로({GIT_HINT})"
            for line_number, line in enumerate(text.split("\n"), 1) if SHELL_DIRECT_GIT.search(line.split("#")[0])]


def find_cmake_git_problems_in_text(path: str, text: str) -> list[str]:
    """CMake 글 하나가 git 을 띄우는 자리."""
    code = "\n".join(line.split("#")[0] for line in text.split("\n"))
    return [f"{path}:{code.count(chr(10), 0, match.start()) + 1}: CMake 가 git 을 띄운다 — 파이썬 Scripts 의 runGit 을 부르는 단계로 옮긴다({GIT_HINT})"
            for match in CMAKE_DIRECT_GIT.finditer(code)]


def find_build_problems(list_source: Iterable[tuple[str, str]]) -> list[str]:
    list_problem: list[str] = []
    for path, text in list_source:
        if path.endswith(".py"):
            list_problem.extend(find_python_problems_in_text(path, text))
        elif path.endswith(".sh"):
            list_problem.extend(find_shell_problems_in_text(path, text))
        else:
            list_problem.extend(find_cmake_problems_in_text(path, text) + find_cmake_git_problems_in_text(path, text))
    return list_problem


def check_platform_chains(project_root: Path, scope: LintScope) -> list[str]:
    table = json.loads((project_root / CONFIG_PATH).read_text(encoding="utf-8"))
    return find_problems(read_sources(project_root, scope), table) + find_build_problems(read_build_sources(project_root, scope))


def run_self_check() -> None:
    table = {"_exemptPaths": {"Source/Editor/": "이유"}, "_platformFolders": ["Linux"]}
    error = '#else\n    #error "Unsupported platform"\n#endif\n'
    clean = (
        "#if defined( SW_PLATFORM_WINDOWS )\nint a;\n#endif\n"
        + "#if defined( SW_PLATFORM_WINDOWS )\nint a;\n#elif defined( SW_PLATFORM_POSIX )\nint b;\n" + error
        + "#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_WAYLAND )\nint a;\n#else\nint b;\n#endif\n"
        + "#if defined( SW_DEBUG )\nint a;\n#else\nint b;\n#endif\n"
    )
    assert find_problems([("Source/a.cpp", clean)], table) == [], "허용 모양은 통과해야 한다"
    assert len(find_problems([("Source/a.cpp", "#if defined( SW_PLATFORM_WINDOWS )\nint a;\n#else\nint b;\n#endif\n")], table)) == 1, "#else 몸"
    assert len(find_problems([("Source/a.cpp", "#if defined( SW_PLATFORM_WINDOWS )\nint a;\n#elif defined( SW_PLATFORM_LINUX )\nint b;\n#endif\n")], table)) == 1, "#else 없음"
    assert len(find_problems([("Source/a.cpp", "#if !defined( SW_PLATFORM_WINDOWS )\nint a;\n#endif\n")], table)) == 1, "부정"
    bad = "#if defined( SW_PLATFORM_WINDOWS )\nint a;\n#else\nint b;\n#endif\n"
    assert find_problems([("Source/Editor/a.cpp", bad), ("Source/Engine/Linux/a.cpp", bad)], table) == [], "예외 폴더"
    fatal = 'else()\n\tmessage(FATAL_ERROR "x")\nendif()\n'
    clean_cmake = (
        "if(WIN32)\n\tset(a 1)\nendif()\n"
        + "if(WIN32)\n\tset(a 1)\nelseif(UNIX)\n\tset(a 2)\n" + fatal
        + "if(NOT WIN32)\n\treturn()\nendif()\n"
        + "if(WIN32 AND NOT SW_SHIPPING_BUILD)\n\tset(a 1)\nelse()\n\tset(a 2)\nendif()\n"
        + 'if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")\n\treturn()\nendif()\n'
    )
    assert find_build_problems([("cmake/a.cmake", clean_cmake)]) == [], "CMake 허용 모양은 통과해야 한다"
    assert len(find_build_problems([("cmake/a.cmake", "if(WIN32)\n\tset(a 1)\nelse()\n\tset(a 2)\nendif()\n")])) == 1, "CMake else 몸"
    assert len(find_build_problems([("cmake/a.cmake", 'if(WIN32)\n\tset(a 1)\nelseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")\n\tset(a 2)\nendif()\n')])) == 1, "CMake else 없음"
    assert len(find_build_problems([("cmake/a.cmake", "if(NOT WIN32)\n\tset(a 1)\nendif()\n")])) == 1, "CMake 부정 몸"
    assert len(find_build_problems([("Scripts/a.py", 'x = sys.platform == "win32"\n')])) == 1, "스크립트 직접 질의"
    assert find_build_problems([(PYTHON_HOST_TABLE, "platform.system()\n")]) == [], "호스트 표 자체"
    assert len(find_build_problems([("Scripts/a.py", "subprocess.run(['git', 'mv', a, b], cwd=root)\n")])) == 1, "git 직접 띄우기"
    assert len(find_build_problems([("Scripts/a.py", "x = subprocess.check_output([\"git\", \"rev-parse\"])\n")])) == 1, "git 직접 띄우기(큰따옴표)"
    assert len(find_build_problems([("Scripts/a.py", "shell(f'cd {CLONE} && git fetch -q')\n")])) == 1, "셸 글 안의 git"
    assert len(find_build_problems([("Scripts/a.py", "shell(f'git -C {CLONE} log')\n")])) == 1, "셸 글 git"
    assert find_build_problems([(PYTHON_GIT_ENTRANCE, "subprocess.run([gitExe, 'status'])\n")]) == [], "입구 자체"
    assert find_build_problems([("Scripts/a.py", "runGit(['mv', a, b], cwd=root)\n")]) == [], "입구를 거친 git"
    assert len(find_build_problems([("Scripts/a.sh", "SHA=$(git rev-parse HEAD)\n")])) == 1, "셸 $( git"
    assert len(find_build_problems([("Scripts/a.sh", "cd x && git status\n")])) == 1, "셸 && git"
    assert find_build_problems([("Scripts/a.sh", "source Scripts/common/git.sh\nSHA=$(sw_git rev-parse HEAD)\n# git push\necho \"use .gitignore\"\n")]) == [], "셸 입구 · 주석 · 낱말 안 git"
    assert find_build_problems([(SHELL_GIT_ENTRANCE, "git \"$@\"\n")]) == [], "셸 입구 자체"
    assert len(find_build_problems([("cmake/a.cmake", "execute_process(\n\tCOMMAND ${GIT_EXECUTABLE} rev-parse HEAD\n\tOUTPUT_VARIABLE x)\n")])) == 1, "CMake execute_process git"
    assert len(find_build_problems([("cmake/a.cmake", "find_package(Git REQUIRED)\n")])) == 1, "CMake find_package(Git)"
    assert find_build_problems([("cmake/a.cmake", "# execute_process(COMMAND git status)\nexecute_process(COMMAND python x.py)\n")]) == [], "CMake 주석 · git 없는 execute_process"
    assert len(find_build_problems([(".claude/hooks/a.py", "subprocess.run(['git', 'status'])\n")])) == 1, "훅의 git 직접 띄우기"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="플랫폼 #if 사슬이 빠진 플랫폼을 컴파일 오류로 내는지 본다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        run_self_check()
        print("[PlatformChains] self-check OK")
        return ExitCode.PASSED
    list_problem = check_platform_chains(get_project_root(), make_whole_scope(get_project_root()))
    for problem in list_problem:
        print(f"  - {problem}")
    print(f"[PlatformChains] {'FAIL' if list_problem else 'OK'} ({len(list_problem)}건)")
    return ExitCode.VIOLATION if list_problem else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
