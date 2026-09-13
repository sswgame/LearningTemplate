#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
렌더 패킷 소유 규칙 검사.

게임 스레드가 만들어 렌더 스레드로 넘기는 것(GpuSceneSnapshot · RenderFramePacket 과 그 안의 구조체)은
**소유를 함께 실어야 한다.** 생포인터를 실으면 GT 가 놓은 뒤 RT 가 해제된 메모리를 읽는다 — 2026-09-12 에
MaterialInstance 로 실제로 그랬다(ASAN heap-use-after-free). 그리고 그 객체를 게임 모듈이 make_shared 로
만들면 제어 블록이 모듈 DLL 에 살아, 엔진이 마지막 참조를 놓을 때 이미 내려간 코드로 뛰어든다 — 벤치 종료
세그폴트가 그것이었다.

강제 규칙 — 하나뿐이다. 나머지는 타입이 지킨다:
  1) 아래 "옮겨지는 헤더" 에 선언된 **모든** 구조체의 멤버에 원시 포인터(`T* _x`)가 없다.
     소유는 shared_ptr, 값은 값으로.
  C++ 는 "필드를 추가하는 것" 자체를 막지 못하므로 이것만 스크립트가 본다. "옮겨지는 값의 집합" 은
  GpuSceneSnapshot 타입이, "Material·MaterialInstance·Mesh 는 Engine 안에서 shared_ptr 로만 태어난다" 는
  패스키 생성자(CreateKey)가 컴파일 시점에 보장한다 — 그래서 make_shared 규칙은 여기 없다.

**구조체 이름을 나열하지 않는다 — 헤더 전체를 본다.**
  예전에는 (파일, 구조체 이름) 다섯 쌍을 여기 적어 두었다. 그러면 **그 헤더에 구조체를 새로 더해도 검사를
  빠져나간다**(2026-09-13 에 실험으로 확인했다: 스냅샷이 싣는 새 구조체에 생포인터를 넣어도 통과했다).
  게다가 목록은 파일이 옮겨질 때마다 손으로 고쳐야 했다. 이제 헤더를 정하고 그 안의 모든 구조체를 본다 —
  **목록이 아니라 자리가 규칙이다.**

**예외는 코드 옆에 이유와 함께 적는다.**
  정체성 키(비교만 하고 역참조하지 않는다)는 생포인터가 맞다. 그런 구조체는 선언 바로 위에
  `// SW_OWNERSHIP_RAW_OK: <이유>` 를 적는다. 이유 없는 예외는 다음 사람이 같은 판단을 다시 하게 만든다.

  python Scripts/lint/gate/CheckRenderOwnership.py [--root <repo>]
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from common import useUtf8Stdout  # noqa: E402

# 옮겨지는 것이 선언되는 헤더. 여기 있는 구조체는 **전부** 검사 대상이다.
_kTransportedHeaders: list[str] = [
    "Source/Engine/Graphics/Renderer/Scene/GpuSceneSnapshot.h",
    "Source/Engine/Graphics/Renderer/Frame/RenderFramePacket.h",
]

_kRawPointerMember = re.compile(r"^\s*(?:const\s+)?[A-Za-z_][\w:<>]*\s*\*\s+_\w+\s*(?:\{|;|=)")
# 선언 시작 — 줄 맨 앞의 `struct X`. 주석 안의 `@struct X` 를 잡지 않으려고 줄 시작을 요구한다.
_kStructDecl = re.compile(r"^[ \t]*struct\s+(?:SW_API\s+)?(\w+)\b[^;{]*$|^[ \t]*struct\s+(?:SW_API\s+)?(\w+)\b[^;]*\{", re.M)
_kExemptMarker = re.compile(r"//\s*SW_OWNERSHIP_RAW_OK\s*:\s*(\S.*)")


def stripComments(text: str) -> str:
    """
    주석을 **같은 길이의 공백**으로 바꿉니다(줄바꿈은 남긴다).

    길이를 유지하는 이유: 오프셋이 어긋나면 아래 중괄호 세기가 다른 구조체를 집는다.
    예전 구현은 주석을 지우지 않아 `@struct Foo` 라는 **문서 주석 언급**에 정규식이 걸렸고,
    거기서부터 다음 `{` 를 찾아 **엉뚱한 구조체의 본문**을 재고 있었다(2026-09-13 확인).
    """
    out = list(text)
    index = 0
    length = len(text)
    while index < length:
        if text.startswith("//", index):
            end = text.find("\n", index)
            end = length if end < 0 else end
            for pos in range(index, end):
                out[pos] = " "
            index = end
        elif text.startswith("/*", index):
            end = text.find("*/", index + 2)
            end = length if end < 0 else end + 2
            for pos in range(index, end):
                if out[pos] != "\n":
                    out[pos] = " "
            index = end
        else:
            index += 1
    return "".join(out)


def findStructBodies(text: str) -> list[tuple[str, int, str]]:
    """
    파일에 선언된 구조체들을 (이름, 선언 줄번호, 본문) 으로 돌려줍니다 — 중첩 구조체 포함.
    """
    stripped = stripComments(text)
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


def findExemptReason(text: str, declLineNo: int) -> str | None:
    """선언 바로 위(주석 블록 포함)에 적힌 예외 사유. 없으면 None."""
    lines = text.splitlines()
    index = declLineNo - 2  # 0-기반, 선언 바로 위
    while index >= 0:
        stripped = lines[index].strip()
        if stripped == "":
            break
        marker = _kExemptMarker.search(lines[index])
        if marker is not None:
            return marker.group(1).strip()
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
            reason = findExemptReason(text, declLineNo)
            for line in stripComments(body).splitlines():
                if _kRawPointerMember.match(line) is None:
                    continue
                if reason is not None:
                    continue
                errors.append(
                    f"{relPath}:{declLineNo}: {structName} 안의 원시 포인터 멤버 — `{line.strip()}` "
                    f"(소유는 shared_ptr, 값은 값으로. 정체성 키라면 선언 위에 "
                    f"`// SW_OWNERSHIP_RAW_OK: <이유>` 를 적으세요)"
                )
    return errors, checkedCount



# 이 린트가 **반드시 잡아야 하는** 조각. `CheckLintsAreAlive.py` 가 임시 트리에 써서 돌려 보고,
# 통과해 버리면 검사가 죽은 것으로 본다. 조각을 여기 두는 이유는 하나다 — 표를 따로 만들면 어긋난다.
kSelfTestCases = [
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
]

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="렌더 패킷 소유 규칙 검사")
    parser.add_argument("--root", default=str(Path(__file__).resolve().parents[2]))
    args = parser.parse_args(argv)
    useUtf8Stdout()
    rootDir = Path(args.root).resolve()

    errors, checkedCount = checkTransportedHeaders(rootDir)
    if errors:
        print(f"[CheckRenderOwnership] 위반 {len(errors)}건", file=sys.stderr)
        for error in errors:
            print(f"  {error}")
        return 1
    print(f"[CheckRenderOwnership] OK ({checkedCount} structs in {len(_kTransportedHeaders)} headers)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
