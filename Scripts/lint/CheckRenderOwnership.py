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
  1) 아래 "옮겨지는 구조체" 의 멤버에 원시 포인터(`T* _x`)가 없다. 소유는 shared_ptr, 값은 값으로.
     (정체성 키 구조체 — GpuMaterialElementKey · GpuSceneSortKey — 는 검사 대상이 아니다. 키는 역참조하지 않는다.)
  C++ 는 "필드를 추가하는 것" 자체를 막지 못하므로 이것만 스크립트가 본다. "옮겨지는 값의 집합" 은
  GpuSceneSnapshot 타입이, "Material·MaterialInstance·Mesh 는 Engine 안에서 shared_ptr 로만 태어난다" 는
  패스키 생성자(CreateKey)가 컴파일 시점에 보장한다 — 그래서 make_shared 규칙은 여기 없다.

  python Scripts/lint/CheckRenderOwnership.py [--root <repo>]
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import useUtf8Stdout  # noqa: E402

# 옮겨지는 구조체: (파일, 구조체 이름)
_kTransportedStructs: list[tuple[str, str]] = [
    ("Source/Engine/Graphics/Renderer/Scene/GpuScene.h", "GpuSceneSnapshot"),
    ("Source/Engine/Graphics/Renderer/Scene/GpuScene.h", "GpuMeshBatch"),
    ("Source/Engine/Graphics/Renderer/Scene/GpuScene.h", "GpuMaterialElement"),
    ("Source/Engine/Graphics/Renderer/Scene/GpuScene.h", "GpuMaterialGroup"),
    ("Source/Engine/Graphics/Renderer/Frame/RenderFramePacket.h", "RenderFramePacket"),
]

_kRawPointerMember = re.compile(r"^\s*(?:const\s+)?[A-Za-z_][\w:<>]*\s*\*\s+_\w+\s*(?:\{|;|=)")


def extractStructBody(text: str, structName: str) -> str | None:
    """`struct Name` 의 중괄호 본문을 돌려줍니다. 없으면 None."""
    match = re.search(r"\bstruct\s+" + re.escape(structName) + r"\b[^{;]*\{", text)
    if match is None:
        return None
    depth = 0
    start = match.end() - 1
    for index in range(start, len(text)):
        ch = text[index]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1 : index]
    return None


def checkTransportedStructs(rootDir: Path) -> list[str]:
    errors: list[str] = []
    for relPath, structName in _kTransportedStructs:
        path = rootDir / relPath
        if path.exists() is False:
            errors.append(f"{relPath}: 파일이 없습니다 (규칙 표를 갱신하세요)")
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        body = extractStructBody(text, structName)
        if body is None:
            errors.append(f"{relPath}: struct {structName} 을 찾지 못했습니다 (규칙 표를 갱신하세요)")
            continue
        for lineNo, line in enumerate(body.splitlines(), start=1):
            stripped = line.strip()
            if stripped.startswith(("//", "/*", "*")):
                continue
            if _kRawPointerMember.match(line):
                errors.append(f"{relPath}: {structName} 안의 원시 포인터 멤버 — `{stripped}` (소유는 shared_ptr, 값은 값으로 실으세요)")
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="렌더 패킷 소유 규칙 검사")
    parser.add_argument("--root", default=str(Path(__file__).resolve().parents[2]))
    args = parser.parse_args(argv)
    useUtf8Stdout()
    rootDir = Path(args.root).resolve()

    errors = checkTransportedStructs(rootDir)
    if errors:
        print(f"[CheckRenderOwnership] 위반 {len(errors)}건", file=sys.stderr)
        for error in errors:
            print(f"  {error}")
        return 1
    print(f"[CheckRenderOwnership] OK ({len(_kTransportedStructs)} structs)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
