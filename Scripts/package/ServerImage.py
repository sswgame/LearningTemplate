#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file ServerImage.py
@brief 리눅스 전용 서버 패키지(`Pack.py --target Server` 의 폴더) + 운영 설정 하나를 배포 묶음(tar.gz + systemd 유닛)으로 만든다. 컨테이너 이미지는 선택이다.

| 단계 | 하는 일 |
| --- | --- |
| check | 묶을 서버 실행 파일로 `-check-config -server-config=<설정>` 을 돌린다 — 종료 0 이 아니면 멈춘다(11 = 잘못된 설정). 리눅스가 아니면 WSL 로 돌린다 |
| unit  | `swserver-<게임>.service` — 루트 아닌 사용자, 종료 신호(SIGTERM)로 드레인, 시한은 설정의 드레인 + 저장 시간보다 길게, 비밀은 `EnvironmentFile`(묶음에 없다) |
| tar   | `opt/swserver/<게임>/`(패키지) · `etc/swserver/<게임>.json`(설정) · `etc/systemd/system/<유닛>` 을 한 tar.gz 로 |
| image | `--image <태그>` 이면 `deploy/docker/Dockerfile` 로 `docker build`(선택) |

비밀(토큰 · 비밀번호 · 키)은 묶지 않는다 — 설정의 `*Environment` 칸이 가리키는 환경 변수를 `/etc/swserver/<게임>.env`(운영자가 두는 파일, 0600)가 준다.
언리얼 전용 서버 패키징(BuildCookRun -server) 뒤 운영이 따로 쓰는 유닛 · 이미지를, 같은 설정 검사를 지난 한 벌로 만든다.

사용법:
    py -3 -m Scripts server-image --package Saved/Packages/<패키지> --config Config/Server/<게임>.json --game <게임>
    py -3 -m Scripts server-image ... --image registry.example.com/<게임>-server:1.0.0
    py -3 -m Scripts server-image --self-check
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tarfile
from pathlib import Path, PurePosixPath
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ExitCode import ExitCode  # noqa: E402
from common.HostPlatform import isWindowsHost  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402

kServerExecutable = "Server"
kInstallRoot = PurePosixPath("/opt/swserver")
kConfigRoot = PurePosixPath("/etc/swserver")
kUnitRoot = PurePosixPath("/etc/systemd/system")
kServiceUser = "swserver"
#: 설정의 드레인 · 저장 시간 위에 더하는 여유(초) — systemd 가 SIGKILL 로 드레인을 끊지 않게.
kStopMarginSeconds = 30
kDockerfile = Path("deploy") / "docker" / "Dockerfile"


class ServerImageError(Exception):
    """단계 하나가 실패했다. 메시지가 `[server-image] FAILED` 줄의 이유다."""


def readStopSeconds(config: dict) -> int:
    """systemd `TimeoutStopSec` — 드레인 + 저장 + 여유. 두 칸이 없으면 멈춘다(서버 설정 스키마의 필수 운영 값이다)."""
    try:
        return int(config["_drainWaitSeconds"]) + int(config["_shutdownGraceSeconds"]) + kStopMarginSeconds
    except (KeyError, TypeError, ValueError) as exception:
        raise ServerImageError(f"설정에 _drainWaitSeconds · _shutdownGraceSeconds 정수가 있어야 한다 ({exception!r})") from exception


def makeUnitText(game: str, stopSeconds: int) -> str:
    """systemd 유닛 글. 경로는 묶음의 설치 자리와 같다."""
    installDir = kInstallRoot / game
    return f"""[Unit]
Description={game} dedicated server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User={kServiceUser}
Group={kServiceUser}
WorkingDirectory={installDir}
ExecStart={installDir / kServerExecutable} -server-config={kConfigRoot / (game + '.json')}
# 비밀(토큰 · 비밀번호)은 운영자가 둔 파일에서만 온다 — 없으면 기동하지 않는다(서버가 빈 토큰으로 뜨지 않는 것과 같은 규칙).
EnvironmentFile={kConfigRoot / (game + '.env')}
KillSignal=SIGTERM
TimeoutStopSec={stopSeconds}
Restart=on-failure
RestartSec=5
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
PrivateTmp=true
ReadWritePaths={installDir / 'Saved'}

[Install]
WantedBy=multi-user.target
"""


def toWslPath(path: Path) -> str:
    drive, rest = path.resolve().drive, path.resolve().as_posix()[2:]
    return f"/mnt/{drive[0].lower()}{rest}"


def runConfigCheck(packageDir: Path, configPath: Path) -> None:
    """묶을 실행 파일로 설정을 검사한다. 리눅스 실행 파일이므로 윈도우에서는 WSL 로 돈다 — 둘 다 없으면 멈춘다(검사 없이 묶지 않는다)."""
    executable = packageDir / kServerExecutable
    if isWindowsHost():
        command = ["wsl.exe", "--", toWslPath(executable), "-check-config", f"-server-config={toWslPath(configPath)}"]
    else:
        command = [str(executable), "-check-config", f"-server-config={configPath.resolve()}"]
    try:
        result = subprocess.run(command, cwd=packageDir, check=False)
    except OSError as exception:
        raise ServerImageError(f"설정 검사를 돌리지 못했다: {exception}") from exception
    if result.returncode != 0:
        raise ServerImageError(f"설정 검사 실패(종료 {result.returncode} — 11 은 잘못된 설정): {configPath}")


def writeArchive(outPath: Path, packageDir: Path, configPath: Path, game: str, unitText: str) -> None:
    unitName = f"swserver-{game}.service"
    unitFile = outPath.with_name(unitName)
    unitFile.write_text(unitText, encoding="utf-8", newline="\n")
    try:
        with tarfile.open(outPath, "w:gz") as archive:
            def setMode(info: tarfile.TarInfo) -> tarfile.TarInfo:
                info.uid = info.gid = 0
                info.uname = info.gname = "root"
                if info.name.endswith(f"/{kServerExecutable}"):
                    info.mode = 0o755
                return info
            archive.add(packageDir, arcname=str((kInstallRoot / game).relative_to("/")), filter=setMode)
            archive.add(configPath, arcname=str((kConfigRoot / f"{game}.json").relative_to("/")), filter=setMode)
            archive.add(unitFile, arcname=str((kUnitRoot / unitName).relative_to("/")), filter=setMode)
    finally:
        unitFile.unlink()


def buildImage(projectRoot: Path, packageDir: Path, tag: str) -> None:
    command = ["docker", "build", "-f", str(projectRoot / kDockerfile), "-t", tag, str(packageDir)]
    if subprocess.run(command, check=False).returncode != 0:
        raise ServerImageError(f"docker build 실패: {tag}")


def makeServerImage(packageDir: Path, configPath: Path, game: str, outDir: Path, imageTag: str | None) -> Path:
    if (packageDir / kServerExecutable).is_file() is False:
        raise ServerImageError(f"서버 패키지에 `{kServerExecutable}` 가 없다: {packageDir} — `Pack.py --target Server` 의 리눅스 패키지 폴더를 준다")
    try:
        config = json.loads(configPath.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exception:
        raise ServerImageError(f"설정을 읽지 못했다: {configPath} ({exception})") from exception
    stopSeconds = readStopSeconds(config)
    runConfigCheck(packageDir, configPath)
    outDir.mkdir(parents=True, exist_ok=True)
    outPath = outDir / f"{game}-server.tar.gz"
    writeArchive(outPath, packageDir, configPath, game, makeUnitText(game, stopSeconds))
    if imageTag:
        buildImage(getProjectRoot(), packageDir, imageTag)
    return outPath


def runSelfCheck() -> None:
    unit = makeUnitText("Arena", readStopSeconds({"_drainWaitSeconds": 60, "_shutdownGraceSeconds": 15}))
    assert "TimeoutStopSec=105" in unit, "시한 = 드레인 + 저장 + 여유"
    assert "ExecStart=/opt/swserver/Arena/Server -server-config=/etc/swserver/Arena.json" in unit
    assert "EnvironmentFile=/etc/swserver/Arena.env" in unit and "User=swserver" in unit
    try:
        readStopSeconds({"_drainWaitSeconds": 60})
        raise AssertionError("빠진 칸은 멈춘다")
    except ServerImageError:
        pass
    assert isWindowsHost() is False or toWslPath(Path("C:/a/b")) == "/mnt/c/a/b"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="리눅스 전용 서버 패키지 + 설정 → 배포 tar.gz(systemd 유닛), 선택으로 컨테이너 이미지")
    parser.add_argument("--package", type=Path, help="`Pack.py --target Server` 가 만든 리눅스 서버 패키지 폴더")
    parser.add_argument("--config", type=Path, help="운영 설정(ServerConfig JSON) — 비밀은 넣지 않는다")
    parser.add_argument("--game", help="게임 이름(설치 폴더 · 유닛 이름)")
    parser.add_argument("--output", type=Path, default=Path("Saved") / "ServerImages")
    parser.add_argument("--image", help="docker 이미지 태그 — 주면 deploy/docker/Dockerfile 로 이미지도 짓는다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[server-image] self-check OK")
        return ExitCode.Passed
    if not (arguments.package and arguments.config and arguments.game):
        parser.error("--package · --config · --game 이 모두 있어야 한다")
    try:
        outPath = makeServerImage(arguments.package, arguments.config, arguments.game, arguments.output, arguments.image)
    except ServerImageError as exception:
        print(f"[server-image] FAILED: {exception}")
        return ExitCode.Violation
    print(f"[server-image] OK: {outPath}")
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
