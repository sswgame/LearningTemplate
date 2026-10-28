# 환경(지형 · 식생 · 물)의 남은 것

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 진행: 4/4

지형 · 식생 · 물의 남은 일 넷이다. E1 · E2 · E3 · E4 모두 코드가 들어갔다. 전부 **빌드 · 셰이더 구움 전**이다. 계약(지형 충돌 · 부력 포함)은 `Source/Engine/Environment` 각 헤더 주석에 있고, 물속 안개 값은 `WaterBodyComponent::findUnderwaterFog` 가 준다.

| 단위 | 내용 | 상태 |
|---|---|---|
| E1 | 강 경로를 스플라인에서 받는다 | 들어감 |
| E2 | 반투명 패스가 장면 색 · 깊이 사본을 받는 굴절과 화면 공간 두께 | 들어감 |
| E3 | 지형 레이어 여덟 | 들어감 |
| E4 | 지오모프(LOD 교체 때 높이를 부드럽게 섞기) | 들어감 |

## E1 강 경로

`SplineComponent` 는 GameFramework 에 있어 Engine 의 `WaterBodyComponent` 가 읽을 수 없다(층 방향). 그래서 GameFramework 쪽 어댑터 `SplineRiverPathComponent` 가 같은 오브젝트의 스플라인을 `sampleUniform` 으로 샘플해 `WaterBodyComponent::setRiverPathFromSource` 에 넘긴다.

- 어댑터가 있으면 `_bRiverPathDriven` 이 켜지고 `_listRiverPoint` 는 인스펙터가 막는다(정본은 스플라인 하나).
- 곡선 번호나 물 위치가 바뀌면 다음 틱에 다시 샘플하고 수면을 다시 만든다.
- 물이나 스플라인이 없으면 오류를 한 번 남기고 넘기지 않는다(폴백 없음). 테스트 `SplineRiverPathTest`.

## E2 굴절

- 들어감: `RenderPassInputRole::SceneColorCopy`(에뮬 슬롯 1 을 G버퍼 알베도 · AO 와 나눠 씀)와 패스 종류 `SceneColorCopy`(`blitTexture` 로 장면 색 복사). 포워드 파이프라인 넷이 Sky 뒤 · Transparent 앞에 이 패스를 선언하고 Transparent 가 그 첨부를 입력으로 받는다. 사본이 없는 파이프라인(2D · 디퍼드)은 굴절 없이 알파 블렌드다. 테스트 `RenderPassTest.SceneColorCopyPassFeedsTransparentRefraction`.
- 장면 깊이 사본: 입력 역할 `SceneDepthCopy`(에뮬 슬롯 2 를 G버퍼 노멀 · 블룸과 나눠 씀)와 패스 종류 `SceneDepthCopy`(일반 풀스크린, `scenedepthcopy.hlsl` 이 R32 실수 출력으로 점 샘플 복사). 반투명 패스는 장면 깊이를 깊이 붙임으로 쥐고 있어 읽을 수 없어 `blitTexture`(깊이 복사 보장 없음) 대신 셰이더로 옮긴다. 포워드 파이프라인 넷과 디퍼드 파이프라인이 Transparent 앞에 색 · 깊이 사본 패스를 선언하고 Transparent 가 둘을 입력으로 받는다(디퍼드는 색 사본도 이번에 처음 선언). 2D · 모듈 파이프라인은 사본이 없어 굴절 없는 알파 블렌드다.
- 셰이더는 직접 들어갔다: `binding.hlsli` PassCB `g_SceneColorCopyIndex` · `g_SceneDepthCopyIndex` + 에뮬 슬롯 표(`sampleIndex` 세 변형), `water.hlsl` 굴절 · 두께(깊이 사본이 있으면 장면 월드 위치와의 거리로 두께를 재고 물보다 앞의 물체는 두께 0 · 굴절을 버린다, 없으면 정점에 구운 깊이). 옛 패치 `06-…` 는 지웠고, 거기 남아 있던 에뮬 슬롯 표의 `SW_EMU_SLOT_*` 정리는 [SilentFallbackAudit](SilentFallbackAudit.md) 이 `binding.hlsli` 에 넣었다.
- 화면 공간 두께 상수(`kAbsorptionPerMeter` 0.8 · `kRefractionFullDepth` 1.5 m)는 눈으로 맞추지 않은 초기값이다.

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

- E3 · E4 의 셰이더는 패치(`05-…E3E4-Terrain.patch`)로 넘긴다. E3 · E4 의 C++ · 머티리얼은 패치와 **함께** 적용해야 앱이 뜬다(새 프로퍼티 이름이 셰이더와 같아야 한다). E2 의 셰이더는 이미 들어갔다.
- 첫 일: 빌드 뒤 `SplineRiverPathTest` · `WaterTest` 를 돌린다. 지오모프를 켠 지형 시나리오는 아직 없다.

## 함정

머티리얼 텍스처 칸 16. 에뮬 슬롯은 Vulkan t 밴드 뒤 t16..t31 이라 SRV 띠(지금 t0..t12)가 자라도 밀리지 않고, DX11 은 정적 머티리얼 샘플러로 읽는다(정본 `ShaderBindingSlots.h`).
