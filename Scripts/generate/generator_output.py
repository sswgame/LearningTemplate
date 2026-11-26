"""
생성 파일을 쓰는 자리 — configure · 빌드가 부르는 생성기(`Scripts/generate/`)가 같이 쓴다.

- 바뀌었을 때만 쓴다. 같은 내용으로 다시 쓰면 파일 시각이 바뀌어 Ninja 가 그 헤더에 기대는 TU 를 다시 짓고(restat 이 막는 것은 같은 시각뿐이다),
  CMake 가 include 하는 파일이면 configure 가 다시 돈다. 비교는 읽은 글끼리(줄끝을 맞춘 뒤)다.
- 줄끝은 생성기가 고른다. 기본은 플랫폼 것(텍스트 모드 — Windows CRLF), `newline="\\n"` 이면 어디서나 LF(바이트를 해시하는 스탬프 · 고지).
- CMake 값은 이스케이프한다. 윈도우 경로(`C:\\Program Files`)를 그대로 넣으면 CMake 가 `\\P` 를 이스케이프로 읽어 조용히 다른 경로가 된다.
- 생성기의 기반 — configure 생성기는 `Generator`, 빌드 단계가 부르는 것은 `Command`(`common/command.py`)다.
"""

from __future__ import annotations

import abc
import contextlib
import importlib
import os
import sys
from pathlib import Path
from typing import Any, Iterator

from common.command import CommandError


def write_generated_file(path: Path, content: str, *, tag: str, summary: str = "", newline: str | None = None,
                       report_unchanged: bool = True, quiet: bool = False) -> bool:
    """
    `content`(줄끝 `\\n`)가 지금 파일과 다를 때만 씁니다. 썼으면 True.

    한 줄 찍는다: `[tag] Wrote|Up to date: <경로> (summary)` — `report_unchanged` 가 거짓이면 쓴 때만, `bQuiet` 면 찍지 않는다.
    """
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    previous = path.read_text(encoding="utf-8") if path.is_file() else None
    changed = previous != content
    if changed:
        with path.open("w", encoding="utf-8", newline=newline) as file:
            file.write(content)
    if not quiet and (changed or report_unchanged):
        suffix = f" ({summary})" if summary else ""
        print(f"[{tag}] {'Wrote' if changed else 'Up to date'}: {path}{suffix}")
    return changed


def to_cmake_value(value: Any) -> str:
    """파이썬 값을 따옴표 안에 넣을 CMake 문자열로. bool → TRUE/FALSE, 집합(정렬) · 목록 → `;` 리스트, 역슬래시 · 따옴표 이스케이프."""
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    if isinstance(value, (set, frozenset)):
        return ";".join(to_cmake_value(item) for item in sorted(value))
    if isinstance(value, (list, tuple)):
        return ";".join(to_cmake_value(item) for item in value)
    return str(value).replace("\\", "\\\\").replace('"', '\\"')


def format_cmake_set(name: str, value: Any) -> str:
    """`set(NAME "값")` 한 줄."""
    return f'set({name} "{to_cmake_value(value)}")'


@contextlib.contextmanager
def record_read_files(root: Path, output_dir: Path) -> Iterator[set[Path]]:
    """
    이 구간에서 `root` 아래 읽은 파일(데이터 · 생성기 모듈)을 모은다 — configure 생성기의 `.deps` 목록.

    CMake 가 이 목록을 CMAKE_CONFIGURE_DEPENDS 에 넣어 생성기 코드나 그것이 읽은 표가 바뀌면 configure 가 다시 돈다. 손으로 적은
    입력 목록은 새 입력을 빠뜨린다(층 표 · 매니페스트 · 생성기 .py). 읽기는 감사 훅의 `open` 사건으로 보고, 쓰는 출력 폴더는 뺀다.
    """
    root_prefix = os.path.normcase(str(root.resolve())) + os.sep
    output_prefix = os.path.normcase(str(output_dir.resolve())) + os.sep
    opened: set[str] = set()
    files: set[Path] = set()
    active = [True]

    # 훅은 열 때마다 불린다(출력 껍데기 6 만여 장을 읽어 견준다) — 여기서는 글자만 모으고 경로 풀이 · 파일 확인은 끝에 한 번씩.
    def _hook(event: str, args: tuple[Any, ...]) -> None:
        if event != "open" or not active[0] or not isinstance(args[0], (str, Path)):
            return
        mode = args[1] if len(args) > 1 and isinstance(args[1], str) else "r"
        if "w" in mode or "a" in mode or "x" in mode or "+" in mode:
            return
        opened.add(os.fspath(args[0]))

    sys.addaudithook(_hook)   # 감사 훅은 지울 수 없다 — 구간이 끝나면 깃발로 끈다
    try:
        yield files
    finally:
        active[0] = False
    # 이 구간 전에 import 한 생성기 모듈도 입력이다(코드가 바뀌면 출력이 바뀐다).
    opened.update(module.__file__ for module in list(sys.modules.values()) if getattr(module, "__file__", None))
    for text in opened:
        normalized = os.path.normcase(os.path.abspath(text))
        if normalized.startswith(root_prefix) and not normalized.startswith(output_prefix) and os.path.isfile(text):
            files.add(Path(os.path.abspath(text)))


def write_input_list(path: Path, files: set[Path], *, tag: str) -> bool:
    """`.deps` 목록(한 줄에 절대 경로 하나, 정렬, `/` 구분자)을 바뀌었을 때만 쓴다."""
    content = "".join(file.as_posix() + "\n" for file in sorted(files))
    return write_generated_file(path, content, tag=tag, newline="\n", report_unchanged=False)


class GeneratorError(CommandError):
    """생성기가 입력(JSON · 상수)을 읽지 못했다 — 트레이스백 대신 한 줄로 찍고 1 로 끝난다(configure 가 무엇을 고칠지 먼저 보게)."""


class Generator(abc.ABC):
    """
    configure 생성기 하나 = 파일 하나. 입력 목록을 손으로 적지 않는다 — 도는 동안 읽은 파일을 `record_read_files` 가 모아 configure 의존이 된다. 출력의 낡음은
    내용 비교(`write_generated_file`)가 본다. 파일을 더하면 configure 가 함께 돌고 지우면 빠진다(`find_generators`).
    """

    @abc.abstractmethod
    def generate(self, output_dir: Path, game_name: str) -> None:
        """`output_dir`(`<빌드>/generated/sw/config`)에 쓴다. 입력이 틀리면 `GeneratorError`."""


def find_generators() -> list[Generator]:
    """`Scripts/generate/*.py` 의 `Generator` 하위 클래스(모듈 이름 순)."""
    for path in sorted(Path(__file__).resolve().parent.glob("*.py")):
        if "(Generator):" in path.read_text(encoding="utf-8"):
            importlib.import_module(f"generate.{path.stem}")
    return [generator() for generator in sorted(Generator.__subclasses__(), key=lambda generator: generator.__module__)]
