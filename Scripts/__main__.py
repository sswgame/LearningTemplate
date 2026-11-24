"""
Scripts/__main__.py

SW Engine 통합 CLI 엔트리포인트:
  프로젝트의 빌드, 에셋 쿠킹, 환경 설정, 정적 분석 및 문서화 도구를 단일 CLI로 실행합니다.

명령 목록은 아래 `_SUBCOMMAND` 하나다 — 이 독스트링이나 `--help` 설명에 목록을 다시 적지 말 것.
여러 곳에 적으면 하나를 더할 때 모두 고쳐야 하고, 독스트링이 가장 먼저 낡는다.

  py -3 -m Scripts --help     # 무엇이 있는지는 이 명령이 답한다
  py -3 -m Scripts <명령> ... # 나머지 인자는 그대로 넘어간다
"""

from __future__ import annotations

import argparse
import importlib
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))   # Scripts — import 루트는 하나다

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.exit_code import ExitCode  # noqa: E402


@dataclass(frozen=True)
class Subcommand:
    """
    하위 명령 하나.

    - `name`        : `py -3 -m Scripts <name>`
    - `modulePath`  : `Scripts` 기준 모듈 경로. 그 모듈의 `main` 을 부른다(`모듈:함수` 로 다른 진입 함수를 고른다).
    - `description` : `--help` 에 찍히는 한 줄.

    모든 진입점은 `(argv)` 를 받는다 — 남은 인자를 그대로 넘긴다.
    """

    name: str
    module_path: str
    description: str

    def run(self, list_argument: list[str]) -> int:
        module_name, _, entry_name = self.module_path.partition(":")
        return getattr(importlib.import_module(module_name), entry_name or "main")(list_argument)


_SUBCOMMAND: tuple[Subcommand, ...] = (
    Subcommand("setup", "setup.environment", "개발 환경 및 도구체인 탐색/설정"),
    Subcommand("vcpkg", "setup.vcpkg", "vcpkg 탐색 및 부트스트랩"),
    Subcommand("llvm", "setup.llvm", "LLVM/Clang 탐색 및 설정"),
    Subcommand("lint", "lint.pre_commit", "린트 — 인자 없음: 커밋 훅(스테이지된 파일), --list: 규칙 표(lint/rules.py), <규칙 id> [인자…]: 규칙 하나"),
    Subcommand("api-diff", "dev.api_diff", "묶음이 새로 들인 함수 이름을 검토 목록으로 내고 갈래 사이 같은 새 이름(함수 · 멤버 · 등록 이름 · Config 키)을 찾는다(--base <묶음 시작> [브랜치 …]) — 통합 검토용"),
    Subcommand("naming-census", "dev.naming_census", "Source 의 모든 이름(형식 · 함수 · 멤버 · 상수 · 매크로 · 파일 · 폴더)을 낱말로 쪼개 종류별로 센다(--out <폴더>) — 이름 전수 대조용"),
    Subcommand("format", "lint.format:main_format", "clang-format(C++ 는 include 순서 포함, 셰이더) · vcpkg format-manifest(vcpkg.json) — 린트 format 의 고치는 명령"),
    Subcommand("tidy", "lint.clang_tidy", "clang-tidy 병렬 실행 · 검사별 집계(본 트리 컴파일 DB 를 읽기만 한다) — 인자 없음: 전체, --changed [기준]: 바뀐 파일만(묶음 검증), --files: 지정 파일"),
    Subcommand("syntax", "dev.syntax_check", "유니티를 푼 구문 확인(clang-cl -fsyntax-only, 본 트리 컴파일 DB 를 읽기만 한다) — --changed [기준]: 바뀐 파일이 닿는 TU, --all: 전부, --files: 지정 파일, --jobs: 동시 수"),
    Subcommand("online-local", "net.online_local_run", "온라인 로컬 실행 — 전용 서버와 게임 클라이언트를 띄워 로그인 → 키트 호출까지 돈다. 저장소 · 키트 · 제공자는 --server-config 가 고른다(run · check)"),
    Subcommand("net-pair", "net.net_pair_run", "두 프로세스 온라인 접속 시나리오 — 서버 · 클라이언트로 로그인 → 게임 접속 티켓 → 암호화 UDP 접속(run · check), 허브 서버 저장 후 재접속(mmo)"),
    Subcommand("new-online-kit", "dev.new_online_kit:main_new_online_kit", "온라인 키트 뼈대(Runtime · Server 모듈, 서술자 · 클라이언트 · 서비스 · 조립 · 테스트)를 만들고 중앙 표(메서드 영역 · 서버 키트 이름)에 한 줄씩 더한다"),
    Subcommand("new-kit", "dev.new_kit:main_new_kit", "키트 뼈대(카탈로그 · 컴포넌트 · 에디터 확장의 맵 검사 · 테스트)를 만들고 카탈로그 키 표에 두 줄을 더한다(--group <묶음 폴더>)"),
    Subcommand("lockstep-pair", "net.lockstep_pair_run", "락스텝 2 인 대전 — RTS 매치 미니게임(Playground starskirmish) 호스트 · 클라이언트 두 프로세스가 명령만 나눠 같은 판을 비동기 없이 도는지 본다(run)"),
    Subcommand("fileserve", "net.fileserve", "장치 대상으로 쿠킹한 팩 폴더를 읽기 전용 HTTP 로 내준다(개발 전용 — 장치는 App -fileserve=<주소>, 기본 127.0.0.1:1112 + adb reverse)"),
    Subcommand("telemetry-local", "net.telemetry_local_run", "텔레메트리 · 크래시 업로드 로컬 실행 — 루프백 HTTP 수신기와 게임 두 번으로 동의한 스풀 파일 · 보낼 줄의 크래시 묶음이 HTTP 로 오는지 본다(run)"),
    Subcommand("header-fanin", "dev.header_fanin", "헤더 팬인 측정 — 헤더마다 포함하는 .cpp 수 × pch 밖에서 끌어오는 헤더 수(빌드 없이, 헤더 다이어트 전후 비교)"),
    Subcommand("move-source", "dev.move_source:main_move", "소스 파일 · 폴더를 옮기고 include · CMake · 문서 참조를 고친다(Source 기준 old=new)"),
    Subcommand("move-asset", "dev.move_asset", "에셋 파일 · 폴더를 옮기고 경로 칸(Config/Dev/AssetReferenceFields.json)의 참조를 고친다(리소스 id old new)"),
    Subcommand("asset-ids", "lint.asset_identity", "에셋 정체성(GUID — 우리 형식 글의 머리 <?asset guid?> · 원본 곁 .meta) 검사, --fix 로 없는 GUID 만들기 · 짝 잃은 .meta 다시 붙이기 · 복사본 새 GUID"),
    Subcommand("migrate-asset-refs", "dev.migrate_asset_refs", "AssetRef<T> 칸의 옛 경로 글을 GUID 참조(guid:<32자>)로 바꿔 쓴다 — 에셋 GUID 2 단계 전수 이전(--fix 로 쓴다)"),
    Subcommand("asset-refs", "lint.asset_references", "작업 트리의 에셋 참조가 모두 있는지 본다(커밋 전 검사 · App --check-asset-refs 와 같은 칸 목록)"),
    Subcommand("rename-symbol", "dev.rename_symbol", "식별자 이름을 코드 · 데이터 · 설정 · 문서에서 한 번에 바꾼다(Old=New 또는 --pairs 표, --reapply) — 문자열 안 · 흔한 이름 · 이미 있는 새 이름은 멈추고, 옛 이름은 금지 이름 표에 올린다"),
    Subcommand("delegate-macros", "dev.delegate_macros", "델리게이트의 맨 create(`X::create<&f>` · `X::create( 람다 )`)를 SW_DELEGATE_* 매크로로 바꾼다(--check 는 남은 자리만 찍는다) — 병합 뒤 충돌 파일에 다시 돌린다"),
    Subcommand("byte-reader-values", "dev.byte_reader_values", "ByteReader · Archive · BitReader 의 출력 인자 읽기(`readPod( x )` · `readCount( n, c )` …)를 값 반환 읽기로 바꾸고 실패해도 바깥 칸을 덮는 꼴을 고친다(--check 는 남은 자리만 찍는다) — 병합 뒤 충돌 파일에 다시 돌린다"),
    Subcommand("disk-path-args", "dev.disk_path_args", "FileUtil 디스크 I/O 의 문자열 경로 인자를 `FileUtil::toDiskPath( … )`(AbsolutePath) 로 감싼다(--check 는 남은 문자열 인자만 찍는다) — 병합 뒤 충돌 파일에 다시 돌린다"),
    Subcommand("disk-path-join", "dev.disk_path_join", "toDiskPath( joinPath( 형식, … ) ) 왕복을 AbsolutePath 조립(join · getParent)으로 바꾸고 형식이 된 글 선언을 형식으로 올린다(--check 는 남은 자리만 찍는다) — 병합 뒤 충돌 파일에 다시 돌린다"),
    Subcommand("android-apk", "package.android_apk", "Android 빌드(lib<이름>.so + 쿠킹한 팩)를 서명한 디버그 APK 로 묶는다(--game, --preset Android-Shipping|Android-Release, aapt2 · zipalign · apksigner — Tools/Android)"),
    Subcommand("web-package", "package.web_package", "Web-Shipping 빌드(App.js · App.wasm + 쿠킹한 팩)를 브라우저 폴더로 묶는다(--game, file_packager — Tools/emsdk), --serve 로 교차 출처 격리 헤더를 주는 로컬 서버까지"),
    Subcommand("web-check", "package.web_check", "Web-Shipping 묶음을 브라우저(헤드리스 Chrome · Edge)에서 두 번 띄워 기동 · WebGPU · 오류 없음 · 화면 PNG · IDBFS 영속을 본다(--game)"),
    Subcommand("server-image", "package.server_image", "리눅스 서버 패키지 + 운영 설정을 배포 tar.gz(systemd 유닛)로 묶는다 — 묶을 서버로 -check-config 를 먼저 돌린다(--image 면 컨테이너 이미지도)"),
    Subcommand("customization-release", "dev.customization_release", "꾸미기 스키마를 출시 스냅숏과 견준다 — 판 올림 단계가 빠진 바뀜은 실패(쿠킹이 먼저 돈다), --fix 는 추론할 수 있는 제거 단계를 적고, accept 는 출시 때 스냅숏을 쓴다"),
    Subcommand("shipping-check", "dev.shipping_check", "배포 프리셋 둘(클라이언트 · 서버 · 개발 인자 거절)을 빌드하고 산출물의 기동 · 종료를 본다(묶음 검증 build 단계)"),
    Subcommand("test-history", "dev.test_history", "테스트 이력(sqlite) — ingest · report(흔들림 · p95 추세) · flaky(끝 줄 FLAKY=) · stage(단계 시간) · quarantine · costs · changed"),
    Subcommand("scenario-bisect", "dev.scenario_bisect", "시나리오 체인의 진 단계를 혼자 → 앞 단계를 붙여 → 이분 탐색으로 다시 돈다(--rerun-failed 는 마지막 보고에서 고른다)"),
    Subcommand("linux-scenarios", "dev.linux_scenarios", "Linux(WSL 클론) 빌드 · 체인 · 새니타이저 · 골든 입구(setup · all · build · chain · stages · rerun · sanitize · goldens …)"),
    Subcommand("next-done", "dev.next_done", "커밋 꼬리말 `NEXT: <줄 앞말> 끝` 이 가리킨 줄이 docs/NEXT.md 에 그대로 남아 있는지 본다(--base, 묶음 검증 시작에서)"),
    Subcommand("git-locks", "dev.git_locks", "강제로 끝난 git 이 남긴 빈 잠금 파일(index.lock 등)을 찾는다 — --remove 는 연 프로세스가 없는 빈 것만 지운다"),
    Subcommand("verify-record", "dev.verify_record", "묶음 검증 단계 통과 기록(skip · pass) · Windows 통과 기록(ok, build/verify.ok) · git pre-push 게이트"),
    Subcommand("destruction-net", "net.destruction_net_pair", "파괴 복제 실제 세션 확인 — 서버 · 클라이언트 두 프로세스의 구조 해시 맞대기(run · compare)"),
)

_SUBCOMMAND_BY_NAME: dict[str, Subcommand] = {command.name: command for command in _SUBCOMMAND}

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="py -3 -m Scripts",
        description="SW Engine 통합 개발 도구체인 CLI",
        epilog="\n".join(f"  {command.name:<15} {command.description}" for command in _SUBCOMMAND),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("command", choices=list(_SUBCOMMAND_BY_NAME), help="실행할 하위 도구 명령")
    parser.add_argument("args", nargs=argparse.REMAINDER, help="선택한 명령에 전달할 추가 인수")

    list_argument = list(sys.argv[1:] if argv is None else argv)
    if not list_argument:
        parser.print_help()
        return ExitCode.VIOLATION

    parsed = parser.parse_args(list_argument[:1])
    return _SUBCOMMAND_BY_NAME[parsed.command].run(list_argument[1:])


if __name__ == "__main__":
    sys.exit(main())
