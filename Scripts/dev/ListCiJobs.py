#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file ListCiJobs.py
@brief GitHub Actions 의 실행 · 잡 · 실패 주석을 공개 API 로 읽어 보입니다(로그인 없이).

    py -3 -m Scripts ci-jobs --runs                       # main 의 최근 실행(상태 · 결과 · 커밋)
    py -3 -m Scripts ci-jobs --runs --branch wt/apply5    # 다른 브랜치
    py -3 -m Scripts ci-jobs --summary                    # main 의 ci.yml 가장 새로 끝난 실행 한 줄 — 실패한 잡과 연속 실패 수(세션 가드가 부른다)
    py -3 -m Scripts ci-jobs <실행 id>                    # 그 실행의 잡 표 — 잡 · 실패한 단계의 경과 시간
    py -3 -m Scripts ci-jobs <실행 id> --annotations      # 표에 종료 코드 · ctest Timeout 수를 더하고, 실패한 잡마다 실패 주석
    py -3 -m Scripts ci-jobs --job <잡 id> [<잡 id> …]    # 잡의 실패 주석 전문

작업 로그와 아티팩트는 저장소 관리자만 받을 수 있어(공개 저장소도 API 가 403) 밖에서 읽을 수 있는 것은 실행 목록, 잡 단계, 주석뿐입니다.
CI 는 실패를 `CiFailureReport.py` 로 주석에 올립니다. 진 테스트 주석의 제목에는 결과가 붙습니다(`[Timeout 180.0 s / 180 s]` 는 ctest 가 한도에서 죽인 것,
`[exit 1 after 12.3 s / 180 s]` 는 스스로 진 것). 단계의 종료 코드는 러너가 남기는 주석(`Process completed with exit code N`)에서 읽습니다.

로그인 없는 API 는 IP 마다 시간당 60 회라 호출을 아낍니다. 응답은 임시 폴더(`<temp>/sw-ci-jobs-cache`)에 ETag 와 함께 두고,
끝난 실행의 잡 목록과 끝난 잡의 주석은 다시 묻지 않습니다. 나머지는 조건부 요청(If-None-Match)이라 바뀌지 않았으면(304) 한도에서 빠지지 않습니다.
한도를 다 쓰면(403 · 429) 다시 차는 시각을 알리고 3 으로 끝납니다. 환경 변수 `GITHUB_TOKEN` 이 있으면 그것을 씁니다(시간당 5000 회).
저장소 이름은 `origin` 원격 주소에서 읽습니다(`--repo owner/name` 으로 바꿀 수 있다).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import tempfile
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Any, Callable

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common import getProjectRoot, runGit  # noqa: E402

kApiRoot = "https://api.github.com"
#: `git@github.com:owner/name.git` · `https://github.com/owner/name(.git)` 에서 owner/name.
_kGithubRemote = re.compile(r"github\.com[:/](?P<owner>[^/\s]+)/(?P<name>[^/\s]+?)(?:\.git)?/?$")
kMaxMessageChars = 1500
kCacheFolderName = "sw-ci-jobs-cache"
#: 러너가 실패한 단계마다 남기는 주석 본문.
_kExitCode = re.compile(r"Process completed with exit code (-?\d+)")
#: 러너가 잡을 끊었을 때 남기는 주석 본문 → 표에 쓰는 말.
_kRunnerNote = (("has exceeded the maximum execution time", "잡 시간 한도 초과"), ("The operation was canceled", "취소됨"))
#: `CiFailureReport.py` 가 진 테스트 주석에 붙이는 제목 — `ctest <이름> [<결과>]`.
_kCtestTitle = re.compile(r"^ctest (?P<name>\S+) \[(?P<result>[^\]]+)\]$")
_kStepIgnored = ("success", "skipped", None)
_kExitRateLimited = 3
#: `--summary` 가 CI 실패를 알리는 종료 코드. 세션 가드 훅이 이 값일 때만 알린다(0 은 정상, 1 · 2 · 3 은 도구 오류 · 한도).
kExitCiFailing = 4
#: `--summary` 가 보는 워크플로 파일과 실행 수. 끝난 실행의 잡 목록은 캐시되므로 두 번째부터는 가장 새 실행만 묻는다.
kSummaryWorkflow = "ci.yml"
kSummaryRunCount = 5


class RateLimitError(Exception):
    """공개 API 시간당 한도를 다 썼습니다. `resetEpoch` 는 한도가 다시 차는 시각(유닉스 초)입니다."""

    def __init__(self, limit: int | None, resetEpoch: int | None) -> None:
        super().__init__("GitHub API rate limit exceeded")
        self.limit = limit
        self.resetEpoch = resetEpoch


class GithubClient:
    """
    GitHub API 를 읽습니다. 응답을 ETag 와 함께 디스크에 두어 한도를 아낍니다.

    - `fetch(url, isFinal)`: 캐시가 있고 `isFinal(캐시 본문)` 이 참이면 묻지 않고 돌려준다. 아니면 조건부 요청을 보내 304 면 캐시를 쓴다.
    - 한도 초과(403 · 429 이고 남은 횟수 0 또는 본문이 rate limit)는 `RateLimitError` 다.
    """

    def __init__(self, cacheDir: Path | None) -> None:
        self.cacheDir = cacheDir
        self.callCount = 0
        self.cacheHitCount = 0
        self.limit: int | None = None
        self.remaining: int | None = None
        self.resetEpoch: int | None = None

    def fetch(self, url: str, isFinal: Callable[[Any], bool] | None = None) -> Any:
        import urllib.error  # 부를 때만 — 모듈 머리에 두면 --help 도 http 모듈을 올린다
        import urllib.request
        cached = self.readCacheInternal(url)
        if cached is not None and isFinal is not None and isFinal(cached["body"]):
            self.cacheHitCount += 1
            return cached["body"]
        headers = {"Accept": "application/vnd.github+json", "User-Agent": "sw-engine-ci-jobs"}
        token = os.environ.get("GITHUB_TOKEN")
        if token:
            headers["Authorization"] = f"Bearer {token}"
        if cached is not None and cached.get("etag"):
            headers["If-None-Match"] = cached["etag"]
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=30) as response:
                self.readRateInternal(response.headers)
                self.callCount += 1
                body = json.load(response)
                self.writeCacheInternal(url, response.headers.get("ETag"), body)
                return body
        except urllib.error.HTTPError as error:
            self.readRateInternal(error.headers)
            if error.code == 304 and cached is not None:
                self.cacheHitCount += 1
                return cached["body"]
            self.callCount += 1
            if error.code in (403, 429):
                text = error.read().decode("utf-8", "replace") if error.fp is not None else ""
                if self.remaining == 0 or "rate limit" in text.lower():
                    raise RateLimitError(self.limit, self.resetEpoch) from error
            raise

    def peekCache(self, url: str) -> Any:
        """캐시에 있는 본문입니다(묻지 않는다). 없으면 None."""
        cached = self.readCacheInternal(url)
        if cached is None:
            return None
        self.cacheHitCount += 1
        return cached["body"]

    def readRateInternal(self, headers: Any) -> None:
        if headers is None:
            return
        for name, attribute in (("X-RateLimit-Limit", "limit"), ("X-RateLimit-Remaining", "remaining"), ("X-RateLimit-Reset", "resetEpoch")):
            value = headers.get(name)
            if value is not None and value.strip().isdigit():
                setattr(self, attribute, int(value))

    def cachePathInternal(self, url: str) -> Path | None:
        if self.cacheDir is None:
            return None
        return self.cacheDir / (hashlib.sha1(url.encode("utf-8")).hexdigest() + ".json")

    def readCacheInternal(self, url: str) -> dict | None:
        path = self.cachePathInternal(url)
        if path is None or not path.is_file():
            return None
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return None
        return payload if isinstance(payload, dict) and payload.get("url") == url and "body" in payload else None

    def writeCacheInternal(self, url: str, etag: str | None, body: Any) -> None:
        path = self.cachePathInternal(url)
        if path is None:
            return
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps({"url": url, "etag": etag, "body": body}), encoding="utf-8")
        except OSError:
            pass  # 캐시는 덤이다 — 못 쓰면 다음에 다시 묻는다

    def describeQuota(self) -> str:
        """`API 호출 2 회 · 캐시 3 회 · 남은 한도 41/60 (15:20 에 다시 참)` 한 줄입니다."""
        quota = f" · 남은 한도 {self.remaining}/{self.limit}" if self.remaining is not None and self.limit is not None else ""
        reset = f" ({formatClock(self.resetEpoch)} 에 다시 참)" if self.resetEpoch is not None and quota else ""
        return f"API 호출 {self.callCount} 회 · 캐시 {self.cacheHitCount} 회{quota}{reset}"


@dataclass
class JobOutcome:
    """잡 하나의 실패 주석에서 읽은 것 — 단계 종료 코드(주석 순서), 러너 메모, ctest 결과(이름, 결과)."""

    listExitCode: list[int] = field(default_factory=list)
    listNote: list[str] = field(default_factory=list)
    listCtestResult: list[tuple[str, str]] = field(default_factory=list)


def formatClock(epochSeconds: int) -> str:
    """유닉스 초를 로컬 `HH:MM` 으로."""
    return time.strftime("%H:%M", time.localtime(epochSeconds))


def parseGithubRepository(remoteUrl: str) -> str | None:
    """원격 주소에서 `owner/name` 을 꺼냅니다. GitHub 주소가 아니면 None."""
    match = _kGithubRemote.search(remoteUrl.strip())
    return f"{match.group('owner')}/{match.group('name')}" if match else None


def findRepository(root: Path) -> str | None:
    """`origin` 원격의 GitHub 저장소 이름입니다."""
    result = runGit(["remote", "get-url", "origin"], cwd=root)
    return parseGithubRepository(result.stdout) if result.bSucceeded else None


def parseTimestamp(text: str | None) -> datetime | None:
    """API 의 `2026-10-11T14:58:22Z` 를 읽습니다. 없거나 못 읽으면 None."""
    if not text:
        return None
    try:
        return datetime.fromisoformat(text.replace("Z", "+00:00"))
    except ValueError:
        return None


def formatDuration(startedAt: str | None, completedAt: str | None) -> str:
    """두 시각 사이를 `31s` · `11m44s` · `1h02m03s` 로. 하나라도 없으면 빈 문자열."""
    begin = parseTimestamp(startedAt)
    end = parseTimestamp(completedAt)
    if begin is None or end is None:
        return ""
    seconds = max(0, int((end - begin).total_seconds()))
    hours, rest = divmod(seconds, 3600)
    minutes, seconds = divmod(rest, 60)
    if hours:
        return f"{hours}h{minutes:02d}m{seconds:02d}s"
    return f"{minutes}m{seconds:02d}s" if minutes else f"{seconds}s"


def isRunFinished(payload: Any) -> bool:
    """`runs/<id>/jobs` 응답의 잡이 모두 끝났는가 — 끝났으면 다시 묻지 않아도 된다."""
    listJob = payload.get("jobs", []) if isinstance(payload, dict) else []
    return bool(listJob) and all(job.get("status") == "completed" for job in listJob)


def isAlwaysFinal(payload: Any) -> bool:
    """끝난 잡의 주석은 바뀌지 않는다."""
    return True


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


def selectJudgedRuns(listRun: list[dict], count: int = kSummaryRunCount) -> list[dict]:
    """끝난 실행 중 결과를 낸 것(새것부터 `count` 개)입니다. 취소된 실행은 뺀다 — main 은 기다리던 실행을 더 새 push 가 취소하므로 결과가 아니다."""
    return [run for run in listRun if run.get("conclusion") != "cancelled"][:count]


def collectFailureStreaks(listJobPayload: list[dict]) -> list[tuple[str, int]]:
    """
    끝난 실행들(새것부터)의 `runs/<id>/jobs` 응답에서, 가장 새 실행에서 실패한 잡마다 새것부터 이어서 실패한 실행 수입니다.

    잡은 이름으로 잇는다. 그 잡이 없는 실행(문서만 바뀌어 빌드 잡을 건너뛴 실행)은 세지도 끊지도 않는다 — 그 실행은 그 잡을 보지 않았다.
    """
    if not listJobPayload:
        return []
    listStreak = []
    for job in listFailedJobs(listJobPayload[0]):
        name = job["name"]
        count = 0
        for payload in listJobPayload:
            listSameJob = [other for other in payload.get("jobs", []) if other["name"] == name]
            if not listSameJob:
                continue
            if listSameJob[0].get("conclusion") != "failure":
                break
            count += 1
        listStreak.append((name, count))
    return listStreak


def formatCiSummary(branch: str, listRun: list[dict], listStreak: list[tuple[str, int]]) -> str:
    """세션 시작에 보일 한 줄 — 가장 새로 끝난 실행의 결과와, 실패한 잡마다 이어서 실패한 실행 수."""
    if not listRun:
        return f"{branch} CI: 끝난 실행이 없다"
    run = listRun[0]
    head = f"{branch} CI 실행 {run.get('id')} ({str(run.get('head_sha', ''))[:7]}, {str(run.get('updated_at') or '')[:10]})"
    if not listStreak:
        return f"{head}: {run.get('conclusion') or run.get('status')}"
    jobs = ", ".join(f"{name} {count}회 연속" for name, count in listStreak)
    return f"{head}: 실패 — {jobs}. `py -3 -m Scripts ci-jobs {run.get('id')} --annotations` 로 원인을 볼 것"


def collectJobOutcome(listAnnotation: list[dict]) -> JobOutcome:
    """잡의 주석에서 단계 종료 코드 · 러너 메모 · ctest 결과를 모읍니다."""
    outcome = JobOutcome()
    for annotation in listAnnotation:
        if annotation.get("annotation_level") != "failure":
            continue
        message = annotation.get("message") or ""
        exitMatch = _kExitCode.search(message)
        if exitMatch:
            outcome.listExitCode.append(int(exitMatch.group(1)))
        for needle, note in _kRunnerNote:
            if needle in message and note not in outcome.listNote:
                outcome.listNote.append(note)
        titleMatch = _kCtestTitle.match((annotation.get("title") or "").strip())
        if titleMatch:
            outcome.listCtestResult.append((titleMatch.group("name"), titleMatch.group("result")))
    return outcome


def formatJobLines(payload: dict, mapOutcome: dict[int, JobOutcome] | None = None) -> list[str]:
    """
    `runs/<id>/jobs` 응답을 잡마다 한 줄 — id, 이름, 결과, 경과 시간, 실패한 단계(경과 시간 · 종료 코드).

    `mapOutcome`(잡 id → 주석에서 읽은 것)이 있으면 종료 코드를 실패한 단계에 순서대로 붙이고, 러너 메모와 ctest 진 수 · Timeout 수를 더한다.
    """
    listLine = []
    for job in payload.get("jobs", []):
        outcome = (mapOutcome or {}).get(job.get("id"))
        listExitCode = list(outcome.listExitCode) if outcome else []
        listFailedStep = []
        for step in job.get("steps", []):
            if step.get("conclusion") in _kStepIgnored:
                continue
            listDetail = []
            if step.get("conclusion") != "failure":
                listDetail.append(str(step.get("conclusion")))
            duration = formatDuration(step.get("started_at"), step.get("completed_at"))
            if duration:
                listDetail.append(duration)
            if step.get("conclusion") == "failure" and listExitCode:
                listDetail.append(f"exit {listExitCode.pop(0)}")
            listFailedStep.append(step["name"] + (f"({', '.join(listDetail)})" if listDetail else ""))
        listPart = [str(job["id"]), job["name"], str(job.get("conclusion") or job.get("status"))]
        duration = formatDuration(job.get("started_at"), job.get("completed_at"))
        if duration:
            listPart.append(duration)
        if listFailedStep:
            listPart.append(f"| 실패 단계: {', '.join(listFailedStep)}")
        if listExitCode:
            listPart.append(f"| 종료 코드: {', '.join(str(code) for code in listExitCode)}")
        if outcome and outcome.listCtestResult:
            timeoutCount = sum(1 for _, result in outcome.listCtestResult if result.startswith("Timeout"))
            listPart.append(f"| ctest 진 것 {len(outcome.listCtestResult)} (Timeout {timeoutCount})")
        if outcome and outcome.listNote:
            listPart.append(f"| {', '.join(outcome.listNote)}")
        listLine.append("  ".join(listPart))
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


def makeAnnotationsUrl(repository: str, jobID: str | int) -> str:
    """잡의 주석 주소입니다(한 쪽 50 개 — GitHub 가 잡마다 보이는 상한과 같다)."""
    return f"{kApiRoot}/repos/{repository}/check-runs/{jobID}/annotations?per_page=50"


def fetchAnnotations(client: GithubClient, repository: str, jobID: str | int, bJobFinished: bool) -> list[dict]:
    """잡의 주석입니다. 끝난 잡이고 캐시에 있으면 묻지 않습니다."""
    return client.fetch(makeAnnotationsUrl(repository, jobID), isAlwaysFinal if bJobFinished else None)


def printAnnotations(jobID: str | int, listAnnotation: list[dict]) -> None:
    print(f"=== 잡 {jobID}")
    listLine = formatFailureAnnotations(listAnnotation)
    print("\n".join(listLine) if listLine else "(실패 주석 없음)")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="GitHub Actions 실행 · 잡 · 실패 주석을 공개 API 로 읽어 보인다.")
    parser.add_argument("run", nargs="?", help="실행 id — 그 실행의 잡을 보인다")
    parser.add_argument("--runs", action="store_true", help="최근 실행 목록")
    parser.add_argument("--summary", action="store_true",
                        help=f"--branch 의 {kSummaryWorkflow} 가장 새로 끝난 실행 한 줄(실패한 잡과 연속 실패 수). 실패면 종료 코드 {kExitCiFailing}")
    parser.add_argument("--branch", default="main", help="--runs 의 브랜치(기본 main)")
    parser.add_argument("--count", type=int, default=20, help="--runs 의 개수(기본 20)")
    parser.add_argument("--annotations", action="store_true", help="실행 id 와 함께: 표에 종료 코드를 더하고 실패한 잡마다 실패 주석")
    parser.add_argument("--job", nargs="+", default=[], help="이 잡들의 실패 주석")
    parser.add_argument("--repo", default=None, help="owner/name (기본: origin 원격에서)")
    parser.add_argument("--no-cache", action="store_true", help="응답 캐시(임시 폴더)를 쓰지 않는다")
    args = parser.parse_args(argv)

    repository = args.repo or findRepository(getProjectRoot())
    if not repository:
        print("[ListCiJobs] origin 이 GitHub 저장소가 아닙니다 — --repo owner/name 을 주세요.", file=sys.stderr)
        return 2
    if not (args.runs or args.run or args.job or args.summary):
        parser.print_help()
        return 1

    client = GithubClient(None if args.no_cache else Path(tempfile.gettempdir()) / kCacheFolderName)
    exitCode = 0
    try:
        if args.summary:
            runsUrl = (f"{kApiRoot}/repos/{repository}/actions/workflows/{kSummaryWorkflow}/runs"
                       f"?per_page={kSummaryRunCount * 4}&branch={args.branch}&status=completed")
            listRun = selectJudgedRuns(client.fetch(runsUrl).get("workflow_runs", []))
            listJobPayload = [client.fetch(f"{kApiRoot}/repos/{repository}/actions/runs/{run['id']}/jobs?per_page=100", isRunFinished)
                              for run in listRun]
            listStreak = collectFailureStreaks(listJobPayload)
            print(formatCiSummary(args.branch, listRun, listStreak))
            exitCode = kExitCiFailing if listStreak else 0
        if args.runs:
            payload = client.fetch(f"{kApiRoot}/repos/{repository}/actions/runs?per_page={args.count}&branch={args.branch}")
            print("\n".join(formatRunLines(payload)) or "(실행 없음)")
        if args.run:
            payload = client.fetch(f"{kApiRoot}/repos/{repository}/actions/runs/{args.run}/jobs?per_page=100", isRunFinished)
            mapAnnotation: dict[int, list[dict]] = {}
            mapOutcome: dict[int, JobOutcome] = {}
            for job in listFailedJobs(payload):
                bFinished = job.get("status") == "completed"
                if args.annotations:
                    mapAnnotation[job["id"]] = fetchAnnotations(client, repository, job["id"], bFinished)
                    mapOutcome[job["id"]] = collectJobOutcome(mapAnnotation[job["id"]])
                elif bFinished:
                    # 묻지 않고 캐시에 있는 것만 표에 쓴다 — 전에 --annotations 로 읽은 잡이다.
                    cached = client.peekCache(makeAnnotationsUrl(repository, job["id"]))
                    if isinstance(cached, list):
                        mapOutcome[job["id"]] = collectJobOutcome(cached)
            print("\n".join(formatJobLines(payload, mapOutcome)))
            if not args.annotations and len(mapOutcome) < len(listFailedJobs(payload)):
                print("(종료 코드 · ctest Timeout · 실패 주석은 --annotations — 실패한 잡마다 API 1 회)")
            for jobID, listAnnotation in mapAnnotation.items():
                printAnnotations(jobID, listAnnotation)
        for jobID in args.job:
            printAnnotations(jobID, fetchAnnotations(client, repository, jobID, False))
    except RateLimitError as error:
        reset = f" — {formatClock(error.resetEpoch)} 에 다시 찹니다" if error.resetEpoch is not None else ""
        print(f"[ListCiJobs] GitHub API 시간당 한도({error.limit or '?'} 회)를 다 썼습니다{reset}. "
              "GITHUB_TOKEN 을 두면 시간당 5000 회입니다.", file=sys.stderr)
        return _kExitRateLimited
    except OSError as error:
        print(f"[ListCiJobs] GitHub API 를 읽지 못했습니다(네트워크 · 없는 id): {error}", file=sys.stderr)
        return 1
    finally:
        print(f"[ListCiJobs] {client.describeQuota()}", file=sys.stderr)
    return exitCode


if __name__ == "__main__":
    sys.exit(main())
