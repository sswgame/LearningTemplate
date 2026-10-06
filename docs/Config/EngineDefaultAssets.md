<!-- 생성 문서 — 손으로 고치지 않는다. 정본은 코드다. 다시 만들기: py -3 Scripts/generate/GenerateConfigReference.py -->

# EngineDefaultAssets

[설정 색인](README.md) · [어디에 두나](../07_Configuration.md)

| | |
|---|---|
| 파일 | `Resource/engine/data/enginedefaultassets.xml` |
| 층 | 엔진 기본값 |
| 읽는 곳 | `EngineDefaultAssets::loadFromResource` (`EngineLoop` 엔진 셸 단계) |
| 언제 | 기동 |
| 배포본 | 엔진 팩에 실림 |
| 커밋 | 한다 |

XML 속성(값 하나) · 자식 원소(목록 · 구조체)의 이름은 아래 칸 이름 그대로입니다. 모르는 이름은 로드 오류입니다.

## 칸

enginedefaultassets.xml 의 엔진 셸 경로입니다. 읽기는 `XmlSerializer` 가 PROPERTY 그래프로 합니다. 필드를 하나 추가하면 읽기가 저절로 따라옵니다(필드마다 손으로 읽으면 한 줄을 빠뜨릴 때 값이 조용히 기본값으로 남습니다).

정본: [`Source/Engine/Config/EngineDefaultAssets.h`](../../Source/Engine/Config/EngineDefaultAssets.h)

| 칸 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_defaultMaterial` | `string` | `engine/materials/defaultmaterial.material` |  |  | 씬 폴백 머티리얼 |
| `_shellInputMap` | `string` | `engine/input/default.input.xml` |  |  | App 셸 InputMap |
| `_userSettingsSchema` | `string` | `engine/settings/engine.settings.xml` |  |  | 플레이어 옵션 메뉴의 엔진 설정 스키마(`UserSettingsManager`) |
| `_telemetrySchema` | `string` | `engine/telemetry/engine.telemetry.xml` |  |  | 엔진 텔레메트리 사건 스키마(`TelemetryService`) |
| `_cultureTable` | `string` | `engine/localization/engine.cultures.json` |  |  | 문화권 표 — 복수형 규칙 · 숫자 · 날짜 형식 · 쓰기 방향 · 글꼴 대체(`LocalizationManager`) |
| `_localizationProject` | `string` | `engine/localization/engine.locproject.json` |  |  | 엔진 문자열(설정 메뉴 등)의 로컬라이제이션 프로젝트 |
| `_defaultForwardPipeline` | `string` | `engine/pipeline/forwardpipeline.xml` |  |  | 포워드 렌더 파이프라인(프레임 그래프) |
| `_defaultDeferredPipeline` | `string` | `engine/pipeline/deferredpipeline.xml` |  |  | 디퍼드 렌더 파이프라인 |
| `_defaultRenderPass` | `string` | `engine/renderpass/defaultrenderpass.xml` |  |  | 기본 렌더 패스 바인드 틀 |
| `_shaderShadowDepth` | `string` | `engine/shaders/shadowdepth.hlsl` |  |  | 그림자 깊이 패스 |
| `_shaderForwardLit` | `string` | `engine/shaders/forwardlit.hlsl` |  |  | 포워드 조명 |
| `_shaderGBuffer` | `string` | `engine/shaders/gbuffer.hlsl` |  |  | 디퍼드 G 버퍼 채우기 |
| `_shaderDeferredLighting` | `string` | `engine/shaders/deferredlighting.hlsl` |  |  | 디퍼드 조명 |
| `_shaderPostBloom` | `string` | `engine/shaders/postbloom.hlsl` |  |  | 후처리 블룸 |
| `_shaderPostOutline` | `string` | `engine/shaders/postoutline.hlsl` |  |  | 후처리 외곽선 |
| `_shaderFullscreenBlit` | `string` | `engine/shaders/fullscreenblit.hlsl` |  |  | 전체 화면 복사 |
| `_shaderGpuCull` | `string` | `engine/shaders/gpucull.hlsl` |  |  | GPU 컬링 · 드로우 커맨드 생성(컴퓨트) |
| `_shaderInstanceAnim` | `string` | `engine/shaders/instanceanim.hlsl` |  |  | 인스턴스 애니메이션(컴퓨트) |
| `_shaderMeshMorph` | `string` | `engine/shaders/meshmorph.hlsl` |  |  | 모프 타깃(컴퓨트) |
| `_shaderMeshSkin` | `string` | `engine/shaders/meshskin.hlsl` |  |  | 스키닝(컴퓨트) |
| `_shaderInstanceSort` | `string` | `engine/shaders/instancesort.hlsl` |  |  | 투명 인스턴스 바이토닉 정렬(컴퓨트) |
| `_shaderFullscreenTriangle` | `string` | `engine/shaders/fullscreentriangle.hlsl` |  |  | 전체 화면 삼각형 정점 셰이더 |
| `_shaderSsao` | `string` | `engine/shaders/ssao.hlsl` |  |  | SSAO |
| `_shaderTaa` | `string` | `engine/shaders/taa.hlsl` |  |  | TAA |
| `_shaderTonemap` | `string` | `engine/shaders/tonemap.hlsl` |  |  | 톤 매핑 |
| `_shaderToon` | `string` | `engine/shaders/toon.hlsl` |  |  | 셀 셰이딩 머티리얼 셰이더 — 메시 외곽선 패스의 기본 셰이더이기도 하다 |
