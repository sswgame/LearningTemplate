# 환경(지형 · 식생 · 물)의 남은 것

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 진행: 3/4

지형 · 식생 · 물의 남은 일 넷이다. E1 · E3 · E4 는 코드가 들어갔고 E2(굴절)는 절반이다. 전부 **빌드 · 셰이더 구움 · GPU 화면 확인 전**이다. 계약(지형 충돌 · 부력 포함)은 `Source/Engine/Environment` 각 헤더 주석에 있고, 지형 LOD 교체 · 쓰기 비용은 [TerrainEditCost](TerrainEditCost.md), 물속 안개 값은 `WaterBodyComponent::findUnderwaterFog` 가 준다.

| 단위 | 내용 | 상태 |
|---|---|---|
| E1 | 강 경로를 스플라인에서 받는다 | 들어감 |
| E2 | 반투명 패스가 장면 색 사본을 받는 굴절 | 절반(굴절만), 화면 공간 두께 남음 |
| E3 | 지형 레이어 여덟 | 들어감 |
| E4 | 지오모프(LOD 교체 때 높이를 부드럽게 섞기) | 들어감 |

## E1 강 경로

`SplineComponent` 는 GameFramework 에 있어 Engine 의 `WaterBodyComponent` 가 읽을 수 없다(층 방향). 그래서 GameFramework 쪽 어댑터 `SplineRiverPathComponent` 가 같은 오브젝트의 스플라인을 `sampleUniform` 으로 샘플해 `WaterBodyComponent::setRiverPathFromSource` 에 넘긴다.

- 어댑터가 있으면 `_bRiverPathDriven` 이 켜지고 `_listRiverPoint` 는 인스펙터가 막는다(정본은 스플라인 하나).
- 곡선 번호나 물 위치가 바뀌면 다음 틱에 다시 샘플하고 수면을 다시 만든다.
- 물이나 스플라인이 없으면 오류를 한 번 남기고 넘기지 않는다(폴백 없음). 테스트 `SplineRiverPathTest`.

## E2 굴절

- 들어감: `RenderPassInputRole::SceneColorCopy`(에뮬 슬롯 1 을 G버퍼 알베도 · AO 와 나눠 씀)와 패스 종류 `SceneColorCopy`(`blitTexture` 로 장면 색 복사). 포워드 파이프라인 넷이 Sky 뒤 · Transparent 앞에 이 패스를 선언하고 Transparent 가 그 첨부를 입력으로 받는다. 사본이 없는 파이프라인(2D · 디퍼드)은 굴절 없이 알파 블렌드다. 테스트 `RenderPassTest.SceneColorCopyPassFeedsTransparentRefraction`.
- 셰이더(`binding.hlsli` · `water.hlsl`)는 패치 `ShaderPatches/06-EnvironmentRemainders-E2-Water.patch` 로 넘긴다. C++ 와 파이프라인은 패치 없이도 앱이 뜬다.
- 남은 일: 화면 공간 두께(장면 깊이 사본과 그 복사 패스, 지금은 지형 깊이를 정점에 굽는다), 디퍼드 파이프라인의 패스 선언, 구운 뒤 GPU 화면에서 굴절 세기(`kRefractionStrength` · `kRefractionFullDepth`)를 눈으로 맞추기.

## E3 레이어 여덟

- 값 형식 `SurfaceLayerWeights`(float4 둘, 개수 정본 `kLayerCount`)를 높이장 · 배치 규칙 · 브러시가 함께 쓴다.
- `.layerweights` 는 버전 2(텍셀마다 8 바이트)이고 버전 1(4 바이트)은 읽을 때 4..7 을 0 으로 채워 이관한다. 지형 상태 저장 형식은 버전 3.
- 지형은 스플랫 텍스처를 평면마다 둘 만들고, 임포터는 `x_splat.png` 옆의 `x_splat1.png` 를 레이어 4..7 로 읽는다.
- 테스트: `SurfaceLayerWeightsTest` · `TerrainLayersTest` · `TerrainTest` · `PlacementRuleTest` · `HeightfieldImporterTest` · `TerrainBrushTest`.

## E4 지오모프

`TerrainComponent::setGeomorph` 를 켜면 쌍둥이 머티리얼 `terrain_geomorph.material` 을 쓰고, `TerrainMeshBuilder::applyGeomorph` 가 정점에 (거친 LOD 와의 높이 차, 섞기 시작 · 끝 거리, 청크 상자, 중앙 샘플 높이)를 굽는다. 정점 셰이더가 카메라 거리로 높이를 거친 LOD 쪽으로 옮긴다.

- 섞기 구간은 교체 임계값의 0.6 ~ 0.9 배라 LOD 가 바뀌는 순간 이미 같은 모양이다.
- 가장 거친 LOD 는 섞지 않고, 지오모프 머티리얼은 깊이 프리패스에서 빠지며, 그림자는 섞지 않고 그린다. 테스트 `TerrainGeomorphTest`.

## 남은 일 요약

- 셰이더 소스를 바꾸므로 E2 와 E3 · E4 는 패치(`05-…E3E4-Terrain.patch` · `06-…E2-Water.patch`)로 넘긴다. E3 · E4 의 C++ · 머티리얼은 패치와 **함께** 적용해야 앱이 뜬다(새 프로퍼티 이름이 셰이더와 같아야 한다).
- 첫 일: 빌드 뒤 `SplineRiverPathTest` · `WaterTest` 를 돌리고, 지형 여덟 레이어 · 지오모프와 물 굴절을 GPU 화면으로 본다. 지오모프를 켠 지형 시나리오는 아직 없다.

## 함정

머티리얼 텍스처 칸 16. 에뮬 슬롯은 Vulkan t 밴드 뒤 t16..t31 이라 SRV 띠(지금 t0..t12)가 자라도 밀리지 않고, DX11 은 정적 머티리얼 샘플러로 읽는다(정본 `ShaderBindingSlots.h`).
