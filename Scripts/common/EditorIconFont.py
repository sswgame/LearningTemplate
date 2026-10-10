"""
Scripts/common/EditorIconFont.py

에디터 아이콘 폰트(`Resource/editor/fonts/sweditoricons.ttf`)와 글리프 상수 헤더(`EditorIconGlyphs.h`)의 내용을 만듭니다.
파일을 쓰는 것은 `Scripts/generate/GenerateEditorIcons.py` 이고, 커밋된 파일이 이 내용과 같은지는 `Scripts/lint/gate/CheckEditorIcons.py` 가 봅니다.

아이콘 하나는 `kListIcon` 의 한 줄과 그리기 함수 하나입니다. 그리기 좌표는 24x24 격자이고 y 는 아래로 커집니다.
선 두께는 2, 가장자리 여백은 2 입니다. 도형은 채운 윤곽의 합집합으로 만듭니다. 선분은 사각형과 둥근 끝, 호는 고리 조각,
원은 2차 곡선 여덟 개입니다. 구멍이 꼭 필요한 곳에만 `cut` 으로 반대 방향 윤곽을 둡니다(nonzero 채우기).
**구멍은 채움 하나 안에만 둡니다.** 구멍이 채움 밖으로 나가거나 구멍끼리 겹치면 그 부분이 반대로 칠해집니다.

아이콘을 폰트로 두는 이유는 ImGui 라벨에 문자열로 붙일 수 있어서입니다(`editoricon::kSave` 를 메뉴 · 트리 · 버튼 라벨 앞에 붙인다).
DPI 배율과 테마 색(텍스트 색)도 그대로 따라갑니다. 뷰포트 빌보드는 같은 글리프를 큰 크기로 그립니다. ImGui 1.92 는 크기마다 글리프를
그때그때 래스터라이즈하므로 크기별 PNG 가 필요 없습니다.

그림은 이 저장소가 직접 그렸으므로 CC0 1.0 으로 배포합니다(폰트 name 테이블에 적는다). 표준 라이브러리만 씁니다.
같은 입력이면 바이트까지 같은 파일이 나옵니다.
"""
from __future__ import annotations

import math
import struct
from typing import Callable

kGrid = 24                      # 아이콘 격자 한 변
kUnitsPerEm = 1024              # 폰트 단위. 격자 한 변이 em 이다
kUnit = kUnitsPerEm / kGrid     # 격자 한 칸은 42.67 단위다
kAscent = 896                   # 격자 y=0 의 폰트 y 좌표. 격자 y=24 는 kAscent - em = -128 이다
kDescent = kAscent - kUnitsPerEm
kStroke = 2.0                   # 기본 선 두께(격자 단위)
kFirstCodepoint = 0xE000        # 사용자 영역(PUA) 시작

#: 폰트 파일 (저장소 기준 경로). 에디터는 `editor/fonts/` 에서 읽는다(`EditorFontSetup`).
kFontRelativePath = "Resource/editor/fonts/sweditoricons.ttf"
#: 글리프 상수 헤더 (저장소 기준 경로).
kHeaderRelativePath = "Source/Editor/Common/GUI/EditorIconGlyphs.h"
kFamilyName = "SW Editor Icons"
kPostScriptName = "SWEditorIcons-Regular"
kVersionText = "Version 1.000"

#: 격자 좌표의 점(x, y) · 윤곽 점(x, y, 곡선 위인가) · 폰트 단위 윤곽 점.
GridPoint = tuple[float, float]
ContourPoint = tuple[float, float, bool]
FontPoint = tuple[int, int, bool]


# ------------------------------------------------------------------------------
# 1) 윤곽 — 점 목록 (x, y, bOnCurve), 격자 좌표(y 아래로)
# ------------------------------------------------------------------------------
class Glyph:
    """아이콘 하나의 윤곽 모음입니다. 그리기 함수가 채웁니다."""

    def __init__(self) -> None:
        self.listContour: list[tuple[list[ContourPoint], bool]] = []   # (listPoint, bCut)

    # -- 기본 윤곽 ---------------------------------------------------------------
    def poly(self, listPoint: list[GridPoint], bCut: bool = False) -> None:
        """다각형(꼭짓점만)."""
        self.listContour.append(([(float(x), float(y), True) for x, y in listPoint], bCut))

    def cut(self, listPoint: list[GridPoint]) -> None:
        """다각형 구멍."""
        self.poly(listPoint, bCut=True)

    def circle(self, cx: float, cy: float, r: float, bCut: bool = False) -> None:
        """채운 원(2차 곡선 여덟 조각)."""
        self.listContour.append((arcPoints(cx, cy, r, 0.0, 360.0, bClose=True), bCut))

    def cutCircle(self, cx: float, cy: float, r: float) -> None:
        self.circle(cx, cy, r, bCut=True)

    def rect(self, x: float, y: float, w: float, h: float, r: float = 0.0, bCut: bool = False) -> None:
        """채운 사각형. r 은 모서리 반지름."""
        if r <= 0.0:
            self.poly([(x, y), (x + w, y), (x + w, y + h), (x, y + h)], bCut)
            return
        r = min(r, w * 0.5, h * 0.5)
        listPoint = []
        listPoint += arcPoints(x + w - r, y + r, r, -90.0, 0.0)
        listPoint += arcPoints(x + w - r, y + h - r, r, 0.0, 90.0)
        listPoint += arcPoints(x + r, y + h - r, r, 90.0, 180.0)
        listPoint += arcPoints(x + r, y + r, r, 180.0, 270.0)
        self.listContour.append((listPoint, bCut))

    def cutRect(self, x: float, y: float, w: float, h: float, r: float = 0.0) -> None:
        self.rect(x, y, w, h, r, bCut=True)

    # -- 선 ------------------------------------------------------------------------
    def line(self, x0: float, y0: float, x1: float, y1: float, w: float = kStroke, cap: str = "round") -> None:
        """두께 w 의 선분. cap = round · butt · square."""
        dx, dy = x1 - x0, y1 - y0
        length = math.hypot(dx, dy)
        if length < 1e-6:
            if cap == "round":
                self.circle(x0, y0, w * 0.5)
            return
        ux, uy = dx / length, dy / length
        if cap == "square":
            x0, y0, x1, y1 = x0 - ux * w * 0.5, y0 - uy * w * 0.5, x1 + ux * w * 0.5, y1 + uy * w * 0.5
        nx, ny = -uy * w * 0.5, ux * w * 0.5
        self.poly([(x0 + nx, y0 + ny), (x1 + nx, y1 + ny), (x1 - nx, y1 - ny), (x0 - nx, y0 - ny)])
        if cap == "round":
            self.circle(x0, y0, w * 0.5)
            self.circle(x1, y1, w * 0.5)

    def polyline(self, listPoint: list[GridPoint], w: float = kStroke, cap: str = "round", bClosed: bool = False) -> None:
        """꺾인 선. 꺾이는 점은 둥글게 연결한다."""
        listSeg = list(zip(listPoint, listPoint[1:] + (listPoint[:1] if bClosed else [])))
        for index, ((x0, y0), (x1, y1)) in enumerate(listSeg):
            self.line(x0, y0, x1, y1, w, "butt")
        listJoint = listPoint if bClosed else listPoint[1:-1]
        for x, y in listJoint:
            self.circle(x, y, w * 0.5)
        if bClosed == False:
            for (x, y), (px, py) in ((listPoint[0], listPoint[1]), (listPoint[-1], listPoint[-2])):
                if cap == "round":
                    self.circle(x, y, w * 0.5)
                elif cap == "square":
                    dx, dy = x - px, y - py
                    length = math.hypot(dx, dy)
                    self.line(x, y, x + dx / length * w * 0.5, y + dy / length * w * 0.5, w, "butt")

    def arc(self, cx: float, cy: float, r: float, a0: float, a1: float, w: float = kStroke, cap: str = "round") -> None:
        """고리 조각(각도는 도, 0 = 오른쪽, 화면 시계 방향으로 커진다)."""
        span = a1 - a0
        count = max(1, int(math.ceil(abs(span) / 180.0 - 1e-9)))
        for index in range(count):
            b0 = a0 + span * index / count
            b1 = a0 + span * (index + 1) / count
            if b1 < b0:
                b0, b1 = b1, b0
            listPoint = arcPoints(cx, cy, r + w * 0.5, b0, b1) + arcPoints(cx, cy, r - w * 0.5, b1, b0)
            self.listContour.append((listPoint, False))
        if cap == "round" and abs(span) < 359.9:
            for a in (a0, a1):
                self.circle(cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a)), w * 0.5)

    def ring(self, cx: float, cy: float, r: float, w: float = kStroke) -> None:
        self.arc(cx, cy, r, 0.0, 360.0, w, "butt")

    def frame(self, x: float, y: float, w: float, h: float, r: float = 0.0, sw: float = kStroke) -> None:
        """사각 테두리(선 중심이 x, y, w, h 위)."""
        if r <= 0.0:
            self.polyline([(x, y), (x + w, y), (x + w, y + h), (x, y + h)], sw, bClosed=True)
            return
        self.line(x + r, y, x + w - r, y, sw, "butt")
        self.line(x + w, y + r, x + w, y + h - r, sw, "butt")
        self.line(x + w - r, y + h, x + r, y + h, sw, "butt")
        self.line(x, y + h - r, x, y + r, sw, "butt")
        self.arc(x + w - r, y + r, r, -90.0, 0.0, sw, "butt")
        self.arc(x + w - r, y + h - r, r, 0.0, 90.0, sw, "butt")
        self.arc(x + r, y + h - r, r, 90.0, 180.0, sw, "butt")
        self.arc(x + r, y + r, r, 180.0, 270.0, sw, "butt")

    def dashedFrame(self, x: float, y: float, w: float, h: float, dash: float = 3.0, gap: float = 2.0, sw: float = kStroke) -> None:
        """점선 사각 테두리 — 모서리에서 시작하는 ㄱ 자 네 개 + 변 가운데 조각."""
        for (px, py, qx, qy) in ((x, y, x + w, y), (x + w, y, x + w, y + h), (x + w, y + h, x, y + h), (x, y + h, x, y)):
            length = math.hypot(qx - px, qy - py)
            ux, uy = (qx - px) / length, (qy - py) / length
            pos = 0.0
            while pos < length - 1e-6:
                end = min(pos + dash, length)
                self.line(px + ux * pos, py + uy * pos, px + ux * end, py + uy * end, sw, "square" if pos == 0.0 else "butt")
                pos = end + gap

    def arrowHead(self, tipX: float, tipY: float, angle: float, size: float = 4.0, spread: float = 40.0, sw: float = kStroke) -> None:
        """화살촉(V 자 선). angle 은 화살이 가리키는 방향(도)."""
        for sign in (-1.0, 1.0):
            a = math.radians(angle + 180.0 + sign * spread)
            self.line(tipX, tipY, tipX + size * math.cos(a), tipY + size * math.sin(a), sw)

    def triangle(self, x0: float, y0: float, x1: float, y1: float, x2: float, y2: float, bCut: bool = False) -> None:
        self.poly([(x0, y0), (x1, y1), (x2, y2)], bCut)


def arcPoints(cx: float, cy: float, r: float, a0: float, a1: float, bClose: bool = False) -> list[ContourPoint]:
    """중심 · 반지름 · 각도 구간의 2차 곡선 점들(시작 점 포함, 끝 점 포함 — bClose 면 끝 점은 시작과 같아 뺀다)."""
    span = a1 - a0
    count = max(1, int(math.ceil(abs(span) / 45.0 - 1e-9)))
    step = span / count
    listPoint = []
    for index in range(count):
        b0 = math.radians(a0 + step * index)
        bm = math.radians(a0 + step * (index + 0.5))
        listPoint.append((cx + r * math.cos(b0), cy + r * math.sin(b0), True))
        rc = r / math.cos(math.radians(step) * 0.5)
        listPoint.append((cx + rc * math.cos(bm), cy + rc * math.sin(bm), False))
    if bClose == False:
        b1 = math.radians(a1)
        listPoint.append((cx + r * math.cos(b1), cy + r * math.sin(b1), True))
    return listPoint


# ------------------------------------------------------------------------------
# 2) 아이콘 목록 — (이름, 그리기 함수). 이름은 C++ 상수 kName 이 되고, 순서가 코드포인트(E000 부터)다. **항목을 지우거나 중간에 넣으면 뒤의
#    코드포인트가 밀린다** — 헤더를 같이 다시 만들므로 C++ 은 상수 이름으로만 쓰면 문제없다(글리프 바이트를 손으로 쓰지 말 것).
# ------------------------------------------------------------------------------
def drawFile(g: Glyph) -> None:
    g.polyline([(13, 3), (6, 3), (6, 21), (18, 21), (18, 8), (13, 3)], bClosed=True)
    g.polyline([(13, 3), (13, 8), (18, 8)])


def drawFolder(g: Glyph) -> None:
    g.poly([(3, 5), (9, 5), (11, 7), (21, 7), (21, 19), (3, 19)])


def drawFolderOpen(g: Glyph) -> None:
    g.poly([(3, 5), (9, 5), (11, 7), (19, 7), (19, 10), (6, 10), (3, 18)])
    g.poly([(6.5, 11.5), (22.5, 11.5), (19.5, 19), (3, 19)])


def drawSave(g: Glyph) -> None:
    g.poly([(4, 4), (17, 4), (20, 7), (20, 20), (4, 20)])
    g.cutRect(7, 5.5, 8, 4.5)
    g.cutCircle(12, 15, 2.5)


def drawUndo(g: Glyph) -> None:
    g.arc(13, 13, 6, 180.0, 450.0, cap="butt")
    g.line(13, 19, 9, 19, cap="round")
    g.triangle(3, 13, 10, 13, 6.5, 8)


def drawRedo(g: Glyph) -> None:
    g.arc(11, 13, 6, 90.0, 360.0, cap="butt")
    g.line(11, 19, 15, 19, cap="round")
    g.triangle(14, 13, 21, 13, 17.5, 8)


def drawSearch(g: Glyph) -> None:
    g.ring(10, 10, 6)
    g.line(14.5, 14.5, 20, 20, 3.0)


def drawTerminal(g: Glyph) -> None:
    g.frame(3, 4, 18, 16, 2)
    g.polyline([(7, 9), (10, 12), (7, 15)])
    g.line(12, 15, 17, 15)


def drawExit(g: Glyph) -> None:
    g.polyline([(13, 4), (5, 4), (5, 20), (13, 20)])
    g.line(10, 12, 20, 12)
    g.arrowHead(20, 12, 0.0, 4.0, 45.0)


def drawSettings(g: Glyph) -> None:
    for index in range(8):
        a = math.radians(index * 45.0)
        g.line(12 + 6.5 * math.cos(a), 12 + 6.5 * math.sin(a), 12 + 9 * math.cos(a), 12 + 9 * math.sin(a), 3.5, "butt")
    g.ring(12, 12, 5.5, 3.0)
    g.circle(12, 12, 1.5)


def drawPalette(g: Glyph) -> None:
    g.poly([(12, 3), (17, 4), (20.5, 7.5), (21, 12), (19, 14.5), (15.5, 14.5), (14, 16.5), (15, 19), (13, 21), (8, 20), (4.5, 17), (3, 12), (4.5, 7), (8, 4)])
    g.cutCircle(8, 10, 1.6)
    g.cutCircle(12, 7, 1.6)
    g.cutCircle(16.5, 9, 1.6)
    g.cutCircle(8, 15, 1.6)


def drawLayout(g: Glyph) -> None:
    g.frame(3, 4, 18, 16, 2)
    g.line(3, 9, 21, 9, cap="butt")
    g.line(10, 9, 10, 20, cap="butt")


def drawTable(g: Glyph) -> None:
    g.frame(3, 4, 18, 16, 2)
    g.line(3, 9.5, 21, 9.5, cap="butt")
    g.line(3, 14.5, 21, 14.5, cap="butt")
    g.line(9, 4, 9, 20, cap="butt")
    g.line(15, 9.5, 15, 20, cap="butt")


def drawHammer(g: Glyph) -> None:
    g.poly([(3.8, 9.5), (9.5, 3.8), (13.4, 7.7), (7.7, 13.4)])
    g.line(10.5, 10.5, 19.5, 19.5, 3.0)


def drawWrench(g: Glyph) -> None:
    g.line(4.5, 19.5, 13, 11, 3.5)
    # 머리 = 원에서 오른쪽 위 입을 뺀 한 윤곽(구멍 윤곽은 채움 하나 안에만 둘 수 있어서 윤곽을 직접 만든다)
    g.listContour.append((arcPoints(16, 8, 5.2, -20.0, 250.0) + [(16.8, 7.2, True)], False))


def drawBuildAll(g: Glyph) -> None:
    g.rect(3, 13, 8, 7, 1)
    g.rect(13, 13, 8, 7, 1)
    g.rect(8, 4, 8, 7, 1)


def drawCancel(g: Glyph) -> None:
    g.ring(12, 12, 8)
    g.line(6.5, 6.5, 17.5, 17.5, cap="butt")


def drawSpinner(g: Glyph) -> None:
    for index in range(8):
        a = math.radians(index * 45.0 - 90.0)
        g.circle(12 + 7.5 * math.cos(a), 12 + 7.5 * math.sin(a), 2.2 - index * 0.17)


def drawCheck(g: Glyph) -> None:
    g.polyline([(4, 12.5), (9.5, 18), (20, 6.5)], 3.0)


def drawClose(g: Glyph) -> None:
    g.line(6, 6, 18, 18, 3.0)
    g.line(18, 6, 6, 18, 3.0)


def drawSuccess(g: Glyph) -> None:
    g.circle(12, 12, 9.5)
    g.cut([(6.6, 12.2), (8.4, 10.4), (10.6, 12.6), (15.6, 7.6), (17.4, 9.4), (10.6, 16.2)])


def drawWarning(g: Glyph) -> None:
    g.poly([(12, 2.5), (22.5, 20.5), (1.5, 20.5)])
    g.cutRect(10.8, 8.5, 2.4, 6.5, 1.1)
    g.cutCircle(12, 17.5, 1.4)


def drawError(g: Glyph) -> None:
    g.circle(12, 12, 9.5)
    g.cutRect(10.8, 6, 2.4, 7.5, 1.1)
    g.cutCircle(12, 16.8, 1.4)


def drawInfo(g: Glyph) -> None:
    g.circle(12, 12, 9.5)
    g.cutRect(10.8, 10.5, 2.4, 7.5, 1.1)
    g.cutCircle(12, 7.3, 1.4)


def cutKeyhole(g: Glyph) -> None:
    """자물쇠 열쇠 구멍 — 원 + 다리를 한 윤곽으로(구멍끼리 겹치면 거꾸로 칠해진다)."""
    g.listContour.append((arcPoints(12, 14.5, 1.6, 120.0, 420.0) + [(12.8, 18.0, True), (11.2, 18.0, True)], True))


def drawLock(g: Glyph) -> None:
    g.rect(5, 10, 14, 11, 2)
    g.arc(12, 9, 4.5, 180.0, 360.0, 2.5, "butt")
    g.line(7.5, 9, 7.5, 10.5, 2.5, "butt")
    g.line(16.5, 9, 16.5, 10.5, 2.5, "butt")
    cutKeyhole(g)


def drawUnlock(g: Glyph) -> None:
    g.rect(5, 10, 14, 11, 2)
    g.arc(12, 7, 4.5, 180.0, 360.0, 2.5, "butt")
    g.line(16.5, 7, 16.5, 10.5, 2.5, "butt")
    cutKeyhole(g)


def drawPlus(g: Glyph) -> None:
    g.line(12, 4, 12, 20, 3.0)
    g.line(4, 12, 20, 12, 3.0)


def drawMinus(g: Glyph) -> None:
    g.line(4, 12, 20, 12, 3.0)


def drawTrash(g: Glyph) -> None:
    g.line(4, 6, 20, 6)
    g.polyline([(9, 6), (9, 3.5), (15, 3.5), (15, 6)])
    g.polyline([(6, 6), (7, 21), (17, 21), (18, 6)], cap="butt")
    g.line(10, 10, 10, 17)
    g.line(14, 10, 14, 17)


def drawCopy(g: Glyph) -> None:
    g.frame(8, 8, 12, 13, 2)
    g.polyline([(5, 16), (4, 16), (4, 3), (16, 3), (16, 4)], cap="butt")


def drawRefresh(g: Glyph) -> None:
    g.arc(12, 12, 7, -40.0, 230.0, cap="butt")
    g.triangle(14.5, 3, 21, 7.5, 14, 10.5)


def drawFilter(g: Glyph) -> None:
    g.poly([(3, 4), (21, 4), (14, 12), (14, 20), (10, 18), (10, 12)])


def drawStar(g: Glyph) -> None:
    listPoint = []
    for index in range(10):
        r = 9.5 if index % 2 == 0 else 4.2
        a = math.radians(-90.0 + index * 36.0)
        listPoint.append((12 + r * math.cos(a), 12.8 + r * math.sin(a)))
    g.poly(listPoint)


def drawLink(g: Glyph) -> None:
    for (cx, cy) in ((8.5, 15.5), (15.5, 8.5)):
        a = math.radians(-45.0)
        ux, uy = math.cos(a), math.sin(a)
        # 기울어진 둥근 고리 하나 = 두 반원 + 두 직선
        L, R = 3.0, 3.0
        g.arc(cx - ux * L, cy - uy * L, R, -45.0 + 90.0, -45.0 + 270.0, cap="butt")
        g.arc(cx + ux * L, cy + uy * L, R, -45.0 - 90.0, -45.0 + 90.0, cap="butt")
        for sign in (-1.0, 1.0):
            nx, ny = -uy * R * sign, ux * R * sign
            g.line(cx - ux * L + nx, cy - uy * L + ny, cx + ux * L + nx, cy + uy * L + ny, cap="butt")


def drawEye(g: Glyph) -> None:
    g.arc(12, 20, 12.0, 213.0, 327.0, cap="butt")
    g.arc(12, 4, 12.0, 33.0, 147.0, cap="butt")
    g.circle(12, 12, 3.2)


def drawEyeSlash(g: Glyph) -> None:
    drawEye(g)
    g.line(4, 3.5, 20, 20.5, 2.0)


def drawPlay(g: Glyph) -> None:
    g.poly([(6, 3.5), (20, 12), (6, 20.5)])


def drawPause(g: Glyph) -> None:
    g.rect(5.5, 4, 4.5, 16, 1)
    g.rect(14, 4, 4.5, 16, 1)


def drawStop(g: Glyph) -> None:
    g.rect(5, 5, 14, 14, 1.5)


def drawStepForward(g: Glyph) -> None:
    g.poly([(4, 4), (16, 12), (4, 20)])
    g.rect(17, 4, 3.5, 16, 0.8)


def drawTranslate(g: Glyph) -> None:
    g.line(12, 3, 12, 21)
    g.line(3, 12, 21, 12)
    for (x, y, a) in ((12, 3, -90.0), (12, 21, 90.0), (3, 12, 180.0), (21, 12, 0.0)):
        g.arrowHead(x, y, a, 3.5, 45.0)


def drawRotate(g: Glyph) -> None:
    g.arc(12, 12, 8, 120.0, 400.0, cap="butt")
    g.triangle(14.5, 1.5, 20, 6.5, 13.5, 9.5)
    g.circle(12, 12, 2)


def drawScale(g: Glyph) -> None:
    g.frame(3, 9, 12, 12, 1)
    g.line(11, 13, 20, 4)
    g.polyline([(14, 4), (20, 4), (20, 10)])


def drawGlobe(g: Glyph) -> None:
    g.ring(12, 12, 9)
    g.line(3, 12, 21, 12, cap="butt")
    # 경선 하나 = 세로로 눌린 타원 — 두 원호로 흉내 낸다
    g.arc(22.5, 12, 13.0, 136.0, 224.0, cap="butt")
    g.arc(1.5, 12, 13.0, -44.0, 44.0, cap="butt")


def drawAxes(g: Glyph) -> None:
    g.line(6, 18, 6, 4)
    g.line(6, 18, 20, 18)
    g.line(6, 18, 14, 12)
    g.arrowHead(6, 4, -90.0, 3.0)
    g.arrowHead(20, 18, 0.0, 3.0)
    g.circle(14.5, 11.5, 2.2)


def drawMagnet(g: Glyph) -> None:
    g.arc(12, 11, 6, 0.0, 180.0, 4.0, "butt")
    g.line(6, 7.2, 6, 11, 4.0, "butt")
    g.line(18, 7.2, 18, 11, 4.0, "butt")
    g.rect(4, 3, 4, 3, 0.4)
    g.rect(16, 3, 4, 3, 0.4)


def drawGrid(g: Glyph) -> None:
    for v in (4, 9.33, 14.67, 20):
        g.line(v, 4, v, 20, 1.5, "square")
        g.line(4, v, 20, v, 1.5, "square")


def drawBookmark(g: Glyph) -> None:
    g.poly([(6, 3), (18, 3), (18, 21), (12, 16), (6, 21)])


def drawAlign(g: Glyph) -> None:
    g.line(4, 3, 4, 21)
    g.rect(7, 5, 13, 5, 1)
    g.rect(7, 14, 8, 5, 1)


def drawChart(g: Glyph) -> None:
    g.polyline([(3, 3), (3, 21), (21, 21)], cap="butt")
    g.rect(6.5, 13, 3.5, 6, 0.5)
    g.rect(12, 8, 3.5, 11, 0.5)
    g.rect(17.5, 11, 3.5, 8, 0.5)


def drawCamera(g: Glyph) -> None:
    g.rect(2, 7, 15, 12, 2)
    g.poly([(17, 11), (22, 7.5), (22, 18.5), (17, 15)])
    g.cutCircle(9.5, 13, 3)


def drawCube(g: Glyph) -> None:
    top = [(12, 2.5), (20.5, 7), (12, 11.5), (3.5, 7)]
    g.polyline(top, 1.8, bClosed=True)
    g.line(3.5, 7, 3.5, 17, 1.8)
    g.line(20.5, 7, 20.5, 17, 1.8)
    g.line(12, 11.5, 12, 21.5, 1.8)
    g.line(3.5, 17, 12, 21.5, 1.8)
    g.line(20.5, 17, 12, 21.5, 1.8)


def drawScene(g: Glyph) -> None:
    g.rect(3, 10, 18, 11, 1.5)
    g.poly([(3, 9), (3.5, 5.5), (20, 3), (20.5, 6.5)])
    g.cut([(7.2, 5.4), (9.9, 5.0), (8.6, 7.9), (5.9, 8.3)])
    g.cut([(13.1, 4.5), (15.8, 4.1), (14.5, 7.0), (11.8, 7.4)])


def drawPrefab(g: Glyph) -> None:
    for (x, y) in ((3, 12), (13, 12), (8, 3)):
        g.rect(x, y, 8, 8, 1.5)
        g.cutRect(x + 2, y + 2, 4, 4, 0.5)


def drawTexture(g: Glyph) -> None:
    g.frame(3, 4, 18, 16, 2)
    g.circle(15.5, 9, 2)
    g.poly([(4, 19), (9.5, 11.5), (13.5, 16.5), (16, 14), (20, 19)])


def drawShader(g: Glyph) -> None:
    g.polyline([(8, 6), (3, 12), (8, 18)], 2.5)
    g.polyline([(16, 6), (21, 12), (16, 18)], 2.5)
    g.line(13.5, 4, 10.5, 20, 2.5)


def drawMaterial(g: Glyph) -> None:
    g.circle(12, 12, 9.5)
    g.cutCircle(8.5, 8.5, 2.5)


def drawAudio(g: Glyph) -> None:
    g.circle(7, 17.5, 3.5)
    g.circle(17, 15.5, 3.5)
    g.line(9.6, 17.5, 9.6, 5.5, 2.2, "butt")
    g.line(19.6, 15.5, 19.6, 3.5, 2.2, "butt")
    g.poly([(8.5, 4.5), (20.7, 2.2), (20.7, 5.4), (8.5, 7.7)])


def drawAnimGraph(g: Glyph) -> None:
    g.rect(2, 3, 7, 6, 1)
    g.rect(15, 9, 7, 6, 1)
    g.rect(2, 15, 7, 6, 1)
    g.polyline([(9, 6), (12, 6), (12, 12), (15, 12)], 1.6, "butt")
    g.polyline([(9, 18), (12, 18), (12, 12)], 1.6, "butt")


def drawDialogue(g: Glyph) -> None:
    g.rect(2, 3, 14, 10, 2)
    g.triangle(5, 12, 9, 12, 5, 16)
    g.poly([(18, 7), (20, 7), (22, 9), (22, 15), (20, 17), (19, 17), (19, 21), (15, 17), (10, 17), (8, 15), (17.5, 15), (18, 14.5)])


def drawSprite(g: Glyph) -> None:
    for (x, y) in ((3, 3), (13, 3), (3, 13), (13, 13)):
        g.frame(x, y, 8, 8, 1, 1.6)
    g.poly([(15.5, 15), (19, 17), (15.5, 19)])


def drawTileMap(g: Glyph) -> None:
    for (x, y) in ((3, 3), (10, 3), (17, 3), (3, 10), (17, 10), (3, 17), (10, 17), (17, 17)):
        g.rect(x, y, 4.5, 4.5, 0.6)
    g.frame(10.5, 10.5, 3.5, 3.5, 0, 1.2)


def drawSequence(g: Glyph) -> None:
    g.rect(2, 4, 20, 16, 1.5)
    for y in (6, 16):
        for x in (4, 8, 12, 16, 20):
            g.cutRect(x - 1, y, 2, 2)
    g.cutRect(4, 9.5, 16, 5, 0.5)


def drawSkeleton(g: Glyph) -> None:
    g.line(7, 17, 17, 7, 3.0, "butt")
    g.circle(4.5, 16.5, 2.3)
    g.circle(7.5, 19.5, 2.3)
    g.circle(16.5, 4.5, 2.3)
    g.circle(19.5, 7.5, 2.3)


def drawPerson(g: Glyph, pose: str = "stand") -> None:
    g.circle(12, 4.5, 2.5)
    if pose == "stand":
        g.line(12, 8.5, 12, 14.5, 3.0)
        g.polyline([(6.5, 11), (12, 9.5), (17.5, 11)], 2.2)
        g.line(12, 14.5, 8.5, 21, 2.5)
        g.line(12, 14.5, 15.5, 21, 2.5)
    elif pose == "walk":
        g.line(12, 8.5, 11, 14.5, 3.0)
        g.polyline([(8, 13), (9.5, 10), (12, 9)], 2.2)
        g.polyline([(12, 9), (15, 11), (16.5, 13.5)], 2.2)
        g.polyline([(11, 14.5), (14, 17.5), (14.5, 21)], 2.5)
        g.polyline([(11, 14.5), (9, 18), (6.5, 21)], 2.5)
    else:  # run
        g.line(13, 8.5, 11, 14, 3.0)
        g.polyline([(7, 12), (9, 9.5), (13, 9)], 2.2)
        g.polyline([(13, 9), (16, 12), (19.5, 11)], 2.2)
        g.polyline([(11, 14), (15.5, 16), (16, 21)], 2.5)
        g.polyline([(11, 14), (8.5, 18), (4, 18.5)], 2.5)


def drawRig(g: Glyph) -> None:
    g.polyline([(12, 5), (12, 13)], 1.6)
    g.polyline([(5, 9), (12, 7.5), (19, 9)], 1.6)
    g.polyline([(8, 21), (12, 13), (16, 21)], 1.6)
    for (x, y) in ((12, 4.5), (12, 13), (5, 9), (19, 9), (8, 21), (16, 21), (12, 7.5)):
        g.circle(x, y, 2.0)


def drawHeightfield(g: Glyph) -> None:
    g.poly([(1.5, 20), (8.5, 7), (12.5, 13.5), (15.5, 9.5), (22.5, 20)])
    g.cut([(6.9, 10.4), (8.5, 7.6), (10.1, 10.4), (9.2, 11.2), (8.5, 10.4), (7.8, 11.2)])


def drawFracture(g: Glyph) -> None:
    listPoint = []
    for index in range(16):
        r = 10.0 if index % 2 == 0 else 4.5
        if index in (2, 9):
            r = 7.5
        a = math.radians(-90.0 + index * 22.5)
        listPoint.append((12 + r * math.cos(a), 12 + r * math.sin(a)))
    g.poly(listPoint)


def drawWidget(g: Glyph) -> None:
    g.frame(3, 4, 18, 16, 2)
    g.line(3, 8.5, 21, 8.5, cap="butt")
    g.rect(6, 11, 6, 2.2, 0.6)
    g.rect(6, 15, 12, 2.2, 0.6)


def drawComponent(g: Glyph) -> None:
    g.poly([(4, 8), (9, 8), (9, 6.5), (10.5, 4.5), (13.5, 4.5), (15, 6.5), (15, 8), (20, 8), (20, 12.5), (18.5, 12.5), (16.5, 14), (16.5, 16), (18.5, 17.5), (20, 17.5), (20, 21), (4, 21)])


def drawLightPoint(g: Glyph) -> None:
    g.arc(12, 9.5, 6.5, 145.0, 395.0, 2.2, "butt")
    g.line(8.6, 14.5, 9.2, 17.5, 2.2, "butt")
    g.line(15.4, 14.5, 14.8, 17.5, 2.2, "butt")
    g.line(9, 17.5, 15, 17.5, 2.0, "round")
    g.line(10, 20.8, 14, 20.8, 2.0, "round")


def drawLightDirectional(g: Glyph) -> None:
    g.circle(12, 12, 4.5)
    for index in range(8):
        a = math.radians(index * 45.0)
        g.line(12 + 7.3 * math.cos(a), 12 + 7.3 * math.sin(a), 12 + 9.8 * math.cos(a), 12 + 9.8 * math.sin(a), 2.0)


def drawLightSpot(g: Glyph) -> None:
    g.rect(9, 2.5, 6, 4.5, 1)
    g.poly([(9.5, 7), (14.5, 7), (19, 15), (5, 15)])
    for x0, x1 in ((7, 4.5), (12, 12), (17, 19.5)):
        g.line(x0, 17.5, x1, 21, 1.8)


def drawLightGlobal(g: Glyph) -> None:
    g.arc(12, 17, 7, 180.0, 360.0, 2.2, "butt")
    g.line(2.5, 17, 21.5, 17, 2.0)
    for a in (-160.0, -125.0, -90.0, -55.0, -20.0):
        r = math.radians(a)
        g.line(12 + 9.5 * math.cos(r), 17 + 9.5 * math.sin(r), 12 + 11.2 * math.cos(r), 17 + 11.2 * math.sin(r), 2.0)
    g.line(6, 21, 18, 21, 1.6)


def drawAudioEmitter(g: Glyph) -> None:
    g.poly([(3, 9), (7, 9), (12, 4.5), (12, 19.5), (7, 15), (3, 15)])
    g.arc(13, 12, 4, -45.0, 45.0, 2.0)
    g.arc(13, 12, 8, -45.0, 45.0, 2.0)


def drawAudioListener(g: Glyph) -> None:
    g.arc(12, 13, 8, 180.0, 360.0, 2.0, "butt")
    g.rect(3, 13, 4.5, 8, 1.5)
    g.rect(16.5, 13, 4.5, 8, 1.5)


def drawAudioZone(g: Glyph) -> None:
    g.dashedFrame(2.5, 2.5, 19, 19, 3.2, 2.2, 1.6)
    g.poly([(6, 10), (8.5, 10), (11.5, 7.5), (11.5, 16.5), (8.5, 14), (6, 14)])
    g.arc(12, 12, 4, -45.0, 45.0, 1.8)


def drawPhysics(g: Glyph) -> None:
    g.circle(14, 12, 6.5)
    g.line(2, 9, 5.5, 9, 2.0)
    g.line(3, 13, 5.5, 13, 2.0)
    g.line(2, 17, 5.5, 17, 2.0)
    g.cutCircle(12, 10, 1.8)


def drawCollider(g: Glyph) -> None:
    g.dashedFrame(3, 3, 18, 18, 3.4, 2.2)
    g.rect(8, 8, 8, 8, 1)


def drawCharacter(g: Glyph) -> None:
    drawPerson(g, "stand")


def drawController(g: Glyph) -> None:
    g.poly([(7, 6), (17, 6), (20.5, 8), (22.5, 17), (20.5, 19.5), (18, 19), (15.5, 15.5), (8.5, 15.5), (6, 19), (3.5, 19.5), (1.5, 17), (3.5, 8)])
    g.cut([(7.2, 8.1), (8.8, 8.1), (8.8, 9.8), (10.5, 9.8), (10.5, 11.4), (8.8, 11.4), (8.8, 13.1), (7.2, 13.1), (7.2, 11.4), (5.5, 11.4), (5.5, 9.8), (7.2, 9.8)])
    g.cutCircle(16, 9.5, 1.2)
    g.cutCircle(18, 12.0, 1.2)


def drawAi(g: Glyph) -> None:
    g.rect(4, 7, 16, 13, 2.5)
    g.line(12, 7, 12, 3.5, 2.0, "butt")
    g.circle(12, 3, 1.8)
    g.cutCircle(8.5, 12.5, 1.8)
    g.cutCircle(15.5, 12.5, 1.8)
    g.cutRect(8.5, 16, 7, 1.5, 0.5)
    g.rect(1.5, 11, 2, 5, 0.5)
    g.rect(20.5, 11, 2, 5, 0.5)


def drawNavigation(g: Glyph) -> None:
    for (cx, cy) in ((6, 6), (18, 13)):
        g.circle(cx, cy, 3.5)
        g.triangle(cx - 3.0, cy + 1.8, cx + 3.0, cy + 1.8, cx, cy + 6.5)
        g.cutCircle(cx, cy, 1.3)
    for (x0, y0, x1, y1) in ((6, 14, 6, 15.5), (7.5, 18, 9.5, 19), (12, 20, 14, 20), (16.5, 20.5, 17.5, 21)):
        g.line(x0, y0, x1, y1, 1.8)


def drawTrigger(g: Glyph) -> None:
    g.dashedFrame(3, 3, 18, 18, 3.4, 2.2)
    g.poly([(13.5, 5.5), (8, 13), (11.5, 13), (10.5, 18.5), (16, 11), (12.5, 11)])


def drawSpawnPoint(g: Glyph) -> None:
    g.line(6, 3, 6, 21, 2.2)
    g.poly([(7, 3.5), (19, 6.5), (7, 12)])
    g.line(3, 21, 11, 21, 2.0)


def drawSpline(g: Glyph) -> None:
    listPoint = []
    for index in range(17):
        t = index / 16.0
        x = (1 - t) ** 3 * 4 + 3 * (1 - t) ** 2 * t * 5 + 3 * (1 - t) * t * t * 19 + t ** 3 * 20
        y = (1 - t) ** 3 * 19 + 3 * (1 - t) ** 2 * t * 3 + 3 * (1 - t) * t * t * 21 + t ** 3 * 5
        listPoint.append((x, y))
    g.polyline(listPoint, 2.0)
    g.rect(2, 17, 4, 4, 0.5)
    g.rect(18, 3, 4, 4, 0.5)


def drawWater(g: Glyph) -> None:
    for y in (6, 12, 18):
        listPoint = []
        for index in range(21):
            x = 3 + 18 * index / 20.0
            listPoint.append((x, y + 1.5 * math.sin((x - 3) / 18.0 * 2 * math.pi * 1.5)))
        g.polyline(listPoint, 2.0)


def drawFoliage(g: Glyph) -> None:
    g.poly([(4, 20), (5, 12), (9, 6.5), (15, 4), (20.5, 3.5), (20, 9), (17.5, 15), (12, 19), (6.5, 19.5)])
    g.cut([(6.6, 17.6), (13.6, 10.2), (14.4, 11.0), (7.4, 18.4)])


def drawWind(g: Glyph) -> None:
    g.polyline([(3, 9), (15, 9)], 2.0)
    g.arc(15, 6, 3, -180.0, 90.0, 2.0)
    g.polyline([(3, 14), (18, 14)], 2.0)
    g.arc(18, 17, 3, -90.0, 180.0, 2.0)
    g.line(3, 19, 10, 19, 2.0)


def drawHealth(g: Glyph) -> None:
    g.circle(8, 9, 4.8)
    g.circle(16, 9, 4.8)
    g.poly([(3.6, 11), (12, 20.5), (20.4, 11), (12, 9)])


def drawAbility(g: Glyph) -> None:
    for (cx, cy, r) in ((10, 13, 8.0), (18.5, 5.5, 3.6)):
        listPoint = []
        for index in range(8):
            rr = r if index % 2 == 0 else r * 0.28
            a = math.radians(-90.0 + index * 45.0)
            listPoint.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
        g.poly(listPoint)


def drawVehicle(g: Glyph) -> None:
    g.poly([(2, 16), (2, 12), (5, 11.5), (8, 7), (16, 7), (19, 11.5), (22, 12.5), (22, 16)])
    g.cut([(9, 8.6), (11.4, 8.6), (11.4, 11.4), (7.2, 11.4)])
    g.cut([(12.6, 8.6), (15.2, 8.6), (17, 11.4), (12.6, 11.4)])
    g.circle(7, 17, 2.8)
    g.circle(17, 17, 2.8)


def drawTag(g: Glyph) -> None:
    g.poly([(3, 3), (12, 3), (21, 12), (12, 21), (3, 12)])
    g.cutCircle(7.5, 7.5, 1.8)


def drawMissing(g: Glyph) -> None:
    g.dashedFrame(3, 3, 18, 18, 3.4, 2.2)
    g.arc(12, 9.5, 3.2, 180.0, 405.0, 2.3, "butt")
    g.line(14.3, 11.8, 12, 13.6, 2.3, "butt")
    g.line(12, 13.4, 12, 15, 2.3, "butt")
    g.circle(12, 17.6, 1.4)


def drawGameObject(g: Glyph) -> None:
    g.frame(4, 4, 16, 16, 2)
    g.circle(12, 12, 3)


def drawBug(g: Glyph) -> None:
    g.circle(12, 6.5, 2.6)
    g.rect(7.5, 9, 9, 12, 4.5)
    g.cutRect(11.4, 11, 1.2, 8.5, 0.5)
    for y, dx in ((11.5, 3.5), (15, 4.0), (18.5, 3.5)):
        g.line(7.5, y, 7.5 - dx, y - 1.5, 1.8)
        g.line(16.5, y, 16.5 + dx, y - 1.5, 1.8)
    g.line(10.5, 4.5, 9, 2.5, 1.6)
    g.line(13.5, 4.5, 15, 2.5, 1.6)


def drawMap(g: Glyph) -> None:
    g.poly([(2.5, 5.5), (8.5, 3.5), (8.5, 18.5), (2.5, 20.5)])
    g.poly([(9.7, 3.5), (14.3, 5.5), (14.3, 20.5), (9.7, 18.5)])
    g.poly([(15.5, 5.5), (21.5, 3.5), (21.5, 18.5), (15.5, 20.5)])


def drawFont(g: Glyph) -> None:
    g.poly([(3, 20), (8.5, 4), (11, 4), (16.5, 20), (13.8, 20), (12.4, 15.5), (7.1, 15.5), (5.7, 20)])
    g.cut([(7.9, 13), (11.6, 13), (9.75, 7.2)])
    g.circle(18.5, 16.5, 3.2)
    g.line(21.5, 13, 21.5, 20, 1.8, "butt")
    g.cutCircle(18.5, 16.5, 1.4)


# 이름은 C++ 상수 kName 이 된다. 분류 머리 주석은 헤더에도 들어간다.
kListIcon: list[tuple[str, Callable[[Glyph], None]]] = [
    # 일반 · 파일 · 편집
    ("file", drawFile), ("folder", drawFolder), ("folderOpen", drawFolderOpen), ("save", drawSave),
    ("undo", drawUndo), ("redo", drawRedo), ("search", drawSearch), ("terminal", drawTerminal), ("exit", drawExit),
    ("settings", drawSettings), ("palette", drawPalette), ("layout", drawLayout), ("table", drawTable),
    ("hammer", drawHammer), ("wrench", drawWrench), ("buildAll", drawBuildAll), ("cancel", drawCancel), ("spinner", drawSpinner),
    ("check", drawCheck), ("close", drawClose), ("success", drawSuccess), ("warning", drawWarning), ("error", drawError), ("info", drawInfo),
    ("lock", drawLock), ("unlock", drawUnlock), ("plus", drawPlus), ("minus", drawMinus), ("trash", drawTrash), ("copy", drawCopy),
    ("refresh", drawRefresh), ("filter", drawFilter), ("star", drawStar), ("link", drawLink), ("eye", drawEye), ("eyeSlash", drawEyeSlash),
    # 재생 · 뷰포트 도구
    ("play", drawPlay), ("pause", drawPause), ("stop", drawStop), ("stepForward", drawStepForward),
    ("translate", drawTranslate), ("rotate", drawRotate), ("scale", drawScale), ("globe", drawGlobe), ("axes", drawAxes),
    ("magnet", drawMagnet), ("grid", drawGrid), ("bookmark", drawBookmark), ("align", drawAlign), ("chart", drawChart), ("camera", drawCamera),
    # 에셋 종류
    ("cube", drawCube), ("scene", drawScene), ("prefab", drawPrefab), ("texture", drawTexture), ("shader", drawShader),
    ("material", drawMaterial), ("audio", drawAudio), ("animGraph", drawAnimGraph), ("dialogue", drawDialogue), ("sprite", drawSprite),
    ("tileMap", drawTileMap), ("sequence", drawSequence), ("skeleton", drawSkeleton), ("animClip", lambda g: drawPerson(g, "walk")),
    ("rig", drawRig), ("heightfield", drawHeightfield), ("fracture", drawFracture), ("widget", drawWidget), ("font", drawFont),
    # 오브젝트 · 컴포넌트 종류(Hierarchy · 인스펙터 · 뷰포트 빌보드)
    ("gameObject", drawGameObject), ("component", drawComponent), ("lightPoint", drawLightPoint), ("lightDirectional", drawLightDirectional),
    ("lightSpot", drawLightSpot), ("lightGlobal", drawLightGlobal), ("audioEmitter", drawAudioEmitter), ("audioListener", drawAudioListener),
    ("audioZone", drawAudioZone), ("physics", drawPhysics), ("collider", drawCollider), ("character", drawCharacter),
    ("animation", lambda g: drawPerson(g, "run")), ("controller", drawController), ("ai", drawAi), ("navigation", drawNavigation),
    ("trigger", drawTrigger), ("spawnPoint", drawSpawnPoint), ("spline", drawSpline), ("water", drawWater), ("foliage", drawFoliage),
    ("wind", drawWind), ("health", drawHealth), ("ability", drawAbility), ("vehicle", drawVehicle), ("tag", drawTag), ("missing", drawMissing),
    # 개발 도구
    ("bug", drawBug), ("map", drawMap),
]

#: ImGuiNotify(vcpkg `imgui-notify`)가 토스트에 박아 둔 Font Awesome 코드포인트 → 우리 글리프. 그 라이브러리 헤더를 고칠 수 없어
#: 같은 코드포인트에 이 저장소의 그림을 둔다(그림 자체는 이 저장소가 그린 것이다).
kMapNotifyCodepoint = {0xF058: "success", 0xF071: "warning", 0xF06A: "error", 0xF05A: "info", 0xF00D: "close"}


# ------------------------------------------------------------------------------
# 3) TrueType 쓰기
# ------------------------------------------------------------------------------
def toFontPoint(x: float, y: float) -> tuple[int, int]:
    # 소수 넷째 자리에서 먼저 반올림한다. 플랫폼마다 libm 의 sin · cos 마지막 비트가 달라도 같은 정수가 나와야
    # Windows 와 리눅스 CI 가 같은 바이트를 만들고 게이트가 통과한다.
    return int(math.floor(round(x * kUnit, 4) + 0.5)), int(math.floor(round(kAscent - y * kUnit, 4) + 0.5))


def signedAreaInternal(listPoint: list[FontPoint]) -> float:
    area = 0.0
    for index in range(len(listPoint)):
        x0, y0 = listPoint[index][0], listPoint[index][1]
        x1, y1 = listPoint[(index + 1) % len(listPoint)][0], listPoint[(index + 1) % len(listPoint)][1]
        area += x0 * y1 - x1 * y0
    return area * 0.5


def buildGlyphContours(glyph: Glyph) -> list[list[FontPoint]]:
    """폰트 좌표의 윤곽 목록. 채움은 시계 방향(TrueType 바깥 윤곽), 구멍은 반시계."""
    listOut = []
    for listPoint, bCut in glyph.listContour:
        listFont = []
        for x, y, bOn in listPoint:
            fx, fy = toFontPoint(x, y)
            if listFont and listFont[-1][0] == fx and listFont[-1][1] == fy and listFont[-1][2] == bOn:
                continue
            listFont.append((fx, fy, bOn))
        while len(listFont) > 1 and listFont[0][:2] == listFont[-1][:2] and listFont[-1][2]:
            listFont.pop()
        if len(listFont) < 3:
            continue
        bClockwise = signedAreaInternal(listFont) < 0.0
        if bClockwise == bCut:
            listFont.reverse()
        if listFont[0][2] == False:  # 첫 점은 곡선 위에 두면 읽기 쉽다(규격상 필수는 아니다)
            for shift in range(len(listFont)):
                if listFont[shift][2]:
                    listFont = listFont[shift:] + listFont[:shift]
                    break
        listOut.append(listFont)
    return listOut


def encodeGlyph(listContour: list[list[FontPoint]]) -> tuple[bytes, tuple[int, int, int, int], int]:
    """단순 글리프 바이트(명령 없음)."""
    if not listContour:
        return b"", (0, 0, 0, 0), 0
    listAll = [p for c in listContour for p in c]
    xMin, xMax = min(p[0] for p in listAll), max(p[0] for p in listAll)
    yMin, yMax = min(p[1] for p in listAll), max(p[1] for p in listAll)
    out = struct.pack(">hhhhh", len(listContour), xMin, yMin, xMax, yMax)
    endIndex = -1
    for c in listContour:
        endIndex += len(c)
        out += struct.pack(">H", endIndex)
    out += struct.pack(">H", 0)
    flags, xs, ys = bytearray(), bytearray(), bytearray()
    prevX = prevY = 0
    for x, y, bOn in listAll:
        flag = 0x01 if bOn else 0x00
        dx, dy = x - prevX, y - prevY
        if dx == 0:
            flag |= 0x10
        elif -255 <= dx <= 255:
            flag |= 0x02 | (0x10 if dx > 0 else 0)
            xs += struct.pack(">B", abs(dx))
        else:
            xs += struct.pack(">h", dx)
        if dy == 0:
            flag |= 0x20
        elif -255 <= dy <= 255:
            flag |= 0x04 | (0x20 if dy > 0 else 0)
            ys += struct.pack(">B", abs(dy))
        else:
            ys += struct.pack(">h", dy)
        flags.append(flag)
        prevX, prevY = x, y
    out += bytes(flags) + bytes(xs) + bytes(ys)
    return out, (xMin, yMin, xMax, yMax), len(listAll)


def tableChecksum(data: bytes) -> int:
    data = data + b"\0" * ((4 - len(data) % 4) % 4)
    return sum(struct.unpack(">%dI" % (len(data) // 4), data)) & 0xFFFFFFFF


def buildCmapInternal(mapCodepointGlyph: dict[int, int]) -> bytes:
    listCode = sorted(mapCodepointGlyph)
    listSegment = []  # (start, end, delta)
    for code in listCode:
        glyphId = mapCodepointGlyph[code]
        if listSegment and listSegment[-1][1] == code - 1 and (listSegment[-1][2] + code) & 0xFFFF == glyphId:
            listSegment[-1] = (listSegment[-1][0], code, listSegment[-1][2])
        else:
            listSegment.append((code, code, (glyphId - code) & 0xFFFF))
    listSegment.append((0xFFFF, 0xFFFF, 1))
    segCount = len(listSegment)
    searchRange = 2 * (2 ** int(math.floor(math.log2(segCount))))
    entrySelector = int(math.floor(math.log2(segCount)))
    rangeShift = 2 * segCount - searchRange
    body = struct.pack(">HHHH", segCount * 2, searchRange, entrySelector, rangeShift)
    body += b"".join(struct.pack(">H", s[1]) for s in listSegment) + struct.pack(">H", 0)
    body += b"".join(struct.pack(">H", s[0]) for s in listSegment)
    body += b"".join(struct.pack(">H", s[2]) for s in listSegment)
    body += b"".join(struct.pack(">H", 0) for s in listSegment)
    sub = struct.pack(">HHH", 4, 6 + len(body), 0) + body
    # 머리(4) + 레코드 둘(8 · 8) 뒤에 하위 테이블 하나를 두 레코드가 함께 가리킨다(유니코드 BMP · Windows 유니코드 BMP).
    return struct.pack(">HH", 0, 2) + struct.pack(">HHI", 0, 3, 20) + struct.pack(">HHI", 3, 1, 20) + sub


def buildNameInternal() -> bytes:
    listRecord = [
        (0, "Public domain (CC0 1.0). Drawn by the SWEngine project."),
        (1, kFamilyName), (2, "Regular"), (3, kPostScriptName), (4, kFamilyName + " Regular"), (5, kVersionText), (6, kPostScriptName),
        (13, "CC0 1.0 Universal - no rights reserved. You can copy, modify and distribute this font without asking permission."),
        (14, "https://creativecommons.org/publicdomain/zero/1.0/"),
    ]
    header = struct.pack(">HHH", 0, len(listRecord), 6 + 12 * len(listRecord))
    records, strings = b"", b""
    for nameId, text in listRecord:
        encoded = text.encode("utf-16-be")
        records += struct.pack(">HHHHHH", 3, 1, 0x409, nameId, len(encoded), len(strings))
        strings += encoded
    return header + records + strings


def buildFont(listIcon: list[tuple[str, Callable[[Glyph], None]]]) -> tuple[bytes, list[tuple[str, int]]]:
    listGlyphData = [encodeGlyph([])]  # .notdef — 빈 글리프
    listNamed = []
    for index, (name, draw) in enumerate(listIcon):
        glyph = Glyph()
        draw(glyph)
        listGlyphData.append(encodeGlyph(buildGlyphContours(glyph)))
        listNamed.append((name, kFirstCodepoint + index))
    mapCodepointGlyph = {code: index + 1 for index, (name, code) in enumerate(listNamed)}
    mapNameGlyph = {name: index + 1 for index, (name, code) in enumerate(listNamed)}
    for code, name in kMapNotifyCodepoint.items():
        mapCodepointGlyph[code] = mapNameGlyph[name]

    glyf, loca = b"", []
    for data, bbox, pointCount in listGlyphData:
        loca.append(len(glyf))
        glyf += data + b"\0" * ((4 - len(data) % 4) % 4)
    loca.append(len(glyf))
    numGlyphs = len(listGlyphData)
    listBox = [bbox for data, bbox, count in listGlyphData if data]
    xMin = min(b[0] for b in listBox); yMin = min(b[1] for b in listBox)
    xMax = max(b[2] for b in listBox); yMax = max(b[3] for b in listBox)
    maxPoints = max(count for data, bbox, count in listGlyphData)
    maxContours = max(struct.unpack(">h", data[:2])[0] for data, bbox, count in listGlyphData if data)

    tables = {}
    tables[b"head"] = struct.pack(">IIIIHHqqhhhhHHhhh", 0x00010000, 0x00010000, 0, 0x5F0F3CF5, 0x000B, kUnitsPerEm,
                                  0, 0, xMin, yMin, xMax, yMax, 0, 8, 2, 1, 0)
    tables[b"hhea"] = struct.pack(">IhhhHhhhhhhhhhhhH", 0x00010000, kAscent, kDescent, 0, kUnitsPerEm,
                                  min(b[0] for b in listBox), min(kUnitsPerEm - b[2] for b in listBox), xMax, 1, 0, 0, 0, 0, 0, 0, 0, numGlyphs)
    tables[b"maxp"] = struct.pack(">IHHHHHHHHHHHHHH", 0x00010000, numGlyphs, maxPoints, maxContours, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0)
    hmtx = b""
    for data, bbox, count in listGlyphData:
        hmtx += struct.pack(">Hh", kUnitsPerEm, bbox[0] if data else 0)
    tables[b"hmtx"] = hmtx
    tables[b"loca"] = b"".join(struct.pack(">I", v) for v in loca)
    tables[b"glyf"] = glyf
    tables[b"cmap"] = buildCmapInternal(mapCodepointGlyph)
    tables[b"name"] = buildNameInternal()
    tables[b"post"] = struct.pack(">IIhhIIIII", 0x00030000, 0, -100, 50, 1, 0, 0, 0, 0)
    listCode = sorted(mapCodepointGlyph)
    tables[b"OS/2"] = struct.pack(">HhHHHhhhhhhhhhhh10sIIII4sHHHhhhHHIIhhHHH",
                                  4, kUnitsPerEm, 400, 5, 0,            # version · xAvgCharWidth · weight · width · fsType(설치 가능)
                                  650, 700, 0, 140, 650, 700, 0, 480, 50, 250, 0,  # 위 · 아래 첨자 · 취소선
                                  b"\0" * 10, 0, 0x10000000, 0, 0,       # panose · unicodeRange(비트 60 = 사용자 영역 — 둘째 낱말의 비트 28)
                                  b"SWEN", 0x0040, listCode[0], min(listCode[-1], 0xFFFF),
                                  kAscent, kDescent, 0, kAscent, -kDescent, 1, 0, 0, 0, 0, 0, 0)

    listTag = sorted(tables)
    numTables = len(listTag)
    entrySelector = int(math.floor(math.log2(numTables)))
    searchRange = (2 ** entrySelector) * 16
    offset = 12 + 16 * numTables
    directory = struct.pack(">IHHHH", 0x00010000, numTables, searchRange, entrySelector, numTables * 16 - searchRange)
    body = b""
    for tag in listTag:
        data = tables[tag]
        directory += struct.pack(">4sIII", tag, tableChecksum(data), offset + len(body), len(data))
        body += data + b"\0" * ((4 - len(data) % 4) % 4)
    font = bytearray(directory + body)
    adjustment = (0xB1B0AFBA - tableChecksum(bytes(font))) & 0xFFFFFFFF
    headOffset = struct.unpack(">I", directory[12 + 16 * listTag.index(b"head") + 8: 12 + 16 * listTag.index(b"head") + 12])[0]
    font[headOffset + 8: headOffset + 12] = struct.pack(">I", adjustment)
    return bytes(font), listNamed


# ------------------------------------------------------------------------------
# 4) C++ 헤더
# ------------------------------------------------------------------------------
def utf8Literal(code: int) -> str:
    return "".join("\\x%02x" % b for b in chr(code).encode("utf-8"))


def buildHeader(listNamed: list[tuple[str, int]]) -> str:
    lines = [
        "/**",
        " * @file EditorIconGlyphs.h",
        " * @brief 에디터 아이콘 폰트(`editor/fonts/sweditoricons.ttf`)의 글리프 문자열입니다. **생성 파일 — 고치지 마십시오.**",
        " * @details `Scripts/generate/GenerateEditorIcons.py` 가 같은 아이콘 목록에서 폰트와 이 헤더를 함께 만듭니다. 아이콘을 추가하려면",
        " *          `Scripts/common/EditorIconFont.py` 의 `kListIcon` 에 항목을 넣고 스크립트를 다시 실행합니다. 코드에서는 상수 이름으로만 쓰고",
        " *          바이트를 직접 쓰지 않습니다. 목록 순서가 코드포인트이기 때문입니다.",
        " */",
        "#pragma once",
        '#include "Core/Common/Types.h"',
        "",
        "namespace sw::editor::editoricon",
        "{",
    ]
    import pathlib
    import sys
    lintDir = str(pathlib.Path(__file__).resolve().parents[1] / "lint")   # Scripts/lint — 약어 등록부 한 자리
    if lintDir not in sys.path:
        sys.path.insert(0, lintDir)
    import AcronymRegistry as acronymRegistry
    width = max(len(name) for name, code in listNamed) + 1
    for name, code in listNamed:
        # 약어는 등록부의 강제 약어만 대문자로(`ai` → `kAI`) — 코드모드가 헤더에 쓰는 철자와 같아야 다시 만들어도 그대로다.
        constant = acronymRegistry.respellName("k" + name[0].upper() + name[1:], tuple(sorted(acronymRegistry.kEnforced)))
        lines.append('    inline constexpr const utf8* %s = "%s"; ///< U+%04X' % (constant.ljust(width), utf8Literal(code), code))
    first, last = listNamed[0][1], listNamed[-1][1]
    listRange = [(first, last)] + [(code, code) for code in sorted(kMapNotifyCodepoint)]
    rangeText = ", ".join("0x%04X, 0x%04X" % r for r in listRange)
    lines += [
        "",
        "    /** @brief 폰트에 있는 코드포인트 구간입니다. ImGui 글리프 범위 형식이고 0 으로 끝납니다. 뒤의 다섯 구간은 ImGuiNotify 가 쓰는 코드포인트입니다. */",
        "    inline constexpr uint16 kArrGlyphRange[] = { %s, 0 };" % rangeText,
        "    /** @brief 폰트에 있는 아이콘 수입니다. */",
        "    inline constexpr uint32 kIconCount = %d;" % len(listNamed),
        "} // namespace sw::editor::editoricon",
        "",
    ]
    return "\r\n".join(lines)


def buildEditorIconFiles() -> dict[str, bytes]:
    """저장소 기준 경로 → 파일 바이트. 폰트와 헤더 두 개입니다."""
    fontBytes, listNamed = buildFont(kListIcon)
    return {kFontRelativePath: fontBytes, kHeaderRelativePath: buildHeader(listNamed).encode("utf-8")}
