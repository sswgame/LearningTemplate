#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
`.pack` 바이너리 레이아웃의 계약을 **읽은 결과**.

`Config/Engine/PackFormat.json` 이 단일 출처라는 것은 그대로다. 문제는 그 파일을 **두 소비자가
각자 파싱하고 있었다**는 것이다:

| | 쿠커 `CookAssets.py` | 헤더 생성기 `GeneratePackFormat.py` |
| --- | --- | --- |
| 타입 표 | `_kStructTypeCodes` + `_kStructTypeSizes` | `kScalarTypes` |
| 배열 표기(`uint8[2]`) 해석 | 문자열 자르기 | 정규식 |
| "필드 합계 = 선언 크기" 검증 | `buildLayout` 안 | `emitStructInternal` 안 |

같은 규칙이 두 벌이라 한쪽만 고치면 조용히 어긋난다 — 이 저장소가 이미 그렇게 **offset 8 부터
어긋난 헤더**를 만들어 "마운트는 되는데 파일이 0개" 인 팩을 배포 직전까지 들고 갔다.

그리고 쿠커 쪽은 읽은 결과를 **딕셔너리에 되쑤셔** 넣고 있었다(`spec["_headerLayout"]`), 거기서 뽑은
모듈 상수 열둘을 파일 앞머리에 늘어놓았다. 계약을 하나 더하면 손댈 자리가 셋이었다.

여기서는 계약을 **객체로 읽는다.** 쿠커는 `packHeader(...)` · `hashPath(...)` 를 부르고, 헤더
생성기는 같은 객체의 `listField` 를 돌며 C++ 을 찍는다. 타입 표도, 배열 해석도, 크기 검증도 하나다.
"""

from __future__ import annotations

import json
import struct
from dataclasses import dataclass
from pathlib import Path

from .Paths import getProjectRoot, normalizePath

#: 계약 파일 (저장소 기준 경로).
kPackFormatConfigRelative = "Config/Engine/PackFormat.json"

#: 계약 파일의 타입 이름 → (struct 포맷 문자, 바이트 크기). C++ 타입 이름은 같은 철자를 쓴다.
_kScalarType: dict[str, tuple[str, int]] = {
    "uint8": ("B", 1),
    "uint16": ("H", 2),
    "uint32": ("I", 4),
    "uint64": ("Q", 8),
}


@dataclass(frozen=True)
class PackField:
    """구조체 필드 하나. `arrayCount` 가 0 이면 스칼라, 1 이상이면 그 길이의 배열이다."""

    name: str
    doc: str
    scalarType: str
    arrayCount: int
    byteSize: int
    offset: int

    @property
    def arraySuffix(self) -> str:
        """C++ 선언에 붙일 `[N]` (스칼라면 빈 문자열)."""
        return f"[{self.arrayCount}]" if self.arrayCount else ""


class PackStruct:
    """
    계약 파일의 구조체 하나 — `struct.pack` 포맷과 C++ 필드 목록이 **같은 필드 표**에서 나온다.

    필드 합계가 선언된 `size` 와 다르면 여기서 멈춘다. 이 검사가 한 곳뿐이라는 것이 요점이다.
    """

    def __init__(self, spec: dict) -> None:
        self.name: str = spec["name"]
        self.doc: str = spec["doc"]
        self.size: int = int(spec["size"])

        listField: list[PackField] = []
        structFormat = "<"
        offset = 0
        for fieldSpec in spec["fields"]:
            typeName = str(fieldSpec["type"])
            scalarType, arrayCount = self.parseTypeInternal(typeName, self.name)
            _, unitSize = _kScalarType[scalarType]
            byteSize = unitSize * arrayCount if arrayCount else unitSize

            # 배열은 고정 길이 바이트열로 다룬다 — 리더는 예약 패딩으로만 쓴다.
            structFormat += f"{byteSize}s" if arrayCount else _kScalarType[scalarType][0]
            listField.append(
                PackField(
                    name=str(fieldSpec["name"]),
                    doc=str(fieldSpec["doc"]),
                    scalarType=scalarType,
                    arrayCount=arrayCount,
                    byteSize=byteSize,
                    offset=offset,
                )
            )
            offset += byteSize

        if offset != self.size:
            raise ValueError(f"{self.name}: 필드 합계 {offset}B 가 선언된 size {self.size}B 와 다릅니다")

        self.listField: tuple[PackField, ...] = tuple(listField)
        self.structFormat: str = structFormat

    @staticmethod
    def parseTypeInternal(typeName: str, structName: str) -> tuple[str, int]:
        """`uint8[2]` 같은 배열 표기를 (스칼라 타입, 원소 수) 로 풉니다. 스칼라는 원소 수 0."""
        arrayCount = 0
        scalarType = typeName
        if typeName.endswith("]"):
            scalarType, _, countText = typeName[:-1].partition("[")
            arrayCount = int(countText)
        if scalarType not in _kScalarType:
            raise ValueError(f"{structName}: 알 수 없는 타입 '{typeName}'")
        return scalarType, arrayCount

    def pack(self, **mapValue) -> bytes:
        """
        필드 **이름**으로 값을 받아 바이트로 찍습니다. 빠뜨린 배열 필드는 0 으로 채웁니다.

        자리 인자를 받지 않는 이유: 헤더는 필드가 열넷이고, 그중 둘은 같은 `uint64` 다.
        자리로 넘기면 두 값을 맞바꿔도 조용히 통과한다 — 이 저장소가 배포 직전까지 들고 갔던
        "마운트는 되는데 파일이 0개" 인 팩이 정확히 그 종류의 사고였다. 이름이 빠지거나 모르는
        이름이 오면 여기서 멈춘다.
        """
        listValue: list = []
        for field in self.listField:
            if field.name in mapValue:
                listValue.append(mapValue.pop(field.name))
            elif field.arrayCount:
                listValue.append(b"\x00" * field.byteSize)  # 예약 패딩
            else:
                raise KeyError(f"{self.name}: 필드 '{field.name}' 의 값이 없습니다")
        if mapValue:
            raise KeyError(f"{self.name}: 계약에 없는 필드 {sorted(mapValue)}")

        packed = struct.pack(self.structFormat, *listValue)
        assert len(packed) == self.size, f"{self.name}: {len(packed)}B != {self.size}B"
        return packed


class PackFormatSpec:
    """
    계약 파일 전체. 쿠커와 헤더 생성기가 **같은 이 객체**에 묻는다.

    `load()` 는 같은 프로젝트 루트에 대해 한 번만 읽는다 — 계약은 한 실행 안에서 바뀌지 않는다.
    """

    _mapCached: dict[Path, PackFormatSpec] = {}

    def __init__(self, spec: dict) -> None:
        self.magicText: str = str(spec["magic"])
        if len(self.magicText) != 4:
            raise ValueError(f"magic 은 4글자여야 합니다: {self.magicText}")
        self.magic: int = int.from_bytes(self.magicText.encode("ascii"), "little")

        self.formatVersion: int = int(spec["formatVersion"])
        self.sectorAlignment: int = int(spec["sectorAlignment"])

        self.header = PackStruct(spec["header"])
        self.entry = PackStruct(spec["entry"])

        self.mapCodec: dict[str, int] = dict(spec["compression"]["codecs"])
        self.mapEncryption: dict[str, int] = dict(spec["encryption"])
        self.mapFlag: dict[str, int] = dict(spec["flags"])
        self.deflateStrategy: str = str(spec["compression"].get("deflateStrategy", "default"))

        self._pathHashOffsetBasis: int = int(spec["pathHash"]["offsetBasis"])
        self._pathHashPrime: int = int(spec["pathHash"]["prime"])

    # --- 읽기 ------------------------------------------------------------------

    @classmethod
    def load(cls, projectRoot: Path | None = None) -> PackFormatSpec:
        root = (projectRoot or getProjectRoot()).resolve()
        if root not in cls._mapCached:
            configPath = root / kPackFormatConfigRelative
            if not configPath.is_file():
                raise FileNotFoundError(f"Pack format contract not found: {configPath}")
            cls._mapCached[root] = cls(json.loads(configPath.read_text(encoding="utf-8")))
        return cls._mapCached[root]

    # --- 값 --------------------------------------------------------------------

    @property
    def codecNone(self) -> int:
        return self.mapCodec["None"]

    @property
    def codecZlib(self) -> int:
        return self.mapCodec["Zlib"]

    @property
    def encryptionNone(self) -> int:
        return self.mapEncryption["None"]

    def codecNameOf(self, codec: int) -> str:
        """코덱 값의 이름 (모르는 값이면 숫자를 그대로 문자열로)."""
        return next((name for name, value in self.mapCodec.items() if value == codec), str(codec))

    # --- 동작 ------------------------------------------------------------------

    def hashPath(self, path: str) -> int:
        """팩 안 경로의 키 — 구분자를 `/` 로 맞추고 소문자로 낮춘 뒤 FNV-1a 64비트."""
        digest = self._pathHashOffsetBasis
        for byteValue in normalizePath(path).lower().encode("utf-8"):
            digest ^= byteValue
            digest = (digest * self._pathHashPrime) & 0xFFFFFFFFFFFFFFFF
        return digest

    def alignOffset(self, offset: int) -> int:
        """오프셋을 섹터 정렬 경계로 올림합니다."""
        mask = self.sectorAlignment - 1
        return (offset + mask) & ~mask
