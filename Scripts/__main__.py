"""
Scripts/__main__.py

SW Engine 통합 CLI 엔트리포인트:
  프로젝트의 빌드, 에셋 쿠킹, 환경 설정, 정적 분석 및 문서화 도구를 단일 CLI로 실행합니다.

명령 목록은 아래 `_kSubcommand` 하나다 — 이 독스트링이나 `--help` 설명에 목록을 다시 적지 말 것.
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
from common.ExitCode import ExitCode  # noqa: E402


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
    modulePath: str
    description: str

    def run(self, listArgument: list[str]) -> int:
        moduleName, _, entryName = self.modulePath.partition(":")
        return getattr(importlib.import_module(moduleName), entryName or "main")(listArgument)


_kSubcommand: tuple[Subcommand, ...] = (
    Subcommand("setup", "setup.Environment", "개발 환경 및 도구체인 탐색/설정"),
    Subcommand("vcpkg", "setup.Vcpkg", "vcpkg 탐색 및 부트스트랩"),
    Subcommand("llvm", "setup.Llvm", "LLVM/Clang 탐색 및 설정"),
    Subcommand("browser-mcp", "setup.BrowserMcp", "에이전트 브라우저 조종 MCP(Chrome DevTools MCP)를 Tools/BrowserMCP 에 고정 판으로 설치(serve 는 띄운다 — .mcp.json)"),
    Subcommand("lint", "lint.PreCommit", "Staged 파일 대상 사전 커밋 린트 검사"),
    Subcommand("include-order", "lint.IncludeOrder:mainCheck", "include 순서 검사(--files 로 좁힌다)"),
    Subcommand("byte-read", "lint.ByteReadShape", "파일에서 읽은 개수를 믿고 할당하는 모양 · LittleEndianUtil 직접 읽기 검사(--files · --all · --self-check)"),
    Subcommand("state-shape", "lint.StateShape", "손으로 짝지은 writeState · readState( Archive& ) 검사(--files · --all · --self-check)"),
    Subcommand("catalog-root", "lint.CatalogRootShape", "게임 데이터 루트의 원소를 손으로 고르는 모양 검사(--files · --all · --self-check)"),
    Subcommand("cpp-shape", "lint.CppShape", "C++ 모양 규칙 검사 — 선언 순서 · 깃발 칸 · 익명 네임스페이스 · 주인 우선(--files · --all · --summary)"),
    Subcommand("naming-words", "lint.NamingWords", "함수 앞 단어 · 형식 끝 단어를 허용 표와 대조한다"),
    Subcommand("acronyms", "lint.Acronyms", "약어 표기 검사 — 식별자 · 파일 · 폴더의 약어는 모두 대문자(Config/Dev/Acronyms.json, --self-check)"),
    Subcommand("folder-layout", "lint.FolderLayout", "파일 자리 검사 — 잡동사니 폴더 · 엔진 쪽 Dev/ · Component 폴더(Config/Dev/FolderLayout.json)"),
    Subcommand("new-names", "lint.NewNames", "묶음이 새로 들인 함수 이름을 검토 목록으로 낸다(--base <묶음 시작> [브랜치 …]) — 통합 검토용"),
    Subcommand("platform-chains", "lint.PlatformChains", "플랫폼 #if 사슬이 빠진 플랫폼을 컴파일 오류로 내는지 본다(--self-check)"),
    Subcommand("client-only-types", "generate.ClientOnlyTypes", "전용 서버가 짓지 않는 소스의 리플렉션 형식 이름 표를 쓴다(--check 는 낡았는지만 본다)"),
    Subcommand("dev-only-variables", "lint.DevOnlyVariables", "테스트용 전역 변수가 개발 전용 자리(Dev/ · Tests/ · DevTools · Editor · 자동화 실행기) 밖에 선언됐는지 본다(--self-check)"),
    Subcommand("mutable-statics", "lint.MutableStatics", "가변 정적 저장소(함수 정적 · 이름공간 범위 · 클래스 static 데이터 멤버)가 정적 등록 공통 틀과 예외 표 밖에 있는지 본다 — 나머지는 주인 객체의 멤버(--self-check)"),
    Subcommand("server-ui", "lint.ServerUIIncludes", "서버도 짓는 게임 코드가 클라이언트 전용 기능(UI · 오디오 · 렌더러 · Client/)을 직접 부르는지 본다(--self-check)"),
    Subcommand("editor-view-includes", "lint.EditorViewIncludes", "MVVM 으로 옮긴 에디터 패널 View 가 모델 · 씬 헤더를 include 하는지 본다(--self-check)"),
    Subcommand("common-shapes", "lint.CommonShapes", "공통 기반을 두고 손으로 적은 모양 검사(--self-check)"),
    Subcommand("shader-kill-order", "lint.ShaderKillOrder", "픽셀 버리기(clip · discard) 뒤의 미분 읽기 검사(--self-check)"),
    Subcommand("shader-branch-shape", "lint.ShaderBranchShape", "`if ( a && b ) … else if ( a )` 분기 사슬 검사 — NVIDIA GL SPIR-V 함정(--self-check)"),
    Subcommand("thread-owners", "lint.ThreadOwners", "TaskManager 밖에서 스레드를 만드는 파일을 허용 목록(Config/Dev/ThreadOwners.json)과 맞춘다(--self-check)"),
    Subcommand("frame-intern", "lint.FrameIntern", "틱 · 갱신 · 칠하기 함수 안의 리터럴 hashed_string 을 찾는다(--self-check)"),
    Subcommand("tick-scene-walk", "lint.TickSceneWalk", "컴포넌트 틱에서 닿는 씬 전체 순회를 찾는다(--self-check)"),
    Subcommand("feature-refs", "lint.FeatureReferences", "컴포넌트 · 감시 값 · 개발 명령의 참조 0 검사(--self-check)"),
    Subcommand("cmake-path-case", "lint.CMakePathCase", "CMake include · add_subdirectory 경로의 대소문자 검사"),
    Subcommand("format-includes", "lint.Format:mainFormatIncludes", "include 순서 · 중복 자동 수정(--check 는 검사만)"),
    Subcommand("format", "lint.Format:mainFormatClang", "C++ 코드 clang-format 자동 포맷팅"),
    Subcommand("online-local", "net.OnlineLocalRun", "온라인 로컬 실행 — 전용 서버와 게임 클라이언트를 띄워 로그인 → 키트 호출까지 돈다. 저장소 · 키트 · 제공자는 --server-config 가 고른다(run · check)"),
    Subcommand("net-pair", "net.NetPairRun", "두 프로세스 온라인 접속 시나리오 — 서버 · 클라이언트로 로그인 → 게임 접속 티켓 → 암호화 UDP 접속(run · check)"),
    Subcommand("new-online-kit", "dev.NewOnlineKit:mainNewOnlineKit", "온라인 키트 뼈대(Runtime · Server 모듈, 서술자 · 클라이언트 · 서비스 · 조립 · 테스트)를 만들고 중앙 표(메서드 영역 · 서버 키트 이름)에 한 줄씩 더한다"),
    Subcommand("lockstep-pair", "net.LockstepPairRun", "락스텝 2 인 대전 — StarSkirmish 호스트 · 클라이언트 두 프로세스가 명령만 나눠 같은 판을 비동기 없이 도는지 본다(run)"),
    Subcommand("telemetry-local", "net.TelemetryLocalRun", "텔레메트리 · 크래시 업로드 로컬 실행 — 루프백 HTTP 수신기와 게임 두 번으로 동의한 스풀 파일 · 보낼 줄의 크래시 묶음이 HTTP 로 오는지 본다(run)"),
    Subcommand("move-source", "dev.MoveSource:mainMove", "소스 파일 · 폴더를 옮기고 include · CMake · 문서 참조를 고친다(Source 기준 old=new)"),
    Subcommand("move-asset", "dev.MoveAsset", "에셋 파일 · 폴더를 옮기고 경로 칸(Config/Dev/AssetReferenceFields.json)의 참조를 고친다(리소스 id old new)"),
    Subcommand("asset-refs", "lint.AssetReferences", "작업 트리의 에셋 참조가 모두 있는지 본다(커밋 전 검사 · App --check-asset-refs 와 같은 칸 목록)"),
    Subcommand("rename-symbol", "dev.MoveSource:mainRename", "C++ 이름을 낱말 경계로 바꾼다(Old=New, 새 이름이 이미 쓰이면 멈춘다)"),
    Subcommand("android-apk", "package.AndroidApk", "Android-Shipping 빌드(lib<이름>.so + 쿠킹한 팩)를 서명한 디버그 APK 로 묶는다(--game, aapt2 · zipalign · apksigner — Tools/Android)"),
    Subcommand("web-package", "package.WebPackage", "Web-Shipping 빌드(App.js · App.wasm + 쿠킹한 팩)를 브라우저 폴더로 묶는다(--game, file_packager — Tools/emsdk), --serve 로 교차 출처 격리 헤더를 주는 로컬 서버까지"),
    Subcommand("shipping-check", "dev.ShippingCheck", "배포 프리셋 셋(클라이언트 · 서버 · 자동화 없음)을 빌드하고 산출물의 기동 · 종료를 본다(통합에서 한 번)"),
    Subcommand("magic-literals", "lint.MagicLiterals", "숫자 리터럴 래칫(일반 코드의 이름 없는 리터럴이 파일마다 늘지 않게 — 커밋 훅)과 집계: 장소 × 종류, `-1` 의 뜻(--check · --ratchet · --accept · --meaning · --out · --self-check)"),
    Subcommand("result-types", "lint.ResultTypes", "결과 형식 래칫(std::optional · string& outError · string* pOutError · 실패 때만 쓰는 글 출력 인자 모양이 Core 에 없고 그 밖에서 늘지 않게 — 커밋 훅, --check · --ratchet · --accept · --self-check)"),
    Subcommand("destruction-net", "net.DestructionNetPair", "파괴 복제 실제 세션 확인 — 서버 · 클라이언트 두 프로세스의 구조 해시 맞대기(run · compare)"),
)

_kSubcommandByName: dict[str, Subcommand] = {command.name: command for command in _kSubcommand}

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="py -3 -m Scripts",
        description="SW Engine 통합 개발 도구체인 CLI",
        epilog="\n".join(f"  {command.name:<15} {command.description}" for command in _kSubcommand),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("command", choices=list(_kSubcommandByName), help="실행할 하위 도구 명령")
    parser.add_argument("args", nargs=argparse.REMAINDER, help="선택한 명령에 전달할 추가 인수")

    listArgument = list(sys.argv[1:] if argv is None else argv)
    if not listArgument:
        parser.print_help()
        return ExitCode.Violation

    parsed = parser.parse_args(listArgument[:1])
    return _kSubcommandByName[parsed.command].run(listArgument[1:])


if __name__ == "__main__":
    sys.exit(main())
