# 환경(지형 · 식생 · 물)의 남은 것 (2026-10-24)

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 옛번호: D2-5, D2-5.2, D2-5.5 · 진행: 3/4

후보 `TerrainWaterVegetationRemainders` 를 계획으로 올린 문서입니다. 들어간 것과 계약(지형 충돌 · 부력 포함)은 `Source/Engine/Environment` 각 헤더 주석에 있습니다. 지형 LOD 교체 · 지형 쓰기의 비용과 식생 부분 계산은 [TerrainEditCost](TerrainEditCost.md), 안개 · 대기는 [RealtimeGlobalIllumination](RealtimeGlobalIllumination.md) R1 ~ R3 이 맡습니다(물속 안개 값은 `WaterBodyComponent::findUnderwaterFog` 가 준다).

## 1. 단위

| 단위 | 내용 |
|---|---|
| E1 | **들어감** — 강 경로를 스플라인에서 받는다. `SplineComponent` 가 GameFramework(`Base/World/Spline`)에 있어 Engine 의 `WaterBodyComponent` 가 읽을 수 없으므로(층 방향) GameFramework 쪽 어댑터 `SplineRiverPathComponent` 가 같은 오브젝트의 스플라인 월드 곡선을 `sampleUniform` 으로 샘플해 물 위치 기준 점으로 `WaterBodyComponent::setRiverPathFromSource` 에 넘긴다. 어댑터가 있으면 `_bRiverPathDriven`(읽기 전용)이 켜지고 `_listRiverPoint`(직렬화 속성이지만 정본은 스플라인 하나)는 `EditCondition` 으로 인스펙터가 막는다. 곡선 번호 `SplineComponent::getPathRevision` 이나 물 위치가 바뀌면 다음 틱에 다시 샘플하고 수면은 틱 뒤로 미뤄 다시 만든다. 물이나 스플라인이 없으면 오류를 한 번 남기고 넘기지 않는다(폴백 없음). `SplineRiverPathTest` |
| E2 | 물의 굴절 · 화면 공간 두께 — 반투명 패스가 장면 색 사본 · 장면 깊이를 입력으로 받는 계약. `RenderPassInputSignature` 에 장면 색 사본 역할을 두고 파이프라인이 Transparent 앞에 복사 패스를 선언한다(선언이 곧 바인딩). 지금은 지형 깊이를 정점에 굽는다 |
| E3 | **들어감** — 레이어 여덟. 값 형식 `SurfaceLayerWeights`(`Engine/Environment/Placement`, float4 둘 `_low` 0..3 · `_high` 4..7, 개수의 정본 `kLayerCount`)를 높이장 · 배치 규칙 · 브러시가 함께 쓴다. `.layerweights` 는 버전 2(텍셀마다 여덟 바이트)이고 버전 1(넷)은 읽을 때 4..7 을 0 으로 채워 이관한다(저장은 늘 2, 지형 상태 저장 형식은 버전 3). 지형은 스플랫 텍스처를 평면마다 하나씩 둘 만들고(`getCPUSplatPath(plane)`) 머티리얼에 `splatMap` · `splatMap1` · `layer4Color..layer7Color` · `layerDetail1` · `layerTriplanar1` 을 싣는다. 임포터는 `x_splat.png` 옆의 `x_splat1.png`(같은 크기 RGBA, 없으면 0)를 레이어 4..7 로 읽는다(임포터 버전 2). 브러시 · 배치 규칙 · 절벽 레이어의 범위는 0..7. 셰이더(`terrain.hlsl` 의 여덟 레이어 섞기)는 패치 `ShaderPatches/05-EnvironmentRemainders-E3E4-Terrain.patch`. `SurfaceLayerWeightsTest` · `TerrainLayersTest` · `TerrainTest`(옛 버전 이관) · `PlacementRuleTest` · `HeightfieldImporterTest` · `TerrainBrushTest` |
| E4 | **들어감** — 지오모프. `TerrainComponent::setGeomorph`(`_bGeomorph`)를 켜면 쌍둥이 머티리얼 `terrain_geomorph.material`(`MATERIAL_VERTEX_DEFORM`)을 쓰고 `TerrainMeshBuilder::applyGeomorph` 가 정점의 색 · 탄젠트 칸에 (거친 LOD 높이와의 차이, 섞기 시작 · 끝 거리, 청크 상자, 중앙 샘플 높이)를 굽는다. 정점 셰이더가 카메라 거리로 섞는 비율을 구해 높이를 거친 LOD 메시의 높이로 옮긴다. 섞기 구간은 교체 임계값의 0.6 ~ 0.9 배(`kMorphStartRatio` · `kMorphEndRatio` = 1 - `kLODHysteresis`)라 LOD 가 바뀌는 순간 이미 같은 모양이다. 가장 거친 LOD 는 섞지 않고 지오모프 머티리얼은 깊이 프리패스에서 빠지며 그림자는 섞지 않고 그린다. `TerrainGeomorphTest`. 셰이더는 E3 와 같은 패치 |

E2 와 E3 · E4 의 셰이더는 소스를 바꾸므로 패치로 넘긴다(`CheckShaderStamps`). E3 · E4 의 C++ 와 머티리얼은 패치와 **함께** 적용해야 앱이 뜬다(새 프로퍼티 이름이 셰이더와 같아야 한다).

**남은 확인(통합에서 확인):** 매니페스트 id `spline-river-path-tests` — 빌드 뒤 `SplineRiverPathTest` · `WaterTest` 필터. 패치 05 는 통합(integF)이 적용해 구웠다 — 지형이 그려지는지(여덟 레이어 · 지오모프)는 GPU 화면으로 본다(GPU 확인 대기). 지오모프를 켠 지형 시나리오는 아직 없다.

## 2. 함정

머티리얼 텍스처 칸 16 — 에뮬 슬롯은 Vulkan t 밴드 뒤 t16..t31 이라 SRV 띠(지금 t0..t12)가 밴드 끝까지 자라도 밀리지 않고 DX11 은 정적 머티리얼 샘플러로 읽는다(옛 D2-6.3, 정본은 `ShaderBindingSlots.h` 헤더).
