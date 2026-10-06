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
  한글 · CJK · 아랍 문자는 OS 시스템 글꼴로 대체합니다(아래 대체 사슬).

## 글꼴 서비스(`FontSystem`) · 카탈로그 · 대체 사슬

| 자리 | 이 엔진 | 언리얼 | 유니티 | Godot |
|---|---|---|---|---|
| 가족 · 대체 | `FontSystem` · `FontFaceChain` · `engine/fonts/fontcatalog.xml` | `FCompositeFont`(서브 서체 · 문자 범위) | Font Asset 폴백 목록 | `Font.fallbacks` |

- **카탈로그**(`FontCatalogDesc`, 경로는 `EngineDefaultAssets::_fontCatalog`): 저장소 가족(이름 → 면 파일 · 굵기 · 기울기)과 시스템 가족(이름 → Windows · 리눅스 파일 이름).
  모르는 키 · 겹친 이름 · 면 없는 가족 · 저장소에 없는 기본 가족은 읽기 오류다.
- **사슬** = 고른 가족의 면 → 지금 문화권의 대체 가족(`LocalizationManager::getFontFallback` — 문화권 표 `fonts`) → 카탈로그 기본 가족. 코드 포인트마다 앞에서부터
  글리프가 있는 면을 쓰고(`findFaceForCodepoint`), 어디에도 없으면 첫 면의 글리프 0(두부) + 코드 포인트마다 경고 한 번. 사슬은 (가족 · 굵기 · 기울기 · 문화권)마다
  캐시하고, 문화권이 바뀌면 `invalidateFaceChains`. 사슬은 값으로 주고받는다(캐시 표가 자라면 원소 자리가 옮겨진다).
- **굵기 · 기울기 고르기**: 같은 기울기 먼저, 그 안에서 굵기 차이가 가장 작은 면(같으면 무거운 쪽 — CSS 짝짓기의 단순판). SemiBold 이상을 원하는데 고른 면이
  Medium 이하면 `_bFauxBold`(SDF 문턱 이동), 기운 면이 없으면 `_bFauxItalic`(배치가 기울인다).
- **시스템 글꼴이 없는 기계**(CI 리눅스 · 전용 서버): 저장소 기본 가족은 늘 열리고, 못 찾은 시스템 가족은 처음 한 번 경고하고 사슬에서 빠진다.
  시스템 글꼴 폴더는 `SystemFontLocator`(에디터 ImGui 글꼴도 같이 쓴다, 리눅스는 하위 폴더까지 한 번 훑어 캐시).
- **기동**: 서비스 `engine::getFontSystem()`, 기동 단계 `Fonts`(Client 대상 — 전용 서버는 열지 않는다, 쿠킹(Headless)이면 열지 않는다). 기본 가족을 못 열면 기동 실패다.
  시험 하네스는 이 단계를 세우지 않는다 — 글꼴 시험은 자기 `FontSystem`(가짜 래스터라이저 `Test/EngineTest/Text/FakeFontRasterizer.h`)을 만든다.
  게임 · 시험은 파일 대신 메모리 글꼴을 `registerMemoryFontFile` 로 같은 경로에 등록할 수 있다.
