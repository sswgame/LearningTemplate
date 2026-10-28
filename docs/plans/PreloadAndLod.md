# 프리로딩 · LOD · 사전 준비

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 진행: 11/11

컷이나 레벨 전환에서 LOD · 텍스처 · 셰이더가 바뀌는 게 보이지 않도록 미리 로드하고 준비한다. P9(GPU 인스턴스 LOD)까지 코드가 들어갔다. 남은 것은 셰이더 재쿠킹과 눈 확인이다.

## 진행

| 단위 | 내용 | 상태 |
|---|---|---|
| P1 | 프리로드 세트 `PreloadSet` — 경로 목록을 동시 요청 상한 안에서 스트리밍 큐에 요청, 진행률 · 완료 · 실패 목록 | 끝 |
| P2 | 에셋 고정 `PreloadSet::enablePinning` — 읽은 바이트 수 비용, 예산 초과 시 가장 오래 안 쓴 것부터 놓는다(`AssetPinSet`, LRU) | 끝 |
| P3 | 프리팹 풀 `PrefabPool` — 돌려받은 인스턴스를 끄고 쉬게 했다가 다시 내준다(`prewarm`) | 끝 |
| P4 | 컷 준비 LOD — 대기 뷰 카메라를 프레임마다 `AnimationSystem` · `MeshLODSystem` 에 넣는다 | 끝 |
| P5 | 메시 LOD — 임포트 규칙 `lods` → `meshopt_simplify` → `.mesh` 의 `LODS` 덩어리, `MeshLODSystem` 이 화면 크기로 단계 선택 | 끝 |
| P6 | PSO 미리 만들기 — 고정한 머티리얼의 PSO 를 기록 전에 만든다(`FrameRenderer::warmPinnedMaterialPSOs`) | 끝 |
| P7 | 텍스처 밉 스트리밍 — 아래쪽 밉 먼저, 위쪽 밉은 프레임당 둘씩 교체(`TextureCache::updateStreaming`) | 끝 |
| P8 | 자동 수집(`<씬>.preload.json`)과 시퀀서 사전 스폰(`Spawn` 항목) | 끝 |
| P9 | GPU 인스턴스 LOD 와 디더 크로스페이드 — ① 가시 구간 · 인스턴스 구간 분리 ② LOD 그룹 + 컬링 컴퓨트 단계 선택 ③ 상보 디더 크로스페이드 · 전환 상태 | 끝(셰이더 재쿠킹 · 눈 확인 필요) |
| P10 | 텍스처 밉 요청 우선순위(`TextureStreamingSystem`), 그림자는 미리 맞출 상태가 없어 불필요 | 끝 |
| P11 | 확인 — 테스트 묶음 작성, 눈 확인은 남음 | 끝 |

## 설계 요약

- **에셋 고정.** 텍스처는 디바이스가 있어야 요청하므로 그것을 쓰는 머티리얼의 고정이 붙든다.
- **메시 LOD.** `Mesh` 는 LOD 0 을 CPU 정점으로 두고(물리 · 내비 · 피킹은 LOD 0) 낮은 단계를 정점 흐름 뒤에 잇는다. 고운 쪽 전환은 10 % 여유를 둔다. 모프 · 구간이 있는 메시는 LOD 를 받지 않는다. LOD 없는 파일은 바이트 그대로다.
- **PSO.** 씬에 있는 머티리얼은 렌더러가 배치마다 PSO 를 만들어 이미 준비돼 있다. 고정한 머티리얼(다음 레벨 · 컷)만 등장 디더 변형까지 미리 만든다.
- **밉 스트리밍.** 옛 GPU 자원은 이미 만든 패킷이 끝날 때까지 미룬다. 시나리오 스크린샷은 위쪽 밉이 다 들어올 때까지 기다린다. 끄기 `-gv_textureStreaming=0`.
- **자동 수집.** 쿠킹이 씬마다 옆에 `<씬>.preload.json` 을 굽는다. 씬 문서를 런타임과 같은 길로 지어(프리팹 스폰 · 중첩 포함) 컴포넌트의 에셋 경로 프로퍼티를 리플렉션으로 모은다. `SceneManager::requestLoadFuture` 가 목록으로 `PreloadSet`(고정 켬)을 열고, 로드가 끝난 뒤 `gv_scenePreloadHoldFrames` 프레임 붙들었다 놓는다. 진행률은 `tryGetLoadProgress` 로 로딩 화면 슬라이더가 그린다. 끄기 `gv_scenePreload`. 머티리얼이 쓰는 텍스처는 싣지 않는다. 쿠킹 전 Dev 실행은 목록이 없어 에셋을 잡을 때 읽는다.
- **시퀀서 사전 스폰.** `Spawn` 항목이 구간 시작 프리롤 앞에 프리팹을 꺼 둔 채 스폰해 첫 포즈 · 머티리얼 · PSO 를 준비하고, 구간 동안 켜며 끝나면 지운다.
- **밉 요청 우선순위.** 위쪽 밉을 기다리는 텍스처가 있을 때만 메시 컴포넌트마다 화면 크기를 구해 텍스처에 가장 큰 값을 알리고, 큰 것부터 올린다. UV 밀도는 모르므로 화면 크기만 쓴다. 끄기 `gv_textureStreamByScreenSize`.
- **그림자(P10).** 캐스케이드는 매 프레임 뷰-투영에서 새로 맞추고 이력 · 캐시가 없으므로 미리 데울 것은 에셋과 PSO 뿐이다.

## P9 — GPU 인스턴스 LOD 와 크로스페이드

컬링 컴퓨트(`gpucull.hlsl`)가 인스턴스마다 화면 크기로 LOD 단계를 고르고, 단계가 바뀐 인스턴스는 상보 디더로 두 단계를 겹쳐 그린다. 더는 CPU 가 컴포넌트마다 단계를 골라(`MeshLODSystem`) 단계가 바뀔 때 배치를 다시 나누고 정렬하지 않는다.

**구조(구현함).**
- **가시 구간과 인스턴스 구간은 다른 축이다.** `GPUMeshBatch::_instanceBase` · `_instanceCount` 는 인스턴스 배열의 구간, `_visibleBase` · `_visibleCapacity` 는 컬링이 압축해 쓰는 가시 목록의 구간이다. `appendBatch` 가 두 구간을 따로 단언하고(`computeInstanceEnd` · `computeVisibleEnd`), 가시 목록 · 인스턴스 슬롯 스트림 · 간접 인자의 시작 인스턴스 · `instancesort.hlsl` 이 가시 구간을 읽는다. 정점 셰이더의 슬롯 범위 검사는 `g_SwVisibleSlotCount`(PassCB)로 한다. LOD 그룹이 없으면 두 구간은 같다.
- **LOD 그룹.** 불투명 · 스킨 없는 메시가 LOD 를 가지면(`Mesh::getLODs`, 단계 수 `kGPULodMaxLevels` 이하) 머리 배치(단계 0, 메시 전체) 뒤에 단계 배치가 이어진다. 같은 메시 · 머티리얼에 정점 구간만 단계의 것이다. 인스턴스는 머리 배치에 한 번만 있고(`_meshBatchIndex` = 머리), 낮은 단계 배치는 인스턴스 구간이 비고(`_instanceCount` 0) 가시 구간만 그룹의 인스턴스 수만큼 있다. 그룹 여부는 후보의 `_lodContentID`(배치 키)이고 메시가 다시 읽혀 단계가 바뀌면 내용 번호가 달라 다시 나뉜다.
- **그룹 표 = 배치 표 행.** 별도 표를 두지 않고 배치 표(`GPUBatchInfo`, 32 B → 40 B)에 두 칸을 더했다. `_pad2` 는 `_visibleBase` 가 되었고, 머리 행의 `_lodLevelCount`(단계 수)와 단계 k 행(머리 + k)의 `_lodScreenSize`(이 단계로 넘어가는 임계값)다. 임계값이 단계마다 하나라 단계 행이 이미 있는 배치 표에 얹는 것이 추가 바인딩 · 업로드 없이 가장 작다. `InstanceData` 의 남은 칸은 쓰지 않았다 — 인스턴스에서 그룹을 찾는 길은 `_meshBatchIndex`(머리 배치 번호)로 충분하다.
- **단계 선택.** 컬링이 절두체와 가림을 통과한 인스턴스의 화면 크기를 CPU `AnimationLODUtil::computeScreenSize` 와 같은 식으로 구하고(`computeProjectionTerms` 가 투영 항을 한 곳에서 뽑는다), `Mesh::selectLOD` 와 같은 규칙으로 단계를 고른다. 고운 쪽으로 돌아갈 때는 `MeshLODSystem::kRefineHysteresis` 와 같은 10 % 여유를 둔다(상태가 있어야 하므로 크로스페이드 상태 버퍼와 함께). 뷰마다 자기 카메라로 고르고(추가 뷰는 자기 카메라, 그림자 슬롯은 주 카메라) 절두체 밖 가림 물체도 거리에서 나온 값으로 고른다.
- **크로스페이드.** 인스턴스마다의 전환 상태(`g_LodState`, 뷰마다 하나, 지금 단계 · 직전 단계 · 진행 0..255)를 컬링이 진행시키고, 전환 중이면 두 단계 배치에 모두 쓴다. 가시 항목의 위쪽 5비트가 상보 디더 코드다(직전 단계 비트 + 진행 L = 1 ~ 15, 아래 27비트가 인스턴스 번호). 정점 셰이더가 `InstanceData::load` 에서 코드를 등장 비율로 바꿔 `Appearance::clipDither` 가 읽는다 — 새 단계는 `L / 16`, 직전 단계는 `L / 16 - 1`(음수는 상보 패턴, 문턱 >= `L / 16`). 4x4 Bayer 문턱이 16 단계라 두 단계가 겹치지도 비지도 않고, 깊이 프리패스 · 그림자 · 기본 패스가 같은 픽셀을 버린다. 크로스페이드가 켜져 있으면 그룹의 모든 인스턴스가 디더 변형(`DrawCandidate::_bLodDither`, 퍼뮤테이션 해시)이라 평소에도 픽셀 버리기는 없고(appearance 1) 전환 중에만 버린다. 등장 디더 중인 인스턴스는 전환을 건너뛰고 바로 바꾼다. 시간은 `gv_lodCrossfadeSeconds`(기본 0.25 초, 0 이하면 끔), 진행량은 렌더 프레임 간격(벽시계, 한 프레임 0.1 초 상한)에서 센다.
- **전환 상태를 비우는 때.** 인스턴스 번호를 새로 매기는 빌드(`GPUSceneSnapshot::_instanceLayoutGeneration`), 상태 버퍼를 새로 만든 프레임, 뷰의 첫 프레임과 컷(`RenderViewSettings::_bCut`)이다 — 그 프레임은 크로스페이드 없이 단계를 바로 맞춘다.
- **CPU 선택이 남는 곳(선언된 분기).** (1) GPU 컬링 능력이 없는 백엔드(`RHICapabilities::_bGPUCulling` — DX11)는 `GPUSceneBuilder::setGPUInstanceLodEnabled` 가 꺼져 그룹이 없고 `MeshLODSystem` 이 그대로 고른다. (2) 이 프레임에 단계를 고정했거나(`setForcedLOD` · `gv_meshLodForce`) 대기 뷰(컷 준비)가 있거나 뷰를 아직 받지 못했으면(`MeshLODSystem::needsCPUSelection`) 그 프레임은 그룹을 만들지 않는다(바뀌면 후보를 버려 다시 모은다). (3) 투명은 그룹이 아니다 — `instancesort.hlsl` 이 배치 안 인스턴스 번호 순서로만 정렬해 단계 배치로 나누면 단계 사이 블렌딩 순서가 깨지고 디더 대상도 아니다(언리얼 · 유니티도 투명은 프리미티브 단위 정렬이다). 모프 · VAT · 구간 메시는 `Mesh::setLODs` 가 LOD 자체를 거절해 그룹 대상이 없다. 스킨드 메시와 인스턴스 배치(식생)는 그룹이다 — 가시 항목의 3비트가 LOD 단계를 들어 정점 셰이더(`InstanceData::load`)가 `_meshBatchIndex` 를 머리 + 단계로 바꾸므로 정점 풀 · 모프(스킨) 시작을 그리는 단계 배치 행에서 읽는다. 인스턴스 번호는 24비트(1677만)다. 끄기 `-gv_gpuInstanceLod=0`.
- **증분 경로.** `refreshInstancesInPlace` 는 인스턴스 값만 갱신해 배치 구성을 건드리지 않는다. `swapSingleInstanceBatchMeshes` 는 그룹 머리 배치를 받지 않는다(`_lodContentID` 가 `hasSameBatchKeyExceptMesh` 에 들어 있고 머리는 `_lodLevelCount` 로 거른다). 투명 꼬리 재방출 · `GPUTransparentSorter` 는 인스턴스 축만 쓰고, 단계 배치가 원소 표에 빈 구간으로 나란히 있어 `truncateToBatches` 가 맞는다.

**설계 결정(상용 엔진과 같은 선).** 언리얼 Nanite 이전의 GPU instanced LOD(HISM 의 인스턴스마다 LOD 선택 + 디더 LOD 전환)와 유니티 GPU Resident Drawer(GPU 컬링이 인스턴스를 LOD 별 목록에 나눠 쓴다)처럼 선택은 GPU 컬링에서, 전환은 상보 디더로 한다. 둘 다 크로스페이드를 인스턴스별 상태로 들고 컷에서 끊는 점도 같다. 다른 점은 그룹 표를 따로 두지 않고 배치 표 행에 얹은 것(위)과, 스킨 · 모프 메시는 변형 풀 구간이 단계마다 달라 CPU 선택으로 남긴 것이다(HISM 도 스켈레탈은 인스턴싱 밖이다).

**남은 것 · 확인.**
- 셰이더(`gpucull` · `instancesort` · `batchdata` · `binding` · `instancedata`)를 바꿨다. `App --cook-shaders` 로 다시 쿠킹해야 하고, 쿠킹 전에는 쿠킹된 배치 표 원소가 32 B 라 C++ 의 40 B 와 어긋난다. dxc 로 DX12 · Vulkan · GL 변형이 문법 오류 없이 도는 것은 확인했다(`gpucull` · `instancesort` · 등장 디더 변형이 있는 `highlightoutline`). FXC(DX11)는 컬링을 돌리지 않아 `gpucull` 의 FXC 변형만 확인하지 못했다.
- 눈 확인(남음): LOD 가 든 모델로 `-gv_gpuInstanceLod=0` 과 `1` 의 단계가 같은지(`-gv_meshLodForce=N` 캡처와 비교), 카메라를 움직여 단계가 바뀔 때 디더가 깨지지 않는지, 컷에서 전환이 남지 않는지.

## P11 — 확인

- 자동: PreloadSet · PinSet · `PrefabPool` · 애니메이션 LOD 대기 뷰 · `.mesh` LOD 왕복 · LOD 단계 선택 · 임포터 LOD 규칙 · 씬 프리로드 목록 · 씬 쿠킹 · 시퀀서 Spawn · 로딩 화면 테스트와, GPU 의 비동기 텍스처 밉 · 화면 큰 순서 업로드 테스트.
- 눈 확인(남음): 시퀀서 컷 프리롤 시나리오로 컷 첫 프레임 포즈가 튀지 않는지, `-gv_meshLodForce=N` 으로 단계마다 캡처해 그려지는지, P9 의 GPU 단계 선택과 크로스페이드(위 P9 절). LOD 가 든 실제 모델과 스폰 항목을 쓴 시퀀스가 아직 없다.
