#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/cook_if_inputs_changed.py

정적 모듈 구성(배포 · 웹 · Android 개발)의 `CookAssets` 는 기본 빌드에 든다 — 바뀐 것이 없어도 빌드마다 App 을 띄워 쿠킹 판정(10 초대)을 했다.
이 도구는 쿠킹 입력(콘텐츠 · 설정 폴더, 쿠커 실행 파일 · 모듈)과 출력(스테이징 · 팩 폴더)의 파일 목록 지문이 지난 쿠킹 뒤와 같으면 명령을 건너뛰고,
다르면 명령을 차례로 돌린 뒤 **돌린 뒤의** 지문을 적는다(임포트가 콘텐츠에 쓰는 산출물까지 넣어 다음 빌드가 다시 돌지 않게).

Ninja depfile 이 아니라 지문인 까닭: 교차 빌드의 쿠커는 호스트 빌드 산출물이라 같은 빌드 그래프 밖에 있다 — depfile 에 적은 호스트 파일은 빌드 첫머리에
stat 되어, 같은 빌드에서 호스트를 다시 지은 변경이 한 빌드 늦게 보인다. 지문은 명령을 돌리기 직전에 잰다(호스트 빌드는 그 앞 명령이다).
언리얼 쿠커의 증분 쿠킹(입력 해시)처럼 판정은 내용이 아니라 크기 · 시각이다 — ninja · git 색인과 같은 판정이라 시각을 되돌린 같은 크기 바뀜은 셋 다 놓친다(입력 수만 개의 해시를 빌드마다 세지 않는다).

  python Scripts/generate/cook_if_inputs_changed.py --stamp <파일> --input <폴더|파일>... --binaries <폴더>... --output <폴더>... -- <명령> [--then <명령>]...
"""

from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
from pathlib import Path
from typing import Sequence

TAG = "CookIfInputsChanged"
#: 쿠커 실행 파일 폴더(`--binaries`)에서 지문에 넣는 파일 — 실행 파일 · 공유 라이브러리(`.so.1` 도) · 그 설정(JSON). 개발 빌드 Bin 에는 실행 로그 · 사진이 쌓여 바뀔 때마다 다시 구웠다.
LIST_BINARY_SUFFIX = {"", ".exe", ".dll", ".json"}
COMMAND_SEPARATOR = "--then"


def is_cooker_binary(name: str) -> bool:
    return Path(name).suffix.lower() in LIST_BINARY_SUFFIX or ".so" in Path(name).suffixes


def fingerprint(list_path: Sequence[Path], list_flat: Sequence[Path] = ()) -> str:
    """
    경로들(폴더면 그 아래 모든 파일)과 바로 아래 파일만 볼 폴더들(`list_flat` — 실행 파일 폴더: 그 아래 `Saved/` 같은 실행 산출물 20 만 개를 훑지 않는다)의
    (경로, 크기, 수정 시각) 목록 지문. 없는 경로도 '없음' 으로 들어간다(생기면 바뀐다).
    """
    digest = hashlib.sha256()
    for folder in list_flat:
        digest.update(f"flat {folder.as_posix()}\n".encode("utf-8"))
        if not folder.is_dir():
            digest.update(b"missing\n")
            continue
        for entry in sorted(os.scandir(folder), key=lambda item: item.name):
            if entry.is_file() and is_cooker_binary(entry.name):
                stat = entry.stat()
                digest.update(f"{entry.name} {stat.st_size} {stat.st_mtime_ns}\n".encode("utf-8"))
    for root in list_path:
        digest.update(f"root {root.as_posix()}\n".encode("utf-8"))
        if root.is_file():
            stat = root.stat()
            digest.update(f"{stat.st_size} {stat.st_mtime_ns}\n".encode("utf-8"))
            continue
        if not root.is_dir():
            digest.update(b"missing\n")
            continue
        list_entry: list[tuple[str, int, int]] = []
        for folder, list_folder, list_file in os.walk(root):
            list_folder.sort()
            for name in list_file:
                path = Path(folder) / name
                try:
                    stat = path.stat()
                except OSError:
                    continue   # 훑는 사이 지워진 임시 파일
                list_entry.append((path.relative_to(root).as_posix(), stat.st_size, stat.st_mtime_ns))
        for relative, size, mtime in sorted(list_entry):
            digest.update(f"{relative} {size} {mtime}\n".encode("utf-8"))
    return digest.hexdigest()


def split_commands(list_argument: Sequence[str]) -> list[list[str]]:
    """`a b --then c d` → [[a, b], [c, d]]. 빈 명령은 오류."""
    list_command: list[list[str]] = [[]]
    for argument in list_argument:
        if argument == COMMAND_SEPARATOR:
            list_command.append([])
        else:
            list_command[-1].append(argument)
    if any(not command for command in list_command):
        raise ValueError(f"empty command around '{COMMAND_SEPARATOR}'")
    return list_command


def run(stamp: Path, list_input: Sequence[Path], list_binary: Sequence[Path], list_output: Sequence[Path],
        list_command: Sequence[Sequence[str]]) -> int:
    list_watched = [*list_input, *list_output]
    before = fingerprint(list_watched, list_binary)
    if stamp.is_file() and stamp.read_text(encoding="utf-8").strip() == before:
        print(f"[{TAG}] inputs and outputs unchanged since the last cook - skipped ({stamp})")
        return 0
    for command in list_command:
        result = subprocess.run(list(command))
        if result.returncode != 0:
            stamp.unlink(missing_ok=True)   # 실패한 쿠킹 뒤에는 다음 빌드가 반드시 다시 돈다
            return result.returncode
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text(fingerprint(list_watched, list_binary) + "\n", encoding="utf-8")
    return 0


def main(argv: Sequence[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if "--" not in argv:
        print(f"[{TAG}] missing '--' before the commands", file=sys.stderr)
        return 2
    split = argv.index("--")
    parser = argparse.ArgumentParser(description="쿠킹 입력 · 출력 지문이 같으면 쿠킹 명령을 건너뛴다")
    parser.add_argument("--stamp", type=Path, required=True, help="지문 파일 — 출력 폴더 밖(안에 두면 쓴 지문이 다음 지문을 바꾼다)")
    parser.add_argument("--input", type=Path, action="append", default=[], help="입력 폴더 · 파일(여럿)")
    parser.add_argument("--binaries", type=Path, action="append", default=[], help="쿠커 실행 파일 폴더(여럿) — 바로 아래 파일만 본다")
    parser.add_argument("--output", type=Path, action="append", default=[], help="출력 폴더(여럿) — 지우거나 고치면 다시 돈다")
    args = parser.parse_args(argv[:split])
    if not args.input:
        print(f"[{TAG}] no --input", file=sys.stderr)
        return 2
    try:
        list_command = split_commands(argv[split + 1:])
    except ValueError as error:
        print(f"[{TAG}] {error}", file=sys.stderr)
        return 2
    return run(args.stamp, args.input, args.binaries, args.output, list_command)


if __name__ == "__main__":
    sys.exit(main())
