#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/byte_read_shape.py

파일 · 세이브 · 패킷에서 온 개수를 믿고 할당하는 모양을 막는다. 2026-11-06 — `MeshVertexAnimation` · `NavMeshAsset` · `ByteReader::readByteVector` · 프로브 격자 · 원시 코덱 · 셰이더 리플렉션에서
같은 결함(위조된 개수가 수십 기가 할당이 되거나, 곱셈이 넘쳐 길이가 맞아 보인다)이 차례로 나왔다.
**왜 형식이 아니라 린트인가.** 길은 형식으로 냈다(`ByteReader::readList` · `readCountedList` · `readPodList` · `canHold`, `Archive::readCount`). `vector::resize` 는 어떤 수든 받으므로 다른 길을 형식으로 막지 못한다.
일반 정수 읽기(`>>` · `readUint32` · `readVarUint` · `readPod` — 약 1600 곳)를 모두 신뢰하지 않는 형식으로 내도, 대부분이 버전 · id · 깃발이라 꺼내는 자리(`value()`)가 늘 생기고 그 뒤 `resize` 로 가는 길은 그대로다(2026-11-12 검토) — 그래서 개수 할당 몫만 여기 남긴다.

1. 개수 할당: 함수 안에서 읽은 변수(`archive >> count` · `count = reader.readUint32()` · `reader.readPod( count )`)를 거르지 않고 `resize` · `reserve` · `assign` · 벡터 생성자에 넘긴다.
   거른 것으로 보는 것: 같은 함수에서 `readCount` · `canHold` · `canHoldCount` · `tryMultiplyCounts` · `hasBytesAvailable` 에 넘겼거나, 0 이 아닌 값과 비교했다.
   다른 까닭으로 안전하면 그 줄에 `NOLINT(sw-unchecked-count): <이유>` 를 적는다.
2. 직접 읽기: `LittleEndianUtil::read*` 로 제 커서를 굴리지 않는다 — 새 형식은 `ByteReader` 로 읽는다. 바깥 형식 · 패킷처럼 까닭이 있는 파일은 `Config/Rules/Lint/ByteReadRawFiles.json` 에 이유와 함께 적는다.

`py -3 -m Scripts lint byte-read [--files …|--all|--self-check]` 로 돌리고, 커밋 훅(`rules.py` 표)이 staged 파일에 돌린다.
"""

from __future__ import annotations

import argparse
import functools
import json
import re
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from lint.cpp_shape import is_target, strip_code  # noqa: E402
from lint.lint_scope import LintScope, make_file_scope, make_whole_scope, read_text  # noqa: E402
from lint.parallel import flat_map_in_processes  # noqa: E402


EXEMPT = "NOLINT(sw-unchecked-count)"
RAW_LIST_FILE = "Config/Rules/Lint/ByteReadRawFiles.json"
ALLOCATION = re.compile(r"(?:\.|->)(?:resize|reserve|assign)\(\s*(?:static_cast<\w+>\(\s*)?([A-Za-z_]\w*(?:(?:\.|->)[A-Za-z_]\w*)*)\s*\)*\s*[,)]")
VECTOR_CONSTRUCTOR = re.compile(r"\bvector<[^;()]*>\s+\w+\(\s*([A-Za-z_]\w*)\s*\)")
BLOCK_KEYWORD = re.compile(r"^(if|for|while|switch|else|do|case|try|catch|namespace|struct|class|enum|union)\b")
LAMBDA_CAPTURE = re.compile(r"\[[&=]?\w*\]\s*\(")
FUNCTION_HEAD_END = (")", "const", "noexcept", "override", "final")


def find_function_start(list_line: list[str], index: int) -> int:
    """@p index 줄을 품은 함수의 시작 줄을 찾습니다. 머리가 키워드나 람다가 아닌 `{` 한 줄을 거슬러 찾는 근사입니다(이 저장소의 clang-format 은 함수 여는 중괄호를 따로 둔다)."""
    for position in range(index, 0, -1):
        if list_line[position].strip() != "{":
            continue
        head = list_line[position - 1].strip()
        if head.endswith(FUNCTION_HEAD_END) and BLOCK_KEYWORD.match(head) is None and LAMBDA_CAPTURE.search(head) is None:
            return position
    return 0


def has_count_from_read(text: str, name: str) -> bool:
    """@p text 에서 @p name 이 스트림에서 읽은 값이면 true 입니다."""
    return re.search(rf">>\s*(?:[\w.]*\.)?{name}\b|\b{name}\s*=\s*[\w:.>-]*read\w*\(|read(?:Pod|VarUint|VarInt)\(\s*{name}\s*\)", text) is not None


def has_count_check(text: str, name: str) -> bool:
    """@p text 에서 @p name 을 남은 바이트로 걸렀거나 0 이 아닌 값과 비교했으면 true 입니다(0 비교는 상한이 아니다)."""
    if re.search(rf"(?:readCount|canHold|canHoldCount|tryMultiplyCounts|hasBytesAvailable)\([^;]*\b{name}\b", text) is not None:
        return True
    # 연산자 양옆의 공백을 본다(clang-format 이 지킨다) — `archive >> count` 와 `vector<uint32> count` 는 비교가 아니다.
    comparison = re.compile(rf"\b{name}\b\s+(?:!=|==|<=|>=|<|>)\s+(\S+)|(\S+)\s+(?:!=|==|<=|>=|<|>)\s+(?:static_cast<\w+>\(\s*)?\b{name}\b")
    return any((match.group(1) or match.group(2)) not in ("0", "0u", "0)", "0u)") for match in comparison.finditer(text))


def inspect_text(text: str, relative: str, set_raw_file: frozenset[str]) -> list[str]:
    """글 하나를 봅니다(자체 시험이 파일 없이 부를 수 있게 나눴다)."""
    list_violation: list[str] = []
    list_raw = text.split("\n")
    list_line = strip_code(text).split("\n")
    for index, line in enumerate(list_line):
        if "LittleEndianUtil::read" in line and relative not in set_raw_file:
            list_violation.append(f"{relative}:{index + 1}: `LittleEndianUtil::read*` 로 제 커서를 굴리지 않는다 — `ByteReader` 로 읽는다(바깥 형식이면 {RAW_LIST_FILE} 에 이유와 함께 적는다)")
        for pattern in (ALLOCATION, VECTOR_CONSTRUCTOR):
            match = pattern.search(line)
            if match is None or EXEMPT in list_raw[index]:
                continue
            name = re.split(r"\.|->", match.group(1))[-1]
            before = "\n".join(list_line[find_function_start(list_line, index):index])
            if has_count_from_read(before, name) and not has_count_check(before, name):
                list_violation.append(f"{relative}:{index + 1}: 읽은 개수 `{name}` 를 거르지 않고 할당한다 — `ByteReader::readList` · `canHold` · `Archive::readCount` 를 지난다(다른 까닭이면 `{EXEMPT}: <이유>`)")
    return list_violation


def inspect_listed_file(project_root: Path, set_raw_file: frozenset[str], file: str) -> list[str]:
    """파일 하나를 봅니다. 대상이 아니면 빈 목록입니다(프로세스로 넘기므로 모듈 최상위에 둔다). 테스트는 일부러 꾸민 바이트를 다루므로 보지 않습니다."""
    path = Path(file).resolve()
    try:
        relative = path.relative_to(project_root).as_posix()
    except ValueError:
        return []
    text = read_text(path, errors="replace") if is_target(relative) and "/Tests/" not in relative else None
    return [] if text is None else inspect_text(text, relative, set_raw_file)


def load_raw_files(project_root: Path) -> frozenset[str]:
    """직접 읽기를 허용한 파일 목록입니다. 없으면 예외로 멈춘다(빈 목록으로 대신하지 않는다)."""
    data = json.loads((project_root / RAW_LIST_FILE).read_text(encoding="utf-8"))
    return frozenset(entry["_path"] for entry in data["_listFile"])


def check_byte_read_shape(project_root: Path, scope: LintScope) -> list[str]:
    """@p listFile 가운데 Source 의 C++ 파일을 보고 어긋남을 `경로:줄: 설명` 으로 돌려준다."""
    list_file = scope.list_absolute(project_root, (".h", ".cpp"))
    return flat_map_in_processes(functools.partial(inspect_listed_file, project_root, load_raw_files(project_root)), list_file)


def run_self_check() -> None:
    """규칙이 잡을 것을 잡고 거른 것을 놓아 주는지 봅니다."""
    none: frozenset[str] = frozenset()
    bad = "bool A::read( Archive& archive )\n{\n    uint32 count = 0;\n    archive >> count;\n    _list.resize( count );\n}\n"
    assert len(inspect_text(bad, "x.cpp", none)) == 1
    bad_reader = "bool A::read()\n{\n    const uint32 count = reader.readUint32();\n    vector<uint8> list( count );\n}\n"
    assert len(inspect_text(bad_reader, "x.cpp", none)) == 1
    zero_only = "bool A::read()\n{\n    const uint32 count = reader.readUint32();\n    if ( count == 0 )\n        return true;\n    _list.reserve( count );\n}\n"
    assert len(inspect_text(zero_only, "x.cpp", none)) == 1
    checked = "bool A::read( Archive& archive )\n{\n    uint32 count = 0;\n    if ( archive.readCount( 4, count ) == false )\n        return false;\n    _list.resize( count );\n}\n"
    assert inspect_text(checked, "x.cpp", none) == []
    other = "void A::a()\n{\n    uint32 count = reader.readUint32();\n}\n\nvoid A::b()\n{\n    const size_t count = _list.size();\n    _other.resize( count );\n}\n"
    assert inspect_text(other, "x.cpp", none) == []
    exempt = "void A::a()\n{\n    archive >> count;\n    _list.resize( count ); // NOLINT(sw-unchecked-count): 범위는 위에서\n}\n"
    assert inspect_text(exempt, "x.cpp", none) == []
    raw = "uint32 A::a() { return LittleEndianUtil::readUint32( p ); }\n"
    assert len(inspect_text(raw, "x.cpp", none)) == 1
    assert inspect_text(raw, "x.cpp", frozenset({"x.cpp"})) == []
    assert inspect_text("// LittleEndianUtil::readUint32\n", "x.cpp", none) == []


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="파일에서 읽은 개수를 믿고 할당하는 모양과 `LittleEndianUtil::read*` 직접 읽기를 본다")
    parser.add_argument("--files", nargs="*", default=None, help="볼 파일(없으면 Source 전체)")
    parser.add_argument("--all", action="store_true", help="Source 전체")
    parser.add_argument("--self-check", action="store_true", help="규칙 자체를 시험한다")
    args = parser.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")
    if args.self_check:
        run_self_check()
        print("[ByteReadShape] self-check OK")
        return 0
    project_root = Path(__file__).resolve().parents[2]
    list_violation = check_byte_read_shape(project_root, make_file_scope(project_root, args.files) if args.files else make_whole_scope(project_root))
    for line in list_violation:
        print(line)
    print(f"[ByteReadShape] {'FAIL' if list_violation else 'OK'} — {len(list_violation)}건")
    return 1 if list_violation else 0


if __name__ == "__main__":
    sys.exit(main())
