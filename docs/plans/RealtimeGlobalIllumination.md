# 실시간 전역 조명과 볼륨 표현 (2026-10-24)

> 상태: 구현중 · 선행: ClusteredLighting · 영역: graphics · 옛번호: D2-14, D2-5.2 · 진행: 5/7

후보 `RealtimeGlobalIllumination` 과 `TerrainWaterVegetationRemainders` 의 안개 항목(옛 D2-5.2)을 합쳐 계획으로 올린 문서입니다.
이 엔진에는 안개가 없고(물속 안개 값 `WaterBodyComponent::findUnderwaterFog` 만 있고 읽는 패스가 없다), 하늘은 3색 그러데이션(`sky.hlsl`)이며, 간접 조명은 베이크한 라이트 프로브(SH9)와 반사 프로브([IndirectLighting](IndirectLighting.md) P0 ~ P6)뿐입니다.

## 1. 선택지와 권장안

아래 비교는 기억에 의존한 요약입니다(구현 세부는 각 엔진의 최신 문서로 다시 확인한다).

| 방식 | 요지 | 이 엔진과의 거리 |
|---|---|---|
| 라이트맵 | 정적 메시의 UV2 에 굽는 간접광. 비용 0, 동적 물체는 프로브로 받는다 | 베이커의 레이 · 가림 질의(`LightProbeBaker`, 물리 레이캐스트)는 있다. 메시 UV2 생성 · 패킹 · 굽기 에디터가 없다 |
| 프로브 갱신(DDGI · 유니티 APV 갱신) | 프로브 볼륨을 런타임에 다시 쏴 갱신 | SH9 격자 · 볼륨 컴포넌트 · 셰이더 읽기가 이미 있다. 갱신 레이를 GPU 에서 쏠 장면 표현(SDF · 복셀)이 없다 |
| SDFGI · Lumen 계열 | 장면 SDF 또는 표면 캐시에서 화면 공간 + 월드 공간으로 추적 | SDF 생성 · 표면 캐시 · 추적 패스가 모두 없다. 규모가 가장 크다 |
| 화면 공간 GI(SSGI) | 화면 색 · 깊이로 한 번 튕김 | 화면 밖 정보가 없다. 패스 하나라 작지만 품질 한계가 뚜렷하다 |

**권장: 프로브 갱신(GPU 에서 SH9 프로브를 다시 계산)을 실시간 GI 의 길로 잡고, 라이트맵과 Lumen 계열은 하지 않는다.** 이유는 SH9 격자 · 볼륨 · 소비 셰이더가 이미 있어 "베이크 결과를 언제 쓰는가" 만 바꾸면 되기 때문이다.
볼륨 표현(안개 · 대기 · 구름)은 GI 와 독립이라 먼저 한다 — 게임에 바로 보이는 효과가 크다. 사용자 결정은 R0 에서 받는다.

## 2. 단위

| 단위 | 내용 | 선행 |
|---|---|---|
| R0 | **들어감** — 결정 기록 5-5 "실시간 전역 조명은 프로브 갱신": 권장안을 받고(프로브 갱신 + 볼륨 표현 선행), 라이트맵 제외 · 구름 포함(평면 노이즈 층) | 없음 |
| R1 | **들어감** — 높이 · 거리 지수 안개. `FogComponent`(씬에 하나) + 값 형식 `FogSettings`(C++ 식이 셰이더와 같다) + 안개 패스 `RenderPassType::Fog`(fog.hlsl). 패스는 **장면 깊이만 읽고** 이미 칠한 색 위에 프리멀티플라이 블렌드한다(복사 첨부 없음 — 전용 실행 `executeFogPass`가 안개 없음 · 표면 값 보기에서는 아무것도 하지 않는다). 다섯 파이프라인(디퍼드 · 포워드 넷)의 HighlightSense 뒤에 들어 있다. 물속이면 물의 안개(`findUnderwaterFog`)가 컴포넌트의 값을 덮는다(`FogComponent::findFog`, 균일 · 볼륨 없음). 테스트 `FogTest`, 시나리오 `game/empty/automation/fog` | 없음 |
| R2 | **들어감** — 볼륨 안개(froxel). 라이트 그리드의 클러스터 목록을 froxel 위치마다 읽어 점광 · 스폿 산란을 넣고(방향광은 키 라이트 하나를 위상 함수로) 시선 방향으로 누적하는 컴퓨트(volumetricfog.hlsl, `RenderPassType::VolumetricFog`)와, 안개 패스가 그 구조 버퍼를 t11 로 읽는 경로. 화면 칸 80 × 45 × 거리 조각 32(`bindingslots.hlsli` 11절). 한계: 그림자 없음 · 시간 누적 없음 · 주 시점만 | R1, ClusteredLighting S3 |
| R3 | **들어감** — 대기 산란 하늘. `SkyComponent::_mode`(Gradient · Atmosphere)와 대기 값(`SkyAtmosphere` — 레일리 · 미 계수 배율, 미 이방성, 해 세기, 행성 반지름 · 대기 두께, 눈 고도, 해 원반). 눈에서 대기 끝(또는 행성 표면)까지 16 구간, 구간마다 해 쪽 8 구간의 단일 산란이고 행성 그림자와 땅 반사(알베도 = 땅 색)를 센다. 시간대는 씬의 첫 활성 방향광이 정한다(`SkyComponent::findSky` → `SkyGradient::setSun`, 셰이더는 키 라이트 상수) — 방향광이 없으면 하늘이 어둡다. C++ `SkyAtmosphere::evaluateScattering` 와 `sky.hlsl` `computeAtmosphere` 가 같은 식 · 같은 상수이고 프로브 베이크가 C++ 쪽을 부른다(해 원반은 셰이더만). 테스트 `SkyTest` 대기 갈래(낮 파랑 · 노을 빨강 · 행성 그림자 · 땅 반사 · 해 찾기) | R1 |
| R4 | **들어감** — 구름. 평면 노이즈 층(레이마치 안 함): 눈 위 `_cloudAltitudeKm` 평면에 정수 해시 값 노이즈 fbm(네 옥타브)을 `_cloudCoverage` 문턱으로 잘라 바람으로 흘린다(시간 = `g_SwMaterialTime`). 해 쪽으로 한 걸음 옮긴 밀도로 그늘을 어림하고 지평선 근처는 지운다. 구름은 하늘 색 위에 얹히고 프로브 베이크에는 보이지 않는다. 한계: 카메라를 따라온다(하늘 상자처럼) · 그림자를 드리우지 않는다 | R3 |
| R5 | GPU 프로브 갱신의 장면 표현 결정 — 복셀 · 깊이 큐브맵 · 소프트웨어 SDF 중 하나(설계 문서 절을 이 문서에 쓴다) | R0 |
| R6 | 프로브 갱신 컴퓨트 — 프레임마다 프로브 일부를 갱신(시간 분산), 볼륨 컴포넌트의 `_bRealtime` 칸 | R5 |

단위 R1 ~ R4 · R6 은 셰이더 소스를 바꾸므로 워크트리에서 커밋하지 못하고 [패치 파일](ShaderPatches/README.md)로 넘긴다(`CheckShaderStamps` — 커밋 훅이 빌드된 App 으로 쿠킹을 요구한다). C++ 쪽(컴포넌트 · 패스 타입 · 입력 서명)은 같은 브랜치에 커밋한다.
새 셰이더 경로(`fog.hlsl` · `volumetricfog.hlsl` · `probeupdate.hlsl`)가 엔진 기본 에셋 표(`enginedefaultassets.xml`)에 들어 있으므로 **C++ 와 패치를 함께** 적용해야 앱이 뜬다.

**남은 확인(통합에서 확인):** 매니페스트 id `shader-patches-cook`, `fog-tests`, `fog-scenario`, `sky-tests`, `sky-scenario` — 패치 `02-RealtimeGI-R1R2-Fog.patch` · `03-RealtimeGI-R3R4-Sky.patch` 를 번호 순으로 적용 · 쿠킹 뒤 테스트, 시나리오 `fog` · `sky` 는 네 백엔드이고 문턱은 첫 측정으로 정한다.
R1 의 입력은 [EnvironmentRemainders](EnvironmentRemainders.md) 가 말하는 "안개 패스" 와 같은 것이다 — 이 문서가 정본이다.
