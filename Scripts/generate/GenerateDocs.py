"""
Scripts/generate/GenerateDocs.py

Doxygen 문서 생성.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import webbrowser
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import getProjectRoot


def generateDocs(*, openBrowser: bool = False) -> int:
    """
    Doxygen을 실행하여 프로젝트의 공식 문서(API 레퍼런스 등)를 생성합니다.
    """
    rootDir = getProjectRoot()
    doxyfilePath = rootDir / "Doxyfile"
    docsDir = rootDir / "Docs" / "Doxygen" / "html"
    indexHtml = docsDir / "index.html"

    if not doxyfilePath.exists():
        print(f"경고: {doxyfilePath}에서 Doxyfile을 찾을 수 없어 문서 생성을 건너뜁니다.")
        return 0

    print("Doxygen 실행 중...")
    try:
        subprocess.run(["doxygen", "Doxyfile"], cwd=rootDir, check=True)
    except FileNotFoundError:
        print(
            "오류: 'doxygen' 명령을 찾을 수 없습니다. Doxygen이 설치되어 있고 시스템 PATH에 추가되어 있는지 확인하세요."
        )
        return 1
    except subprocess.CalledProcessError as exception:
        print(f"오류: Doxygen 실행 실패 (종료 코드 {exception.returncode})")
        return 1

    print("Doxygen 문서 생성이 완료되었습니다.")
    if indexHtml.exists():
        if openBrowser:
            print(f"기본 브라우저에서 {indexHtml} 문서를 엽니다...")
            webbrowser.open(f"file://{indexHtml.as_posix()}")
        else:
            print(f"문서 경로: {indexHtml}")
    else:
        print(f"경고: 문서 생성 후 {indexHtml} 파일을 찾을 수 없습니다.")
    return 0


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Doxygen HTML 문서를 생성합니다.")
    parser.add_argument("--open", action="store_true", help="생성 완료 후 기본 웹 브라우저에서 문서를 엽니다.")
    parsedArgs = parser.parse_args(argv)
    return generateDocs(openBrowser=parsedArgs.open)


if __name__ == "__main__":
    raise SystemExit(main())
