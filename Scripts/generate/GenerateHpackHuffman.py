r"""
Scripts/generate/GenerateHpackHuffman.py

RFC 7541 부록 B 의 허프만 부호표(257 줄)를 RFC 본문 텍스트에서 기계적으로 읽어 C++ 헤더로 씁니다. 표를 사람이 옮겨 적지 않기 위한 도구입니다.
빌드가 부르지 않습니다. RFC 가 바뀔 일이 없어 결과 헤더를 커밋하고, 다시 만들 때만 손으로 돌립니다.

사용법:
  python Scripts/generate/GenerateHpackHuffman.py <rfc7541.txt> <출력 헤더 경로>
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다

kSymbolCount = 257
kLinePattern = re.compile(r"^\s*(?:EOS|'.')?\s*\(\s*(\d+)\)\s+\|([01|]+)\s+([0-9a-f]+)\s+\[\s*(\d+)\]\s*$")


def readCodeList(rfcPath: Path) -> list[tuple[int, int]]:
    """부록 B 의 줄을 읽어 기호 순서의 (부호, 비트 수) 목록을 돌려줍니다. 줄이 빠졌거나 모양이 다르면 멈춥니다."""
    mapCode: dict[int, tuple[int, int]] = {}
    for line in rfcPath.read_text(encoding="utf-8").splitlines():
        match = kLinePattern.match(line)
        if match is None:
            continue
        symbol = int(match.group(1))
        if symbol in mapCode:
            raise ValueError(f"기호 {symbol} 이 두 번 나왔습니다")
        pattern = match.group(2).replace("|", "")
        code = int(match.group(3), 16)
        bits = int(match.group(4))
        if len(pattern) != bits or int(pattern, 2) != code:
            raise ValueError(f"기호 {symbol} 의 비트 열 · 16 진 · 길이가 서로 맞지 않습니다")
        mapCode[symbol] = (code, bits)
    if sorted(mapCode) != list(range(kSymbolCount)):
        raise ValueError(f"부호표 줄이 {len(mapCode)} 개입니다(257 개여야 합니다)")
    return [mapCode[symbol] for symbol in range(kSymbolCount)]


def writeHeader(listCode: list[tuple[int, int]], outputPath: Path) -> None:
    """부호표 헤더를 씁니다."""
    listLine = [f"            {{ 0x{code:x}u, {bits} }}, // {symbol}" for symbol, (code, bits) in enumerate(listCode)]
    body = "\n".join(listLine)
    text = (
        "/**\n"
        " * @file HpackHuffmanTable.h\n"
        " * @brief RFC 7541 부록 B 의 허프만 부호표입니다. 생성 파일 — Scripts/generate/GenerateHpackHuffman.py 가 RFC 본문에서 읽어 썼으므로 손으로 고치지 않는다.\n"
        " * @details 기호 0 ~ 255 는 바이트, 256 은 EOS 다. 부호는 오른쪽 맞춤이고 비트 수가 길이다.\n"
        " */\n"
        "#pragma once\n"
        '#include "Core/Common/Types.h"\n'
        "\n"
        "namespace sw\n"
        "{\n"
        "    struct HpackHuffmanCode\n"
        "    {\n"
        "        uint32 _code;\n"
        "        uint8  _bits;\n"
        "    };\n"
        "\n"
        "    namespace HpackHuffman\n"
        "    {\n"
        f"        inline constexpr int32 kSymbolCount = {kSymbolCount};\n"
        "        inline constexpr int32 kEndOfString = 256;\n"
        "\n"
        "        inline constexpr HpackHuffmanCode kArrCode[kSymbolCount] = {\n"
        f"{body}\n"
        "        };\n"
        "    } // namespace HpackHuffman\n"
        "} // namespace sw\n"
    )
    outputPath.parent.mkdir(parents=True, exist_ok=True)
    outputPath.write_bytes(text.replace("\n", "\r\n").encode("utf-8"))


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="RFC 7541 부록 B 허프만 부호표 헤더 생성")
    parser.add_argument("rfcPath", type=Path)
    parser.add_argument("outputPath", type=Path)
    args = parser.parse_args(argv)
    try:
        writeHeader(readCodeList(args.rfcPath), args.outputPath)
    except ValueError as error:
        print(f"[GenerateHpackHuffman] 오류: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
