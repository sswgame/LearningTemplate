r"""
Scripts/generate/GenerateConfigReference.py

설정 참조 문서(`docs/Config/`)를 코드에서 다시 만듭니다 — 설정 파일마다의 칸 표 · 전역 변수 · 명령줄 · CMake 옵션 · 사용자 설정 · 색인과
기계가 읽는 `ConfigReference.json`. 결과는 커밋합니다(손으로 고치지 않는다). 낡았는지는 `Scripts/lint/gate/CheckConfigReference.py` 가 봅니다.

사용법:
  py -3 Scripts/generate/GenerateConfigReference.py [--root <저장소>]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import getProjectRoot, useUtf8Stdout  # noqa: E402
from common.ConfigReference import buildConfigReference, kConfigReferenceDir  # noqa: E402


def writeConfigReference(repositoryRoot: Path) -> int:
    """생성 문서를 쓰고 폴더에 남은 옛 파일을 지웁니다. 표 밖 설정 파일 · 설명 없는 칸이 있으면 1 입니다(문서는 그래도 쓴다)."""
    mapOutput, violations = buildConfigReference(repositoryRoot)
    outputDir = repositoryRoot / kConfigReferenceDir
    outputDir.mkdir(parents=True, exist_ok=True)
    changedCount = 0
    for relative, text in mapOutput.items():
        path = repositoryRoot / relative
        if path.is_file() and path.read_text(encoding="utf-8") == text:
            continue
        path.write_text(text, encoding="utf-8")
        changedCount += 1
    for path in sorted(outputDir.iterdir()):
        if path.is_file() and path.relative_to(repositoryRoot).as_posix() not in mapOutput:
            path.unlink()
            changedCount += 1
    for violation in violations:
        print(violation)
    print(f"[GenerateConfigReference] {len(mapOutput)} files, {changedCount} changed, {len(violations)} problems")
    return 1 if violations else 0


def main(argv: Sequence[str] | None = None) -> int:
    useUtf8Stdout()
    parser = argparse.ArgumentParser(description="docs/Config 설정 참조 문서를 코드에서 만든다")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트(생략하면 이 스크립트의 저장소)")
    args = parser.parse_args(argv)
    return writeConfigReference((args.root or getProjectRoot()).resolve())


if __name__ == "__main__":
    sys.exit(main())
