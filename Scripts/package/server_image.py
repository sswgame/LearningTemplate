#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file server_image.py
@brief 리눅스 전용 서버 패키지(`pack.py --target Server` 의 폴더) + 운영 설정 하나를 배포 묶음(tar.gz + systemd 유닛)으로 만든다. 컨테이너 이미지는 선택이다.

| 단계 | 하는 일 |
| --- | --- |
| check | 묶을 서버 실행 파일로 `-check-config -server-config=<설정>` 을 돌린다 — 종료 0 이 아니면 멈춘다(11 = 잘못된 설정). 리눅스가 아니면 WSL 로 돌린다 |
| unit  | `swserver-<게임>.service` — 루트 아닌 사용자, 종료 신호(SIGTERM)로 드레인, 시한은 설정의 드레인 + 저장 시간보다 길게, 비밀은 `EnvironmentFile`(묶음에 없다) |
| tar   | `opt/swserver/<게임>/`(패키지) · `etc/swserver/<게임>.json`(설정) · `etc/systemd/system/<유닛>` · `etc/sysusers.d` · `etc/tmpfiles.d` 조각을 한 tar.gz 로 |
| image | `--image <태그>` 이면 `deploy/docker/Dockerfile` 로 `docker build`(선택) |

비밀(토큰 · 비밀번호 · 키)은 묶지 않는다 — 설정의 `*Environment` 칸이 가리키는 환경 변수를 `/etc/swserver/<게임>.env`(운영자가 두는 파일, 0600)가 준다.
설치(대상 기계, 루트): `tar -xzf <묶음> -C /` → `systemd-sysusers && systemd-tmpfiles --create` → `systemctl enable --now swserver-<게임>`. 가운데 줄이 서비스 사용자와
쓰기 폴더(`Saved`)를 만든다 — 유닛은 그 폴더만 쓸 수 있고(`ProtectSystem=strict`), 없으면 systemd 가 유닛을 띄우지 못한다.
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
import tarfile
import tempfile
from pathlib import Path, PurePosixPath

from common.command import Command, CommandError, ScriptContext
from common.exit_code import ExitCode
from common.host_platform import is_windows_host
from common.paths import get_project_root

SERVER_EXECUTABLE = "Server"
INSTALL_ROOT = PurePosixPath("/opt/swserver")
CONFIG_ROOT = PurePosixPath("/etc/swserver")
UNIT_ROOT = PurePosixPath("/etc/systemd/system")
USER_FRAGMENT_ROOT = PurePosixPath("/etc/sysusers.d")
FOLDER_FRAGMENT_ROOT = PurePosixPath("/etc/tmpfiles.d")
SERVICE_USER = "swserver"
#: 설정의 드레인 · 저장 시간 위에 더하는 여유(초) — systemd 가 SIGKILL 로 드레인을 끊지 않게.
STOP_MARGIN_SECONDS = 30
DOCKERFILE = Path("deploy") / "docker" / "Dockerfile"


class ServerImageError(CommandError):
    """단계 하나가 실패했다. 메시지가 `[server-image] FAILED` 줄의 이유다."""


def read_stop_seconds(config: dict) -> int:
    """systemd `TimeoutStopSec` — 드레인 + 저장 + 여유. 두 칸이 없으면 멈춘다(서버 설정 스키마의 필수 운영 값이다)."""
    try:
        return int(config["_drainWaitSeconds"]) + int(config["_shutdownGraceSeconds"]) + STOP_MARGIN_SECONDS
    except (KeyError, TypeError, ValueError) as exception:
        raise ServerImageError(f"설정에 _drainWaitSeconds · _shutdownGraceSeconds 정수가 있어야 한다 ({exception!r})") from exception


def make_unit_text(game: str, stop_seconds: int) -> str:
    """systemd 유닛 글. 경로는 묶음의 설치 자리와 같다."""
    install_dir = INSTALL_ROOT / game
    return f"""[Unit]
Description={game} dedicated server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User={SERVICE_USER}
Group={SERVICE_USER}
WorkingDirectory={install_dir}
ExecStart={install_dir / SERVER_EXECUTABLE} -server-config={CONFIG_ROOT / (game + '.json')}
# 비밀(토큰 · 비밀번호)은 운영자가 둔 파일에서만 온다 — 없으면 기동하지 않는다(서버가 빈 토큰으로 뜨지 않는 것과 같은 규칙).
EnvironmentFile={CONFIG_ROOT / (game + '.env')}
KillSignal=SIGTERM
TimeoutStopSec={stop_seconds}
Restart=on-failure
RestartSec=5
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
PrivateTmp=true
ReadWritePaths={install_dir / 'Saved'}

[Install]
WantedBy=multi-user.target
"""


def make_service_user_text() -> str:
    """서비스 사용자를 만드는 sysusers.d 조각(같은 이름의 그룹도 생긴다). https://www.freedesktop.org/software/systemd/man/latest/sysusers.d.html"""
    return f'u {SERVICE_USER} - "SW Engine dedicated server" {INSTALL_ROOT}\n'


def make_writable_folder_text(game: str) -> str:
    """쓰기 폴더를 서비스 사용자 것으로 만드는 tmpfiles.d 조각. https://www.freedesktop.org/software/systemd/man/latest/tmpfiles.d.html"""
    return f"d {INSTALL_ROOT / game / 'Saved'} 0750 {SERVICE_USER} {SERVICE_USER} -\n"


def to_wsl_path(path: Path) -> str:
    drive, rest = path.resolve().drive, path.resolve().as_posix()[2:]
    return f"/mnt/{drive[0].lower()}{rest}"


def run_config_check(package_dir: Path, config_path: Path) -> None:
    """묶을 실행 파일로 설정을 검사한다. 리눅스 실행 파일이므로 윈도우에서는 WSL 로 돈다 — 둘 다 없으면 멈춘다(검사 없이 묶지 않는다)."""
    executable = package_dir / SERVER_EXECUTABLE
    if is_windows_host():
        command = ["wsl.exe", "--", to_wsl_path(executable), "-check-config", f"-server-config={to_wsl_path(config_path)}"]
    else:
        command = [str(executable), "-check-config", f"-server-config={config_path.resolve()}"]
    try:
        result = subprocess.run(command, cwd=package_dir, check=False)
    except OSError as exception:
        raise ServerImageError(f"설정 검사를 돌리지 못했다: {exception}") from exception
    if result.returncode != 0:
        raise ServerImageError(f"설정 검사 실패(종료 {result.returncode} — 11 은 잘못된 설정): {config_path}")


def write_archive(out_path: Path, package_dir: Path, config_path: Path, game: str, unit_text: str) -> None:
    map_text = {
        UNIT_ROOT / f"swserver-{game}.service": unit_text,
        USER_FRAGMENT_ROOT / f"{SERVICE_USER}.conf": make_service_user_text(),
        FOLDER_FRAGMENT_ROOT / f"swserver-{game}.conf": make_writable_folder_text(game),
    }
    with tempfile.TemporaryDirectory(prefix="swserverimage-") as folder:
        with tarfile.open(out_path, "w:gz") as archive:
            def set_mode(info: tarfile.TarInfo) -> tarfile.TarInfo:
                info.uid = info.gid = 0
                info.uname = info.gname = "root"
                if info.name.endswith(f"/{SERVER_EXECUTABLE}"):
                    info.mode = 0o755
                return info
            archive.add(package_dir, arcname=str((INSTALL_ROOT / game).relative_to("/")), filter=set_mode)
            archive.add(config_path, arcname=str((CONFIG_ROOT / f"{game}.json").relative_to("/")), filter=set_mode)
            for index, (path, text) in enumerate(map_text.items()):
                text_file = Path(folder) / str(index)
                text_file.write_text(text, encoding="utf-8", newline="\n")
                archive.add(text_file, arcname=str(path.relative_to("/")), filter=set_mode)


def build_image(project_root: Path, package_dir: Path, tag: str) -> None:
    command = ["docker", "build", "-f", str(project_root / DOCKERFILE), "-t", tag, str(package_dir)]
    if subprocess.run(command, check=False).returncode != 0:
        raise ServerImageError(f"docker build 실패: {tag}")


def make_server_image(package_dir: Path, config_path: Path, game: str, out_dir: Path, image_tag: str | None) -> Path:
    if (package_dir / SERVER_EXECUTABLE).is_file() is False:
        raise ServerImageError(f"서버 패키지에 `{SERVER_EXECUTABLE}` 가 없다: {package_dir} — `pack.py --target Server` 의 리눅스 패키지 폴더를 준다")
    try:
        config = json.loads(config_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exception:
        raise ServerImageError(f"설정을 읽지 못했다: {config_path} ({exception})") from exception
    stop_seconds = read_stop_seconds(config)
    run_config_check(package_dir, config_path)
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / f"{game}-server.tar.gz"
    write_archive(out_path, package_dir, config_path, game, make_unit_text(game, stop_seconds))
    if image_tag:
        build_image(get_project_root(), package_dir, image_tag)
    return out_path


class ServerImageCommand(Command):
    name = "server-image"
    help = "리눅스 서버 패키지 + 운영 설정을 배포 tar.gz(systemd 유닛)로 묶는다 — 묶을 서버로 -check-config 를 먼저 돌린다(--image 면 컨테이너 이미지도)"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--package", type=Path, required=True, help="`pack.py --target Server` 가 만든 리눅스 서버 패키지 폴더")
        parser.add_argument("--config", type=Path, required=True, help="운영 설정(ServerConfig JSON) — 비밀은 넣지 않는다")
        parser.add_argument("--game", required=True, help="게임 이름(설치 폴더 · 유닛 이름)")
        parser.add_argument("--image", help="docker 이미지 태그 — 주면 deploy/docker/Dockerfile 로 이미지도 짓는다")

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        out_path = make_server_image(args.package, args.config, args.game, Path("Saved") / "ServerImages", args.image)
        print(f"[server-image] OK: {out_path}")
        return ExitCode.PASSED
