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

enginedefaultassets.xml 의 엔진 셸 경로입니다. 읽기는 `XMLSerializer` 가 PROPERTY 그래프로 합니다. 필드를 하나 추가하면 읽기가 저절로 따라옵니다(필드마다 손으로 읽으면 한 줄을 빠뜨릴 때 값이 조용히 기본값으로 남습니다). 값의 기준은 `enginedefaultassets.xml` 하나다 — 칸은 `DataPath` 라 헤더에 값을 적을 수 없고, 모든 칸이 `Required` 다. 표는 데이터에서 읽어서만 얻는다(`load` · `EngineDefaultAssetsSubsystem`). 기본 생성한 표를 기준처럼 쓰는 길이 없다. `Abstract` 는 리플렉션이 생성자를 등록하지 않게 한다 — 리플렉션으로도 만들 수 없다.

원본: [`Source/Engine/Config/EngineDefaultAssets.h`](../../Source/Engine/Config/EngineDefaultAssets.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_defaultMaterial` | `DataPath` | — |  |  | 씬 폴백 머티리얼 |
| `_missingMaterial` | `DataPath` | — |  |  | 못 읽은 머티리얼 대신 쓰는 마젠타 체커 |
| `_missingTexture` | `DataPath` | — |  |  | 못 읽은 텍스처 대신 샘플하는 마젠타 체커 |
| `_loadingTexture` | `DataPath` | — |  |  | 비동기로 읽는 텍스처가 도착하기 전에 샘플하는 중간 회색(실패 표시가 아니라 정해진 로드 중 상태) |
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
| `_uiOptionsRowBool` | `DataPath` | — |  |  | 옵션 메뉴 행 견본: 불리언 설정(체크 상자) |
| `_uiOptionsRowInt` | `DataPath` | — |  |  | 옵션 메뉴 행 견본: 정수 설정 |
| `_uiOptionsRowFloat` | `DataPath` | — |  |  | 옵션 메뉴 행 견본: 실수 설정 |
| `_uiOptionsRowEnum` | `DataPath` | — |  |  | 옵션 메뉴 행 견본: 고르기 설정(마우스 · 키보드 — 콤보 상자) |
| `_uiOptionsRowEnumPad` | `DataPath` | — |  |  | 옵션 메뉴 행 견본: 고르기 설정(패드 탐색 — 좌우로 넘기기) |
| `_uiOptionsRowKeyBinding` | `DataPath` | — |  |  | 옵션 메뉴 행 견본: 키 바인딩 설정(단추) |
| `_uiOptionsRowString` | `DataPath` | — |  |  | 옵션 메뉴 행 견본: 글 설정 |
| `_uiSettingsConfirm` | `DataPath` | — |  |  | 바꾼 설정을 되돌리기 전 카운트다운 확인 창 |
| `_uiSettingsUnsaved` | `DataPath` | — |  |  | 저장하지 않은 설정을 두고 닫을 때의 확인 창 |
| `_uiKeyRebind` | `DataPath` | — |  |  | 키 바인딩을 받는 창 |
| `_uiNotifications` | `DataPath` | — |  |  | 알림 화면(`UINotificationService`) |
| `_uiNotificationEntry` | `DataPath` | — |  |  | 알림 한 줄 견본 |
| `_uiSubtitles` | `DataPath` | — |  |  | 자막 화면(`UISubtitleService`) |
| `_uiTooltip` | `DataPath` | — |  |  | 툴팁 화면(`UITooltipService`) |
| `_uiDebugHUD` | `DataPath` | — |  |  | 디버그 HUD 설정 창(`DebugHUD`, 개발 빌드) |
| `_debugHUDSettingsSchema` | `DataPath` | — |  |  | 디버그 HUD 설정 스키마(`DebugHUD`, 개발 빌드) |
| `_uiResolutionPresets` | `DataPath` | — |  |  | UI 미리보기 해상도 프리셋 표(`UIResolutionPresetTable`) |
| `_netEmulationPresets` | `DataPath` | — |  |  | 네트워크 에뮬레이션 프리셋 표(`NetEmulationPresetTable`) |
| `_tileMapMaterial` | `DataPath` | — |  |  | 타일맵 렌더러(`TileMapRendererComponent`) — 픽셀 아트 스프라이트 머티리얼 |
| `_foliageMaterial` | `DataPath` | — |  |  | 풀 · 나무 층(`FoliageComponent`) |
| `_terrainMaterial` | `DataPath` | — |  |  | 지형(`TerrainComponent`) |
| `_waterMaterial` | `DataPath` | — |  |  | 물(`WaterBodyComponent`) |
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
| `_shaderVfxSimulation` | `DataPath` | — |  |  | GPU 방출기의 입자 시뮬레이션(컴퓨트) |
| `_shaderVfxMeshInstance` | `DataPath` | — |  |  | GPU 메시 입자의 인스턴스 쓰기(컴퓨트) |
| `_shaderDepthHistory` | `DataPath` | — |  |  | 뷰의 깊이 히스토리 복사(풀스크린) — GPU 입자의 깊이 충돌이 읽는다 |
| `_shaderAutoExposure` | `DataPath` | — |  |  | 자동 노출 측정 · 적응(풀스크린 1x1) — 톤맵이 그 EV 를 읽는다 |
| `_shaderUpscale` | `DataPath` | — |  |  | FSR 1 업스케일(EASU · RCAS) — Present 가 배율 크기 뷰를 출력 크기로 키울 때(graphics.upscaler) |
| `_shaderOcclusionTiles` | `DataPath` | — |  |  | 가림 타일(컴퓨트) — 지난 장면 깊이를 타일마다 최대 깊이로 줄인다. GPU 컬링의 가림 판정이 읽는다 |
| `_shaderFullscreenTriangle` | `DataPath` | — |  |  | 전체 화면 삼각형 정점 셰이더 |
| `_shaderSsao` | `DataPath` | — |  |  | SSAO |
| `_shaderTaa` | `DataPath` | — |  |  | TAA |
| `_shaderMotionBlur` | `DataPath` | — |  |  | 카메라 모션 블러(지난 프레임 뷰-투영으로 구한 화면 속도) |
| `_shaderTonemap` | `DataPath` | — |  |  | 톤 매핑 |
| `_shaderDepthOfField` | `DataPath` | — |  |  | 피사계 심도 — 장면 깊이에서 착란원을 구해 HDR 색을 흐린다 |
| `_shaderSky` | `DataPath` | — |  |  | 하늘 그러데이션(스카이 패스) — 깊이 1 의 풀스크린 삼각형 |
| `_shaderFog` | `DataPath` | — |  |  | 높이 · 거리 지수 안개(안개 패스) — 장면 깊이로 광학 두께를 구해 장면 색 위에 블렌드한다 |
| `_shaderVolumetricFog` | `DataPath` | — |  |  | 볼륨 안개(컴퓨트) — froxel 마다 라이트 산란을 구해 깊이 방향으로 누적한다 |
| `_shaderProbeUpdate` | `DataPath` | — |  |  | 실시간 라이트 프로브 갱신(컴퓨트) — 프로브마다 복셀로 광선을 쏘아 SH9 를 갱신한다 |
| `_shaderToon` | `DataPath` | — |  |  | 셀 셰이딩 머티리얼 셰이더 — 메시 외곽선 패스의 기본 셰이더이기도 하다 |
| `_shaderHighlightOutline` | `DataPath` | — |  |  | 인스턴스 강조 외곽선(상호작용 대상 · 감각 모드) — 강조 패스 둘의 셰이더 |
| `_shaderCanvas` | `DataPath` | — |  |  | 화면 2D(UI · 월드 글자) 사각형 — 사각형 하나 = 인스턴스 하나, SDF 모양 · 글리프 |
| `_shaderSprite2D` | `DataPath` | — |  |  | 스프라이트(2D) 머티리얼 셰이더 — 머티리얼 없이 쿠킹 목록에도 든다 |
| `_shaderSprite2DLit` | `DataPath` | — |  |  | 빛 받는 스프라이트(2D 빛 · 그림자) 머티리얼 셰이더 — 머티리얼 없이 쿠킹 목록에도 든다 |
| `_shaderParticle` | `DataPath` | — |  |  | 파티클 빌보드(Vfx) — 방출기 하나 = 그리기 하나, 입자 하나 = 인스턴스 하나. 투명 패스 PSO 를 물려받는다 |
| `_shaderProbeProvokingVertex` | `DataPath` | — |  |  | 프로브: 플랫 보간이 첫 정점 값을 쓰는가(RHIDeviceTest) |
| `_shaderProbeInstanceSlot` | `DataPath` | — |  |  | 프로브: 인스턴스 슬롯 스트림이 네 API 에서 같은가(RHIDeviceTest) |
| `_shaderProbeSampleCompute` | `DataPath` | — |  |  | 프로브: 컴퓨트 쿠킹 · 리플렉션(ShaderTest) |
| `_shaderProbeComputeTextureWrite` | `DataPath` | — |  |  | 프로브: 컴퓨트 RW 텍스처 쓰기 · 기본이 아닌 진입점(RHIDeviceTest) |
| `_shaderProbeWaterWave` | `DataPath` | — |  |  | 프로브: 거스트너 파도 셰이더 식이 CPU 와 같은가(RenderPassGPUTest) |
