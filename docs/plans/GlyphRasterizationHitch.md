# 새 글리프 래스터화 히치

> 상태: 설계 · 선행: 없음 · 영역: ui · 옛번호: C-9 · 진행: 0/3

2026-10-17 Release 측정(PC SW, AMD Family 25, `-lang=ko`)에서 App `-gv_benchMeshes=100 -gv_benchUiHangulGlyphs=400` 탐침 프레임 `GT.Frame` max 784 ms(`GT.UI.Paint` 766 ms, `Text.GlyphsRasterized` 401), `RT.Canvas.Upload` max 4.6 ms 였다.
`GlyphBenchTest`(`Test/EngineTest/Text/TestGlyphBench.cpp`) 새 글리프 400 개 CPU p50 0.93 s(글리프당 약 2.3 ms, FreeType `sdf` 렌더러 — 외곽선에서 정확한 거리)다. 기준 4 ms 를 크게 넘는다.
SDF 만 컴퓨트(점프 플러딩)로 옮겨도 커버리지 래스터화(FreeType 보통 렌더)는 CPU 에 남아 400 개가 한 프레임이면 여전히 기준을 넘는다 — 예산과 글리프당 비용 둘 다 필요하다.
함정: 영어 문화권 대체 글꼴 사슬에는 한글 글꼴이 없어 `-lang=ko` 로 재야 한다(아니면 두부만 그려진다).

## 1. 단위

| 단위 | 내용 | 선행 |
|---|---|---|
| G1 | `bsdf` 의 글리프당 비용 · 화질을 `GlyphBenchTest` 로 재 컴퓨트(JFA)가 필요한지 정한다 | 없음 |
| G2 | 프레임 예산: 새 글리프는 예산 안에서만 만들고 나머지는 다음 프레임에 만든다 | 없음 |
| G3 | (G1 이 필요하다고 하면) SDF 변환을 컴퓨트로 | G1 |

### G1 글리프당 비용 측정

`FreeTypeRasterizer::rasterizeSdf`(`FT_RENDER_MODE_SDF`: 외곽선에서 정확한 거리)를 `bsdf`(보통 렌더한 비트맵에서 거리 변환)로 바꿔 400 글리프의 CPU p50 과 화질(작은 글자 · 큰 글자의 모서리)을 견준다. 4 ms/프레임 안에 글리프 하나가 들면 G3 은 취소한다.

### G2 프레임 예산

`GlyphCache::findOrAddGlyph` 는 호출 즉시 래스터화한다(`CanvasPainter::drawGlyph` 가 유일한 호출처). 설계에서 정할 것:
- 예산은 프레임당 래스터화 개수가 아니라 시간(ms)으로 둔다. 값은 데이터가 정본이다(`GlyphCache` 에 상수로 두지 않는다).
- 예산이 다 찬 글리프는 "아직 없음" 을 구별되게 돌려준다(`nullptr` 는 래스터화 실패와 섞이므로 안 된다). 배치(전진)는 글리프 비트맵 없이도 정해져야 줄바꿈이 흔들리지 않는다 — 전진은 `FT_Load_Glyph` 만으로 얻고 렌더는 늦춘다.
- 그림 캐시(패널 하위 출력 캐시)가 빈 글리프 상태로 굳으면 안 된다. 못 그린 글리프가 있으면 그 위젯이 다음 프레임에 다시 칠해지도록 무효화를 요청한다.

### G3 컴퓨트 SDF

G1 결과가 컴퓨트를 요구할 때만. 렌더러 영역(컴퓨트 패스 · 셰이더)이라 `ModuleRenderPass` 의 방식을 따른다.

## 2. 확인

`App -gv_benchMeshes=100 -gv_benchUiHangulGlyphs=400 -lang=ko` Release 탐침 프레임의 `GT.Frame` max 를 견준다(옛 784 ms). 글리프가 몇 프레임에 걸쳐 나타나는 것은 허용한다.
