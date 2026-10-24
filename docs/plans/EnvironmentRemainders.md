# 환경(지형 · 식생 · 물)의 남은 것 (2026-10-24)

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 옛번호: D2-5, D2-5.2, D2-5.5 · 진행: 1/4

후보 `TerrainWaterVegetationRemainders` 를 계획으로 올린 문서입니다. 들어간 것과 계약(지형 충돌 · 부력 포함)은 `Source/Engine/Environment` 각 헤더 주석에 있습니다.
지형 LOD 교체 · 지형 쓰기의 비용과 식생 부분 계산은 [TerrainEditCost](TerrainEditCost.md) 에 있고, 안개 · 대기는 [RealtimeGlobalIllumination](RealtimeGlobalIllumination.md) R1 ~ R3 이 맡습니다(물속 안개 값은 `WaterBodyComponent::findUnderwaterFog` 가 이미 준다).

## 1. 단위

| 단위 | 내용 | 선행 |
|---|---|---|
| E1 | **들어감**(어댑터 `SplineRiverPathComponent` — 같은 오브젝트의 스플라인 월드 곡선을 `sampleUniform` 으로 샘플해 물 위치 기준 점으로 `WaterBodyComponent::setRiverPathFromSource` 에 넘긴다. 어댑터가 있으면 `_bRiverPathDriven`(읽기 전용 속성)이 켜지고 `_listRiverPoint` 는 `EditCondition` 으로 인스펙터가 막는다. 곡선 번호 `SplineComponent::getPathRevision` 이나 물 위치가 바뀌면 다음 틱에 다시 샘플하고 수면은 틱 뒤로 미뤄 다시 만든다. 물이나 스플라인이 없으면 오류를 한 번 남기고 넘기지 않는다 — 폴백 없음. 테스트 `SplineRiverPathTest`). 강 경로를 스플라인에서 받는다. `SplineComponent` 는 GameFramework(`Base/World/Spline`)에 있어 Engine 의 `WaterBodyComponent` 가 읽을 수 없다(층 방향). 방향을 지키는 길은 GameFramework 쪽 어댑터 컴포넌트(스플라인의 `getWorldPath` 를 일정 간격으로 샘플해 물 소유자의 로컬 좌표로 바꾸고(`_listRiverPoint` 는 소유자 기준이다) 같은 오브젝트의 `WaterBodyComponent::setRiverPath` 를 부른다)다. 정할 것: `_listRiverPoint` 가 직렬화 속성이라 어댑터가 있으면 그 값은 어댑터가 덮는다(정본은 스플라인 하나) — 어댑터가 있을 때 속성을 읽기 전용으로 보이게 한다. 스플라인을 편집하면 물 표면을 다시 만든다 | 없음 |
| E2 | 물의 굴절 · 화면 공간 두께 — 반투명 패스가 장면 색 사본 · 장면 깊이를 입력으로 받는 계약. `RenderPassInputSignature` 에 장면 색 사본 역할을 두고, 파이프라인이 Transparent 앞에 복사 패스를 선언한다(선언이 곧 바인딩). 지금은 지형 깊이를 정점에 굽는다 | 없음 |
| E3 | 레이어 다섯 이상 — 머티리얼 텍스처 칸은 16 개로 늘었지만 `LayerWeightsData::kLayerCount`(4) · `.layerweights` · 스플랫 업로드 · 브러시 · `terrain.hlsl` 이 넷이다. 두 번째 스플랫을 두고 형식 버전을 올린다(옛 파일은 이관 한 번에 읽는다) | 없음 |
| E4 | 지오모프 — 지형 LOD 가 바뀔 때의 튐을 정점 셰이더가 이웃 LOD 높이로 섞어 없앤다 | 없음 |

E2 ~ E4 는 셰이더 소스를 바꾸므로 워크트리에서 커밋하지 못하고 패치로 넘긴다(`CheckShaderStamps`). E1 은 C++ 뿐이라 먼저 착수했다.

**남은 확인(통합에서 확인):** 매니페스트 id `spline-river-path-tests` — 빌드 뒤 `SplineRiverPathTest` · `WaterTest` 필터.

## 2. 함정

머티리얼 텍스처 칸 16 — 에뮬 슬롯은 Vulkan t 밴드 뒤 t16..t31 이라 SRV 띠(지금 t0..t12)가 밴드 끝까지 자라도 밀리지 않고 DX11 은 정적 머티리얼 샘플러로 읽는다(옛 D2-6.3, 정본은 `ShaderBindingSlots.h` 헤더).
