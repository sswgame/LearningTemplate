#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file ListCiJobs.py
@brief GitHub Actions 의 실행 · 잡 · 실패 주석을 공개 API 로 읽어 보입니다(로그인 없이).

    py -3 -m Scripts ci-jobs --runs                       # main 의 최근 실행(상태 · 결과 · 커밋)
    py -3 -m Scripts ci-jobs --runs --branch wt/apply5    # 다른 브랜치
    py -3 -m Scripts ci-jobs <실행 id>                    # 그 실행의 잡과 실패한 단계
    py -3 -m Scripts ci-jobs <실행 id> --annotations      # 실패한 잡마다 실패 주석(진 시험 · 크래시 스택 · vcpkg 로그 끝)
    py -3 -m Scripts ci-jobs --job <잡 id> [<잡 id> …]    # 잡의 실패 주석 전문

작업 로그와 아티팩트는 저장소 관리자만 받을 수 있어(공개 저장소도 API 가 403) 밖에서 읽을 수 있는 것은 실행 목록, 잡 단계, 주석뿐입니다.
CI 는 실패를 `CiFailureReport.py` 로 주석에 올립니다. 로그인 없는 API 는 IP 마다 시간당 60 회라, 환경 변수 `GITHUB_TOKEN` 이 있으면 그것을 씁니다.
저장소 이름은 `origin` 원격 주소에서 읽습니다(`--repo owner/name` 으로 바꿀 수 있다).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path
from typing import Any, Callable

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common import getProjectRoot, runGit  # noqa: E402

kApiRoot = "https://api.github.com"
#: `git@github.com:owner/name.git` · `https://github.com/owner/name(.git)` 에서 owner/name.
_kGithubRemote = re.compile(r"github\.com[:/](?P<owner>[^/\s]+)/(?P<name>[^/\s]+?)(?:\.git)?/?$")
kMaxMessageChars = 1500

JsonFetcher = Callable[[str], Any]


def parseGithubRepository(remoteUrl: str) -> str | None:
    """원격 주소에서 `owner/name` 을 꺼냅니다. GitHub 주소가 아니면 None."""
    match = _kGithubRemote.search(remoteUrl.strip())
    return f"{match.group('owner')}/{match.group('name')}" if match else None


def findRepository(root: Path) -> str | None:
    """`origin` 원격의 GitHub 저장소 이름입니다."""
    result = runGit(["remote", "get-url", "origin"], cwd=root)
    return parseGithubRepository(result.stdout) if result.bSucceeded else None


def fetchJson(url: str) -> Any:
    """GitHub API 하나를 읽습니다. `GITHUB_TOKEN` 이 있으면 인증 머리를 붙입니다."""
    import urllib.request  # 부를 때만 — 모듈 머리에 두면 --help 도 http 모듈을 올린다
    headers = {"Accept": "application/vnd.github+json", "User-Agent": "sw-engine-ci-jobs"}
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        headers["Authorization"] = f"Bearer {token}"
    with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=30) as response:
        return json.load(response)


def formatRunLines(payload: dict) -> list[str]:
    """`actions/runs` 응답을 한 줄씩 — id, 상태/결과, 커밋 7 자리, 제목."""
    listLine = []
    for run in payload.get("workflow_runs", []):
        state = run.get("conclusion") or run.get("status") or "?"
        title = (run.get("display_title") or "").replace("\n", " ")[:90]
        listLine.append(f"{run.get('id')}  {run.get('name', '')}  {state}  {str(run.get('head_sha', ''))[:7]}  {title}")
    return listLine


def listFailedJobs(payload: dict) -> list[dict]:
    """`runs/<id>/jobs` 응답 중 실패한 잡입니다."""
    return [job for job in payload.get("jobs", []) if job.get("conclusion") == "failure"]


def formatJobLines(payload: dict) -> list[str]:
    """`runs/<id>/jobs` 응답을 잡마다 한 줄 — id, 이름, 결과, 실패한 단계."""
    listLine = []
    for job in payload.get("jobs", []):
        listFailedStep = [step.get("name", "") for step in job.get("steps", []) if step.get("conclusion") == "failure"]
        failed = f"  | 실패 단계: {', '.join(listFailedStep)}" if listFailedStep else ""
        listLine.append(f"{job.get('id')}  {job.get('name', '')}  {job.get('conclusion') or job.get('status')}{failed}")
    return listLine


def formatFailureAnnotations(listAnnotation: list[dict], maxChars: int = kMaxMessageChars) -> list[str]:
    """`check-runs/<잡 id>/annotations` 응답 중 failure 수준만 — 경로 · 줄 · 제목 다음 줄에 본문."""
    listLine = []
    for annotation in listAnnotation:
        if annotation.get("annotation_level") != "failure":
            continue
        listLine.append(f"-- {annotation.get('path') or ''}:{annotation.get('start_line') or ''} {annotation.get('title') or ''}".rstrip())
        listLine.append((annotation.get("message") or "")[:maxChars])
    return listLine


def printAnnotationsOfJob(fetch: JsonFetcher, repository: str, jobId: str | int) -> None:
    print(f"=== 잡 {jobId}")
    listAnnotation = fetch(f"{kApiRoot}/repos/{repository}/check-runs/{jobId}/annotations?per_page=50")
    listLine = formatFailureAnnotations(listAnnotation)
    print("\n".join(listLine) if listLine else "(실패 주석 없음)")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="GitHub Actions 실행 · 잡 · 실패 주석을 공개 API 로 읽어 보인다.")
    parser.add_argument("run", nargs="?", help="실행 id — 그 실행의 잡을 보인다")
    parser.add_argument("--runs", action="store_true", help="최근 실행 목록")
    parser.add_argument("--branch", default="main", help="--runs 의 브랜치(기본 main)")
    parser.add_argument("--count", type=int, default=20, help="--runs 의 개수(기본 20)")
    parser.add_argument("--annotations", action="store_true", help="실행 id 와 함께: 실패한 잡마다 실패 주석")
    parser.add_argument("--job", nargs="+", default=[], help="이 잡들의 실패 주석")
    parser.add_argument("--repo", default=None, help="owner/name (기본: origin 원격에서)")
    args = parser.parse_args(argv)

    repository = args.repo or findRepository(getProjectRoot())
    if not repository:
        print("[ListCiJobs] origin 이 GitHub 저장소가 아닙니다 — --repo owner/name 을 주세요.", file=sys.stderr)
        return 2
    if not (args.runs or args.run or args.job):
        parser.print_help()
        return 1

    try:
        if args.runs:
            payload = fetchJson(f"{kApiRoot}/repos/{repository}/actions/runs?per_page={args.count}&branch={args.branch}")
            print("\n".join(formatRunLines(payload)) or "(실행 없음)")
        if args.run:
            payload = fetchJson(f"{kApiRoot}/repos/{repository}/actions/runs/{args.run}/jobs?per_page=50")
            print("\n".join(formatJobLines(payload)))
            if args.annotations:
                for job in listFailedJobs(payload):
                    printAnnotationsOfJob(fetchJson, repository, job["id"])
        for jobId in args.job:
            printAnnotationsOfJob(fetchJson, repository, jobId)
    except OSError as error:
        print(f"[ListCiJobs] GitHub API 를 읽지 못했습니다(시간당 한도 · 네트워크): {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
