"""
glTF 바이너리(BIN 청크)와 그 위의 bufferView · accessor 목록을 쌓는 도우미입니다.

합성 리그(`dev/MakeSyntheticRig.py`)와 활 · 화살 프롭(`dev/MakeBowProps.py`) 두 생성기가 함께 쓴다. 명령(층 2)끼리 import 하지 않도록
공용 층(`common`)에 둔다(`docs/decisions/5-2_Build.md`).
"""

from __future__ import annotations

import struct

#: glTF accessor 성분 형식 FLOAT
kComponentFloat = 5126


class BufferWriter:
    """BIN 청크 하나와 그 위의 bufferView · accessor 목록을 쌓는다. 모든 뷰는 4 바이트 정렬이다."""

    def __init__(self) -> None:
        self.data = bytearray()
        self.listView: list[dict] = []
        self.listAccessor: list[dict] = []

    def addAccessor(self, payload: bytes, componentType: int, count: int, kind: str, target: int | None = None,
                    bounds: tuple[list[float], list[float]] | None = None) -> int:
        while len(self.data) % 4 != 0:
            self.data.append(0)
        view: dict = {"buffer": 0, "byteOffset": len(self.data), "byteLength": len(payload)}
        if target is not None:
            view["target"] = target
        self.data.extend(payload)
        self.listView.append(view)
        accessor: dict = {"bufferView": len(self.listView) - 1, "componentType": componentType, "count": count, "type": kind}
        if bounds is not None:
            accessor["min"] = bounds[0]
            accessor["max"] = bounds[1]
        self.listAccessor.append(accessor)
        return len(self.listAccessor) - 1

    def addPackedAccessors(self, listPart: list[tuple[bytes, int, int, str, bool, tuple[list[float], list[float]] | None]]) -> list[int]:
        """(바이트, 성분 형식, 개수, 형식, 정규화, min · max) 여럿을 bufferView 하나에 이어 싣는다 — 클립의 표본 수백 개가 뷰 수백 개가 되지 않게."""
        while len(self.data) % 4 != 0:
            self.data.append(0)
        viewOffset = len(self.data)
        listIndex: list[int] = []
        viewIndex = len(self.listView)
        for payload, componentType, count, kind, bNormalized, bounds in listPart:
            accessor: dict = {"bufferView": viewIndex, "byteOffset": len(self.data) - viewOffset, "componentType": componentType, "count": count,
                              "type": kind}
            if bNormalized:
                accessor["normalized"] = True
            if bounds is not None:
                accessor["min"] = bounds[0]
                accessor["max"] = bounds[1]
            self.data.extend(payload)
            while len(self.data) % 4 != 0:
                self.data.append(0)
            self.listAccessor.append(accessor)
            listIndex.append(len(self.listAccessor) - 1)
        self.listView.append({"buffer": 0, "byteOffset": viewOffset, "byteLength": len(self.data) - viewOffset})
        return listIndex

    def addVec3(self, listValue: list[tuple[float, float, float]], target: int | None) -> int:
        payload = b"".join(struct.pack("<3f", *value) for value in listValue)
        lower = [min(value[axis] for value in listValue) for axis in range(3)]
        upper = [max(value[axis] for value in listValue) for axis in range(3)]
        # min · max 는 f32 로 저장된 값과 같아야 한다 — 저장한 바이트에서 다시 읽는다.
        lower = [struct.unpack("<f", struct.pack("<f", value))[0] for value in lower]
        upper = [struct.unpack("<f", struct.pack("<f", value))[0] for value in upper]
        return self.addAccessor(payload, kComponentFloat, len(listValue), "VEC3", target, (lower, upper))
