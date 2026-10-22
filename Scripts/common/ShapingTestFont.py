"""
Scripts/common/ShapingTestFont.py

복합 문자 셰이핑 테스트용 작은 TrueType 글꼴(`Test/EngineTest/Text/Fonts/swshapingtest.ttf`)의 바이트를 만듭니다.
파일을 쓰는 것은 `Scripts/generate/GenerateShapingTestFont.py` 이고, 표준 라이브러리만 씁니다. 같은 입력이면 바이트까지 같은 파일이 나옵니다.

글리프는 아랍어 연결형을 확인하는 데 필요한 만큼만 있다 — 바(U+0628)의 네 모양(독립 · 어두 · 어중 · 어말)과 알리프(U+0627)의 두 모양(독립 · 어말), 라틴 `A`, 공백.
모양 바꾸기는 GSUB 의 `arab` 문자 체계 아래 `init` · `medi` · `fina` 기능(단일 치환)이 한다. 글리프마다 폭이 달라서(독립 700 · 어두 500 · 어중 400 · 어말 600 …)
셰이핑 결과의 글리프 번호와 전진 폭으로 어느 모양이 골라졌는지 안다. 윤곽은 폭에 맞춘 상자 하나다. 그림은 이 저장소가 직접 만들었으므로 CC0 1.0 으로 배포한다(name 테이블에 적는다).
"""
from __future__ import annotations

import struct

kUnitsPerEm = 1000
kAscent = 800
kDescent = -200
kGlyphHeight = 500          # 상자 윤곽의 높이
kSideBearing = 50           # 상자 윤곽의 좌우 여백

#: 글리프 번호 (0 은 .notdef)
kGlyphBehIsolated = 1
kGlyphBehInitial = 2
kGlyphBehMedial = 3
kGlyphBehFinal = 4
kGlyphAlefIsolated = 5
kGlyphAlefFinal = 6
kGlyphLatinA = 7
kGlyphSpace = 8

#: 글리프마다의 전진 폭 (글리프 번호 순서)
kListAdvance = [500, 700, 500, 400, 600, 300, 350, 600, 250]

#: 코드 포인트 → 글리프 번호 (cmap, 코드 포인트 오름차순)
kListCodepoint = [(0x20, kGlyphSpace), (0x41, kGlyphLatinA), (0x627, kGlyphAlefIsolated), (0x628, kGlyphBehIsolated)]

#: GSUB 기능 (태그 오름차순) → 단일 치환 (원래 글리프, 바꾼 글리프) 목록
kMapFeature = {
    "fina": [(kGlyphBehIsolated, kGlyphBehFinal), (kGlyphAlefIsolated, kGlyphAlefFinal)],
    "init": [(kGlyphBehIsolated, kGlyphBehInitial)],
    "medi": [(kGlyphBehIsolated, kGlyphBehMedial)],
}

kFamilyName = "SW Shaping Test"
kPostScriptName = "SWShapingTest-Regular"
kLicenseText = "CC0 1.0 - drawn by the SW Engine repository"


def _u16(value: int) -> bytes:
    return struct.pack(">H", value & 0xFFFF)


def _i16(value: int) -> bytes:
    return struct.pack(">h", value)


def _u32(value: int) -> bytes:
    return struct.pack(">I", value & 0xFFFFFFFF)


def _pad4(data: bytes) -> bytes:
    return data + b"\x00" * (-len(data) % 4)


def _checksum(data: bytes) -> int:
    padded = _pad4(data)
    return sum(struct.unpack(">%dI" % (len(padded) // 4), padded)) & 0xFFFFFFFF


# ------------------------------------------------------------------------------
# 1) 윤곽 · 글리프 목록
# ------------------------------------------------------------------------------
def _buildGlyph(advance: int, bEmpty: bool) -> bytes:
    """폭 `advance` 에 맞춘 상자 윤곽 하나의 glyf 항목입니다. 빈 글리프(공백)는 길이 0 입니다."""
    if bEmpty:
        return b""
    x0 = kSideBearing
    x1 = advance - kSideBearing
    listPoint = [(x0, 0), (x1, 0), (x1, kGlyphHeight), (x0, kGlyphHeight)]
    data = _i16(1) + _i16(x0) + _i16(0) + _i16(x1) + _i16(kGlyphHeight)   # 윤곽 수 · 경계 상자
    data += _u16(len(listPoint) - 1)                                       # 윤곽 끝 점 번호
    data += _u16(0)                                                        # 명령 길이
    data += bytes([1]) * len(listPoint)                                    # 점 플래그: 곡선 위, 좌표는 16 비트 변화량
    previousX = 0
    for x, _ in listPoint:
        data += _i16(x - previousX)
        previousX = x
    previousY = 0
    for _, y in listPoint:
        data += _i16(y - previousY)
        previousY = y
    return data


def _buildGlyfAndLoca() -> tuple[bytes, bytes]:
    glyf = b""
    listOffset = [0]
    for glyphIndex, advance in enumerate(kListAdvance):
        glyf += _pad4(_buildGlyph(advance, glyphIndex == kGlyphSpace))
        listOffset.append(len(glyf))
    loca = b"".join(_u16(offset // 2) for offset in listOffset)           # 짧은 형식은 2 바이트 단위
    return glyf, loca


# ------------------------------------------------------------------------------
# 2) cmap · 헤더 표들
# ------------------------------------------------------------------------------
def _buildCmap() -> bytes:
    """형식 4(BMP) 하위 표 하나 — 코드 포인트마다 길이 1 구간, idDelta 로 글리프를 가리킨다."""
    listSegment = [(code, code, (glyph - code) & 0xFFFF) for code, glyph in kListCodepoint]
    listSegment.append((0xFFFF, 0xFFFF, 1))
    segmentCount = len(listSegment)
    searchRange = 2 * (1 << (segmentCount.bit_length() - 1))
    entrySelector = segmentCount.bit_length() - 1
    rangeShift = 2 * segmentCount - searchRange
    body = _u16(segmentCount * 2) + _u16(searchRange) + _u16(entrySelector) + _u16(rangeShift)
    body += b"".join(_u16(end) for _, end, _ in listSegment) + _u16(0)       # 끝 코드 · 예약
    body += b"".join(_u16(start) for start, _, _ in listSegment)
    body += b"".join(_u16(delta) for _, _, delta in listSegment)
    body += b"".join(_u16(0) for _ in listSegment)                           # idRangeOffset
    subtable = _u16(4) + _u16(6 + len(body)) + _u16(0) + body                # 형식 · 길이 · 언어
    return _u16(0) + _u16(1) + _u16(3) + _u16(1) + _u32(12) + subtable       # cmap 머리 · 레코드(플랫폼 3, 인코딩 1)


def _buildHead(bounds: tuple[int, int, int, int]) -> bytes:
    xMin, yMin, xMax, yMax = bounds
    return (_u32(0x00010000) + _u32(0x00010000) + _u32(0) + _u32(0x5F0F3CF5) + _u16(0x0003) + _u16(kUnitsPerEm)
            + struct.pack(">qq", 0, 0) + _i16(xMin) + _i16(yMin) + _i16(xMax) + _i16(yMax)
            + _u16(0) + _u16(8) + _i16(2) + _i16(0) + _i16(0))


def _buildHhea() -> bytes:
    return (_u32(0x00010000) + _i16(kAscent) + _i16(kDescent) + _i16(0) + _u16(max(kListAdvance)) + _i16(kSideBearing)
            + _i16(kSideBearing) + _i16(max(kListAdvance)) + _i16(1) + _i16(0) + _i16(0) + b"\x00" * 8 + _i16(0)
            + _u16(len(kListAdvance)))


def _buildHmtx() -> bytes:
    return b"".join(_u16(advance) + _i16(kSideBearing) for advance in kListAdvance)


def _buildMaxp() -> bytes:
    return (_u32(0x00010000) + _u16(len(kListAdvance)) + _u16(4) + _u16(1) + _u16(0) + _u16(0) + _u16(2)
            + _u16(0) * 8)


def _buildPost() -> bytes:
    return _u32(0x00030000) + _u32(0) + _i16(-100) + _i16(50) + _u32(0) * 5


def _buildName() -> bytes:
    listRecord = [(0, kLicenseText), (1, kFamilyName), (2, "Regular"), (4, kFamilyName), (6, kPostScriptName)]
    stringData = b""
    listEntry = b""
    for nameID, text in listRecord:
        encoded = text.encode("utf-16-be")
        listEntry += _u16(3) + _u16(1) + _u16(0x409) + _u16(nameID) + _u16(len(encoded)) + _u16(len(stringData))
        stringData += encoded
    return _u16(0) + _u16(len(listRecord)) + _u16(6 + len(listEntry)) + listEntry + stringData


# ------------------------------------------------------------------------------
# 3) GSUB — arab 문자 체계의 단일 치환 기능들
# ------------------------------------------------------------------------------
def _buildCoverage(listGlyph: list[int]) -> bytes:
    return _u16(1) + _u16(len(listGlyph)) + b"".join(_u16(glyph) for glyph in listGlyph)


def _buildSingleSubstitution(listPair: list[tuple[int, int]]) -> bytes:
    listPair = sorted(listPair)
    header = _u16(2) + _u16(6 + 2 * len(listPair)) + _u16(len(listPair))     # 형식 2 · 커버리지 오프셋 · 글리프 수
    return header + b"".join(_u16(substitute) for _, substitute in listPair) + _buildCoverage([glyph for glyph, _ in listPair])


def _buildGsub() -> bytes:
    listTag = sorted(kMapFeature)
    # 찾아보기 목록: 기능마다 찾아보기 하나, 번호는 기능 순서
    listLookup = []
    for tag in listTag:
        subtable = _buildSingleSubstitution(kMapFeature[tag])
        listLookup.append(_u16(1) + _u16(0) + _u16(1) + _u16(8) + subtable)    # 종류 1 · 표지 0 · 하위 표 수 1 · 하위 표 오프셋
    lookupHeader = _u16(len(listLookup))
    offset = 2 + 2 * len(listLookup)
    for lookup in listLookup:
        lookupHeader += _u16(offset)
        offset += len(lookup)
    lookupList = lookupHeader + b"".join(listLookup)
    # 기능 목록
    featureRecord = b""
    featureTables = b""
    featureBase = 2 + 6 * len(listTag)
    for index, tag in enumerate(listTag):
        featureRecord += tag.encode("ascii") + _u16(featureBase + len(featureTables))
        featureTables += _u16(0) + _u16(1) + _u16(index)
    featureList = _u16(len(listTag)) + featureRecord + featureTables
    # 문자 체계 목록: arab 하나, 기본 언어 체계가 모든 기능을 쓴다
    langSys = _u16(0) + _u16(0xFFFF) + _u16(len(listTag)) + b"".join(_u16(index) for index in range(len(listTag)))
    script = _u16(4) + _u16(0) + langSys
    scriptList = _u16(1) + b"arab" + _u16(8) + script
    scriptOffset = 10
    featureOffset = scriptOffset + len(scriptList)
    lookupOffset = featureOffset + len(featureList)
    header = _u16(1) + _u16(0) + _u16(scriptOffset) + _u16(featureOffset) + _u16(lookupOffset)
    return header + scriptList + featureList + lookupList


# ------------------------------------------------------------------------------
# 4) 글꼴 파일
# ------------------------------------------------------------------------------
def buildShapingTestFont() -> bytes:
    """글꼴 파일의 바이트를 만듭니다."""
    glyf, loca = _buildGlyfAndLoca()
    bounds = (kSideBearing, 0, max(kListAdvance) - kSideBearing, kGlyphHeight)
    mapTable = {
        "GSUB": _buildGsub(),
        "cmap": _buildCmap(),
        "glyf": glyf,
        "head": _buildHead(bounds),
        "hhea": _buildHhea(),
        "hmtx": _buildHmtx(),
        "loca": loca,
        "maxp": _buildMaxp(),
        "name": _buildName(),
        "post": _buildPost(),
    }
    listTag = sorted(mapTable)
    tableCount = len(listTag)
    searchRange = 16 * (1 << (tableCount.bit_length() - 1))
    entrySelector = tableCount.bit_length() - 1
    rangeShift = 16 * tableCount - searchRange
    directory = _u32(0x00010000) + _u16(tableCount) + _u16(searchRange) + _u16(entrySelector) + _u16(rangeShift)
    offset = 12 + 16 * tableCount
    body = b""
    headOffset = 0
    for tag in listTag:
        data = mapTable[tag]
        if tag == "head":
            headOffset = offset + len(body)
        directory += tag.encode("ascii") + _u32(_checksum(data)) + _u32(offset + len(body)) + _u32(len(data))
        body += _pad4(data)
    font = bytearray(directory + body)
    adjustment = (0xB1B0AFBA - _checksum(bytes(font))) & 0xFFFFFFFF            # head 의 checkSumAdjustment 는 0 인 채로 합을 구했다
    font[headOffset + 8:headOffset + 12] = _u32(adjustment)
    return bytes(font)
