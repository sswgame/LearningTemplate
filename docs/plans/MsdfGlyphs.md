# MSDF 글리프

> 상태: 구현중 · 선행: 없음 · 영역: ui · 진행: 4/5

단일 채널 SDF(거리장 글리프)는 큰 글자의 모서리가 뭉툭하다. MSDF(윤곽 모서리를 칠해 채널별로 거리를 갖는 거리장)를 직접 구현해 카탈로그에서 고르게 했다. 기본은 SDF 그대로다.

## 진행

| 단위 | 내용 | 상태 |
|---|---|---|
| M1 | 생성기 `MsdfGenerator` — 간선 칠하기 · 채널별 의사 거리 · 알파에 참 SDF · 보간 오차 정정 | 끝(빌드 · 테스트 확인) |
| M2 | 래스터라이저 `IFontRasterizer::rasterizeMsdf` — FreeType 윤곽 → 도형, 크기 · 베어링 · 전진은 SDF 와 같은 규약 | 끝(빌드 · 테스트 확인) |
| M3 | 아틀라스 · 캐시 · 카탈로그 · 캔버스 — `FontCatalogDesc::_glyphField`(기본 Sdf), 페이지 형식 R8 / RGBA8, 플래그 `kMultiChannelGlyph` | 끝(그리기 확인은 M5) |
| M4 | 셰이더 `canvas.hlsl` — 플래그가 서면 RGB 중앙값을 거리로 쓴다 | 끝(그리기 확인은 M5) |
| M5 | 실기 확인 — `-gv_fontMsdfGlyphs=1` 로 견본 화면을 SDF 와 견주는 `AppUITest.MsdfGlyphsMatchSdfDemoScreen`, 글리프당 CPU 비용 `GlyphBenchTest.HangulFirstFrameRasterizeMsdf`(SDF 기준 p50 2.3 ms) | 남음(테스트 · 스위치는 만들어 둠) |

첫 일: 두 테스트를 돌려 값을 적고, 큰 글자 모서리가 날카로운지 `uidemo_msdf_<백엔드>.ppm` 을 `uidemo_<백엔드>.ppm` 과 눈으로 견준다.

## 설계 결정

- **카탈로그 한 칸으로 고른다(`_glyphField`: Sdf · Msdf).** 아틀라스 페이지 형식이 모두 같아야 사각형이 어느 페이지에 있든 같은 셰이더 경로로 그려지므로, 글꼴마다가 아니라 서비스 하나에 한 종류다.
- **RGBA8 한 장에 MSDF(RGB) + 일반 SDF(A) 를 둔다(MTSDF).** 3 채널 형식이 없고, 알파의 참 SDF 는 외곽선 · 굵게 효과가 모서리 보정 없이 쓸 수 있다. 페이지 메모리는 SDF 의 네 배다.
- **한계:** 겹치는 윤곽(가변 폰트)은 합치지 않아 겹친 자리에 거짓 선이 생길 수 있고, 오차 정정은 msdfgen 의 모서리 보호보다 거칠다.
- **`Msdf` 로 바꾸기 전에 M4 셰이더가 쿠킹돼야 한다.** 없으면 R 채널만 읽어 글리프가 틀리게 그려진다(M5 테스트가 평균 휘도 · 가장자리 수로 잡는다).
- **글자가 빠지던 결함을 고쳤다.** 글리프 래스터 예산이 한 번 차면 다시 안 열렸고, 프레임 시계를 `GlyphCache::beginFrame` 으로 옮겨 해결했다.
