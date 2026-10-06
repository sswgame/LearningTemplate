#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
렌더 패킷 소유 규칙 검사.

게임 스레드가 만들어 렌더 스레드로 넘기는 것(GpuSceneSnapshot · RenderFramePacket 과 그 안의 구조체)은
**소유를 함께 실어야 한다.** 생포인터를 실으면 GT 가 놓은 뒤 RT 가 해제된 메모리를 읽는다(ASAN
heap-use-after-free — MaterialInstance 가 대표적이다). 그리고 그 객체를 게임 모듈이 make_shared 로
만들면 제어 블록이 모듈 DLL 에 살아, 엔진이 마지막 참조를 놓을 때 이미 내려간 코드로 뛰어든다(종료 시 세그폴트).

강제 규칙 — 하나뿐이다. 나머지는 타입이 지킨다:
  1) 아래 "옮겨지는 헤더" 에 선언된 **모든** 구조체의 멤버에 원시 포인터(`T* _x`)가 없다.
     소유는 shared_ptr, 값은 값으로.
  C++ 는 "필드를 추가하는 것" 자체를 막지 못하므로 이것만 스크립트가 본다. "옮겨지는 값의 집합" 은
  GpuSceneSnapshot 타입이, "Material·MaterialInstance·Mesh 는 Engine 안에서 shared_ptr 로만 태어난다" 는
  패스키 생성자(CreateKey)가 컴파일 시점에 보장한다 — 그래서 make_shared 규칙은 여기 없다.

**구조체 이름을 나열하지 않는다 — 헤더 전체를 본다.**
  (파일, 구조체 이름) 쌍을 나열하면 **그 헤더에 구조체를 새로 더할 때 검사를 빠져나가고**(스냅샷이 싣는 새
  구조체에 생포인터를 넣어도 통과한다), 목록은 파일이 옮겨질 때마다 손으로 고쳐야 한다. 그래서 헤더를 정하고
  그 안의 모든 구조체를 본다 — **목록이 아니라 자리가 규칙이다.**

**예외는 코드 옆에 멤버 이름 · 이유와 함께 적는다.**
  정체성 키(비교만 하고 역참조하지 않는다)는 생포인터가 맞다. 그런 구조체는 선언 바로 위에
  `// SW_OWNERSHIP_RAW_OK( _pA, _pB ): <이유>` 를 적는다 — **적은 멤버만** 면제된다. 구조체를 통째로 면제하면 같은 구조체에
  소유 포인터를 더해도 통과한다. 표식이 적은 멤버가 그 구조체에 원시 포인터로 없으면 낡은 표식으로 실패한다.
  이유 없는 예외는 다음 사람이 같은 판단을 다시 하게 만든다.

  python Scripts/lint/gate/CheckRenderOwnership.py [--root <repo>]
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import blankComments  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

# 옮겨지는 것이 선언되는 헤더. 여기 있는 구조체는 **전부** 검사 대상이다.
_kTransportedHeaders: list[str] = [
    "Source/Engine/Graphics/Renderer/Scene/GpuSceneSnapshot.h",
    "Source/Engine/Graphics/Renderer/Frame/RenderFramePacket.h",
]

_kRawPointerMember = re.compile(r"^\s*(?:const\s+)?[A-Za-z_][\w:<>]*\s*\*\s+(?P<name>_\w+)\s*(?:\{|;|=)")
# 선언 시작 — 줄 맨 앞의 `struct X`. 주석 안의 `@struct X` 를 잡지 않으려고 줄 시작을 요구한다.
_kStructDecl = re.compile(r"^[ \t]*struct\s+(?:SW_API\s+)?(\w+)\b[^;{]*$|^[ \t]*struct\s+(?:SW_API\s+)?(\w+)\b[^;]*\{", re.M)
_kExemptMarker = re.compile(r"//\s*SW_OWNERSHIP_RAW_OK\s*\(\s*(?P<members>[^)]*)\)\s*:\s*(?P<reason>\S.*)")
#: 멤버를 적지 않은 옛 모양 — 구조체 통째 면제라 받지 않는다.
_kBareMarker = re.compile(r"//\s*SW_OWNERSHIP_RAW_OK\b(?!\s*\()")


def findStructBodies(text: str) -> list[tuple[str, int, str]]:
    """
    파일에 선언된 구조체들을 (이름, 선언 줄번호, 본문) 으로 돌려줍니다 — 중첩 구조체 포함.
    """
    stripped = blankComments(text)
    results: list[tuple[str, int, str]] = []
    for match in re.finditer(r"(?m)^[ \t]*struct\s+(?:SW_API\s+)?(\w+)\b", stripped):
        name = match.group(1)
        brace = stripped.find("{", match.end())
        if brace < 0:
            continue
        # 선언과 `{` 사이에 `;` 가 있으면 전방 선언이다.
        if ";" in stripped[match.end():brace]:
            continue
        depth = 0
        for index in range(brace, len(stripped)):
            if stripped[index] == "{":
                depth += 1
            elif stripped[index] == "}":
                depth -= 1
                if depth == 0:
                    lineNo = stripped.count("\n", 0, match.start()) + 1
                    results.append((name, lineNo, text[brace + 1:index]))
                    break
    return results


def findExemptMembers(text: str, declLineNo: int) -> tuple[set[str], str] | str | None:
    """
    선언 바로 위(주석 블록 포함)에 적힌 예외 — (면제 멤버 집합, 이유). 없으면 None, 멤버를 적지 않은 옛 모양이면 그 줄(글자)입니다.
    """
    lines = text.splitlines()
    index = declLineNo - 2  # 0-기반, 선언 바로 위
    while index >= 0:
        stripped = lines[index].strip()
        if stripped == "":
            break
        marker = _kExemptMarker.search(lines[index])
        if marker is not None:
            setMember = {member.strip() for member in marker.group("members").split(",") if member.strip()}
            return setMember, marker.group("reason").strip()
        if _kBareMarker.search(lines[index]) is not None:
            return stripped
        if not stripped.startswith(("//", "*", "/**", "/*")):
            break
        index -= 1
    return None


def checkTransportedHeaders(rootDir: Path) -> tuple[list[str], int]:
    errors: list[str] = []
    checkedCount = 0
    for relPath in _kTransportedHeaders:
        path = rootDir / relPath
        if path.exists() is False:
            errors.append(f"{relPath}: 파일이 없습니다 (검사 대상 헤더 목록을 갱신하세요)")
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        bodies = findStructBodies(text)
        if not bodies:
            errors.append(f"{relPath}: 구조체를 하나도 찾지 못했습니다 (검사가 헛돌고 있습니다)")
            continue
        for structName, declLineNo, body in bodies:
            checkedCount += 1
            exempt = findExemptMembers(text, declLineNo)
            if isinstance(exempt, str):
                errors.append(f"{relPath}:{declLineNo}: {structName} 의 예외 표식이 멤버를 적지 않습니다 — "
                              f"`// SW_OWNERSHIP_RAW_OK( _pA, _pB ): <이유>` 로 면제할 멤버를 적으세요(`{exempt}`)")
                exempt = None
            setExemptMember = exempt[0] if exempt is not None else set()
            setRawMember: set[str] = set()
            for line in blankComments(body).splitlines():
                match = _kRawPointerMember.match(line)
                if match is None:
                    continue
                setRawMember.add(match.group("name"))
                if match.group("name") in setExemptMember:
                    continue
                errors.append(
                    f"{relPath}:{declLineNo}: {structName} 안의 원시 포인터 멤버 — `{line.strip()}` "
                    f"(소유는 shared_ptr, 값은 값으로. 정체성 키라면 선언 위에 "
                    f"`// SW_OWNERSHIP_RAW_OK( {match.group('name')} ): <이유>` 를 적으세요)"
                )
            for member in sorted(setExemptMember - setRawMember):
                errors.append(f"{relPath}:{declLineNo}: {structName} 의 예외 표식이 적은 '{member}' 는 원시 포인터 멤버로 없습니다 — "
                              f"낡은 표식에서 지우세요")
    return errors, checkedCount


class CheckRenderOwnershipGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있어 어긋날 수 없다."""

    description = "렌더 패킷 소유 규칙 검사"
    buildComment = "Checking render packet ownership rules (no raw pointers in snapshots, factory-only shared materials)..."
    timeoutSeconds = 15
    preCommitPattern = ("*.cpp", "*.cc", "*.cxx", "*.c", "*.h", "*.hpp", "*.inl")
    selfTestCases = [
        {
            "name": "스냅샷 구조체에 생포인터",
            "files": {
                "Source/Engine/Graphics/Renderer/Scene/GpuSceneSnapshot.h": (
                    "#pragma once\n\n"
                    "struct GpuProbe\n"
                    "{\n"
                    "    Material* _pMaterial{ nullptr };\n"
                    "};\n"
                ),
                "Source/Engine/Graphics/Renderer/Frame/RenderFramePacket.h": (
                    "#pragma once\n\nstruct RenderFramePacketProbe\n{\n    int32 _value{ 0 };\n};\n"
                ),
            },
        },
        {
            "name": "표식이 _pA 만 면제하는 구조체에 생포인터 _pB 를 더함",
            "files": {
                "Source/Engine/Graphics/Renderer/Scene/GpuSceneSnapshot.h": (
                    "#pragma once\n\n"
                    "// SW_OWNERSHIP_RAW_OK( _pA ): 정체성 키다.\n"
                    "struct GpuProbeKey\n"
                    "{\n"
                    "    Material* _pA{ nullptr };\n"
                    "    Mesh* _pB{ nullptr };\n"
                    "};\n"
                ),
                "Source/Engine/Graphics/Renderer/Frame/RenderFramePacket.h": (
                    "#pragma once\n\nstruct RenderFramePacketProbe\n{\n    int32 _value{ 0 };\n};\n"
                ),
            },
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        errors, checkedCount = checkTransportedHeaders(repositoryRoot)
        return GateResult(
            listViolation=errors,
            summary=f"{checkedCount} structs in {len(_kTransportedHeaders)} headers",
        )


main = CheckRenderOwnershipGate.run


if __name__ == "__main__":
    sys.exit(main())
