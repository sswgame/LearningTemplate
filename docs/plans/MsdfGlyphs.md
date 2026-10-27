# MSDF 글리프

> 상태: 구현중 · 선행: 없음 · 영역: ui · 옛번호: G-10 · 진행: 4/5

**남은 확인(통합에서 확인):** 매니페스트 id `app-ui-msdf-glyphs`, 매니페스트 id `glyph-bench-msdf`(`Test/QA/VerifyBatch.json`). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

단일 채널 SDF(FreeType `sdf` 렌더러, 결정 5-7)는 큰 글자의 모서리가 날카롭지 않다. MSDF(윤곽 모서리 칠하기 · 채널별 거리)를 직접 구현해 카탈로그가 고르게 했다. 기본은 SDF 그대로다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| M1 | 생성기 — `Engine/Text/Msdf/MsdfGenerator`(간선 칠하기 `colorEdges`: 모서리 검출 · 모서리 하나는 삼등분 · 여럿은 색 전환, 텍셀마다 채널별 가장 가까운 간선의 의사 거리 `generate`, 알파에 참 SDF, 보간 거짓 윤곽을 접는 오차 정정). 테스트 `MsdfGeneratorTest` | 끝남(빌드 · 테스트 확인) |
| M2 | 래스터라이저 — `IFontRasterizer::rasterizeMsdf`(FreeType 윤곽 → `MsdfShape`, TrueType 시계 방향 윤곽은 뒤집어 안쪽이 왼쪽이 되게), 크기 · 베어링 · 전진은 `rasterizeSdf` 와 같은 규약. 테스트 `FontRasterizerTest.MsdfIsBrightInsideTheGlyphLikeSdf` | 끝남(빌드 · 테스트 확인) |
| M3 | 아틀라스 · 캐시 · 카탈로그 · 캔버스 — `GlyphFieldKind`(`FontCatalogDesc::_glyphField`, 기본 Sdf), `GlyphAtlas` 텍셀당 바이트(MSDF RGBA8), `GlyphCache` 가 종류로 래스터화 길을 고름, `CanvasQuadFlag::kMultiChannelGlyph`, `CanvasRenderer` 페이지 텍스처 형식(R8 / RGBA8) | 끝남(빌드 · 테스트 확인, 그리기 확인은 M5) |
| M4 | 셰이더 — `canvas.hlsl` 이 플래그를 보고 RGB 중앙값을 거리값으로 쓴다(`kCanvasFlagMultiChannelGlyph`, 병합 완료 — 따로 적용할 패치 없음) | 끝남(셰이더 반영, 그리기 확인은 M5) |
| M5 | 실기 확인 — 측정 스위치 `-gv_fontMsdfGlyphs=1`(카탈로그 `_glyphField` 를 덮음)로 견본 화면을 MSDF 로 그려 SDF 와 견주는 `AppUITest.MsdfGlyphsMatchSdfDemoScreen`(알려진 영역 · 평균 휘도 · 가장자리 수)과, 글리프당 CPU 비용 `GlyphBenchTest.HangulFirstFrameRasterizeMsdf`(SDF 의 `HangulFirstFrameRasterize` p50 2.3 ms 와 견줌). 테스트 · 스위치는 만들어 둠 | 남음(통합이 두 테스트를 돌려 값을 적고, 큰 글자 모서리가 날카로운지는 `uidemo_msdf_<백엔드>.ppm` 을 `uidemo_<백엔드>.ppm` 과 눈으로 견준다) |

## 2. 정한 것

- **MSDF 는 카탈로그의 한 칸으로 고른다.** `FontCatalogDesc::_glyphField`(Sdf · Msdf). 아틀라스 페이지의 형식이 모두 같아야 사각형 하나가 어느 페이지에 있든 같은 셰이더 경로로 그려지므로 글꼴마다가 아니라 서비스 하나에 한 종류다. 코드는 형식만 가진다.
- **RGBA8 한 장에 MSDF(RGB) + 일반 SDF(A)를 둔다**(MTSDF). 3 채널 텍스처 형식이 없어서이기도 하고, 알파의 참 SDF 는 외곽선 · 굵게 같은 효과가 모서리 보정 없이 쓸 수 있다. 페이지 메모리는 SDF 의 네 배다.
- 알려진 한계: 겹치는 윤곽(가변 폰트 글리프)은 합치지 않아 겹친 자리에 거짓 선이 생길 수 있다. 오차 정정은 단순 판정(보간한 중앙값이 두 끝의 안팎과 어긋나는 텍셀 쌍을 일반 SDF 로 접기)이라 msdfgen 의 모서리 보호보다 거칠다.
- 이 설정을 `Msdf` 로 바꾸기 전에 M4 셰이더가 쿠킹돼야 한다(셰이더가 없으면 R 채널만 읽어 글리프가 틀리게 그려진다). M5 의 `AppUITest` 가 그 틀림을 평균 휘도 · 가장자리 수로 잡는다.
- 글자가 빠지던 결함: 기본 예산(`gv_uiGlyphRasterBudgetMicros` 4000)이 프로파일러 꺼진 설정에서 한 번 차면 다시 안 열려 SDF · MSDF 모두 글리프가 빠졌다. 프레임 시계를 `GlyphCache::beginFrame` 으로 옮겨 고쳤다([결정 기록 5-7](../decisions/5-7_RuntimeUI.md) 1절). `uidemo` 시나리오는 `UI.DeferredGlyphs` 가 0 이 된 뒤에 찍으므로 M5 의 픽셀 비교는 완성된 화면을 본다.
