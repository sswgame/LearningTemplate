# 실시간 전역 조명과 볼륨 표현 (2026-10-24)

> 상태: 측정대기 · 선행: ClusteredLighting · 영역: graphics · 옛번호: D2-14, D2-5.2 · 진행: 7/7

후보 `RealtimeGlobalIllumination` 과 `TerrainWaterVegetationRemainders` 의 안개 항목(옛 D2-5.2)을 합쳐 계획으로 올린 문서입니다. 이 엔진에는 안개가 없었고(`WaterBodyComponent::findUnderwaterFog` 만 있고 읽는 패스가 없다) 하늘은 3색 그러데이션이며 간접 조명은 베이크한 라이트 프로브(SH9)와 반사 프로브([IndirectLighting](IndirectLighting.md) P0 ~ P6)뿐이었습니다.

## 1. 선택지와 결정

비교는 기억에 의존한 요약입니다(구현 세부는 각 엔진의 최신 문서로 다시 확인).

| 방식 | 이 엔진과의 거리 |
|---|---|
| 라이트맵(정적 메시 UV2 에 굽기) | 베이커의 레이 · 가림 질의(`LightProbeBaker`)는 있으나 메시 UV2 생성 · 패킹 · 굽기 에디터가 없다 — 제외 |
| 프로브 갱신(DDGI · 유니티 APV 갱신) | SH9 격자 · 볼륨 컴포넌트 · 셰이더 읽기가 이미 있다. 갱신 레이를 GPU 에서 쏠 장면 표현만 없다 — **채택** |
| SDFGI · Lumen 계열 | SDF 생성 · 표면 캐시 · 추적 패스가 모두 없어 규모가 가장 크다 — 제외 |
| 화면 공간 GI | 화면 밖 정보가 없어 품질 한계가 뚜렷하다 |

권장(R0 에서 받음, 결정 기록 5-5): 프로브 갱신(GPU 에서 SH9 프로브를 다시 계산)을 실시간 GI 의 길로 잡는다 — "베이크 결과를 언제 쓰는가" 만 바뀌고 소비 셰이더 · 프로브 격자 · 볼륨 컴포넌트는 그대로다. 볼륨 표현(안개 · 대기 · 구름)은 GI 와 독립이라 먼저 한다.

## 2. 단위 (모두 들어감)

| 단위 | 내용 | 선행 |
|---|---|---|
| R0 | 결정 기록 5-5 "실시간 전역 조명은 프로브 갱신": 라이트맵 제외 · 구름 포함(평면 노이즈 층) | 없음 |
| R1 | 높이 · 거리 지수 안개. `FogComponent`(씬에 하나) + `FogSettings` + 안개 패스 `RenderPassType::Fog`(fog.hlsl). 패스는 **장면 깊이만 읽고** 이미 칠한 색 위에 프리멀티플라이 블렌드한다(복사 첨부 없음, 안개 없음 · 표면 값 보기에서는 아무것도 하지 않음). 다섯 파이프라인의 HighlightSense 뒤. 물속이면 물의 안개가 컴포넌트 값을 덮는다(`FogComponent::findFog`). `FogTest`, 시나리오 `game/empty/automation/fog` | 없음 |
| R2 | 볼륨 안개(froxel). 라이트 그리드의 클러스터 목록을 froxel 위치마다 읽어 점광 · 스폿 산란(방향광은 키 라이트 하나를 위상 함수로)을 넣고 시선 방향으로 누적하는 컴퓨트(volumetricfog.hlsl)와 안개 패스가 그 구조 버퍼를 t11 로 읽는 경로. 80 × 45 × 거리 조각 32. 한계: 그림자 없음 · 시간 누적 없음 · 주 시점만 | R1, ClusteredLighting S3 |
| R3 | 대기 산란 하늘. `SkyComponent::_mode`(Gradient · Atmosphere)와 `SkyAtmosphere`(레일리 · 미 계수 배율, 이방성, 해 세기, 행성 반지름 · 대기 두께, 눈 고도, 해 원반). 눈에서 대기 끝까지 16 구간 × 해 쪽 8 구간의 단일 산란, 행성 그림자와 땅 반사. 시간대는 씬의 첫 활성 방향광이 정한다(없으면 어둡다). C++ `evaluateScattering` 와 `sky.hlsl` `computeAtmosphere` 가 같은 식 · 상수이고 프로브 베이크가 C++ 쪽을 부른다. `SkyTest` | R1 |
| R4 | 구름. 평면 노이즈 층(레이마치 없음): 눈 위 `_cloudAltitudeKm` 평면에 fbm(네 옥타브)을 `_cloudCoverage` 문턱으로 잘라 바람으로 흘린다. 해 쪽으로 한 걸음 옮긴 밀도로 그늘을 어림하고 지평선 근처는 지운다. 프로브 베이크에는 안 보인다. 한계: 카메라를 따라오고 그림자를 드리우지 않는다 | R3 |
| R5 | 장면 표현은 **정적 메시의 1 비트 점유 복셀**(3절). `SceneVoxelGrid`(복셀 형식 · 삼각형 칠하기 · DDA 광선) + 렌더러의 `SceneVoxelizer`(불투명 · 보이는 · 스킨 없는 메시) | R0 |
| R6 | 프로브 갱신 컴퓨트. `LightProbeVolumeComponent::_bRealtime`(+ 복셀 크기 · 광선 수 · 갱신 주기 · 섞는 비율) → 수집이 갱신 요청(`RealtimeProbeRequest`)을 만들고 렌더 스레드의 `RealtimeProbeUpdater` 가 컴퓨트 `probeupdate.hlsl`(`RenderPassType::ProbeUpdate`)을 라이트 업로드 뒤 · 조명 전에 한 번 돈다. 프로브마다 갱신 조(번호 % 주기)가 프레임마다 광선을 쏘아 SH9 기록을 섞고 프로브 버퍼(t13)에 쓴다(소비 셰이더는 그대로). `SceneVoxelGridTest` · `SceneVoxelizerTest` · `RealtimeProbeTest`, 시나리오 `realtimegi` | R5 |

R1 ~ R4 · R6 의 셰이더 소스는 [패치 파일](ShaderPatches/README.md)로 넘겼다가 통합(integE)이 `02 → 03 → 04` 순으로 적용하고 네 백엔드로 구웠다(`common.hlsli` 의 `kPi` 겹친 정의 삭제, `traceVoxelRay` 펼친 루프에서 `continue` 를 빼 FXC X3531 해결). 새 셰이더 경로(`fog.hlsl` · `volumetricfog.hlsl` · `probeupdate.hlsl`)가 `enginedefaultassets.xml` 에 들어 있다. R1 의 입력은 [EnvironmentRemainders](EnvironmentRemainders.md) 가 말하는 "안개 패스" 와 같고 이 문서가 정본이다.

## 3. R5 설계: 프로브 갱신의 장면 표현

| 후보 | 이 엔진에서의 비용 | 판단 |
|---|---|---|
| 복셀 점유 격자 | 광선이 칸을 따라 걷는 DDA 한 가지, 메시 정점(CPU 사본)만 있으면 됨, 볼륨 하나당 최대 128³ 비트(256 KiB). 칸 크기만큼 거칠고(0.5 m 기본) 면의 노멀이 축 정렬이며 움직이는 물체는 빠진다 | **채택** |
| 깊이 큐브맵(프로브마다) | 프로브마다 여섯 면을 다시 그려야 해 갱신 비용이 프로브 수에 비례, 수천 개에는 맞지 않는다 | 제외 |
| 소프트웨어 SDF | SDF 생성 · 저장 · 갱신이 모두 새 시설 | 제외(규모) |

**규칙.** 복셀 상자는 볼륨 상자에 간격 + 2 m 의 여유를 더한 것이다. 칸 수가 축마다 128 을 넘으면 칸 크기를 키운다. 칸은 삼각형-상자 분리축 판정으로 켜 얇은 면도 빠지지 않는다. 광선이 격자 밖으로 나가면 하늘 SH 를 읽는다.
**한계.** (1) 불투명 · 보이는 · 스킨 없는 메시만 — 반투명 · 스킨 메시 · 지형 · 식생은 빛을 가리지 않는다. (2) 복셀은 메시 등록 · 해제(프리미티브 집합 세대)와 볼륨 상자 · 칸 크기가 바뀔 때만 다시 만든다 — 정적 메시를 **옮기면** 반영되지 않는다. (3) 닿은 면의 밝기는 베이커와 같은 규칙(볼륨의 반사율 × 직접광 한 번 튐 + 하늘)이고 직접광은 라이트 버퍼의 앞 8 개(방향광 먼저)를 복셀 가림으로 본다(방향광 그림자 맵은 쓰지 않음). (4) 하늘은 SH9 로 투영해(64 방향) 읽으므로 해 원반은 보이지 않는다.

## 4. 확인 대기

문턱은 모두 첫 측정 전의 어림값이다.

- 매니페스트 id `fog-tests` — R1 · R2 안개 테스트.
- 매니페스트 id `fog-scenario` — 시나리오 `fog`(네 백엔드).
- 매니페스트 id `sky-tests` — R3 · R4 하늘 테스트.
- 매니페스트 id `sky-scenario` — 시나리오 `sky`(네 백엔드). 첫 실행(2026-10-25)은 무대의 해가 수평선 위 4.5° 노을이라 졌고 해를 46° 로 올렸다(wt/gfxfix). 빌드 · GPU 대기: 다시 돌려 문턱(`atMost` -0.02)을 측정값으로 정한다.
- 매니페스트 id `realtime-probe-tests` — R5 · R6 테스트(복셀 형식 · 복셀화 · 입력 행 배치 · 수집).
- 매니페스트 id `realtimegi-scenario` — 시나리오 `realtimegi`(네 백엔드 — 직접광이 닿지 않는 면이 바닥에서 튕긴 빛으로 밝아진다). `probeupdate.hlsl` 의 SH 투영 · 복셀 DDA 가 C++ 쪽과 같은지는 시나리오 밝기와 `SceneVoxelGridTest` 의 DDA 값을 견줘 본다.
- 매니페스트 id `realtime-probe-measure` — 프로브 수 · 광선 수에 따른 컴퓨트 GPU 시간(`RT.Probe.Update`)과 갱신 주기별 수렴 속도.
