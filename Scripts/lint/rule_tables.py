#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/rule_tables.py

린트 규칙의 표 형식 — 규칙 모듈은 판정만 하고, 표 경로 · 머리 칸 확인 · 까닭 검사 · 낡은 줄 찾기 · 래칫 기준선 쓰기는 이 형식이 한다.
예전에는 규칙 모듈마다 표 읽기 · "까닭이 비었다" · 낡은 예외 · 기준선 래칫을 손으로 다시 짜, 표 모양이 규칙마다 달라졌다(목록 · 사전 · `_why` · `_reason`).

경로: 규칙 id 하나에 표 하나 — `Config/Rules/Lint/<규칙 id 의 PascalCase>.json`(`thread-owners` → `ThreadOwners.json`). 경로는 `get_table_path` 만 짓는다.
기계가 통째로 다시 쓰는 표는 같은 머리에 종류를 붙인 다른 파일이다(`MagicLiteralsBaseline.json` · `NamingWordsBannedNames.json`) — 사람이 까닭을 적는
예외와 `--ratchet` 이 다시 쓰는 수를 한 파일에 두면 갈래마다 병합이 부딪힌다(ESLint 설정과 `eslint-suppressions.json`, PHPStan `phpstan-baseline.neon` 이 나눈 것과 같다).

머리: 모든 표는 `_comment`(무엇을 담고 누가 읽는가) 하나를 머리로 두고, 칸 키는 모두 `_` 접두 camelCase 다 — 읽을 때 형식이 확인한다.

두 형식:
- `ExceptionTable` — 칸 → {키: 까닭}(언리얼 ini 의 섹션 → 키 = 값). 값은 까닭 글이거나 `_reason` 을 든 객체(까닭 + 상한 같은 값)다. 까닭이 빈 줄은
  위반이고(clang-tidy NOLINT 는 까닭을 강제하지 않아 이유 없는 예외가 쌓인다), 판정이 쓴 키를 기록해 저장소 전체 실행에서 쓰이지 않은 줄을 낡은 예외로 낸다
  (ESLint `--report-unused-disable-directives`).
- `Baseline` — 파일 → 종류 → 수. 늘면 실패, 줄면 `--ratchet` 이 낮추고, 올리는 길은 `--accept` 하나다. 파일 하나가 한 줄이라 갈래가 다른 파일을 고쳐도
  병합이 부딪히지 않는다.
"""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Callable, Iterable, Mapping, Sequence

TABLE_FOLDER = "Config/Rules/Lint/"
KEY_SHAPE = re.compile(r"_[a-z][A-Za-z0-9]*")


def get_table_path(rule_id: str, kind: str = "") -> str:
    """규칙 @p rule_id 의 표 경로(저장소 기준) — @p kind 는 같은 규칙의 기계가 쓰는 표(`Baseline` · `BannedNames`)."""
    return TABLE_FOLDER + "".join(part.capitalize() for part in rule_id.split("-")) + kind + ".json"


def check_head(path: str, data: object) -> None:
    """표 머리 — 객체이고, `_comment` 가 비지 않았고, 칸 키가 모두 `_` 접두 camelCase 다. 어기면 멈춘다(폴백 없음)."""
    if not isinstance(data, dict):
        raise ValueError(f"{path}: 표는 JSON 객체여야 한다")
    comment = data.get("_comment")
    if not isinstance(comment, str) or not comment.strip():
        raise ValueError(f"{path}: 머리 칸 `_comment` 가 없거나 비었다 — 무엇을 담고 누가 읽는지 적는다")
    list_bad = [key for key in data if KEY_SHAPE.fullmatch(key) is None]
    if list_bad:
        raise ValueError(f"{path}: 칸 키 {', '.join(list_bad)} 는 `_` 접두 camelCase 가 아니다")


def read_table(project_root: Path, path: str) -> dict:
    """표 하나를 읽고 머리를 확인한다. 파일이 없으면 멈춘다(빈 표로 대신하지 않는다)."""
    data = json.loads((project_root / path).read_text(encoding="utf-8"))
    check_head(path, data)
    return data


def read_rule_table(project_root: Path, rule_id: str, kind: str = "") -> dict:
    return read_table(project_root, get_table_path(rule_id, kind))


def get_reason(value: object) -> str:
    """예외 값의 까닭 — 글이면 그것, 객체면 `_reason`."""
    if isinstance(value, dict):
        value = value.get("_reason", "")
    return value.strip() if isinstance(value, str) else ""


class ExceptionTable:
    """
    사유가 붙은 예외 표 — 칸(@p sections) → {키: 까닭 또는 `_reason` 객체}. 판정은 `allows` · `allows_prefix` 로 묻고(쓴 키가 기록된다),
    끝에 `list_problems` 가 까닭 빈 줄과(저장소 전체일 때) 쓰이지 않은 줄을 낸다. 칸이 없으면 멈춘다.
    """

    def __init__(self, path: str, data: Mapping, sections: Sequence[str]) -> None:
        check_head(path, data)
        self.path = path
        self._map_section: dict[str, dict[str, object]] = {}
        for section in sections:
            if section not in data:
                raise ValueError(f"{path}: 칸 `{section}` 이 없다")
            entries = data[section]
            if not isinstance(entries, dict):
                raise ValueError(f"{path}: 칸 `{section}` 은 키 → 까닭 객체여야 한다")
            self._map_section[section] = dict(entries)
        self._set_used: set[tuple[str, str]] = set()

    @classmethod
    def load(cls, project_root: Path, rule_id: str, sections: Sequence[str]) -> "ExceptionTable":
        path = get_table_path(rule_id)
        return cls(path, read_table(project_root, path), sections)

    @classmethod
    def make(cls, rule_id: str, **sections: Mapping[str, object]) -> "ExceptionTable":
        """파일 없이 만든 표 — 규칙 모듈의 판정 테스트가 쓴다."""
        return cls(get_table_path(rule_id), {"_comment": "테스트 표", **sections}, tuple(sections))

    def get(self, section: str) -> dict[str, object]:
        return self._map_section[section]

    def mark_used(self, section: str, key: str) -> None:
        self._set_used.add((section, key))

    def allows(self, section: str, key: str) -> bool:
        """@p key 가 칸에 있으면 쓴 것으로 기록하고 참."""
        if key not in self._map_section[section]:
            return False
        self.mark_used(section, key)
        return True

    def allows_prefix(self, section: str, text: str) -> bool:
        """칸의 키 가운데 @p text 의 앞머리인 것이 있으면 쓴 것으로 기록하고 참(폴더 · 경로 앞머리 예외)."""
        for key in self._map_section[section]:
            if text.startswith(key):
                self.mark_used(section, key)
                return True
        return False

    def hint(self, section: str) -> str:
        """위반 안내 끝 — 예외를 어디에 적는가."""
        return f"{self.path} 의 {section} 에 까닭과 함께"

    def list_problems(self, whole_repo: bool, stale_sections: Iterable[str] | None = None) -> list[str]:
        """까닭이 빈 줄(늘)과 쓰이지 않은 줄(@p whole_repo 일 때만 — 바뀐 파일만 보고 "쓰이지 않는다" 고 말할 수 없다).
        @p stale_sections 는 낡은 줄을 볼 칸(없으면 모든 칸)."""
        list_problem = [f"{self.path}: {section} '{key}' 의 까닭이 비었다" for section, entries in self._map_section.items()
                        for key, value in entries.items() if not get_reason(value)]
        if whole_repo:
            for section in self._map_section if stale_sections is None else stale_sections:
                list_problem += [f"{self.path}: 낡은 예외 {section} '{key}' — 더는 걸리지 않는다, 줄을 지운다"
                                 for key in self._map_section[section] if (section, key) not in self._set_used]
        return list_problem


class NameBaseline:
    """
    이름 목록 래칫 — 칸(판정)마다 지금 남은 위반 이름 목록(`Baseline` 의 수 대신 이름 — 이름 판정은 파일이 아니라 이름 하나가 단위다).
    `compare` 는 목록에 없는 새 위반을 늘, 고쳐 사라졌는데 목록에 남은 이름을 저장소 전체일 때 낸다. `lower` 는 빼기만 하고 `replace` 는 새 판정을 들일 때 한 번 쓴다.
    """

    def __init__(self, path: str, data: Mapping, sections: Sequence[str]) -> None:
        check_head(path, data)
        self.path = path
        self.comment: str = data["_comment"]
        self.names: dict[str, list[str]] = {}
        for section in sections:
            if section not in data:
                raise ValueError(f"{path}: 칸 `{section}` 이 없다")
            self.names[section] = list(data[section])

    @classmethod
    def load(cls, project_root: Path, rule_id: str, sections: Sequence[str]) -> "NameBaseline":
        path = get_table_path(rule_id, "Baseline")
        return cls(path, read_table(project_root, path), sections)

    def compare(self, current: Mapping[str, Mapping[str, str]], whole_repo: bool, lower_command: str) -> list[str]:
        """@p current 는 칸 → {위반 이름: 경로 · 안내}. 줄 머리는 칸 이름에서 `_` 를 뗀 판정 이름이다(`[verb]`)."""
        list_problem: list[str] = []
        for section, list_name in self.names.items():
            label, allowed = section.lstrip("_"), set(list_name)
            list_problem += [f"[{label}] {where}" for name, where in sorted(current[section].items()) if name not in allowed]
            for name in sorted(allowed - set(current[section])) if whole_repo else ():   # 고친 이름은 저장소 전체를 봐야 안다
                list_problem.append(f"[{label}] 기준선의 '{name}' 가 더는 없다 — 고친 이름을 기준선에서 뺀다(`{lower_command}`)")
        return list_problem

    def lower(self, current: Mapping[str, Mapping[str, str]]) -> None:
        """고쳐 사라진 이름만 뺀다(늘리지 않는다)."""
        self.names = {section: [name for name in list_name if name in current[section]] for section, list_name in self.names.items()}

    def replace(self, current: Mapping[str, Iterable[str]]) -> None:
        """지금 위반을 모두 기준선으로(새 판정을 들일 때 한 번)."""
        self.names = {section: list(current[section]) for section in self.names}

    def dump(self) -> str:
        data = {"_comment": self.comment, **{section: sorted(set(list_name)) for section, list_name in self.names.items()}}
        return json.dumps(data, ensure_ascii=False, indent=2) + "\n"

    def write(self, project_root: Path) -> None:
        (project_root / self.path).write_text(self.dump(), encoding="utf-8", newline="\n")


class Baseline:
    """
    래칫 기준선 표 — `_files` 칸에 파일 → 종류 → 수. 판정(`compare`)은 늘면 실패 · 줄면 낮추라고 하고, `lower` · `accept` 가 고친 뒤 `write` 가
    머리(`_comment`)를 지켜 파일 하나를 한 줄로 쓴다. 파일이 없으면 멈춘다(빈 기준선으로 돌면 검사 없이 지나갈 수 있다).
    """

    def __init__(self, path: str, data: Mapping) -> None:
        check_head(path, data)
        self.path = path
        self.comment: str = data["_comment"]
        self.files: dict[str, dict[str, int]] = {file: dict(counts) for file, counts in data["_files"].items()}

    @classmethod
    def load(cls, project_root: Path, rule_id: str) -> "Baseline":
        path = get_table_path(rule_id, "Baseline")
        return cls(path, read_table(project_root, path))

    def compare(self, actual: Mapping[str, Mapping[str, int]], list_path: Iterable[str], map_old: Mapping[str, str], kinds: Sequence[str],
                advise: Callable[[str], str], ratchet_command: str) -> list[str]:
        """@p list_path 파일마다 지금 수(@p actual)를 기준선과 견준다. 옮긴 파일(@p map_old 새 → 옛)은 옛 경로의 기준선을 이어받는다."""
        list_problem: list[str] = []
        for path in sorted(set(list_path)):
            allowed = self.files.get(path, self.files.get(map_old.get(path, ""), {}))
            have = actual.get(path, {})
            for kind in kinds:
                count, limit = have.get(kind, 0), allowed.get(kind, 0)
                if count > limit:
                    list_problem.append(f"{path}: {kind} {limit} -> {count} — 늘었다: {advise(kind)}")
                elif count < limit:
                    list_problem.append(f"{path}: {kind} {limit} -> {count} — 줄었다: `{ratchet_command}` 로 기준선을 낮추고 함께 커밋한다")
        return list_problem

    def list_missing(self, set_existing: set[str]) -> list[str]:
        return [f"{path}: 없는 파일의 기준선 — `--ratchet` 으로 지운다" for path in sorted(self.files) if path not in set_existing]

    def lower(self, actual: Mapping[str, Mapping[str, int]], set_existing: set[str]) -> None:
        """줄어든 수만 낮춘다(늘어난 수는 기준선을 지킨다). 없어진 파일은 지운다."""
        lowered: dict[str, dict[str, int]] = {}
        for path, allowed in self.files.items():
            if path not in set_existing:
                continue
            have = actual.get(path, {})
            lowered[path] = {kind: min(limit, have.get(kind, 0)) for kind, limit in allowed.items() if min(limit, have.get(kind, 0)) > 0}
        self.files = lowered

    def accept(self, path: str, counts: Mapping[str, int]) -> None:
        """그 파일의 기준선을 지금 수로(올리는 유일한 길)."""
        self.files[path] = dict(counts)

    def dump(self) -> str:
        # 파일 하나가 한 줄이고 합계 줄은 두지 않는다 — 병렬 브랜치가 다른 파일을 낮출 때 병합이 부딪히지 않는다.
        list_line = [f"    {json.dumps(path)}: {json.dumps(self.files[path])}" for path in sorted(self.files) if self.files[path]]
        return f"{{\n  \"_comment\": {json.dumps(self.comment, ensure_ascii=False)},\n  \"_files\": {{\n" + ",\n".join(list_line) + "\n  }\n}\n"

    def write(self, project_root: Path) -> None:
        (project_root / self.path).write_text(self.dump(), encoding="utf-8", newline="\n")

    def update(self, project_root: Path, tag: str, list_file: Sequence[Path], count: Callable[[Sequence[Path]], dict[str, dict[str, int]]],
               list_accept: Sequence[str] | None) -> None:
        """`--ratchet`(@p list_accept 없음 — 모든 파일 @p list_file 을 세어 낮춘다) · `--accept <파일…>`(그 파일만 지금 수로)을 적용하고 쓴다."""
        if list_accept is None:
            self.lower(count(list_file), {path.relative_to(project_root).as_posix() for path in list_file})
        else:
            list_path = [Path(path).resolve() for path in list_accept]
            actual = count([path for path in list_path if path.is_file()])
            for path in list_path:
                relative = path.relative_to(project_root).as_posix()
                print(f"[{tag}] accept {relative}: {self.files.get(relative, {})} -> {actual.get(relative, {})}")
                self.accept(relative, actual.get(relative, {}))
        self.write(project_root)
        print(f"[{tag}] baseline -> {self.path}")
