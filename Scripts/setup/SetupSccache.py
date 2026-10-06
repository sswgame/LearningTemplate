#!/usr/bin/env python3
"""
Scripts/setup/SetupSccache.py

sccache(컴파일 캐시) 확보와 **경로 무관 캐시** 설정.

- 확보: search_paths 의 `sccache_version` 을 `Tools/Sccache` 에 받는다(`sccache_sha256` 로 검사). 찾은 sccache 의 버전이 다르면
  서버를 멈추고 바꾼다 — Windows 는 도는 서버가 실행 파일을 잡고 있다. 빌드가 돌고 있으면 바꾸지 않는다.
- 경로 무관 캐시: sccache 의 캐시 키에는 소스 · 인클루드 · 인자의 절대 경로가 들어, 같은 커밋을 다른 워크트리에서 지으면 맞지 않는다.
  sccache 0.14+ 의 `basedirs`(ccache `CCACHE_BASEDIR` 와 같은 생각, 가장 긴 접두를 지운 경로로 키를 만든다)에 **이 저장소와 모든 git
  워크트리 루트**를 적는다. 캐시 폴더도 하나(주 저장소 `build/sccache_cache`) — 서버를 띄운 워크트리에 따라 캐시가 갈리지 않게.
- 설정 파일은 sccache 가 늘 읽는 자리(`SCCACHE_CONF`, 없으면 Windows `%APPDATA%/Mozilla/sccache/config/config` ·
  리눅스 `~/.config/sccache/config`)라 어느 워크트리 · IDE 가 서버를 띄워도 같다. 첫 줄 표지가 있는 파일(이 스크립트가 쓴 것)만 고쳐 쓴다.
- 서버는 설정을 뜰 때만 읽는다 — 내용이 바뀌었고 빌드가 돌지 않으면 `--stop-server`(다음 컴파일이 새 설정으로 띄운다).
  빌드가 돌고 있으면 멈추지 않는다(도는 컴파일이 깨진다) — 서버가 쉬다 내려간 뒤(기본 600 초) 적용된다.

주의: 다른 워크트리에서 적중한 오브젝트는 그 워크트리의 경로를 품는다(`/Z7` 디버그 정보의 소스 경로 · `__FILE__`) — 디버거가 소스를
다른 워크트리에서 열 수 있다. 캐시 키에서만 경로를 지우고 내용은 고치지 않는 것이 basedirs 의 정의다.

  py -3 Scripts/setup/SetupSccache.py                # 확보 + 설정 갱신(+ 필요하면 서버 재시작)
  py -3 Scripts/setup/SetupSccache.py --config-only  # 워크트리를 만들거나 지운 뒤 — 루트 목록만 다시 쓴다
"""

from __future__ import annotations

import argparse
import json
import logging
import os
import platform
import re
import shutil
import sys
import time
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common import (
    extractTarSafe,
    getProjectRoot,
    kKeySccacheDownloadUrls,
    kKeySccacheSearchRoots,
    kKeySccacheSha256,
    kKeySccacheToolsSubdir,
    kKeySccacheVersion,
    loadSearchPaths,
    normalizePath,
    runProcess,
    toolsCacheDir,
)
from setup.HostTools import setupBuildToolInternal

#: 이 스크립트가 쓴 설정 파일의 첫 줄 — 없으면 사용자가 손으로 쓴 파일로 보고 건드리지 않는다.
kSccacheConfigMarker = "# Scripts/setup/SetupSccache.py 가 쓴다 — 손으로 고치지 말 것(워크트리 목록에서 다시 만든다)."

#: 이 이름의 프로세스가 돌면 빌드 중으로 본다 — 서버를 멈추거나 실행 파일을 바꾸지 않는다. cmake 는 넣지 않는다 — 구성(configure)이
#: SetupEnvironment 를 부르므로 자기 부모를 빌드로 보게 된다.
kSetBuildProcessName = frozenset({"ninja", "clang-cl", "clang", "clang++"})

#: 캐시 폴더 — 주 저장소(첫 워크트리) 기준. 프리셋 빌드 폴더가 아니라 모든 워크트리가 나눠 쓰는 자리다.
kSccacheCacheDirRelative = "build/sccache_cache"

#: `basedirs` 를 처음 읽는 sccache 버전(0.14 — `SCCACHE_BASEDIRS`; 0.18 부터 컴파일러 인자의 경로도 지운다).
kBasedirsMinimumVersion = (0, 14)

#: 멈춘 서버가 실행 파일을 놓을 때까지 기다리는 시간.
kExeReleaseWaitSeconds = 10.0


def sccacheExeName() -> str:
    return "sccache.exe" if platform.system() == "Windows" else "sccache"


def localSccacheExe() -> Path:
    """`Tools/Sccache` 의 실행 파일 자리(없을 수 있다)."""
    return getProjectRoot() / str(loadSearchPaths().get(kKeySccacheToolsSubdir) or "Tools/Sccache") / sccacheExeName()


def readSccacheVersion(exePath: Path) -> str:
    """`sccache --version` 의 버전 문자열("0.18.0") — 띄우지 못하면 빈 문자열."""
    result = runProcess([exePath, "--version"], timeoutSeconds=30)
    match = re.search(r"sccache\s+(\d+(?:\.\d+)+)", result.stdout) if result.bSucceeded else None
    return match.group(1) if match else ""


def isBasedirsSupported(exePath: Path) -> bool:
    """`basedirs` 를 아는 버전(0.14+)인가 — 모르는 버전은 그 키가 든 설정 파일을 읽다 서버가 뜨지 못할 수 있다."""
    version = readSccacheVersion(exePath)
    return bool(version) and tuple(int(part) for part in version.split(".")[:2]) >= kBasedirsMinimumVersion


def isPinnedSccache(exePath: Path) -> bool:
    """search_paths 가 고정한 버전인가."""
    pinned = str(loadSearchPaths().get(kKeySccacheVersion) or "")
    return bool(pinned) and readSccacheVersion(exePath) == pinned


def isBuildRunning() -> bool:
    """이 기계에서 빌드(ninja · 컴파일러 · cmake)가 도는가 — 어느 워크트리의 것이든."""
    if platform.system() == "Windows":
        result = runProcess(["tasklist", "/FO", "CSV", "/NH"], timeoutSeconds=30)
        names = [line.split(",", 1)[0].strip('"') for line in result.stdout.splitlines() if line]
    else:
        result = runProcess(["ps", "-eo", "comm="], timeoutSeconds=30)
        names = result.stdout.split()
    return any(Path(name).stem.lower() in kSetBuildProcessName for name in names)


def stopSccacheServer(exePath: Path) -> bool:
    """도는 서버를 멈춘다 — 서버가 없었으면 거짓."""
    return runProcess([exePath, "--stop-server"], timeoutSeconds=60).bSucceeded


def sccacheConfigPath() -> Path:
    """sccache 가 읽는 설정 파일 — `SCCACHE_CONF` 가 이긴다."""
    if configured := os.environ.get("SCCACHE_CONF"):
        return Path(configured)
    if platform.system() == "Windows":
        return Path(os.environ.get("APPDATA", str(Path.home() / "AppData" / "Roaming"))) / "Mozilla" / "sccache" / "config" / "config"
    if platform.system() == "Darwin":
        return Path.home() / "Library" / "Application Support" / "Mozilla.sccache" / "config"
    return Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))) / "sccache" / "config"


def listWorktreeRoot(projectRoot: Path) -> list[str]:
    """
    `git worktree list --porcelain` 의 루트 — 첫째가 주 저장소. 폴더가 사라진 워크트리(prunable)는 뺀다.
    git 저장소가 아니면(`.git` 없음) `projectRoot` 하나. 저장소인데 git 이 실패하면 빈 목록 — 루트 하나짜리 설정으로 덮어써
    다른 워크트리의 적중과 공용 캐시 폴더를 잃지 않게.
    """
    if not (projectRoot / ".git").exists():
        return [normalizePath(projectRoot)]
    result = runProcess(["git", "-C", projectRoot, "worktree", "list", "--porcelain"], timeoutSeconds=60)
    if not result.bSucceeded:
        return []
    listRoot = [line[len("worktree "):].strip() for line in result.stdout.splitlines() if line.startswith("worktree ")]
    return [normalizePath(root) for root in listRoot if Path(root).is_dir()]


def renderSccacheConfig(listRoot: Sequence[str], cacheDir: str) -> str:
    """설정 파일 본문(TOML). 문자열은 JSON 따옴표 — 이 경로들에서 TOML 기본 문자열과 같다."""
    lines = [kSccacheConfigMarker,
             "# basedirs: 캐시 키에서 지우는 루트(이 저장소 + git 워크트리). 가장 긴 접두가 이긴다.",
             f"basedirs = [{', '.join(json.dumps(root) for root in listRoot)}]",
             "",
             "[cache.disk]",
             f"dir = {json.dumps(cacheDir)}",
             ""]
    return "\n".join(lines)


def applySccacheConfig(projectRoot: Path, exePath: Path | None, *, bRestart: bool = True) -> bool:
    """
    루트 목록으로 설정 파일을 다시 쓰고, 바뀌었으면 서버를 다시 띄우게 한다. 설정이 지금 목록과 같으면(또는 같게 만들었으면) 참.
    사용자가 손으로 쓴 설정 파일(표지 없음) · basedirs 를 모르는 sccache · 워크트리 목록을 못 읽음 — 건드리지 않고 거짓.
    `exePath` 가 None 이면 버전을 보지 않고 서버도 건드리지 않는다.
    """
    logger = logging.getLogger("SetupSccache")
    if exePath is not None and not isBasedirsSupported(exePath):
        logger.warning(f"[SetupSccache] {exePath} 는 basedirs 를 모른다(0.14 미만) — 설정을 쓰지 않는다.")
        return False
    listRoot = listWorktreeRoot(projectRoot)
    if not listRoot:
        logger.warning("[SetupSccache] git worktree 목록을 읽지 못했다 — 설정을 그대로 둔다.")
        return False
    cacheDir = normalizePath(Path(listRoot[0]) / kSccacheCacheDirRelative)
    configPath = sccacheConfigPath()
    text = renderSccacheConfig(listRoot, cacheDir)

    previous = configPath.read_text(encoding="utf-8") if configPath.is_file() else ""
    if previous and not previous.startswith(kSccacheConfigMarker):
        logger.warning(f"[SetupSccache] {configPath} 는 손으로 쓴 설정이다 — 건드리지 않는다. basedirs 를 직접 넣어라: {listRoot}")
        return False
    if previous == text:
        logger.info(f"[SetupSccache] 설정 그대로: {configPath} (루트 {len(listRoot)})")
        return True

    configPath.parent.mkdir(parents=True, exist_ok=True)
    configPath.write_text(text, encoding="utf-8")
    logger.info(f"[SetupSccache] 설정을 썼다: {configPath} (루트 {len(listRoot)}, 캐시 {cacheDir})")
    if not bRestart or exePath is None or not exePath.is_file():
        return True
    if isBuildRunning():
        logger.warning("[SetupSccache] 빌드가 돌고 있어 서버를 멈추지 않는다 — 서버가 쉬다 내려간 뒤(또는 `sccache --stop-server` 뒤) 적용된다.")
    elif stopSccacheServer(exePath):
        logger.info("[SetupSccache] 서버를 멈췄다 — 다음 컴파일이 새 설정으로 띄운다.")
    return True


def replaceSccacheExeInternal(archive: Path, destDir: Path, localExe: Path, exe: str) -> None:
    """받은 압축에서 sccache 를 꺼내 `localExe` 자리에 둔다. 옛 실행 파일을 서버가 잡고 있으면 서버를 먼저 멈춘다."""
    tempExtDir = toolsCacheDir() / "sccache_extracted"
    if tempExtDir.is_dir():
        shutil.rmtree(tempExtDir, ignore_errors=True)
    extractTarSafe(archive, tempExtDir, mode="r:gz")
    try:
        extractedExe = next((cand for cand in tempExtDir.rglob(exe) if cand.is_file()), None)
        if extractedExe is None:
            raise RuntimeError(f"{exe} not found in {archive}")
        if localExe.is_file():
            if isBuildRunning():
                raise RuntimeError("a build is running — stop it and rerun to replace sccache")
            stopSccacheServer(localExe)
        deadline = time.monotonic() + kExeReleaseWaitSeconds
        while True:
            try:
                shutil.copy2(extractedExe, localExe)
                break
            except PermissionError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.2)
    finally:
        shutil.rmtree(tempExtDir, ignore_errors=True)


def setupSccache() -> str:
    """고정 버전 sccache 를 찾거나 `Tools/Sccache` 에 받고, 경로 무관 캐시 설정을 맞춘다. 실행 파일 경로(없으면 빈 문자열)."""
    path = setupBuildToolInternal(
        "SetupSccache", sccacheExeName(), kKeySccacheToolsSubdir,
        kKeySccacheSearchRoots, kKeySccacheDownloadUrls, replaceSccacheExeInternal,
        acceptExe=isPinnedSccache, sha256Key=kKeySccacheSha256,
    )
    if not path:
        # 빌드가 돌아 교체를 미뤘으면 옛 실행 파일로 계속 짓는다(다음 setup 이 다시 바꾼다).
        localExe = localSccacheExe()
        path = normalizePath(localExe) if localExe.is_file() else ""
    if path:
        applySccacheConfig(getProjectRoot(), Path(path))
    return path


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Install the pinned sccache and write its path-independent cache config.")
    parser.add_argument("--config-only", action="store_true",
                        help="Only rewrite the basedirs / cache dir config (after adding or removing a git worktree).")
    args = parser.parse_args(list(argv) if argv is not None else None)
    logging.basicConfig(level=logging.INFO, format="%(message)s")

    if args.config_only:
        localExe = localSccacheExe()
        exePath = localExe if localExe.is_file() else (Path(found) if (found := shutil.which("sccache")) else None)
        return 0 if applySccacheConfig(getProjectRoot(), exePath) else 1

    if path := setupSccache():
        print(path)
        return 0
    sys.stderr.write("[SetupSccache Error] sccache not available\n")
    return 1


if __name__ == "__main__":
    sys.exit(main())
