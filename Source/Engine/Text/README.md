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

## 글리프 아틀라스(`GlyphAtlas`) · 글리프 캐시(`GlyphCache`)

- 아틀라스는 **CPU 바이트 페이지**(R8, 1024², 최대 8 장)입니다. GPU 를 모릅니다 — 렌더러가 프레임마다 `takeUploads` 로 쓴 구간의 **바이트 사본**을 받아
  자기 텍스처에 올립니다(렌더 스레드는 사본만 본다). 새 페이지 · 비운 페이지는 전체 한 건(`_bWholePage`), 그 밖은 쓴 글리프 사각형마다 한 건.
- 패킹은 스카이라인(bottom-left — 윗변이 가장 낮은 자리, 같으면 좁은 마디)이고 결정적입니다(같은 순서 = 같은 자리). 글리프 사이 1 텍셀 여백.
- 가득 차면 **이번 프레임에 안 쓴 가장 오래된 페이지 하나**를 비웁니다(언리얼 폰트 캐시 플러시의 페이지 단위판). 세대(`getGeneration`)가 오르고,
  `GlyphCache` 는 그 페이지의 글리프를 표에서 지워 다음 조회에서 다시 래스터화합니다. 한 프레임이 페이지를 다 쓰면 그 글자는 그 프레임에 안 보이고 경고합니다.
- `GlyphCache` 는 `FontSystem` 이 시작에서 만듭니다(`getGlyphCache()`). 래스터 크기는 `SdfRasterParams`(48 px/em, spread 6 px) 하나 — 화면 크기는 SDF 가 늘린다.
  돌려준 `CachedGlyph*` 는 다음 `findOrAddGlyph` 까지만 유효합니다(밀집 해시 표). 프로파일 카운터 `Text.GlyphsRasterized`.

## 셰이핑(`ITextShaper`) · 런 나누기(`TextItemizer`)

| 자리 | 이 엔진 | 언리얼 | 유니티 | Godot |
|---|---|---|---|---|
| 셰이핑 | `ITextShaper` → `SimpleTextShaper`(나중에 HarfBuzz) | `FShapedGlyphSequence`(HarfBuzz) | TextCore(합자 일부) | `TextServer`(HarfBuzz) |

- **런 나누기**: 글을 (면 · 방향)이 같은 런으로 끊는다. 면은 사슬에서 코드 포인트마다, 방향은 강한 RTL 문자(히브리 · 아랍 범위)에서 바뀐다.
  중립 문자(공백 · 숫자 · 구두점)는 앞 런을 잇는다 — 그 면에 글리프가 있으면 면도 그대로(한글 문장의 공백이 라틴 면으로 튀지 않게).
  방향은 기록만 하고, 눈에 보이는 순서로 뒤집는 것은 배치의 일이다.
- **셰이퍼 출력**은 논리 순서의 글리프 열(em 비율 전진 · 오프셋 · 글리프 번호 · 클러스터 = 원문 바이트 위치 · 코드 포인트). 커닝 쌍은 눈에 보이는
  (왼쪽, 오른쪽)이라 LTR 런은 앞 글리프 전진에, RTL 런은 (지금, 앞) 쌍을 지금 글리프 전진에 더한다(배치가 뒤집으면 지금 글리프가 왼쪽이다).
- **단순 셰이퍼의 한계(HarfBuzz 가 필요한 경계)**: 합자 · GPOS 위치 표 · 아랍어 연결형 · 인도계 문자 재배열이 없다. 결합 분음(U+0300..U+036F) ·
  폭 없는 제어 문자(ZWJ · ZWNJ · 방향 제어 · 변이 선택자)는 글리프를 내지 않고 버린다 — 앞 글자에 겹쳐 그리지 않는다. 라틴 · 한글 완성형 · 한자 · 가나는 맞다.

## 배치(`TextLayoutEngine`) — 측정과 배치

| 자리 | 이 엔진 | 언리얼 | 유니티 | Godot |
|---|---|---|---|---|
| 배치 | `TextLayoutEngine::layout` · `measure` | `FTextLayout` · `FSlateFontMeasure` | TextCore 생성기 | `TextParagraph` |

- 위젯은 두 질문만 한다: **측정**(`measure` — 크기만, (글 · 스타일 · 너비 · 지금 면 사슬)로 캐시)과 **배치**(`layout` — 글리프 · 줄).
  길이는 UI 단위(글꼴 크기 × em), 원점은 글 상자 왼쪽 위 · y 아래가 +, 글리프 원점은 기준선 위의 펜 자리다. 같은 입력은 같은 결과다.
- **줄 바꿈 기회**(UAX #14 단순판): 공백 · 하이픈 뒤, 한자 · 가나 · 한글 경계(`TextWordBreak::Normal` 일 때만 — 기본 `KeepAll` 은 한국어 조판처럼
  공백에서만), `\n` 은 반드시. 닫는 부호(`) ] } , . ! ? : ;` · `、 。 」 』` …) 앞과 여는 부호 뒤는 끊지 않는다.
- **줄 채우기**는 욕심쟁이다 — 넘치면 마지막 기회에서 끊고, 기회가 없으면(한 낱말이 줄보다 길다) 그 자리에서 강제로 끊되 닫는 부호를 줄 머리로
  보내지 않게 한 글자 물린다. 끝 공백은 줄 너비에서 빼고 글리프는 남긴다(커서 · 선택). 줄 바꿈 문자는 그리지 않는다.
- **줄 높이** = (상승 − 하강 + 줄 간격) × 크기 × `_lineHeight`, 그 줄에 쓰인 면(+ 사슬 첫 면) 중 가장 큰 메트릭 — 대체 면이 섞인 줄이 겹치지 않게.
  남는 높이는 위아래로 나눈다.
- **넘침**: `_maxLines` 를 넘거나 줄 바꿈을 끈 줄이 너비를 넘으면 `_bTruncated`, `Ellipsis` 면 마지막 줄 끝에 "…"(사슬에 없으면 "...")가 들어갈 만큼 글리프를 뺀다.
- **정렬**은 상자 너비(`maxWidth`, 무한이면 가장 넓은 줄) 기준이다. `Start` · `End` 는 문단 방향(`TextLayoutStyle::_paragraphDirection`)을 따른다 —
  RTL 이면 Start 가 오른쪽. `Left` · `Right` 는 방향과 상관없다.
- 줄 안 글리프(`_listGlyph`)는 **눈에 보이는 순서**(왼쪽 → 오른쪽)다. 커서 · 선택은 클러스터(원문 바이트)로 논리 자리를 찾는다.

## 양방향(`TextBidi`) — UAX #9 단순판

| 자리 | 이 엔진 | 언리얼 | 유니티 | Godot |
|---|---|---|---|---|
| 양방향 | `TextBidi` · `TextLayoutStyle::_paragraphDirection` | `TextBiDi`(ICU) · `ETextFlowDirection` | TextCore(부분) · `direction: rtl` | TextServer(ICU) · `layout_direction` |

- **문단 방향**은 위젯이 정한다(`Widget::isRightToLeft` — 루트는 문화권 `LocalizationManager::isRightToLeft`). 글은 그 아래에서 코드 포인트마다 수준을 받는다:
  강한 L · R(히브리 · 아랍 범위), 숫자(앞 강한 문자가 L 이면 L — W7, 아니면 수준 2 로 RTL 안에서도 왼쪽에서 오른쪽), 중립(공백 · 구두점 — 양쪽 강한 방향이
  같으면 그 방향, 다르면 문단 방향, 숫자는 R 로 본다 — N1 · N2). 수준은 LTR 문단 L 0 · R 1 · 숫자 2, RTL 문단 R 1 · L 2 · 숫자 2.
- **줄을 나눈 뒤 줄마다** 뒤집는다(L2): 줄 끝 공백 · 줄 바꿈은 문단 수준(L1)으로 되돌리고, 가장 높은 수준부터 가장 낮은 홀수 수준까지 그 수준 이상의 구간을 뒤집는다.
  RTL 문단의 끝 공백은 왼쪽 끝으로 가므로 너비 밖(음의 x)에 놓는다. 줄임표는 줄 끝에 문단 수준으로 붙어 함께 뒤집힌다.
- **거울 문자**(L4): RTL 수준의 `( ) [ ] { } < > « » ‹ ›` 는 짝 글리프로 그린다(면에 짝이 없으면 그대로).
- 수준은 **원문 코드 포인트**로 정한다 — 셰이퍼가 버린 폭 없는 방향 제어도 수준을 바꾼다. 덮어쓰기 **한 겹**(RLO · LRO … PDF — 안의 글자를 모두 강한 R · L)을 읽어
  의사 문화권 `qps-plocm` 의 글이 실제로 거꾸로 선다. LTR 문단에 RTL 문자 · RLO 가 없으면 재배열을 건너뛴다(라틴 · 한글 글은 비용 없음).
- **지원하지 않는 것**: 포개진 방향 제어(LRE · RLE · LRI · RLI · FSI · PDI, 덮어쓰기 둘 이상), 숫자 앞뒤 기호의 세부 규칙(W2 ~ W6 — 아랍 숫자와 유럽 숫자를
  가르지 않는다), 아랍어 연결형(셰이퍼 몫 — 시험은 연결형이 없는 히브리어로 한다). HarfBuzz + SheenBidi 를 넣으면 `TextBidi` 를 대신한다(백로그 1-12).

## 리치 텍스트(`RichTextParser`)

- 표기는 **BBCode 꼴**(Godot `RichTextLabel` 과 같다): `[b]` · `[i]` · `[color=#rrggbb]` · `[color=#rrggbbaa]` · `[color=이름]`(스타일 변수 — `_listColorName` 의 번호
  + 1 이 구간의 `_colorToken`) · `[size=배]`(0 초과 16 이하), 글자 `[` 는 `[[`. XML 속성 안에서 이스케이프가 필요 없고 번역가가 다루기 쉽다.
- 태그는 바르게 포개져야 한다. **모르는 태그 · 짝 없는 태그 · 틀린 값은 글자 그대로 남기고 경고한다**(`_problemCount`) — 번역 실수가 화면에서 보이게.
  태그 모양이 아닌 `[`(`[1]` · `[ `)는 그냥 글자다(경고 없음).
- 결과는 평문 + 겹치지 않는 구간(기본 스타일인 곳은 구간 없음). 배치(`layout( …, &listSpan )` · `measure`)는 구간 경계에서도 런을 끊고, 굵게 · 기울임은
  그 굵기 · 기울기의 사슬(굵은 면이 없으면 가짜 굵게), 크기 배는 글리프 크기(줄 높이는 가장 큰 글자), 색은 `LaidOutGlyph::_colorRgba`(0xRRGGBBAA)로 간다.
- 토큰 읽기는 `Core/String/MarkupTagScanner` 하나다 — 번역 검사(`LocalizationTools` — 번역의 태그 열(이름 · 순서)이 원문과 다르면 보고)와 의사 로컬라이저
  (`PseudoLocalizer` — 태그를 바꾸지 않고, 표기로 시작하는 글은 바깥 `[` 뒤에 폭 없는 공백을 넣어 `[[` 로 읽히지 않게)가 같은 규칙으로 읽는다.
