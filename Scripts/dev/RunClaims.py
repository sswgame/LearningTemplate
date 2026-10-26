#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file RunClaims.py
@brief 에이전트끼리 중복 작업을 줄이는 작업 장부 — 맡은 파일 · 폴더를 적어 두고 겹침을 묻고, 짧은 공지를 남긴다.

    py -3 -m Scripts claims add <owner> --paths <경로...> [--note <글>] [--pid <번호>]   # 맡은 경로 등록(같은 owner 는 덮어쓴다)
    py -3 -m Scripts claims check <경로...>                                              # 다른 owner 와 겹치면 owner · 메모를 보이고 종료 코드 1
    py -3 -m Scripts claims check <경로...> --owner <owner>                              # 내 항목은 겹침에서 뺀다
    py -3 -m Scripts claims list                                                         # 항목 전부(owner 프로세스가 끝났으면 표시)
    py -3 -m Scripts claims release <owner>                                              # 내 항목을 지운다
    py -3 -m Scripts claims post <owner> <글>                                            # 다른 에이전트가 볼 공지(예: "X API 시그니처를 바꿨다")
    py -3 -m Scripts claims read [--since <시각>]                                        # 공지 읽기(시각은 ISO 8601 · 에포크 초)

저장은 git 공통 폴더 아래 `sw-claims/<owner>.json`(항목)과 `sw-claims/posts.jsonl`(공지)이다 — 워크트리 주인 기록(`sw-owner.json`)과 같은
자리 규약(작업 폴더 밖이라 `git status` 에 안 잡힌다)이고, 살아 있는지는 워크트리 주인과 같이 주인 프로세스(`--pid`, 없으면 `CLAUDE_PID`)로 본다.
경로는 저장소 루트 기준 `/` 구분으로 정규화하고, 겹침은 경로 마디 단위 접두 비교다(`Source/Engine` 은 `Source/Engine/A.cpp` 와 겹치고 `Source/EngineX` 와는 안 겹친다).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ExitCode import ExitCode  # noqa: E402
from common.Host import runGit  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.Process import isProcessAlive  # noqa: E402
from common.Worktree import kOwnerPidEnvironment, resolveOwnerProcessId  # noqa: E402

kClaimsFolderName = "sw-claims"
kPostsFileName = "posts.jsonl"
kOwnerPattern = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]*$")


@dataclass(frozen=True)
class Claim:
    owner: str
    paths: tuple[str, ...]
    note: str
    pid: int
    time: float


def getClaimsDirectory(projectRoot: Path) -> Path:
    """git 공통 폴더 아래 장부 폴더(없으면 만든다)."""
    result = runGit(["rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=projectRoot)
    if not result.bSucceeded:
        raise RuntimeError(f"git 저장소가 아닙니다: {projectRoot}\n{result.stderr.strip()}")
    directory = Path(result.stdout.strip()) / kClaimsFolderName
    directory.mkdir(parents=True, exist_ok=True)
    return directory


def checkOwnerName(owner: str) -> str:
    if not kOwnerPattern.match(owner):
        raise ValueError(f"owner 는 영숫자 · '-' · '_' 만 씁니다: '{owner}'")
    return owner


def normalizeClaimPath(rawPath: str, projectRoot: Path) -> str:
    """저장소 루트 기준 `/` 구분 경로. 절대 경로는 저장소 안이어야 하고, 빈 경로 · 루트 밖(`..`)은 오류."""
    text = rawPath.strip().replace("\\", "/")
    if not text:
        raise ValueError("빈 경로는 받지 않습니다")
    candidate = Path(text)
    if candidate.is_absolute():
        try:
            text = candidate.resolve().relative_to(projectRoot.resolve()).as_posix()
        except ValueError as error:
            raise ValueError(f"저장소 밖의 경로입니다: {rawPath}") from error
    parts = [part for part in text.split("/") if part not in ("", ".")]
    if not parts or ".." in parts:
        raise ValueError(f"저장소 루트 기준의 하위 경로여야 합니다: {rawPath}")
    return "/".join(parts)


def isOverlapping(left: str, right: str) -> bool:
    """마디 단위 접두 비교 — 같거나 한쪽이 다른 쪽의 폴더 아래(대소문자 무시: Windows)."""
    leftParts = left.lower().split("/")
    rightParts = right.lower().split("/")
    shortCount = min(len(leftParts), len(rightParts))
    return leftParts[:shortCount] == rightParts[:shortCount]


def writeClaim(directory: Path, claim: Claim) -> None:
    record = {"owner": claim.owner, "paths": list(claim.paths), "note": claim.note, "pid": claim.pid, "time": claim.time}
    (directory / f"{claim.owner}.json").write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding="utf-8")


def readClaims(directory: Path) -> list[Claim]:
    """모든 항목. 깨진 기록은 오류로 멈춘다(조용히 건너뛰면 겹침을 놓친다)."""
    listClaim: list[Claim] = []
    for path in sorted(directory.glob("*.json")):
        try:
            record = json.loads(path.read_text(encoding="utf-8"))
            listClaim.append(Claim(owner=str(record["owner"]), paths=tuple(str(item) for item in record["paths"]), note=str(record["note"]),
                                   pid=int(record["pid"]), time=float(record["time"])))
        except (ValueError, KeyError, TypeError) as error:
            raise ValueError(f"작업 장부 기록이 깨졌습니다: {path} ({error})") from error
    return listClaim


def findConflicts(listClaim: list[Claim], listPath: list[str], excludeOwner: str | None) -> list[tuple[Claim, str, str]]:
    """(겹친 항목, 내 경로, 그 항목의 경로) 목록."""
    listConflict: list[tuple[Claim, str, str]] = []
    for claim in listClaim:
        if claim.owner == excludeOwner:
            continue
        for mine in listPath:
            for theirs in claim.paths:
                if isOverlapping(mine, theirs):
                    listConflict.append((claim, mine, theirs))
    return listConflict


def formatAge(seconds: float) -> str:
    minutes = int(max(seconds, 0) / 60)
    return f"{minutes}분 전" if minutes < 120 else f"{minutes // 60}시간 전"


def describeClaim(claim: Claim, now: float) -> str:
    state = "진행 중" if isProcessAlive(claim.pid) else "주인 프로세스 끝남(release 안 됨)"
    note = f" — {claim.note}" if claim.note else ""
    return f"{claim.owner} [{state}, {formatAge(now - claim.time)}]{note}"


def parseSince(text: str) -> float:
    """ISO 8601(`2026-10-27T10:00:00`) 또는 에포크 초."""
    try:
        return float(text)
    except ValueError:
        pass
    try:
        return datetime.fromisoformat(text).timestamp()
    except ValueError as error:
        raise ValueError(f"시각을 읽지 못했습니다(ISO 8601 또는 에포크 초): {text}") from error


def runAdd(directory: Path, projectRoot: Path, args: argparse.Namespace) -> int:
    owner = checkOwnerName(args.owner)
    pid = resolveOwnerProcessId(args.pid)
    if pid is None:
        raise ValueError(f"주인 프로세스를 정할 수 없습니다 — --pid 를 줍니다(환경 변수 {kOwnerPidEnvironment} 가 없다)")
    listPath = sorted({normalizeClaimPath(raw, projectRoot) for raw in args.paths})
    claim = Claim(owner=owner, paths=tuple(listPath), note=args.note, pid=pid, time=time.time())
    listConflict = findConflicts(readClaims(directory), listPath, owner)
    writeClaim(directory, claim)
    print(f"[Claims] {owner}: {len(listPath)} 경로 등록")
    for other, mine, theirs in listConflict:
        print(f"[Claims] 주의 — {mine} 이(가) {other.owner} 의 {theirs} 와 겹친다: {describeClaim(other, claim.time)}")
    return ExitCode.Passed


def runCheck(directory: Path, projectRoot: Path, args: argparse.Namespace) -> int:
    listPath = [normalizeClaimPath(raw, projectRoot) for raw in args.paths]
    now = time.time()
    listConflict = findConflicts(readClaims(directory), listPath, args.owner)
    for other, mine, theirs in listConflict:
        print(f"[Claims] {mine} ↔ {other.owner} 의 {theirs}: {describeClaim(other, now)}")
    if not listConflict:
        print("[Claims] 겹침 없음")
        return ExitCode.Passed
    return ExitCode.Violation


def runList(directory: Path) -> int:
    listClaim = readClaims(directory)
    now = time.time()
    if not listClaim:
        print("[Claims] 등록된 항목 없음")
    for claim in listClaim:
        print(describeClaim(claim, now))
        for path in claim.paths:
            print(f"    {path}")
    return ExitCode.Passed


def runRelease(directory: Path, args: argparse.Namespace) -> int:
    path = directory / f"{checkOwnerName(args.owner)}.json"
    if not path.is_file():
        raise ValueError(f"등록된 항목이 없습니다: {args.owner}")
    path.unlink()
    print(f"[Claims] {args.owner} 항목을 지웠다")
    return ExitCode.Passed


def runPost(directory: Path, args: argparse.Namespace) -> int:
    owner = checkOwnerName(args.owner)
    text = args.text.strip()
    if not text:
        raise ValueError("공지 글이 비어 있습니다")
    record = {"owner": owner, "time": time.time(), "text": text}
    with (directory / kPostsFileName).open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(record, ensure_ascii=False) + "\n")
    print(f"[Claims] 공지를 남겼다: {owner}")
    return ExitCode.Passed


def readPosts(directory: Path, since: float) -> list[dict]:
    path = directory / kPostsFileName
    if not path.is_file():
        return []
    listPost: list[dict] = []
    for lineNumber, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
            float(record["time"])
            str(record["owner"])
            str(record["text"])
        except (ValueError, KeyError, TypeError) as error:
            raise ValueError(f"공지 기록이 깨졌습니다: {path}:{lineNumber} ({error})") from error
        if float(record["time"]) > since:
            listPost.append(record)
    return listPost


def runRead(directory: Path, args: argparse.Namespace) -> int:
    since = parseSince(args.since) if args.since is not None else 0.0
    listPost = readPosts(directory, since)
    if not listPost:
        print("[Claims] 새 공지 없음")
    for record in listPost:
        stamp = datetime.fromtimestamp(float(record["time"])).strftime("%Y-%m-%d %H:%M:%S")
        print(f"{stamp} {record['owner']}: {record['text']}")
    return ExitCode.Passed


def buildParser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="작업 장부 — 에이전트끼리 맡은 경로와 공지를 나눠 중복 작업을 줄인다(add · check · list · release · post · read).")
    sub = parser.add_subparsers(dest="command", required=True)
    addParser = sub.add_parser("add", help="맡은 경로를 등록한다(같은 owner 는 덮어쓴다)")
    addParser.add_argument("owner", help="owner 이름(워크트리 이름과 같게)")
    addParser.add_argument("--paths", nargs="+", required=True, help="맡은 파일 · 폴더(저장소 루트 기준)")
    addParser.add_argument("--note", default="", help="한 줄 메모")
    addParser.add_argument("--pid", type=int, default=None, help=f"주인 프로세스 번호(기본: 환경 변수 {kOwnerPidEnvironment}) — 죽었는지 list 가 표시한다")
    checkParser = sub.add_parser("check", help="다른 owner 와 겹치는지 묻는다(겹치면 종료 코드 1)")
    checkParser.add_argument("paths", nargs="+", help="확인할 파일 · 폴더")
    checkParser.add_argument("--owner", default=None, help="이 owner 의 항목은 겹침에서 뺀다")
    sub.add_parser("list", help="항목 전부를 보인다")
    releaseParser = sub.add_parser("release", help="항목을 지운다")
    releaseParser.add_argument("owner")
    postParser = sub.add_parser("post", help="다른 에이전트가 볼 짧은 공지를 남긴다")
    postParser.add_argument("owner")
    postParser.add_argument("text", help="공지 글")
    readParser = sub.add_parser("read", help="공지를 읽는다")
    readParser.add_argument("--since", default=None, help="이 시각 이후의 공지만(ISO 8601 또는 에포크 초)")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = buildParser().parse_args(argv)
    try:
        projectRoot = getProjectRoot()
        directory = getClaimsDirectory(projectRoot)
        if args.command == "add":
            return runAdd(directory, projectRoot, args)
        if args.command == "check":
            return runCheck(directory, projectRoot, args)
        if args.command == "list":
            return runList(directory)
        if args.command == "release":
            return runRelease(directory, args)
        if args.command == "post":
            return runPost(directory, args)
        return runRead(directory, args)
    except (RuntimeError, ValueError) as error:
        print(f"[Claims] {error}", file=sys.stderr)
        return ExitCode.CheckInvalid


if __name__ == "__main__":
    sys.exit(main())
