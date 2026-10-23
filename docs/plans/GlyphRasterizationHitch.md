# 새 글리프 래스터화 히치

> 상태: 측정대기 · 선행: 없음 · 영역: ui · 옛번호: C-9 · 진행: 3/3

2026-10-17 Release 측정(PC SW, AMD Family 25, `-lang=ko`)에서 App `-gv_benchMeshes=100 -gv_benchUiHangulGlyphs=400` 탐침 프레임 `GT.Frame` max 784 ms(`GT.UI.Paint` 766 ms, `Text.GlyphsRasterized` 401), `RT.Canvas.Upload` max 4.6 ms 였다.
`GlyphBenchTest`(`Test/EngineTest/Text/TestGlyphBench.cpp`) 새 글리프 400 개 CPU p50 0.93 s(글리프당 약 2.3 ms, FreeType `sdf` 렌더러 — 외곽선에서 정확한 거리)다.
함정: 영어 문화권 대체 글꼴 사슬에는 한글 글꼴이 없어 `-lang=ko` 로 재야 한다(아니면 두부만 그려진다).

## 1. 들어간 것 (계약은 코드 헤더 주석이 정본)

- **G2 프레임 예산**(`GlyphCache::findOrAddGlyph`). 프레임(`frameIndex`)마다 새 글리프에 쓴 시간을 더하고, 예산(`setRasterBudgetNanoseconds`, 0 이면 제한 없음)이 차면 `GlyphLookupStatus::Deferred` 를 돌려준다.
  결과는 `GlyphLookup`(`_pGlyph` + `_status`)이라 래스터화 실패(`Failed`)와 섞이지 않고, 상태를 읽지 않으면 글리프 포인터를 쓸 수도 없다. 프레임의 첫 새 글리프는 늘 만들어 프레임마다 하나는 나아간다.
  값의 정본은 데이터다 — `gv_uiGlyphRasterBudgetMicros`(기본 4000, `UISystem` 이 프레임마다 캐시에 넣는다). 배치(전진)는 글리프 비트맵 없이 `findGlyphMetrics` 로 정해져 있어 줄바꿈이 흔들리지 않는다.
- **못 그린 글리프는 그림 캐시에 굳지 않는다.** `CanvasPainter::drawGlyph` 가 `Deferred` 를 `getDeferredGlyphCount` 로 세고, `UIPaintPass::paintWidget` 이 위젯의 두 캐시를 칠한 앞뒤 값을 견줘 미룬 위젯을 `UIPaintScratch::_listGlyphRetryWidget` 에 적는다.
  `UIPaintPass::paint` 가 순회 뒤 그 위젯을 `kPaint` 로 다시 무효화한다(트리의 "바뀜" 표시가 서므로 다음 프레임이 출력 재사용으로 건너뛰지 않는다). 글리프가 다 나타나면 무효화가 멈춘다.
- **G1 · G3 — 컴퓨트 SDF 는 하지 않는다.** G3 의 취소 조건("4 ms/프레임 안에 글리프 하나가 든다")이 위 측정(글리프당 2.3 ms)으로 이미 맞는다. 프레임당 비용은 예산이 묶으므로(예산 + 글리프 하나) 히치는 컴퓨트 없이 사라진다.
  `bsdf` 로 바꾸는 비교는 한 프레임에 나오는 글리프 수(처리량)만 바꾸고 히치는 바꾸지 않아 따로 두지 않았다. 처리량이 모자라 보이면 예산 값을 올리는 것이 먼저다.
- 테스트: `GlyphAtlasTest.GlyphCacheDefersNewGlyphsOverTheFrameBudget` · `CanvasDrawListTest.DeferredGlyphIsCountedNotDrawn` · `UIPaintTest.DeferredGlyphsRepaintTheTextWidgetNextFrame`(빌드 미확인).

## 2. 확인 (남음)

`App -gv_benchMeshes=100 -gv_benchUiHangulGlyphs=400 -lang=ko` Release 탐침 프레임의 `GT.Frame` max 를 견준다(옛 784 ms). 글리프가 몇 프레임에 걸쳐 나타나는 것은 허용한다.
한글 400 개는 프레임당 두 개 안팎이라 200 프레임쯤 걸린다 — 길면 `-gv_uiGlyphRasterBudgetMicros` 를 올려 `GT.Frame` max 와 나타나는 속도의 균형을 정한다(값은 측정으로 확정).
`-gv_uiGlyphRasterBudgetMicros=0`(제한 없음)으로 옛 동작과 같은지도 본다. 자동화 시나리오의 스크린샷은 한글 같은 무거운 글리프가 몇 프레임에 걸쳐 나타나므로 글이 모두 나온 뒤에 찍히는지 본다.
