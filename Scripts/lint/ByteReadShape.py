#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/ByteReadShape.py

파일 · 세이브 · 패킷에서 온 개수를 믿고 할당하는 모양을 막는다. 2026-11-06 — `MeshVertexAnimation` · `NavMeshAsset` · `ByteReader::readByteVector` · 프로브 격자 · 원시 코덱 · 셰이더 리플렉션에서
같은 결함(위조된 개수가 수십 기가 할당이 되거나, 곱셈이 넘쳐 길이가 맞아 보인다)이 차례로 나왔다.
**왜 형식이 아니라 린트인가.** 길은 형식으로 냈다(`ByteReader::readList` · `readCountedList` · `readPodList` · `canHold`, `Archive::readCount`). `vector::resize` 는 어떤 수든 받으므로 다른 길을 형식으로 막지 못한다.

1. 개수 할당: 함수 안에서 읽은 변수(`archive >> count` · `count = reader.readUint32()` · `reader.readPod( count )`)를 거르지 않고 `resize` · `reserve` · `assign` · 벡터 생성자에 넘긴다.
   거른 것으로 보는 것: 같은 함수에서 `readCount` · `canHold` · `canHoldCount` · `tryMultiplyCounts` · `hasBytesAvailable` 에 넘겼거나, 0 이 아닌 값과 비교했다.
   다른 까닭으로 안전하면 그 줄에 `NOLINT(sw-unchecked-count): <이유>` 를 적는다.
2. 직접 읽기: `LittleEndianUtil::read*` 로 제 커서를 굴리지 않는다 — 새 형식은 `ByteReader` 로 읽는다. 바깥 형식 · 패킷처럼 까닭이 있는 파일은 `Config/Rules/Lint/ByteReadRawFiles.json` 에 이유와 함께 적는다.

`py -3 -m Scripts lint byte-read [--files …|--all|--self-check]` 로 돌리고, 커밋 훅(`Rules.py` 표)이 staged 파일에 돌린다.
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

from lint.CppShape import isTarget, stripCode  # noqa: E402
from lint.Parallel import flatMapInProcesses  # noqa: E402


kExempt = "NOLINT(sw-unchecked-count)"
kRawListFile = "Config/Rules/Lint/ByteReadRawFiles.json"
kAllocation = re.compile(r"(?:\.|->)(?:resize|reserve|assign)\(\s*(?:static_cast<\w+>\(\s*)?([A-Za-z_]\w*(?:(?:\.|->)[A-Za-z_]\w*)*)\s*\)*\s*[,)]")
kVectorConstructor = re.compile(r"\bvector<[^;()]*>\s+\w+\(\s*([A-Za-z_]\w*)\s*\)")
kBlockKeyword = re.compile(r"^(if|for|while|switch|else|do|case|try|catch|namespace|struct|class|enum|union)\b")
kLambdaCapture = re.compile(r"\[[&=]?\w*\]\s*\(")
kFunctionHeadEnd = (")", "const", "noexcept", "override", "final")


def findFunctionStart(listLine: list[str], index: int) -> int:
    """@p index 줄을 품은 함수의 시작 줄을 찾습니다. 머리가 키워드나 람다가 아닌 `{` 한 줄을 거슬러 찾는 근사입니다(이 저장소의 clang-format 은 함수 여는 중괄호를 따로 둔다)."""
    for position in range(index, 0, -1):
        if listLine[position].strip() != "{":
            continue
        head = listLine[position - 1].strip()
        if head.endswith(kFunctionHeadEnd) and kBlockKeyword.match(head) is None and kLambdaCapture.search(head) is None:
            return position
    return 0


def hasCountFromRead(text: str, name: str) -> bool:
    """@p text 에서 @p name 이 스트림에서 읽은 값이면 true 입니다."""
    return re.search(rf">>\s*(?:[\w.]*\.)?{name}\b|\b{name}\s*=\s*[\w:.>-]*read\w*\(|read(?:Pod|VarUint|VarInt)\(\s*{name}\s*\)", text) is not None


def hasCountCheck(text: str, name: str) -> bool:
    """@p text 에서 @p name 을 남은 바이트로 걸렀거나 0 이 아닌 값과 비교했으면 true 입니다(0 비교는 상한이 아니다)."""
    if re.search(rf"(?:readCount|canHold|canHoldCount|tryMultiplyCounts|hasBytesAvailable)\([^;]*\b{name}\b", text) is not None:
        return True
    # 연산자 양옆의 공백을 본다(clang-format 이 지킨다) — `archive >> count` 와 `vector<uint32> count` 는 비교가 아니다.
    comparison = re.compile(rf"\b{name}\b\s+(?:!=|==|<=|>=|<|>)\s+(\S+)|(\S+)\s+(?:!=|==|<=|>=|<|>)\s+(?:static_cast<\w+>\(\s*)?\b{name}\b")
    return any((match.group(1) or match.group(2)) not in ("0", "0u", "0)", "0u)") for match in comparison.finditer(text))


def inspectText(text: str, relative: str, setRawFile: frozenset[str]) -> list[str]:
    """글 하나를 봅니다(자체 시험이 파일 없이 부를 수 있게 나눴다)."""
    listViolation: list[str] = []
    listRaw = text.split("\n")
    listLine = stripCode(text).split("\n")
    for index, line in enumerate(listLine):
        if "LittleEndianUtil::read" in line and relative not in setRawFile:
            listViolation.append(f"{relative}:{index + 1}: `LittleEndianUtil::read*` 로 제 커서를 굴리지 않는다 — `ByteReader` 로 읽는다(바깥 형식이면 {kRawListFile} 에 이유와 함께 적는다)")
        for pattern in (kAllocation, kVectorConstructor):
            match = pattern.search(line)
            if match is None or kExempt in listRaw[index]:
                continue
            name = re.split(r"\.|->", match.group(1))[-1]
            before = "\n".join(listLine[findFunctionStart(listLine, index):index])
            if hasCountFromRead(before, name) and not hasCountCheck(before, name):
                listViolation.append(f"{relative}:{index + 1}: 읽은 개수 `{name}` 를 거르지 않고 할당한다 — `ByteReader::readList` · `canHold` · `Archive::readCount` 를 지난다(다른 까닭이면 `{kExempt}: <이유>`)")
    return listViolation


def inspectListedFile(projectRoot: Path, setRawFile: frozenset[str], file: str) -> list[str]:
    """파일 하나를 봅니다. 대상이 아니면 빈 목록입니다(프로세스로 넘기므로 모듈 최상위에 둔다). 테스트는 일부러 꾸민 바이트를 다루므로 보지 않습니다."""
    path = Path(file).resolve()
    try:
        relative = path.relative_to(projectRoot).as_posix()
    except ValueError:
        return []
    if not isTarget(relative) or "/Tests/" in relative or not path.exists():
        return []
    return inspectText(path.read_text(encoding="utf-8", errors="replace"), relative, setRawFile)


def loadRawFiles(projectRoot: Path) -> frozenset[str]:
    """직접 읽기를 허용한 파일 목록입니다. 없으면 예외로 멈춘다(빈 목록으로 대신하지 않는다)."""
    data = json.loads((projectRoot / kRawListFile).read_text(encoding="utf-8"))
    return frozenset(entry["_path"] for entry in data["_listFile"])


def checkByteReadShape(projectRoot: Path, listFile: Sequence[str]) -> list[str]:
    """@p listFile 가운데 Source 의 C++ 파일을 보고 어긋남을 `경로:줄: 설명` 으로 돌려준다."""
    return flatMapInProcesses(functools.partial(inspectListedFile, projectRoot, loadRawFiles(projectRoot)), listFile)


def selfCheck() -> None:
    """규칙이 잡을 것을 잡고 거른 것을 놓아 주는지 봅니다."""
    kNone: frozenset[str] = frozenset()
    bad = "bool A::read( Archive& archive )\n{\n    uint32 count = 0;\n    archive >> count;\n    _list.resize( count );\n}\n"
    assert len(inspectText(bad, "x.cpp", kNone)) == 1
    badReader = "bool A::read()\n{\n    const uint32 count = reader.readUint32();\n    vector<uint8> list( count );\n}\n"
    assert len(inspectText(badReader, "x.cpp", kNone)) == 1
    zeroOnly = "bool A::read()\n{\n    const uint32 count = reader.readUint32();\n    if ( count == 0 )\n        return true;\n    _list.reserve( count );\n}\n"
    assert len(inspectText(zeroOnly, "x.cpp", kNone)) == 1
    checked = "bool A::read( Archive& archive )\n{\n    uint32 count = 0;\n    if ( archive.readCount( 4, count ) == false )\n        return false;\n    _list.resize( count );\n}\n"
    assert inspectText(checked, "x.cpp", kNone) == []
    other = "void A::a()\n{\n    uint32 count = reader.readUint32();\n}\n\nvoid A::b()\n{\n    const size_t count = _list.size();\n    _other.resize( count );\n}\n"
    assert inspectText(other, "x.cpp", kNone) == []
    exempt = "void A::a()\n{\n    archive >> count;\n    _list.resize( count ); // NOLINT(sw-unchecked-count): 범위는 위에서\n}\n"
    assert inspectText(exempt, "x.cpp", kNone) == []
    raw = "uint32 A::a() { return LittleEndianUtil::readUint32( p ); }\n"
    assert len(inspectText(raw, "x.cpp", kNone)) == 1
    assert inspectText(raw, "x.cpp", frozenset({"x.cpp"})) == []
    assert inspectText("// LittleEndianUtil::readUint32\n", "x.cpp", kNone) == []


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="파일에서 읽은 개수를 믿고 할당하는 모양과 `LittleEndianUtil::read*` 직접 읽기를 본다")
    parser.add_argument("--files", nargs="*", default=None, help="볼 파일(없으면 Source 전체)")
    parser.add_argument("--all", action="store_true", help="Source 전체")
    parser.add_argument("--self-check", action="store_true", help="규칙 자체를 시험한다")
    args = parser.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")
    if args.self_check:
        selfCheck()
        print("[ByteReadShape] self-check OK")
        return 0
    projectRoot = Path(__file__).resolve().parents[2]
    listFile = args.files if args.files else [str(path) for path in (projectRoot / "Source").rglob("*") if path.suffix in (".h", ".cpp")]
    listViolation = checkByteReadShape(projectRoot, listFile)
    for line in listViolation:
        print(line)
    print(f"[ByteReadShape] {'FAIL' if listViolation else 'OK'} — {len(listViolation)}건")
    return 1 if listViolation else 0


if __name__ == "__main__":
    sys.exit(main())
