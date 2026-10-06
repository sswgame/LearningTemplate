# Engine/Text — 런타임 글자

게임 UI · 월드 글자가 쓰는 글자 계층입니다. 티어 5(글꼴 파일을 Resource 에서 읽고, GPU 를 모른다 — 아틀라스 업로드는 렌더러의 몫).
상용 엔진과 같은 순서로 나뉩니다: **래스터화(외부) → 아틀라스(엔진) → 셰이핑(단순 · 나중에 HarfBuzz) → 줄 바꿈 · 배치(엔진)**.

| 자리 | 이 엔진 | 언리얼 | 유니티 | Godot |
|---|---|---|---|---|
| 래스터화 | `IFontRasterizer` → `FreeType/FreeTypeRasterizer.cpp` | `FFreeTypeFace` | TextCore `FontEngine` | `FontFile`(FreeType) |

## 래스터라이저(`IFontRasterizer`)

- 면을 **글꼴 파일 바이트**로 엽니다(`loadFace` — 바이트는 면이 사는 동안 래스터라이저가 쥔다). 면 번호는 1 부터, 0 은 `kInvalidFontFaceId`.
- 길이는 둘입니다. 메트릭(`FontFaceMetrics` · `GlyphMetrics` · 커닝)은 **em 비율**, 래스터 결과(`SdfGlyphBitmap`)는 **래스터 픽셀**(`SdfRasterParams::_pixelSize`).
  섞지 않습니다 — 화면 크기는 em 비율 × 글꼴 크기입니다.
- SDF 는 **단일 채널**(FreeType 2.11+ 의 `sdf` 렌더러 — 윤곽에서 정확한 거리)입니다. 128 = 윤곽, 큰 값 = 안쪽, 거리 폭은 `_spreadPx`(기본 6 px) —
  굵게 · 외곽선 · 그림자는 셰이더가 문턱을 옮겨 그립니다. MSDF 는 하지 않습니다(백로그).
- FreeType 헤더는 `Text/FreeType/` 에서만 include 하고, 링크는 `Source/Engine/CMakeLists.txt` 만 합니다(`CheckThirdPartyIsolation.py`). 클래스는 .cpp 안에
  있고 밖은 `IFontRasterizer::createDefault()` 로만 봅니다. 시험은 결정적인 가짜 구현을 씁니다.
- 스레드: 게임 스레드에서만 부릅니다(FreeType 라이브러리 객체는 스레드 안전하지 않다).

## 글꼴 리소스

- 저장소에는 **CC0 라틴 글꼴만** 둡니다(`Resource/engine/fonts/` — Kenney Fonts, 출처는 `Resource/engine/credits.md`).
  한글 · CJK · 아랍 문자는 OS 시스템 글꼴로 대체합니다.
