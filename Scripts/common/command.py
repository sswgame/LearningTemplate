"""
명령의 기반 — 명령 하나 = `Command` 하위 클래스 하나 = 파일 하나(언리얼 AutomationTool 의 BuildCommand, Django 의 management command 와 같은 모양).

파일을 더하면 명령이 생기고 지우면 사라진다: `find_commands` 가 `Scripts/<폴더>/*.py` 의 클래스 머리만 글로 훑는다. import 는 부른 명령의 모듈 하나만 하므로
명령을 더해도 다른 명령(커밋 훅 린트)이 느려지지 않고, 망가진 명령 하나가 나머지를 막지 않는다. 그래서 `name` · `help` 는 한 줄 문자열 리터럴이다(아니면 멈춘다).
"""

from __future__ import annotations

import abc
import argparse
import functools
import importlib
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

from common.exit_code import ExitCode
from common.git import get_main_working_tree
from common.paths import get_project_root

SCRIPTS_DIR = Path(__file__).resolve().parents[1]
CLASS_HEAD = re.compile(r"^class (\w+)\((?:\w+, )?Command\):\n", re.M)
LITERAL_ROW = re.compile(r'^    (name|help) = "((?:[^"\\]|\\.)*)"$', re.M)


class CommandError(Exception):
    """일이 실패했다 — 트레이스백 대신 `[명령] FAILED 까닭` 한 줄과 VIOLATION 으로 끝난다."""


@dataclass(frozen=True)
class ScriptContext:
    """명령이 도는 저장소 — 루트를 스스로 찾지 않고 여기서 받는다."""

    root: Path   # 이 작업 폴더(워크트리면 그 워크트리)

    @functools.cached_property
    def main_root(self) -> Path:
        """메인 작업 폴더 — 빌드 산출물 · 검증 기록 · 이력이 있는 곳."""
        return get_main_working_tree(self.root)


class Command(abc.ABC):
    name: str   # `py -3 -m Scripts <name>`
    help: str   # `--help` 에 찍히는 한 줄
    parser: argparse.ArgumentParser   # `run` 이 인자 조합 오류를 알릴 때(`self.parser.error`)

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        """인자 선언(없으면 그대로 둔다)."""

    @abc.abstractmethod
    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        """일 하나 — 돌려준 값이 종료 코드다."""

    @classmethod
    def main(cls, argv: Sequence[str] | None = None) -> int:
        command = cls()
        command.parser = argparse.ArgumentParser(prog=f"py -3 -m Scripts {cls.name}", description=cls.help, epilog=sys.modules[cls.__module__].__doc__,
                                                 formatter_class=argparse.RawDescriptionHelpFormatter)
        command.add_arguments(command.parser)
        args = command.parser.parse_args(argv)
        try:
            return command.run(ScriptContext(get_project_root()), args)
        except CommandError as error:
            print(f"[{cls.name}] FAILED {error}", flush=True)
            return ExitCode.VIOLATION


@dataclass(frozen=True)
class CommandEntry:
    """아직 import 하지 않은 명령 하나."""

    module: str
    class_name: str
    help: str

    def run(self, argv: Sequence[str]) -> int:
        return getattr(importlib.import_module(self.module), self.class_name).main(argv)


def find_commands() -> dict[str, CommandEntry]:
    """이름 → 명령. 이름이 겹치거나 `name` · `help` 리터럴이 없으면 멈춘다."""
    map_entry: dict[str, CommandEntry] = {}
    for path in sorted(SCRIPTS_DIR.glob("*/*.py")):
        text = path.read_text(encoding="utf-8")
        for match in CLASS_HEAD.finditer(text):
            body = text[match.end():text.find("\n\n", match.end())]
            literal = {key: value.replace('\\"', '"') for key, value in LITERAL_ROW.findall(body)}
            module = f"{path.parent.name}.{path.stem}"
            if literal.keys() != {"name", "help"} or literal["name"] in map_entry:
                sys.exit(f"[Scripts] {module}.{match.group(1)}: 머리에 한 줄 리터럴 name · help 가 있고 이름이 겹치지 않아야 한다")
            map_entry[literal["name"]] = CommandEntry(module, match.group(1), literal["help"])
    return map_entry
