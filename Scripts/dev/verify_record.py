#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/dev/verify_record.py

묶음 검증(`preflight.sh`)의 통과 기록과 push 게이트. 기록은 메인 작업 폴더의 `build/` 에 둔다(빌드 · 검증은 그 폴더 하나).

- `build/verify.stages.json`: 단계 → 마지막으로 통과한 sha. 다음 실행에서 그 sha 뒤 바뀐 파일이 **모두** 입력이 아닌 경로(`Config/Dev/Verify.json` 의
  `_nonInputPaths`)면 그 단계를 건너뛴다. 거꾸로 적는 목록이다 — 단계마다 입력 경로를 적으면 git 밖 입력(도구 · 드라이버 · 임포트 산출물 · 셰이더 요청 기록)까지
  닫지 못해 빠진 경로가 "검증 안 된 단계 건너뛰기" 가 된다. 이 목록에서 빠뜨리면 더 돌 뿐이다(Bazel 은 샌드박스가 입력을 강제하지만 여기는 강제할 것이 없다).
- `build/verify.ok`: Windows 단계가 모두 통과한 sha 한 줄. git `pre-push` 훅(`setup/git_hooks.py`)이 보호 ref(`_verifiedPushRefs`)로 가는 sha 가 이것과 같은지
  본다 — 스킬을 읽었다는 표지(도구 정책 훅)가 아니라 진짜 통과 기록을 본다. `--dry-run` 은 아무것도 적지 않는다(preflight.sh).

  py -3 Scripts/dev/verify_record.py skip <단계> <sha>           # 건너뛸 수 있으면 0(까닭을 찍는다), 아니면 1
  py -3 Scripts/dev/verify_record.py pass <단계> <sha>           # 단계 통과를 적는다
  py -3 Scripts/dev/verify_record.py ok <sha>                   # Windows 통과를 적는다(build/verify.ok)
  py -3 Scripts/dev/verify_record.py pre-push                   # git pre-push 훅 — 표준 입력은 `<로컬 ref> <로컬 sha> <원격 ref> <원격 sha>` 줄
  py -3 Scripts/dev/verify_record.py --self-check
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.command import Command, ScriptContext  # noqa: E402
from common.exit_code import ExitCode  # noqa: E402
from common.git import get_main_working_tree, run_git  # noqa: E402

VERIFY_CONFIG = Path("Config") / "Dev" / "Verify.json"
STAGES_FILE = Path("build") / "verify.stages.json"
OK_FILE = Path("build") / "verify.ok"
ZERO_SHA_CHARACTERS = frozenset("0")


def read_verify_config(project_root: Path, key: str) -> list[str]:
    """`Verify.json` 의 목록 값 — 없으면 멈춘다(폴백 없음)."""
    value = json.loads((project_root / VERIFY_CONFIG).read_text(encoding="utf-8")).get(key)
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise SystemExit(f"[VerifyRecord] {VERIFY_CONFIG} 의 {key} 가 글 목록이 아니다")
    return value


def is_non_input(path: str, list_prefix: Sequence[str]) -> bool:
    """`list_prefix` 의 줄은 `/` 로 끝나면 폴더, 아니면 파일 하나다."""
    return any(path.startswith(prefix) if prefix.endswith("/") else path == prefix for prefix in list_prefix)


def find_input_changes(list_changed: Iterable[str], list_prefix: Sequence[str]) -> list[str]:
    """바뀐 파일 가운데 입력일 수 있는 것(목록 밖 — 모르는 경로는 입력으로 본다)."""
    return [path for path in list_changed if not is_non_input(path, list_prefix)]


def read_stages(main_tree: Path) -> dict[str, str]:
    path = main_tree / STAGES_FILE
    return json.loads(path.read_text(encoding="utf-8")) if path.is_file() else {}


def decide_skip(stage: str, sha: str, map_stage: dict[str, str], list_changed: Sequence[str] | None, list_prefix: Sequence[str]) -> tuple[bool, str]:
    """(건너뛰나, 까닭). `list_changed` 가 None 이면 기록한 sha 와 비교하지 못했다(지운 커밋 · 다른 저장소)."""
    recorded = map_stage.get(stage)
    if not recorded:
        return False, f"{stage}: 통과 기록이 없다"
    if recorded == sha:
        return True, f"{stage}: 이 커밋({sha[:10]})에서 이미 통과했다"
    if list_changed is None:
        return False, f"{stage}: 기록한 커밋 {recorded[:10]} 과 견주지 못했다"
    list_input = find_input_changes(list_changed, list_prefix)
    if list_input:
        return False, f"{stage}: {recorded[:10]} 뒤 입력이 바뀌었다({len(list_input)} 개 — 예: {list_input[0]})"
    return True, f"{stage}: {recorded[:10]} 에서 통과했고 그 뒤 입력이 아닌 경로만 바뀌었다({len(list_changed)} 개)"


def find_unverified_pushes(list_line: Iterable[str], verified_sha: str, list_protected: Sequence[str]) -> list[str]:
    """pre-push 표준 입력 줄 가운데 보호 ref 로 가는데 검증한 sha 가 아닌 것(지우기 포함)의 까닭."""
    list_problem: list[str] = []
    for line in list_line:
        parts = line.split()
        if len(parts) != 4 or parts[2] not in list_protected:
            continue
        local_sha, remote_ref = parts[1], parts[2]
        if set(local_sha) <= ZERO_SHA_CHARACTERS:
            list_problem.append(f"{remote_ref} 를 지우는 push 는 막는다")
        elif local_sha != verified_sha:
            list_problem.append(f"{remote_ref} 로 가는 {local_sha[:10]} 는 검증한 커밋({verified_sha[:10] or '없음'})이 아니다")
    return list_problem


def run_pre_push(project_root: Path, list_line: Sequence[str]) -> int:
    main_tree = get_main_working_tree(project_root)
    ok_path = main_tree / OK_FILE
    verified_sha = ok_path.read_text(encoding="utf-8").strip() if ok_path.is_file() else ""
    list_problem = find_unverified_pushes(list_line, verified_sha, read_verify_config(main_tree, "_verifiedPushRefs"))
    for problem in list_problem:
        print(f"[pre-push] {problem} — `bash Scripts/dev/preflight.sh --no-linux` 의 끝 줄 WINDOWS_OK=<sha> 를 push 한다({ok_path})", file=sys.stderr)
    return ExitCode.VIOLATION if list_problem else ExitCode.PASSED


def run_skip(project_root: Path, stage: str, sha: str) -> int:
    main_tree = get_main_working_tree(project_root)
    map_stage = read_stages(main_tree)
    list_changed: list[str] | None = None
    recorded = map_stage.get(stage)
    if recorded and recorded != sha:
        result = run_git(["diff", "--name-only", "--no-renames", recorded, sha], cwd=project_root)
        list_changed = [line.strip() for line in result.stdout.splitlines() if line.strip()] if result.return_code == 0 else None
    skip, reason = decide_skip(stage, sha, map_stage, list_changed, read_verify_config(project_root, "_nonInputPaths"))
    print(f"[VerifyRecord] {'건너뜀' if skip else '돈다'} — {reason}")
    return ExitCode.PASSED if skip else ExitCode.VIOLATION


def write_stage(project_root: Path, stage: str, sha: str) -> int:
    main_tree = get_main_working_tree(project_root)
    map_stage = read_stages(main_tree)
    map_stage[stage] = sha
    path = main_tree / STAGES_FILE
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(map_stage, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return ExitCode.PASSED


def write_ok(project_root: Path, sha: str) -> int:
    path = get_main_working_tree(project_root) / OK_FILE
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(sha + "\n", encoding="utf-8")
    return ExitCode.PASSED


class VerifyRecordCommand(Command):
    name = "verify-record"
    help = "묶음 검증 단계 통과 기록(skip · pass) · Windows 통과 기록(ok, build/verify.ok) · git pre-push 게이트"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        sub = parser.add_subparsers(dest="command", required=True)
        for name in ("skip", "pass"):
            stage_parser = sub.add_parser(name)
            stage_parser.add_argument("stage")
            stage_parser.add_argument("sha")
        sub.add_parser("ok").add_argument("sha")
        sub.add_parser("pre-push")

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        project_root = Path.cwd()
        if args.command == "skip":
            return run_skip(project_root, args.stage, args.sha)
        if args.command == "pass":
            return write_stage(project_root, args.stage, args.sha)
        if args.command == "ok":
            return write_ok(project_root, args.sha)
        return run_pre_push(project_root, sys.stdin.read().splitlines())


if __name__ == "__main__":
    sys.exit(VerifyRecordCommand.main())
