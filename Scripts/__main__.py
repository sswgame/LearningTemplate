"""
Scripts/__main__.py

SW Engine 통합 CLI 엔트리포인트:
  프로젝트의 빌드, 에셋 쿠킹, 환경 설정, 정적 분석 및 문서화 도구를 단일 CLI로 실행합니다.

사용법:
  py -3 -m Scripts setup         # 개발 환경 및 도구체인 탐색/설정 (SetupEnvironment)
  py -3 -m Scripts vcpkg         # vcpkg 탐색 및 부트스트랩 (SetupVcpkg)
  py -3 -m Scripts llvm          # LLVM/Clang 탐색 및 설정 (SetupLlvm)
  py -3 -m Scripts cook          # 프리팹, 씬, 리소스 팩 일괄 쿠킹
  py -3 -m Scripts lint          # Staged 파일 대상 사전 커밋 린트 검사 (PreCommitLint)
  py -3 -m Scripts format        # C++ 코드 clang-format 자동 포맷팅 (RunClangFormat)
  py -3 -m Scripts docs          # Doxygen API 레퍼런스 문서 생성 (GenerateDocs)
  py -3 -m Scripts defender      # Windows Defender 빌드 디렉터리 제외 등록 (AddDefenderExclusions)
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# 부모 경로(Scripts) 및 프로젝트 루트 임포트 보장
_scriptRoot = Path(__file__).resolve().parent
if str(_scriptRoot) not in sys.path:
    sys.path.insert(0, str(_scriptRoot))


def cmdSetup(args: list[str]) -> int:
    from setup.SetupEnvironment import main
    return main(args)


def cmdVcpkg(args: list[str]) -> int:
    from setup.SetupVcpkg import main
    return main(args)


def cmdLlvm(args: list[str]) -> int:
    from setup.SetupLlvm import main
    return main(args)


def cmdCook(args: list[str]) -> int:
    from generate.CookAssets import main
    return main(args)


def cmdLint(args: list[str]) -> int:
    from lint.PreCommitLint import main
    return main()


def cmdFormat(args: list[str]) -> int:
    from lint.RunClangFormat import main
    return main(args)


def cmdDocs(args: list[str]) -> int:
    from generate.GenerateDocs import main
    return main(args)


def cmdDefender(args: list[str]) -> int:
    from setup.AddDefenderExclusions import main
    return main()


_kSubcommandMap = {
    "setup": cmdSetup,
    "vcpkg": cmdVcpkg,
    "llvm": cmdLlvm,
    "cook": cmdCook,
    "lint": cmdLint,
    "format": cmdFormat,
    "docs": cmdDocs,
    "defender": cmdDefender,
}


def main() -> int:
    parser = argparse.ArgumentParser(
        prog="py -3 -m Scripts",
        description="SW Engine 통합 개발 도구체인 CLI",
    )
    parser.add_argument(
        "command",
        choices=list(_kSubcommandMap.keys()),
        help="실행할 하위 도구 명령 (setup, vcpkg, llvm, cook, lint, format, docs, defender)",
    )
    parser.add_argument(
        "args",
        nargs=argparse.REMAINDER,
        help="선택한 명령에 전달할 추가 인수",
    )

    if len(sys.argv) < 2:
        parser.print_help()
        return 1

    parsed = parser.parse_args(sys.argv[1:2])
    handler = _kSubcommandMap.get(parsed.command)
    if handler is None:
        parser.print_help()
        return 1

    return handler(sys.argv[2:])


if __name__ == "__main__":
    sys.exit(main())
