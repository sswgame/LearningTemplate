r"""
Scripts/generate/GenerateHpackStatic.py

RFC 7541 부록 A 의 정적 표(61 줄)를 RFC 본문 텍스트에서 기계적으로 읽어 C++ 헤더로 씁니다. 표를 사람이 옮겨 적지 않기 위한 도구입니다.
빌드가 부르지 않습니다. RFC 가 바뀔 일이 없어 결과 헤더를 커밋하고, 다시 만들 때만 손으로 돌립니다.

사용법:
  python Scripts/generate/GenerateHpackStatic.py <rfc7541.txt> <출력 헤더 경로>
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다
from common.ExitCode import ExitCode  # noqa: E402

kEntryCount = 61
kLinePattern = re.compile(r"^\s*\|\s*(\d+)\s*\|\s*([^\s|]+)\s*\|\s*([^|]*?)\s*\|\s*$")


def readEntryList(rfcPath: Path) -> list[tuple[str, str]]:
    """부록 A 의 줄을 읽어 색인 순서의 (이름, 값) 목록을 돌려줍니다. 줄이 빠졌거나 두 번 나오면 멈춥니다."""
    mapEntry: dict[int, tuple[str, str]] = {}
    bInSection = False
    for line in rfcPath.read_text(encoding="utf-8").splitlines():
        if line.startswith("Appendix A."):
            bInSection = True
        elif line.startswith("Appendix B."):
            bInSection = False
        if bInSection is False:
            continue
        match = kLinePattern.match(line)
        if match is None:
            continue
        index = int(match.group(1))
        if index in mapEntry:
            raise ValueError(f"색인 {index} 이 두 번 나왔습니다")
        mapEntry[index] = (match.group(2), match.group(3))
    if sorted(mapEntry) != list(range(1, kEntryCount + 1)):
        raise ValueError(f"정적 표 줄이 {len(mapEntry)} 개입니다({kEntryCount} 개이고 색인이 1 부터 이어져야 합니다)")
    return [mapEntry[index] for index in range(1, kEntryCount + 1)]


def writeHeader(listEntry: list[tuple[str, str]], outputPath: Path) -> None:
    """정적 표 헤더를 씁니다."""
    listLine = [f'            {{ "{name}", "{value}" }}, // {index}' for index, (name, value) in enumerate(listEntry, start=1)]
    body = "\n".join(listLine)
    text = (
        "/**\n"
        " * @file HpackStaticTable.h\n"
        " * @brief RFC 7541 부록 A 의 HPACK 정적 표입니다. 생성 파일 — Scripts/generate/GenerateHpackStatic.py 가 RFC 본문에서 읽어 썼으므로 손으로 고치지 않는다.\n"
        " * @details 색인 1 부터 61 까지이며 배열 위치는 색인 - 1 이다.\n"
        " */\n"
        "#pragma once\n"
        '#include "Core/Common/Types.h"\n'
        "\n"
        "namespace sw\n"
        "{\n"
        "    struct HpackStaticEntry\n"
        "    {\n"
        "        const utf8* _pName;\n"
        "        const utf8* _pValue;\n"
        "    };\n"
        "\n"
        "    namespace HpackStatic\n"
        "    {\n"
        f"        inline constexpr uint32 kEntryCount = {kEntryCount};\n"
        "\n"
        "        inline constexpr HpackStaticEntry kArrEntry[kEntryCount] = {\n"
        f"{body}\n"
        "        };\n"
        "    } // namespace HpackStatic\n"
        "} // namespace sw\n"
    )
    outputPath.parent.mkdir(parents=True, exist_ok=True)
    outputPath.write_bytes(text.replace("\n", "\r\n").encode("utf-8"))


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="RFC 7541 부록 A 정적 표 헤더 생성")
    parser.add_argument("rfcPath", type=Path)
    parser.add_argument("outputPath", type=Path)
    args = parser.parse_args(argv)
    try:
        writeHeader(readEntryList(args.rfcPath), args.outputPath)
    except ValueError as error:
        print(f"[GenerateHpackStatic] 오류: {error}", file=sys.stderr)
        return ExitCode.Violation
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
