# -*- coding: utf-8 -*-
"""
소켓 초안(`*.sockets.xml`) 만들기 — 엔진 `SocketSet::loadFromXMLText` 가 읽는 꼴. **bpy 를 쓰지 않는다.**

초안은 **파일이 없을 때만** 쓴다(엔진 `SocketImportUtil::writeIfMissing` 과 같은 규칙). 그 뒤로는 사람이 고치는 원본이라
다시 내보내도 덮어쓰지 않는다 — 소켓을 임포트 산출물과 따로 두는 이유가 그것이다. 덮어쓰려면 지우고 다시 내보낸다.
"""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from xml.sax.saxutils import quoteattr

from . import Conventions


@dataclass
class SocketDraft:
    """소켓 하나 — 엔진 공간 값(이동 m · 회전 도 · 스케일)."""

    name: str
    parent: str = ""
    kind: str = "Attach"
    translation: list[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    rotationDegrees: list[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    scale: list[float] = field(default_factory=lambda: [1.0, 1.0, 1.0])
    preview: str = ""


def makeSocketDraft(name: str, parentBone: str, localMatrix: Conventions.Matrix4, kind: str = "Attach", preview: str = "") -> SocketDraft:
    """Blender 공간의 부모 기준 행렬(열벡터 4x4)에서 엔진 공간 소켓을 만듭니다."""
    translation, rotation, scale = Conventions.decomposeMatrix4(localMatrix)
    engineRotation = Conventions.convertRotationToEngine(rotation)
    return SocketDraft(
        name=Conventions.makeSocketName(name),
        parent=parentBone,
        kind=kind or "Attach",
        translation=Conventions.convertVectorToEngine(translation),
        rotationDegrees=Conventions.computeEngineEulerDegrees(engineRotation),
        scale=Conventions.convertScaleToEngine(scale),
        preview=preview,
    )


def makeSocketXml(listSocket: list[SocketDraft]) -> str:
    """소켓 목록을 XML 텍스트로 씁니다(이름순 · 기본값인 칸은 생략 · 끝에 줄바꿈). 이름이 겹치면 ValueError 입니다."""
    uniqueName: set[str] = set()
    listLine = ["<SocketSet>"]
    for socket in sorted(listSocket, key=lambda item: item.name):
        if socket.name in uniqueName:
            raise ValueError(f"socket '{socket.name}' appears twice")
        uniqueName.add(socket.name)
        listAttribute = [f"name={quoteattr(socket.name)}"]
        if socket.parent:
            listAttribute.append(f"parent={quoteattr(socket.parent)}")
        listAttribute.append(f"kind={quoteattr(socket.kind)}")
        listAttribute.append(f'translation="{Conventions.formatFloat3(socket.translation)}"')
        if any(abs(value) > 1e-4 for value in socket.rotationDegrees):
            listAttribute.append(f'rotation="{Conventions.formatFloat3(socket.rotationDegrees)}"')
        if any(abs(value - 1.0) > 1e-4 for value in socket.scale):
            listAttribute.append(f'scale="{Conventions.formatFloat3(socket.scale)}"')
        if socket.preview:
            listAttribute.append(f"preview={quoteattr(socket.preview)}")
        listLine.append(f"\t<Socket {' '.join(listAttribute)} />")
    listLine.append("</SocketSet>")
    return "\n".join(listLine) + "\n"


def writeSocketXmlIfMissing(path: str, listSocket: list[SocketDraft]) -> bool:
    """파일이 없을 때만 씁니다. 썼으면 True, 이미 있어 두었으면 False 입니다(소켓이 없으면 쓰지 않고 False)."""
    if not listSocket or os.path.exists(path):
        return False
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as file:
        file.write(makeSocketXml(listSocket))
    return True
