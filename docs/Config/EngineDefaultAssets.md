<!-- 생성 문서입니다. 손으로 고치지 말고 원본 코드를 고친 뒤 다시 만듭니다: py -3 Scripts/generate/GenerateConfigReference.py -->

# EngineDefaultAssets

[설정 색인](README.md) · [어디에 두나](../07_Configuration.md)

| | |
|---|---|
| 파일 | `Resource/engine/data/enginedefaultassets.xml` |
| 층 | 엔진 기본값 |
| 읽는 곳 | `EngineDefaultAssets::loadFromResource` (`Engine` 엔진 셸 단계) |
| 언제 | 기동 |
| 배포본 | 엔진 팩에 실림 |
| 커밋 | 한다 |

XML 속성(값 하나)과 자식 요소(목록, 구조체)의 이름은 아래 필드 이름 그대로입니다. 모르는 이름은 로드 오류입니다.

## 필드

enginedefaultassets.xml 의 엔진 셸 경로입니다. 읽기는 `XMLSerializer` 가 PROPERTY 그래프로 합니다. 필드를 하나 추가하면 읽기가 저절로 따라옵니다(필드마다 손으로 읽으면 한 줄을 빠뜨릴 때 값이 조용히 기본값으로 남습니다). 값의 정본은 `enginedefaultassets.xml` 하나다 — 칸은 `DataPath` 라 헤더에 값을 적을 수 없고, 모든 칸이 `Required` 다. 표는 데이터에서 읽어서만 얻는다(`load` · `EngineDefaultAssetsSubsystem`). 기본 생성한 표를 정본처럼 쓰는 길이 없다. `Abstract` 는 리플렉션이 생성자를 등록하지 않게 한다 — 리플렉션으로도 만들 수 없다.

원본: [`Source/Engine/Config/EngineDefaultAssets.h`](../../Source/Engine/Config/EngineDefaultAssets.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_defaultMaterial` | `DataPath` | — |  |  | 씬 폴백 머티리얼 |
| `_missingMaterial` | `DataPath` | — |  |  | 못 읽은 머티리얼 대신 쓰는 마젠타 체커 |
| `_missingTexture` | `DataPath` | — |  |  | 못 읽은 텍스처 대신 샘플하는 마젠타 체커 |
| `_shellInputMap` | `DataPath` | — |  |  | App 셸 InputMap |
| `_uiInputMap` | `DataPath` | — |  |  | 런타임 UI 의 행동 맵 — 탐색 · 확인 · 뒤로 · 탭(`UISystem`, Shipping 에도 있다) |
| `_photoModeInputMap` | `DataPath` | — |  |  | 포토 모드 자유 카메라의 행동 맵 — 이동 · 오르내림 · 둘러보기 · 기울이기 · 줌 · 노출(`CameraDirectorComponent`, 포토 모드 동안만 읽는다) |
| `_userSettingsSchema` | `DataPath` | — |  |  | 플레이어 옵션 메뉴의 엔진 설정 스키마(`UserSettingsManager`) |
| `_telemetrySchema` | `DataPath` | — |  |  | 엔진 텔레메트리 사건 스키마(`TelemetryService`) |
| `_cultureTable` | `DataPath` | — |  |  | 문화권 표 — 복수형 규칙 · 숫자 · 날짜 형식 · 쓰기 방향 · 글꼴 대체(`LocalizationManager`) |
| `_localizationProject` | `DataPath` | — |  |  | 엔진 문자열(설정 메뉴 등)의 로컬라이제이션 프로젝트 |
| `_fontCatalog` | `DataPath` | — |  |  | 글꼴 카탈로그 — 저장소 글꼴 가족 · 시스템 글꼴 가족(`FontService`) |
| `_uiScaleSettings` | `DataPath` | — |  |  | 런타임 UI 배율 규칙 — 해상도 → 배율(`UIScaleSettings`, 팩 설정 `PackEngineSettings` 가 덮어쓴다) |
| `_uiThemes` | `DataPath` | — |  |  | 런타임 UI 테마 목록 — 테마 이름 → 스타일 시트들(`UIThemeCatalog`, 팩 설정이 덮어쓴다) |
| `_uiOptionsMenu` | `DataPath` | — |  |  | 옵션 메뉴 문서. 설정 스키마에서 탭과 행을 만드는 화면(`OptionsMenuScreen`, 팩 설정이 덮어쓴다) |
| `_uiPauseMenu` | `DataPath` | — |  |  | 일시정지 메뉴 문서 — 팩 설정이 `_bUIPauseMenu` 를 켤 때 `UI.Pause` 가 연다(`PauseMenuScreen`) |
| `_defaultForwardPipeline` | `DataPath` | — |  |  | 포워드 렌더 파이프라인(프레임 그래프) |
| `_defaultDeferredPipeline` | `DataPath` | — |  |  | 디퍼드 렌더 파이프라인 |
| `_defaultRenderPass` | `DataPath` | — |  |  | 기본 렌더 패스 바인드 틀 |
| `_shaderShadowDepth` | `DataPath` | — |  |  | 그림자 깊이 패스 |
| `_shaderForwardLit` | `DataPath` | — |  |  | 포워드 조명 |
| `_shaderGBuffer` | `DataPath` | — |  |  | 디퍼드 G 버퍼 채우기 |
| `_shaderDeferredLighting` | `DataPath` | — |  |  | 디퍼드 조명 |
| `_shaderPostBloom` | `DataPath` | — |  |  | 후처리 블룸 |
| `_shaderPostOutline` | `DataPath` | — |  |  | 후처리 외곽선 |
| `_shaderFullscreenBlit` | `DataPath` | — |  |  | 전체 화면 복사 |
| `_shaderGPUCull` | `DataPath` | — |  |  | GPU 컬링 · 드로우 커맨드 생성(컴퓨트) |
| `_shaderInstanceAnimation` | `DataPath` | — |  |  | 인스턴스 애니메이션(컴퓨트) |
| `_shaderMeshMorph` | `DataPath` | — |  |  | 모프 타깃(컴퓨트) |
| `_shaderMeshSkin` | `DataPath` | — |  |  | 스키닝(컴퓨트) |
| `_shaderInstanceSort` | `DataPath` | — |  |  | 투명 인스턴스 바이토닉 정렬(컴퓨트) |
| `_shaderLightGrid` | `DataPath` | — |  |  | 라이트 그리드 생산(컴퓨트) — 클러스터드 라이팅의 빛 목록 |
| `_shaderFullscreenTriangle` | `DataPath` | — |  |  | 전체 화면 삼각형 정점 셰이더 |
| `_shaderSsao` | `DataPath` | — |  |  | SSAO |
| `_shaderTaa` | `DataPath` | — |  |  | TAA |
| `_shaderTonemap` | `DataPath` | — |  |  | 톤 매핑 |
| `_shaderSky` | `DataPath` | — |  |  | 하늘 그러데이션(스카이 패스) — 깊이 1 의 풀스크린 삼각형 |
| `_shaderToon` | `DataPath` | — |  |  | 셀 셰이딩 머티리얼 셰이더 — 메시 외곽선 패스의 기본 셰이더이기도 하다 |
| `_shaderCanvas` | `DataPath` | — |  |  | 화면 2D(UI · 월드 글자) 사각형 — 사각형 하나 = 인스턴스 하나, SDF 모양 · 글리프 |
| `_shaderSprite2D` | `DataPath` | — |  |  | 스프라이트(2D) 머티리얼 셰이더 — 머티리얼 없이 쿠킹 목록에도 든다 |
| `_shaderSprite2DLit` | `DataPath` | — |  |  | 빛 받는 스프라이트(2D 빛 · 그림자) 머티리얼 셰이더 — 머티리얼 없이 쿠킹 목록에도 든다 |
| `_shaderParticle` | `DataPath` | — |  |  | 파티클 빌보드(Vfx) — 방출기 하나 = 그리기 하나, 입자 하나 = 인스턴스 하나. 투명 패스 PSO 를 물려받는다 |
| `_shaderProbeProvokingVertex` | `DataPath` | — |  |  | 프로브: 플랫 보간이 첫 정점 값을 쓰는가(RHIDeviceTest) |
| `_shaderProbeInstanceSlot` | `DataPath` | — |  |  | 프로브: 인스턴스 슬롯 스트림이 네 API 에서 같은가(RHIDeviceTest) |
| `_shaderProbeSampleCompute` | `DataPath` | — |  |  | 프로브: 컴퓨트 쿠킹 · 리플렉션(ShaderTest) |
| `_shaderProbeComputeTextureWrite` | `DataPath` | — |  |  | 프로브: 컴퓨트 RW 텍스처 쓰기 · 기본이 아닌 진입점(RHIDeviceTest) |
| `_shaderProbeWaterWave` | `DataPath` | — |  |  | 프로브: 거스트너 파도 셰이더 식이 CPU 와 같은가(RenderPassGPUTest) |
