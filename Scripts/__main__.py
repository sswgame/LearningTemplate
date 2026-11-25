"""
Scripts/__main__.py

스크립트 명령의 입구 하나. 명령 목록은 어디에도 적지 않는다 — `Scripts/<폴더>/*.py` 의 `Command` 하위 클래스가 곧 명령이다(`common/command.py`).

  py -3 -m Scripts --help     # 무엇이 있는지는 이 명령이 답한다
  py -3 -m Scripts <명령> ... # 나머지 인자는 그대로 넘어간다
"""

from __future__ import annotations

import argparse
import importlib
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))   # Scripts — import 루트는 하나다

from common.command import CommandEntry, find_commands  # noqa: E402
from common.exit_code import ExitCode  # noqa: E402


@dataclass(frozen=True)
class LegacyCommand:
    """아직 `Command` 로 옮기지 않은 모듈의 `main(argv)` — 다른 갈래가 고치는 중이라 남겼다. 옮기면 표와 이 형식을 지운다."""

    name: str
    module: str
    help: str

    def run(self, argv: Sequence[str]) -> int:
        return importlib.import_module(self.module).main(argv)


LEGACY_COMMANDS: tuple[LegacyCommand, ...] = (
    LegacyCommand("online-local", "net.online_local_run", "온라인 로컬 실행 — 전용 서버와 게임 클라이언트를 띄워 로그인 → 키트 호출까지 돈다. 저장소 · 키트 · 제공자는 --server-config 가 고른다(run · check)"),
    LegacyCommand("net-pair", "net.net_pair_run", "두 프로세스 온라인 접속 시나리오 — 서버 · 클라이언트로 로그인 → 게임 접속 티켓 → 암호화 UDP 접속(run · check), 허브 서버 저장 후 재접속(mmo)"),
    LegacyCommand("lockstep-pair", "net.lockstep_pair_run", "락스텝 2 인 대전 — RTS 매치 미니게임(Playground starskirmish) 호스트 · 클라이언트 두 프로세스가 명령만 나눠 같은 판을 비동기 없이 도는지 본다(run)"),
    LegacyCommand("fileserve", "net.fileserve", "장치 대상으로 쿠킹한 팩 폴더를 읽기 전용 HTTP 로 내준다(개발 전용 — 장치는 App -fileserve=<주소>, 기본 127.0.0.1:1112 + adb reverse)"),
    LegacyCommand("telemetry-local", "net.telemetry_local_run", "텔레메트리 · 크래시 업로드 로컬 실행 — 루프백 HTTP 수신기와 게임 두 번으로 동의한 스풀 파일 · 보낼 줄의 크래시 묶음이 HTTP 로 오는지 본다(run)"),
    LegacyCommand("destruction-net", "net.destruction_net_pair", "파괴 복제 실제 세션 확인 — 서버 · 클라이언트 두 프로세스의 구조 해시 맞대기(run · compare)"),
    LegacyCommand("test-history", "dev.test_history", "테스트 이력(sqlite) — ingest · report(흔들림 · p95 추세) · flaky(끝 줄 FLAKY=) · stage(단계 시간) · quarantine · costs · changed"),
    LegacyCommand("scenario-bisect", "dev.scenario_bisect", "시나리오 체인의 진 단계를 혼자 → 앞 단계를 붙여 → 이분 탐색으로 다시 돈다(--rerun-failed 는 마지막 보고에서 고른다)"),
)


def main(argv: Sequence[str] | None = None) -> int:
    map_command: dict[str, CommandEntry | LegacyCommand] = {**find_commands(), **{command.name: command for command in LEGACY_COMMANDS}}
    parser = argparse.ArgumentParser(
        prog="py -3 -m Scripts",
        description="SW Engine 통합 개발 도구체인 CLI",
        epilog="\n".join(f"  {name:<22} {map_command[name].help}" for name in sorted(map_command)),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("command", choices=sorted(map_command), metavar="명령", help="아래 목록의 하나 — 나머지 인자는 그 명령에 그대로 넘어간다")

    list_argument = list(sys.argv[1:] if argv is None else argv)
    if not list_argument:
        parser.print_help()
        return ExitCode.VIOLATION
    return map_command[parser.parse_args(list_argument[:1]).command].run(list_argument[1:])


if __name__ == "__main__":
    sys.exit(main())
