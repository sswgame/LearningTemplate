# 프리로딩 · LOD · 사전 준비

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-7 · 진행: 10/11

컷이나 레벨 전환에서 LOD · 텍스처 · 셰이더가 바뀌는 게 보이지 않게 미리 로드하고 준비한다. 아래가 들어갔다.

- 프리로드 세트: `PreloadSet`(`Engine/Resource/PreloadSet.h`) — 경로 목록을 동시 요청 상한(프레임 예산) 안에서 스트리밍 큐에 요청하고 진행률 · 완료 · 실패 목록을 낸다.
- 에셋 고정: `PreloadSet::enablePinning` 을 부르면 읽기에 성공한 경로를 그 종류의 캐시에 요청하고 토큰을 보관한다(`AssetManager::pinAsset` → `IAssetCache::pinAsset`). 비용은 읽은 바이트 수이고 예산을 넘으면 마지막으로 쓴 때가 가장 오래된 것부터 놓는다(`AssetPinSet`, LRU). 텍스처는 디바이스가 있어야 요청하므로 그것을 쓰는 머티리얼의 고정이 붙든다.
- 프리팹 풀: `PrefabPool`(`GameFramework/Base/World/Lifetime/PrefabPool.h`) — 돌려받은 인스턴스를 끄고 쉬게 했다가 켜서 다시 내준다(상한 넘으면 지움, `prewarm`).
- 컷 준비의 LOD: `CameraManagerComponent` 가 대기 뷰의 카메라를 프레임마다 `AnimationSystem::setPendingLODView` · `MeshLODSystem::setPendingView` 로 넣는다.
- 메시 LOD: 모델 임포트 규칙의 `lods`(단계마다 삼각형 비율 · 화면 크기 임계값 · 허용 오차)로 `meshopt_simplify` 가 단계를 만들어 `.mesh` 의 `LODS` 덩어리에 싣는다(LOD 없는 파일은 바이트 그대로). `Mesh` 는 LOD 0 을 CPU 정점으로 두고(물리 · 내비 · 피킹은 LOD 0) 낮은 단계를 정점 흐름 뒤에 연결한다. 씬 서비스 `MeshLODSystem` 이 프레임마다 화면 크기로 단계를 고르고(고운 쪽은 10 % 여유) 렌더 수집이 그 단계의 정점 범위로 배치를 나눈다. 모프 · 구간이 있는 메시는 LOD 를 받지 않는다.
- PSO 미리 만들기: 씬에 있는 머티리얼은 렌더러가 배치마다 PSO 를 만들므로 이미 준비돼 있고, 고정한 머티리얼(아직 씬에 없는 다음 레벨 · 컷의 것)은 렌더러가 기록 전에 보기 모드의 불투명 또는 투명 패스마다 등장 디더 변형까지 PSO 를 미리 만든다(`FrameRenderer::warmPinnedMaterialPSOs`).
- 텍스처 밉 스트리밍: 비동기로 읽는 텍스처는 아래쪽 밉(짧은 변 64 근처)만 먼저 업로드하고 렌더 스레드가 한 프레임에 둘씩 위쪽 밉까지 새 GPU 텍스처 · SRV 로 바꿔 끼운다(`TextureCache::updateStreaming`). 옛 자원은 이미 만든 패킷이 끝날 때까지 미룬다. 시나리오 스크린샷은 위쪽 밉이 다 들어올 때까지 기다린다(`countLoadsInFlight`). `-gv_textureStreaming=0` 이면 처음부터 밉 전부.
- 자동 수집(P8): 쿠킹이 씬마다 옆에 `<씬>.preload.json` 을 굽는다(`ScenePreloadList`, `SceneCooker::cookAllScenes` — 쿠킹 기록은 작업 하나에 출력 하나라 SCN1 에 싣지 않고 두 번째 작업으로 돈다). 목록은 씬 문서를 런타임과 같은 길로 지어(프리팹 스폰 · 중첩까지) 모든 컴포넌트의 에셋 경로 프로퍼티(`PROPERTY( AssetPath )` · `AssetType`)를 리플렉션으로 읽어 모은 것이다(`ComponentAssetPathUtil`). `SceneManager::requestLoadFuture` 가 로드를 띄울 때 목록이 있으면 `PreloadSet`(고정 켬)으로 열고 `updateTransitions` 가 진행하며 로드가 끝난 뒤 `gv_scenePreloadHoldFrames` 프레임 붙들고 놓는다. 진행률은 `SceneManager::tryGetLoadProgress` 이고 로딩 화면의 `Progress` 슬라이더가 그린다. 끄기 `gv_scenePreload`. 쿠킹 전 Dev 실행은 목록이 없어 에셋을 잡을 때 읽는다. 머티리얼이 쓰는 텍스처는 싣지 않는다. 로딩 화면 팁은 gamesettings `loadingTips`(`*.loadingtips.xml`)가 채우고 글은 현지화 프로젝트 `assetRules` 로 수집한다.
- 시퀀서 사전 스폰(P8): 시퀀스 항목 `Spawn`(프리팹 `_prefab`, 이름 `_targetObject`)이 구간 시작 프리롤(`_prerollFrames`) 앞에 프리팹을 꺼 둔 채 스폰해 첫 포즈 · 머티리얼 · PSO 를 준비하고 구간 동안 켜며 끝나거나 멈추면 지운다(`SequenceTimelineUtil::applySpawns` · `releaseSpawns`).
- 텍스처 밉 요청 우선순위(P10): `TextureStreamingSystem`(씬 서비스, `FlushEnd`)이 위쪽 밉을 기다리는 텍스처가 있을 때만 메시 컴포넌트마다 `MeshLODSystem` 과 같은 화면 크기를 구해 그 머티리얼의 텍스처에 가장 큰 값을 `TextureCache::noteScreenSize` 로 알리고 `updateStreaming` 이 그 내림차순(알리지 않은 것은 0, 같으면 먼저 읽힌 순)으로 올린다. UV 밀도는 모르므로 화면 크기만 쓴다. 끄기 `gv_textureStreamByScreenSize`.

## P9 GPU 인스턴스 LOD 와 크로스페이드 (남음 — 셰이더)

지금은 컴포넌트마다 CPU 에서 고른다(`MeshLODSystem`). 단계가 바뀌면 배치 키가 달라져 그 프레임은 다시 나누고 정렬한다(단계가 자주 바뀌는 큰 씬에서는 비용). GPU 컬링이 인스턴스마다 화면 크기로 단계를 고르고(`gpucull.hlsl` 이 단계별 간접 인자 구간에 압축) 단계 사이는 디더 크로스페이드(머티리얼 셰이더, 등장 디더와 같은 패턴)로 넘기며, 컷 프레임과 대기 뷰는 크로스페이드를 끈다.
**못 하는 근거**: 셰이더 소스를 커밋하면 커밋 훅이 빌드한 `App --cook-shaders` 로 전 백엔드 바이너리를 굽는데 작업자는 빌드하지 않아 셰이더 변경을 커밋할 수 없다. **풀리는 조건**: 빌드한 App 이 있는 통합/셰이더 담당이 `gpucull.hlsl` · 머티리얼 디더 크로스페이드 · 간접 인자 구간(CPU 쪽 배치 나눔 제거)을 한 묶음으로 쓰고 쿠킹한다(CPU 쪽 선행 없음 — 단계 임계값은 `Mesh::selectLOD` 가 이미 정한다).
**2026-10-28 착수 조사(구현 보류)**: 빌드 · 테스트 없이 건드리면 안 되는 불변식이 겹쳐 한 묶음 코드를 쓰지 않았다. 통합이 처음 볼 곳: ① `GPUSceneSnapshot::appendBatch` 가 배치 인스턴스 구간이 앞 배치 끝에서 빈틈없이 이어진다고 단언하고(`computeInstanceEnd`, `getOpaqueInstanceCount` 가 그것으로 불투명 수를 읽는다) 가시 목록 버퍼는 인스턴스 수 길이다(`GPUScene.cpp` `_visibleInstances.ensureCapacity( …, instanceCount )`) — 단계마다 구간이 따로 필요하면 단계 수만큼 곱한 용량 또는 그룹 안 단계 구간 뒤 압축이 정해야 한다. ② `GPUBatchInfo`(32 바이트) 는 `_pad2` 한 칸만 남고(`batchdata.hlsli` · `GPUScene.h` 가 같이 바뀐다) `InstanceData` 는 `_reserved1` 한 칸이 남는다 — 단계 그룹(첫 배치 번호 · 단계 수 · 임계값 표)은 별도 표가 필요하다. ③ 배치를 나누는 지점은 `GPUSceneBuilder` 의 `DrawCandidate::hasSameBatchKey`(메시 포함 — 현재 CPU 가 고른 단계의 메시 구간이 키에 들어 있다)이고, 같은 키를 쓰는 증분 경로(`refreshInstancesInPlace` · `swapSingleInstanceBatchMeshes` · 투명 꼬리 정렬 `GPUTransparentSorter`)가 모두 새 그룹 구조를 따라야 한다. ④ 스키닝 · 모프 · VAT 는 단계마다 정점 풀 구간이 달라(`GPUMeshVertexPool` 이 LOD 정점을 뒤에 잇는다) 첫 정점만 다르게 하는 것으로는 안 되고 `_sectionVertexOffset` · `_morphVertexBase` 도 단계별이어야 한다 — 1차는 정적 불투명 메시만 그룹으로 만들고 나머지는 지금처럼 CPU 단계로 남기는 것이 현실적이다. ⑤ 크로스페이드는 전환 중인 인스턴스를 두 단계 목록에 모두 쓰고(디더 방향 반대) 컷 프레임 · 대기 뷰는 한쪽만 쓰므로 `gpucull.hlsl` 의 가시 기록이 인스턴스당 둘이 될 수 있다.

## P10 그림자 (끝)

대기 뷰 기준 캐스케이드를 미리 맞출 상태가 없다. 캐스케이드는 매 프레임 뷰-투영에서 새로 맞추고(`ShadowSetupUtil::populateFrame` — 이력 · 캐시 없음) 컷으로 주 시점이 바뀌어도 같은 뷰 타깃을 쓴다. 미리 데울 것은 에셋과 PSO 뿐이다.

## P11 확인

- `preload-lod-tests`: `AssetStreamingTest.*`(PreloadSet · PinSet) · `PrefabPoolTest.*` · `AnimationLODTest.PendingViewWarmsUnitsOfTheNextCut` · `MeshAssetTest.LODLevelsRoundTripAndFollowLODZero` · `MeshAssetTest.MeshLODSystemPicksLevelsByScreenSize` · `ModelImporterTest.LODRuleSimplifiesStaticMeshIntoLevels` · `ScenePreloadListTest.*` · `SceneCookTest.*`(작업 수가 목록만큼 늘었다) · `SequencerTest.Spawn*` · `LoadingScreenTest.*`.
- `preload-lod-gpu-tests`: `RenderPassGPUTest.AsyncTextureStreamsTopMipsAfterBase` · `StreamingUploadsTheTextureNotedAsLargestOnScreenFirst`.
- `preload-lod-visual-review`(사람 눈): 시퀀서 컷 트랙의 프리롤이 있는 시나리오로 컷 첫 프레임의 캐릭터 포즈가 튀지 않는지, LOD 모델을 `-gv_meshLodForce=N` 으로 단계마다 캡처해 단계가 그려지는지(데이터에 LOD 가 든 실제 모델과 스폰 항목을 쓴 시퀀스가 아직 없다).
