#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
`Source/` 의 class · struct 마다 **패딩이 몇 바이트이고, 필드 순서만 바꾸면 얼마나 줄어드는지**를 묻는다.

[왜 필요한가 — 패딩은 아무도 세지 않는다]
AGENTS.md 는 "Arrange fields to minimize byte padding" 을 요구하지만, 필드를 더하는 사람은 자기 필드가
어디에 몇 바이트 구멍을 만드는지 보지 못한다. 컴파일러의 `-Wpadded` 는 clang-cl(MS ABI) 레이아웃에서는
아무것도 내지 않고, `-fdump-record-layouts` 는 필드 크기와 선언 위치를 내지 않는다. 그래서 여기서는
**libclang 으로 실제 빌드 플래그 그대로** TU 를 파싱해, 레코드마다 크기 · 정렬 · 필드 오프셋 · 필드 크기를
직접 묻는다(`clang_Type_getSizeOf` · `clang_Cursor_getOffsetOfField` · `clang_getOffsetOfBase`).
컴파일 DB 의 컴파일러 옆 libclang 을 쓰므로 빌드와 같은 레이아웃이 나온다.

[무엇을 세나]
- 크기(sizeof) · 정렬(alignof)
- 패딩 = 크기 − (베이스 · 필드 · 비트필드 저장 단위 · vfptr 가 덮는 바이트)
- 최소 = 덮는 바이트를 정렬 크기로 올림한 값 — 필드를 정렬 크기 내림차순으로 놓으면 닿는 하한이다.
  베이스는 맨 앞에 고정이므로 베이스 정렬이 작은 파생 타입은 이 하한에 못 닿을 수 있다.
- 절약 = 크기 − 최소. 기본 표는 절약이 1 바이트 이상인 레코드만 보인다(`--all` 은 패딩이 있는 전부).
- `alignas` 를 단 필드가 있는 레코드(`[alignas]`)는 캐시 줄 분리처럼 일부러 둔 패딩이라 절약을 0 으로 센다.
- 빈 타입(태그 · 정적 함수 모음)의 1 바이트는 패딩이 아니라 C++ 의 최소 크기라 표에 넣지 않는다.

[구성마다 다르다]
`#if !defined( SW_SHIPPING )` 으로 빠지는 필드가 있으면 레이아웃이 구성마다 다르다. `--preset Ninja-Shipping` 으로
그 구성의 컴파일 DB 를 쓰거나, `--define SW_SHIPPING` 으로 Debug DB 에 정의만 더해 본다. 두 구성 모두에서 패딩이
적어야 한다 — 빠지는 필드는 끝에 모아 두는 것이 보통 답이다.

[게이트가 아니다]
`Run*` 은 보고하고 `Check*` 이 막는다 — 늘 0 으로 끝난다. 전 트리는 TU 마다 libclang 파싱이라 수 분이 걸린다.
한 덩어리 작업에서 필드를 여럿 더했을 때 돌린다.

사용법:
  py -3 Scripts/lint/report/RunPaddingReport.py                                   # Ninja-Debug, Source/ 전 TU
  py -3 Scripts/lint/report/RunPaddingReport.py --preset Ninja-Shipping
  py -3 Scripts/lint/report/RunPaddingReport.py --define SW_SHIPPING --filter Engine/Graphics
  py -3 Scripts/lint/report/RunPaddingReport.py --files Source/Engine/Scene/Scene.h --detail   # 그 파일에 정의된 레코드만, 필드 배치까지
  py -3 Scripts/lint/report/RunPaddingReport.py --all --out padding.tsv
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import shlex
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintReport
from common import TranslationUnitSweep, runProcess  # noqa: E402
from LintReport import LintReport, ReportContext  # noqa: E402

_kTag = "RunPaddingReport"

#: 결과물을 만드는 인자 · PCH 사용 인자. libclang 은 문법만 보고, MSVC PCH(`/Yu` · `/Fp`)는 읽지 못한다.
#: `/FI…cmake_pch.hxx` 는 남긴다 — 그 헤더가 TU 가 기대는 include 를 넣어 준다.
_kDropExact = ("-c", "/c", "/showIncludes", "-MD", "-clang:-MD", "--")
_kDropWithValue = ("-o", "-MT", "-MF")
_kDropPrefix = ("/Fo", "/Fd", "/Fp", "/Yu", "/Yc", "-clang:-MT", "-clang:-MF")

# libclang 커서 종류(Index.h 의 CXCursorKind).
_kCursorStructDecl = 2
_kCursorUnionDecl = 3
_kCursorClassDecl = 4
_kCursorFieldDecl = 6
_kCursorCxxMethod = 21
_kCursorDestructor = 25
_kMethodKinds = (_kCursorCxxMethod, _kCursorDestructor)
_kCursorNamespace = 22
_kCursorLinkageSpec = 23
_kCursorUnexposedDecl = 1
_kCursorBaseSpecifier = 44
_kCursorAlignedAttr = 441
_kRecordKinds = (_kCursorStructDecl, _kCursorUnionDecl, _kCursorClassDecl)
_kContainerKinds = (_kCursorNamespace, _kCursorLinkageSpec, _kCursorUnexposedDecl)

_kVisitBreak = 0
_kVisitContinue = 1
_kVisitRecurse = 2

_kParseIncomplete = 0x02
_kDiagnosticError = 3

_kPointerSize = 8


class CxCursor(ctypes.Structure):
    _fields_ = [("kind", ctypes.c_int), ("xdata", ctypes.c_int), ("data", ctypes.c_void_p * 3)]


class CxType(ctypes.Structure):
    _fields_ = [("kind", ctypes.c_int), ("data", ctypes.c_void_p * 2)]


class CxString(ctypes.Structure):
    _fields_ = [("data", ctypes.c_void_p), ("privateFlags", ctypes.c_uint)]


class CxSourceLocation(ctypes.Structure):
    _fields_ = [("ptrData", ctypes.c_void_p * 2), ("intData", ctypes.c_uint)]


_kVisitorType = ctypes.CFUNCTYPE(ctypes.c_int, CxCursor, CxCursor, ctypes.c_void_p)


class LibClang:
    """이 스크립트가 쓰는 libclang C API 만 ctypes 로 묶는다(파이썬 `clang` 패키지는 설치돼 있지 않다)."""

    def __init__(self, libraryPath: Path) -> None:
        self._library = ctypes.CDLL(str(libraryPath))
        self._declareInternal("clang_createIndex", ctypes.c_void_p, [ctypes.c_int, ctypes.c_int])
        self._declareInternal("clang_disposeIndex", None, [ctypes.c_void_p])
        self._declareInternal("clang_parseTranslationUnit", ctypes.c_void_p,
                              [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int,
                               ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint])
        self._declareInternal("clang_disposeTranslationUnit", None, [ctypes.c_void_p])
        self._declareInternal("clang_getNumDiagnostics", ctypes.c_uint, [ctypes.c_void_p])
        self._declareInternal("clang_getDiagnostic", ctypes.c_void_p, [ctypes.c_void_p, ctypes.c_uint])
        self._declareInternal("clang_getDiagnosticSeverity", ctypes.c_int, [ctypes.c_void_p])
        self._declareInternal("clang_getDiagnosticSpelling", CxString, [ctypes.c_void_p])
        self._declareInternal("clang_disposeDiagnostic", None, [ctypes.c_void_p])
        self._declareInternal("clang_getTranslationUnitCursor", CxCursor, [ctypes.c_void_p])
        self._declareInternal("clang_visitChildren", ctypes.c_uint, [CxCursor, _kVisitorType, ctypes.c_void_p])
        self._declareInternal("clang_getCursorLocation", CxSourceLocation, [CxCursor])
        self._declareInternal("clang_getExpansionLocation", None,
                              [CxSourceLocation, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_uint),
                               ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint)])
        self._declareInternal("clang_getFileName", CxString, [ctypes.c_void_p])
        self._declareInternal("clang_getCString", ctypes.c_char_p, [CxString])
        self._declareInternal("clang_disposeString", None, [CxString])
        self._declareInternal("clang_getCursorSpelling", CxString, [CxCursor])
        self._declareInternal("clang_getCursorType", CxType, [CxCursor])
        self._declareInternal("clang_getTypeSpelling", CxString, [CxType])
        self._declareInternal("clang_Type_getSizeOf", ctypes.c_longlong, [CxType])
        self._declareInternal("clang_Type_getAlignOf", ctypes.c_longlong, [CxType])
        self._declareInternal("clang_Type_getOffsetOf", ctypes.c_longlong, [CxType, ctypes.c_char_p])
        self._declareInternal("clang_Cursor_getOffsetOfField", ctypes.c_longlong, [CxCursor])
        self._declareInternal("clang_getOffsetOfBase", ctypes.c_longlong, [CxCursor, CxCursor])
        self._declareInternal("clang_Cursor_isBitField", ctypes.c_uint, [CxCursor])
        self._declareInternal("clang_getFieldDeclBitWidth", ctypes.c_int, [CxCursor])
        self._declareInternal("clang_Cursor_isAnonymousRecordDecl", ctypes.c_uint, [CxCursor])
        self._declareInternal("clang_isCursorDefinition", ctypes.c_uint, [CxCursor])
        self._declareInternal("clang_CXXMethod_isVirtual", ctypes.c_uint, [CxCursor])
        self._declareInternal("clang_getTypeDeclaration", CxCursor, [CxType])

    def _declareInternal(self, name: str, resultType: Any, listArgumentType: list) -> None:
        function = getattr(self._library, name)
        function.restype = resultType
        function.argtypes = listArgumentType

    def __getattr__(self, name: str) -> Any:
        return getattr(self._library, name)

    def toText(self, cxString: CxString) -> str:
        """CXString 을 파이썬 문자열로 옮기고 해제한다."""
        raw = self._library.clang_getCString(cxString)
        text = raw.decode("utf-8", errors="replace") if raw else ""
        self._library.clang_disposeString(cxString)
        return text

    def getLocation(self, cursor: CxCursor) -> tuple[str, int]:
        """커서가 놓인 (파일, 줄). 매크로 전개라면 전개된 자리다."""
        fileHandle = ctypes.c_void_p()
        line = ctypes.c_uint()
        column = ctypes.c_uint()
        offset = ctypes.c_uint()
        self._library.clang_getExpansionLocation(self._library.clang_getCursorLocation(cursor), ctypes.byref(fileHandle),
                                                 ctypes.byref(line), ctypes.byref(column), ctypes.byref(offset))
        if not fileHandle.value:
            return "", 0
        return self.toText(self._library.clang_getFileName(fileHandle)), int(line.value)

    def visitChildren(self, cursor: CxCursor, callback: Callable[[CxCursor], int]) -> None:
        """`callback(child) -> _kVisit*` 로 자식을 훑는다."""
        def trampolineInternal(child: CxCursor, parent: CxCursor, clientData: object) -> int:
            return callback(child)
        self._library.clang_visitChildren(cursor, _kVisitorType(trampolineInternal), None)


@dataclass
class MemberSpan:
    """레코드 안에서 바이트를 차지하는 것 하나 — 필드 · 베이스 · 비트필드 저장 단위 · vfptr."""
    name: str
    offset: int          # 바이트
    size: int            # 바이트
    align: int
    bitText: str = ""    # 비트필드면 "bit 3..4" 처럼


@dataclass
class RecordLayout:
    name: str
    file: str
    line: int
    size: int
    align: int
    listSpan: list[MemberSpan] = field(default_factory=list)
    #: `alignas` 를 단 필드가 있다 — 캐시 줄 분리처럼 **일부러 둔** 패딩이라 재배치로 줄일 대상이 아니다.
    bHasAlignedField: bool = False

    def computeCoveredBytes(self) -> int:
        """구간을 합쳐 덮인 바이트 수를 센다(빈 베이스 · 비트필드 저장 단위가 겹칠 수 있다)."""
        listInterval = sorted((span.offset, span.offset + span.size) for span in self.listSpan if span.size > 0)
        covered = 0
        currentStart = -1
        currentEnd = -1
        for start, end in listInterval:
            if start > currentEnd:
                if currentEnd > currentStart:
                    covered += currentEnd - currentStart
                currentStart, currentEnd = start, end
            elif end > currentEnd:
                currentEnd = end
        if currentEnd > currentStart:
            covered += currentEnd - currentStart
        return min(covered, self.size)

    def computeMinimumSize(self) -> int:
        covered = self.computeCoveredBytes()
        alignment = max(1, self.align)
        return max(alignment, (covered + alignment - 1) // alignment * alignment)

    def toJSON(self) -> str:
        return json.dumps({
            "name": self.name, "file": self.file, "line": self.line, "size": self.size, "align": self.align,
            "spans": [[span.name, span.offset, span.size, span.align, span.bitText] for span in self.listSpan],
            "aligned": self.bHasAlignedField,
        }, ensure_ascii=False)

    @staticmethod
    def fromJSON(text: str) -> "RecordLayout":
        data = json.loads(text)
        record = RecordLayout(data["name"], data["file"], data["line"], data["size"], data["align"])
        record.listSpan = [MemberSpan(*values) for values in data["spans"]]
        record.bHasAlignedField = bool(data.get("aligned"))
        return record


def normalizeSlashInternal(path: str) -> str:
    return path.replace("\\", "/")


class RecordCollector:
    """TU 하나를 훑어 `sourceRoot` 아래에 정의된 레코드의 레이아웃을 모은다."""

    def __init__(self, libClang: LibClang, sourceRoot: str) -> None:
        self._clang = libClang
        self._sourceRootLower = normalizeSlashInternal(sourceRoot).lower().rstrip("/") + "/"
        self._listRecord: list[RecordLayout] = []
        self._uniqueSeen: set[tuple[str, int, str]] = set()

    def collect(self, translationUnit: int) -> list[RecordLayout]:
        self._clang.visitChildren(self._clang.clang_getTranslationUnitCursor(translationUnit), self._visitInternal)
        return self._listRecord

    def _isOurFileInternal(self, filePath: str) -> bool:
        normalized = normalizeSlashInternal(filePath).lower()
        return normalized.startswith(self._sourceRootLower) and "/generated/" not in normalized

    def _visitInternal(self, cursor: CxCursor) -> int:
        kind = cursor.kind
        if kind not in _kRecordKinds and kind not in _kContainerKinds:
            return _kVisitContinue
        filePath, line = self._clang.getLocation(cursor)
        if self._isOurFileInternal(filePath) is False:
            return _kVisitContinue
        if kind in _kContainerKinds:
            return _kVisitRecurse
        if self._clang.clang_isCursorDefinition(cursor) and self._clang.clang_Cursor_isAnonymousRecordDecl(cursor) == 0:
            self._recordInternal(cursor, normalizeSlashInternal(filePath), line)
        return _kVisitRecurse

    def _recordInternal(self, cursor: CxCursor, filePath: str, line: int) -> None:
        clang = self._clang
        recordType = clang.clang_getCursorType(cursor)
        size = clang.clang_Type_getSizeOf(recordType)
        align = clang.clang_Type_getAlignOf(recordType)
        if size <= 0 or align <= 0:
            return   # 의존 타입(템플릿 안) · 불완전 타입
        name = clang.toText(clang.clang_getTypeSpelling(recordType))
        key = (filePath, line, name)
        if key in self._uniqueSeen:
            return
        self._uniqueSeen.add(key)

        record = RecordLayout(name, filePath, line, int(size), int(align))
        state = {"bOwnVirtual": False}

        def visitMemberInternal(child: CxCursor) -> int:
            kind = child.kind
            if kind == _kCursorFieldDecl:
                self._appendFieldInternal(record, child)
                if self._hasAlignedAttributeInternal(child):
                    record.bHasAlignedField = True
            elif kind == _kCursorBaseSpecifier:
                self._appendBaseInternal(record, cursor, child)
            elif kind in _kMethodKinds:
                if clang.clang_CXXMethod_isVirtual(child):
                    state["bOwnVirtual"] = True
            elif kind in _kRecordKinds and clang.clang_Cursor_isAnonymousRecordDecl(child):
                self._appendAnonymousInternal(record, recordType, child)
            return _kVisitContinue

        clang.visitChildren(cursor, visitMemberInternal)

        # MS ABI: 가상 함수를 처음 들이는 클래스는 오프셋 0 에 vfptr 를 둔다(베이스가 이미 가졌으면 베이스 안에 있다).
        bCoversZero = any(span.offset == 0 and span.size > 0 for span in record.listSpan)
        if state["bOwnVirtual"] and bCoversZero is False and record.size >= _kPointerSize:
            record.listSpan.insert(0, MemberSpan("(vfptr)", 0, _kPointerSize, _kPointerSize))
        self._listRecord.append(record)

    def _appendFieldInternal(self, record: RecordLayout, child: CxCursor) -> None:
        clang = self._clang
        fieldType = clang.clang_getCursorType(child)
        fieldSize = clang.clang_Type_getSizeOf(fieldType)
        fieldAlign = clang.clang_Type_getAlignOf(fieldType)
        offsetBits = clang.clang_Cursor_getOffsetOfField(child)
        if fieldSize < 0 or offsetBits < 0:
            return
        name = clang.toText(clang.clang_getCursorSpelling(child))
        if clang.clang_Cursor_isBitField(child):
            width = clang.clang_getFieldDeclBitWidth(child)
            if width <= 0:
                return
            unitBits = int(fieldSize) * 8
            unitOffset = (int(offsetBits) // unitBits) * int(fieldSize)
            bitStart = int(offsetBits) - unitOffset * 8
            record.listSpan.append(MemberSpan(name, unitOffset, int(fieldSize), int(fieldAlign),
                                              f"bit {bitStart}..{bitStart + width - 1}"))
            return
        record.listSpan.append(MemberSpan(name, int(offsetBits) // 8, int(fieldSize), int(fieldAlign)))

    def _hasAlignedAttributeInternal(self, cursor: CxCursor) -> bool:
        state = {"bFound": False}

        def visitInternal(child: CxCursor) -> int:
            if child.kind == _kCursorAlignedAttr:
                state["bFound"] = True
                return _kVisitBreak
            return _kVisitContinue

        self._clang.visitChildren(cursor, visitInternal)
        return state["bFound"]

    def _appendBaseInternal(self, record: RecordLayout, parent: CxCursor, child: CxCursor) -> None:
        clang = self._clang
        baseType = clang.clang_getCursorType(child)
        baseSize = clang.clang_Type_getSizeOf(baseType)
        baseAlign = clang.clang_Type_getAlignOf(baseType)
        offsetBits = clang.clang_getOffsetOfBase(parent, child)
        if baseSize < 0 or offsetBits < 0:
            return
        baseName = clang.toText(clang.clang_getTypeSpelling(baseType))
        # 빈 베이스(크기 1)가 빈 베이스 최적화로 다른 멤버와 겹치면 구간 합치기가 알아서 0 으로 센다.
        record.listSpan.append(MemberSpan(f"(base {baseName})", int(offsetBits) // 8, int(baseSize), int(baseAlign)))

    def _appendAnonymousInternal(self, record: RecordLayout, recordType: CxType, child: CxCursor) -> None:
        """이름 없는 union/struct 멤버 — 첫 이름 있는 필드의 바깥 오프셋에서 안쪽 오프셋을 빼 자리를 구한다."""
        clang = self._clang
        anonymousType = clang.clang_getCursorType(child)
        anonymousSize = clang.clang_Type_getSizeOf(anonymousType)
        anonymousAlign = clang.clang_Type_getAlignOf(anonymousType)
        state = {"name": ""}

        def visitInternal(member: CxCursor) -> int:
            if member.kind == _kCursorFieldDecl:
                state["name"] = clang.toText(clang.clang_getCursorSpelling(member))
                if state["name"]:
                    return _kVisitBreak
            return _kVisitContinue

        clang.visitChildren(child, visitInternal)
        if not state["name"] or anonymousSize < 0:
            return
        encodedName = state["name"].encode("utf-8")
        outerBits = clang.clang_Type_getOffsetOf(recordType, encodedName)
        innerBits = clang.clang_Type_getOffsetOf(anonymousType, encodedName)
        if outerBits < 0 or innerBits < 0:
            return
        record.listSpan.append(MemberSpan("(anonymous)", int(outerBits - innerBits) // 8, int(anonymousSize),
                                          int(anonymousAlign)))


def splitCommandInternal(entry: dict) -> list[str]:
    """
    컴파일 DB 한 줄을 libclang 에 넘길 인자 목록으로 만든다(컴파일러 이름 포함).

    윈도우 Ninja 는 `command` 문자열에 명령줄 규칙으로 이스케이프한 값(`-DSW_LOG_TAG=\\"Engine\\"`)을 넣는다.
    libclang 은 셸을 거치지 않으므로 그 이스케이프를 여기서 풀어야 한다.
    """
    if isinstance(entry.get("arguments"), list):
        return list(entry["arguments"])
    if os.name != "nt":
        return shlex.split(entry.get("command", ""), posix=True)
    listToken: list[str] = []
    for token in shlex.split(entry.get("command", ""), posix=False):
        if len(token) >= 2 and token.startswith('"') and token.endswith('"'):
            token = token[1:-1]
        listToken.append(token.replace('\\"', '"'))
    return listToken


def makeParseArgumentsInternal(entry: dict, listExtraDefine: tuple[str, ...]) -> tuple[str, list[str]]:
    """(컴파일러 경로, libclang 인자) — 출력 · PCH 인자와 소스 파일을 떼고 드라이버 모드를 밝힌다."""
    listToken = splitCommandInternal(entry)
    if listToken and "sccache" in Path(listToken[0]).name.lower():
        listToken = listToken[1:]
    if not listToken:
        return "", []
    compilerPath = listToken[0]
    uniqueSourceFile = {normalizeSlashInternal(str(entry.get(key, ""))).lower() for key in ("file", "seedFile")}

    listArgument: list[str] = []
    bSkipNext = False
    for token in listToken[1:]:
        if bSkipNext:
            bSkipNext = False
            continue
        if token in _kDropWithValue:
            bSkipNext = True
            continue
        if token in _kDropExact or token.startswith(_kDropPrefix):
            continue
        if normalizeSlashInternal(token).lower() in uniqueSourceFile:
            continue
        listArgument.append(token)

    driverName = Path(compilerPath).name.lower()
    if "clang-cl" in driverName or driverName in ("cl.exe", "cl"):
        listArgument.insert(0, "--driver-mode=cl")
    for define in listExtraDefine:
        listArgument.append(f"-D{define}")
    # 경고는 필요 없다 — 오류만 센다.
    listArgument.append("-Wno-everything")
    return compilerPath, listArgument


def findLibClangInternal(compilerPath: str) -> Path | None:
    """컴파일러 옆 libclang — 빌드와 같은 버전이어야 레이아웃이 같다."""
    binDir = Path(compilerPath).parent
    for candidate in (binDir / "libclang.dll", binDir.parent / "lib" / "libclang.so"):
        if candidate.is_file():
            return candidate
    return None


def findResourceDirInternal(compilerPath: str) -> Path | None:
    """
    컴파일러의 리소스 디렉터리(`lib/clang/<버전>`). libclang 은 argv[0] 에 경로가 없어 이것을 스스로 찾지 못하고,
    그러면 clang 의 `stddef.h` 대신 MSVC 것을 읽어 `offsetof` 가 상수식이 아니게 된다(constexpr 표가 오류로 무너진다).
    """
    clangLibDir = Path(compilerPath).parent.parent / "lib" / "clang"
    if clangLibDir.is_dir() is False:
        return None
    listVersionDir = sorted((path for path in clangLibDir.iterdir() if (path / "include").is_dir()), key=lambda path: path.name)
    return listVersionDir[-1] if listVersionDir else None


def parseUnitInWorker(request: dict) -> int:
    """
    자식 프로세스 진입점: TU 하나를 libclang 으로 파싱해 레코드를 JSON 한 줄씩 낸다.

    libclang 이 죽어도 그 TU 하나만 잃도록 TU 마다 프로세스를 나눈다.
    """
    libClang = LibClang(Path(request["libclang"]))
    index = libClang.clang_createIndex(0, 0)
    listArgument = request["arguments"]
    arrArgument = (ctypes.c_char_p * len(listArgument))(*[argument.encode("utf-8") for argument in listArgument])
    previousDirectory = os.getcwd()
    if request.get("directory"):
        os.chdir(request["directory"])
    try:
        translationUnit = libClang.clang_parseTranslationUnit(index, request["file"].encode("utf-8"), arrArgument,
                                                              len(listArgument), None, 0,
                                                              _kParseIncomplete)
    finally:
        os.chdir(previousDirectory)
    if not translationUnit:
        print(json.dumps({"error": f"{request['file']}: clang_parseTranslationUnit 실패"}, ensure_ascii=False))
        return 0

    listError: list[str] = []
    for diagnosticIndex in range(libClang.clang_getNumDiagnostics(translationUnit)):
        diagnostic = libClang.clang_getDiagnostic(translationUnit, diagnosticIndex)
        if libClang.clang_getDiagnosticSeverity(diagnostic) >= _kDiagnosticError:
            listError.append(libClang.toText(libClang.clang_getDiagnosticSpelling(diagnostic)))
        libClang.clang_disposeDiagnostic(diagnostic)
    if listError:
        # 오류가 있어도 레이아웃은 낸다 — 다만 그 TU 의 숫자는 의심하라고 알린다.
        print(json.dumps({"error": f"{request['file']}: 파싱 오류 {len(listError)}건 — 첫 오류: {listError[0]}"},
                         ensure_ascii=False))

    collector = RecordCollector(libClang, request["sourceRoot"])
    for record in collector.collect(translationUnit):
        print(record.toJSON())
    libClang.clang_disposeTranslationUnit(translationUnit)
    libClang.clang_disposeIndex(index)
    return 0


def runUnitInternal(entry: dict, sourceRoot: Path, listExtraDefine: tuple[str, ...]) -> str:
    """TU 하나를 자식 프로세스로 파싱하고 그 출력(JSON 줄)을 돌려준다."""
    compilerPath, listArgument = makeParseArgumentsInternal(entry, listExtraDefine)
    libClangPath = findLibClangInternal(compilerPath) if compilerPath else None
    if libClangPath is None:
        return json.dumps({"error": f"{entry.get('file')}: 컴파일러 옆에서 libclang 을 찾지 못했습니다 ({compilerPath})"},
                          ensure_ascii=False) + "\n"
    resourceDir = findResourceDirInternal(compilerPath)
    if resourceDir is not None:
        listArgument = listArgument + ["-resource-dir", str(resourceDir)]
    request = {
        "libclang": str(libClangPath), "arguments": listArgument, "file": str(entry.get("file", "")),
        "directory": str(entry.get("directory", "")), "sourceRoot": str(sourceRoot),
    }
    completed = runProcess([sys.executable, Path(__file__).resolve(), "--parse-unit"], stdinText=json.dumps(request), timeoutSeconds=900)
    if completed.bTimedOut:
        return json.dumps({"error": f"{entry.get('file')}: 시간 초과(900s)"}, ensure_ascii=False) + "\n"
    if completed.returnCode != 0:
        lastLine = completed.stderr.strip().splitlines()[-1:] or ["(출력 없음)"]
        return json.dumps({"error": f"{entry.get('file')}: 파서 프로세스 종료 코드 {completed.returnCode} — {lastLine[0]}"},
                          ensure_ascii=False) + "\n"
    return completed.stdout


def makeProbeEntriesInternal(repositoryRoot: Path, listHeader: list[Path], listEntry: list[dict],
                             probeDir: Path) -> list[dict]:
    """
    헤더마다 그 헤더만 include 하는 탐침 TU 를 만들고, 경로가 가장 많이 겹치는 TU 의 플래그를 빌려 준다
    (`RunHeaderSelfContained.py` 와 같은 방식).
    """
    sourceRoot = (repositoryRoot / "Source").resolve()
    listProbe: list[dict] = []
    for header in listHeader:
        try:
            spelling = header.resolve().relative_to(sourceRoot).as_posix()
        except ValueError:
            continue
        headerText = normalizeSlashInternal(str(header.resolve())).lower()
        bestEntry: dict | None = None
        bestLength = -1
        for entry in listEntry:
            entryDir = normalizeSlashInternal(str(entry.get("file", ""))).lower().rsplit("/", 1)[0] + "/"
            length = len(os.path.commonprefix([entryDir, headerText]))
            if length > bestLength:
                bestLength = length
                bestEntry = entry
        if bestEntry is None:
            continue
        probePath = probeDir / (spelling.replace("/", "_")[:-2] + "_probe.cpp")
        probePath.write_text(f'#include "{spelling}"\n', encoding="utf-8")
        probe = dict(bestEntry)
        # 빌린 TU 의 소스는 인자에서 떼고(`makeParseArgumentsInternal`) 탐침을 대신 파싱한다.
        probe["seedFile"] = str(bestEntry.get("file", ""))
        probe["file"] = str(probePath)
        listProbe.append(probe)
    return listProbe


def collectRecordsInternal(rawText: str) -> tuple[dict[tuple[str, int, str], list[RecordLayout]], list[str]]:
    """자식 출력을 레코드별로 모은다. 같은 헤더가 TU 마다 나오므로 (파일, 줄, 이름) 으로 유일화하되 크기가 다른 변형은 남긴다."""
    mapKeyToRecord: dict[tuple[str, int, str], list[RecordLayout]] = {}
    listError: list[str] = []
    for line in rawText.splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        data = json.loads(line)
        if "error" in data:
            listError.append(data["error"])
            continue
        record = RecordLayout.fromJSON(line)
        key = (record.file, record.line, record.name)
        listVariant = mapKeyToRecord.setdefault(key, [])
        if all(variant.size != record.size for variant in listVariant):
            listVariant.append(record)
    return mapKeyToRecord, listError


def printDetailInternal(record: RecordLayout) -> None:
    """필드 배치 — 오프셋 · 크기 · 정렬과 그 앞의 구멍."""
    cursor = 0
    for span in sorted(record.listSpan, key=lambda item: (item.offset, -item.size)):
        if span.offset > cursor:
            print(f"        {'':>6}  ({span.offset - cursor} 바이트 패딩)")
        bitText = f"  [{span.bitText}]" if span.bitText else ""
        print(f"        {span.offset:>6}  {span.name}  size={span.size} align={span.align}{bitText}")
        cursor = max(cursor, span.offset + span.size)
    if record.size > cursor:
        print(f"        {'':>6}  ({record.size - cursor} 바이트 꼬리 패딩)")


class RunPaddingReport(LintReport):
    description = "Source/ 레코드의 패딩과 필드 재배치로 줄일 수 있는 크기를 보고합니다 (게이트 아님)"
    bUsesBuildTree = True
    bUsesJobs = True
    bUsesFilter = True
    bUsesOut = True

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--define", action="append", default=[],
                            help="모든 TU 에 더할 전처리 정의(NAME 또는 NAME=VALUE) — 예: --define SW_SHIPPING")
        parser.add_argument("--files", nargs="*", default=None,
                            help="이 파일들에 정의된 레코드만 봅니다. .h 는 그 헤더만 include 하는 탐침 TU 로, .cpp 는 그 TU 로 파싱합니다")
        parser.add_argument("--all", action="store_true", help="줄일 수 없는 패딩(꼬리 등)까지 패딩이 있는 레코드 전부를 보입니다")
        parser.add_argument("--min-saving", type=int, default=1, help="이 바이트 이상 줄일 수 있는 레코드만 보입니다 (기본 1)")
        parser.add_argument("--detail", action="store_true", help="표에 나온 레코드마다 필드 배치를 찍습니다")
        parser.add_argument("--parse-unit", action="store_true", help=argparse.SUPPRESS)   # 자식 프로세스 갈래(runUnitInternal)

    def produce(self, context: ReportContext, args: argparse.Namespace) -> int:
        # 자기 자신을 자식 프로세스로 띄워 TU 하나를 파싱하는 갈래(runUnitInternal) — 표준 입력의 요청을 읽는다.
        if args.parse_unit:
            return parseUnitInWorker(json.loads(sys.stdin.read()))

        repositoryRoot = context.repositoryRoot
        tree = context.buildTree
        sweep = TranslationUnitSweep(tree, tag=_kTag)
        if sweep.bHasDatabase is False:
            sweep.reportMissingDatabase()
            return 0
        listEntry = sweep.selectUnits(args.filter, requirePathPart="/Source/")

        uniqueTargetFile: set[str] = set()
        with tempfile.TemporaryDirectory(prefix="swPaddingProbe") as probeDirName:
            if args.files:
                listPath = [Path(p) if Path(p).is_absolute() else repositoryRoot / p for p in args.files]
                uniqueTargetFile = {normalizeSlashInternal(str(p.resolve())).lower() for p in listPath}
                listHeader = [p for p in listPath if p.suffix == ".h" and p.is_file()]
                listSource = {normalizeSlashInternal(str(p.resolve())).lower() for p in listPath if p.suffix == ".cpp"}
                listUnit = [entry for entry in listEntry
                            if normalizeSlashInternal(str(entry.get("file", ""))).lower() in listSource]
                listUnit += makeProbeEntriesInternal(repositoryRoot, listHeader, listEntry, Path(probeDirName))
            else:
                listUnit = listEntry
            if not listUnit:
                print(f"[{_kTag}] 파싱할 TU 가 없습니다.")
                return 0

            defineText = f", 정의 {' '.join(args.define)}" if args.define else ""
            print(f"[{_kTag}] {tree.name}{defineText}: TU {len(listUnit)}개를 libclang 으로 파싱합니다 …")
            sourceRoot = repositoryRoot / "Source"
            rawText = sweep.run(listUnit, lambda entry: runUnitInternal(entry, sourceRoot, tuple(args.define)),
                                workerCount=context.jobs, progressEvery=100)

        mapKeyToRecord, listError = collectRecordsInternal(rawText)
        listRecord: list[RecordLayout] = [variant for listVariant in mapKeyToRecord.values() for variant in listVariant]
        if uniqueTargetFile:
            listRecord = [record for record in listRecord if record.file.lower() in uniqueTargetFile]

        listRow: list[tuple[int, int, int, RecordLayout]] = []
        for record in listRecord:
            if not record.listSpan:
                continue   # 빈 타입(태그 · 정적 함수 모음) — 1 바이트는 패딩이 아니라 C++ 의 최소 크기다.
            padding = record.size - record.computeCoveredBytes()
            # `alignas` 필드의 패딩은 의도한 것이다 — 절약으로 세지 않는다(`--all` 표에는 `alignas` 표시와 함께 나온다).
            saving = 0 if record.bHasAlignedField else record.size - record.computeMinimumSize()
            listRow.append((saving, padding, record.computeMinimumSize(), record))
        listRow.sort(key=lambda row: (-row[0], -row[1], row[3].name))

        if args.out:
            with open(args.out, "w", encoding="utf-8", newline="\n") as stream:
                stream.write("saving\tpadding\tsize\tminimum\talign\talignas\tname\tfile\tline\n")
                for saving, padding, minimum, record in listRow:
                    relative = os.path.relpath(record.file, repositoryRoot).replace("\\", "/")
                    stream.write(f"{saving}\t{padding}\t{record.size}\t{minimum}\t{record.align}\t{int(record.bHasAlignedField)}\t{record.name}\t{relative}\t{record.line}\n")
            print(f"[{_kTag}] 전체 {len(listRow)}개 → {args.out}")

        listShown = [row for row in listRow if (row[1] > 0 if args.all else row[0] >= args.min_saving)]
        print("")
        print(f"  {'절약':>4} {'패딩':>4} {'크기':>6} {'최소':>6} {'정렬':>4}  타입  (위치)")
        print("  " + "-" * 76)
        for saving, padding, minimum, record in listShown:
            relative = os.path.relpath(record.file, repositoryRoot).replace("\\", "/")
            alignedText = "  [alignas]" if record.bHasAlignedField else ""
            print(f"  {saving:>6} {padding:>6} {record.size:>6} {minimum:>6} {record.align:>6}  {record.name}  ({relative}:{record.line}){alignedText}")
            if args.detail:
                printDetailInternal(record)
        print("")
        totalSaving = sum(row[0] for row in listShown)
        print(f"[{_kTag}] 레코드 {len(listRow)}개 중 {len(listShown)}개 표시 — 표시한 것의 절약 합 {totalSaving} 바이트(인스턴스 하나 기준).")
        if listError:
            print(f"[{_kTag}] 파싱 문제 {len(listError)}건 (그 TU 의 숫자는 의심할 것) — 첫 셋:")
            for message in listError[:3]:
                print(f"    {message}")
        # 보고 도구 — 늘 0 이다.
        return 0


main = RunPaddingReport.run


if __name__ == "__main__":
    sys.exit(main())
