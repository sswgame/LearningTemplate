# 프리로딩 · LOD · 사전 준비

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-7 · 진행: 7/11

컷이나 레벨 전환에서 LOD · 텍스처 · 셰이더가 바뀌는 게 보이지 않게 미리 로드하고 준비한다(로드맵 규모). 아래가 들어갔다.

- 프리로드 세트: `PreloadSet`(`Engine/Resource/PreloadSet.h`) — 경로 목록을 동시 요청 상한(프레임 예산) 안에서 스트리밍 큐에 바이트 읽기로 요청하고, 진행률 · 완료 · 실패 목록을 낸다.
- 에셋 고정: `PreloadSet::enablePinning` 을 부르면 읽기에 성공한 경로를 그 종류의 캐시에 요청하고 토큰을 보관한다(`AssetManager::pinAsset` → `IAssetCache::pinAsset`). 메시 · 프리팹은 로드 블록, 머티리얼은 참조 하나와 GPU 초기화 요청이다.
  비용은 읽은 바이트 수이고 예산을 넘으면 마지막으로 쓴 때가 가장 오래된 것부터 놓는다(`AssetPinSet`, LRU). 텍스처는 디바이스가 있어야 요청하므로 직접 고정하지 않고 그것을 쓰는 머티리얼의 고정이 붙든다.
- 프리팹 풀: `PrefabPool`(`GameFramework/Base/World/Lifetime/PrefabPool.h`) — 돌려받은 인스턴스를 끄고 쉬게 했다가 켜서 다시 내준다(상한 넘으면 지움, `prewarm` 으로 미리 만들어 꺼 둠).
- 컷 준비의 애니메이션 LOD · 메시 LOD: `CameraManagerComponent` 가 대기 뷰의 카메라를 프레임마다 `AnimationSystem::setPendingLODView` · `MeshLODSystem::setPendingView` 로 넣는다.
- 메시 LOD: 모델 임포트 규칙의 `lods`(단계마다 삼각형 비율 · 화면 크기 임계값 · 허용 오차)로 `meshopt_simplify` 가 단계를 만들고 `.mesh` 의 `LODS` 덩어리에 싣는다(LOD 없는 파일은 바이트가 그대로).
  `Mesh` 는 LOD 0 을 CPU 정점으로 두고(물리 · 내비 · 피킹은 LOD 0) 낮은 단계를 그릴 정점 흐름 뒤에 연결한다. 씬 서비스 `MeshLODSystem` 이 프레임마다 화면 크기로 컴포넌트의 단계를 고르고(고운 쪽은 10 % 여유),
  렌더 수집은 그 단계의 정점 범위로 배치를 나눈다. 모프 · 구간이 있는 메시는 LOD 를 받지 않는다. 스킨드 메시는 받는다 — 단계마다 스킨 정점(`.mesh` 의 `LSKN`)을 싣고 GPU 스키닝이 낮은 단계 정점도 변형한다([AnimationLodAndCrowd](AnimationLodAndCrowd.md) C4).
- PSO 미리 만들기: 씬에 있는 머티리얼은 렌더러가 배치마다(보이든 아니든) PSO 를 만들므로 대기 뷰에 보일 것은 이미 준비돼 있다. 고정한 머티리얼(아직 씬에 없는 다음 레벨 · 컷의 것)은
  렌더러가 기록 전에 이번 프레임 보기 모드의 불투명 또는 투명 패스마다, 등장 디더 변형까지 PSO 를 미리 만든다(`FrameRenderer::warmPinnedMaterialPSOs`).
- 텍스처 밉 스트리밍: 비동기로 읽는 텍스처는 아래쪽 밉(짧은 변 64 근처)만 먼저 업로드하고, 렌더 스레드가 한 프레임에 둘씩 위쪽 밉까지 새 GPU 텍스처 · SRV 로 바꿔 끼운다(`TextureCache::updateStreaming`).
  옛 자원은 게임 스레드가 해제해 이미 만든 패킷이 끝날 때까지 미룬다. 시나리오 스크린샷은 위쪽 밉이 다 들어올 때까지 기다린다(`countLoadsInFlight`). `-gv_textureStreaming=0` 이면 처음부터 밉 전부다.

## P8 자동 수집과 시퀀서 사전 스폰

- 자동 수집: 쿠킹 때 레벨 · 시퀀스 · 샷이 참조하는 프리팹(`PrefabAsset::collectReferencedPrefabPaths`)과 그 메시 · 머티리얼 · 텍스처를 모아 `<레벨>.preload.json` 으로 쓰고, 레벨 전환이 그것을 `PreloadSet` 으로 연다.
  선행: 쿠킹 기록(`CookStore::cookFile`)은 작업 하나에 출력 하나라, 씬 작업에 두 번째 출력을 더하거나 씬 쿠킹본(SCN1)에 목록을 싣는 형식 결정이 먼저다. 씬 작업이 기록하는 입력 목록(씬이 만들어지며 읽은 파일)이 수집의 출발점이다.
- 시퀀서 사전 스폰: 프리롤 동안 컷의 캐릭터를 `PrefabPool::prewarm` 처럼 꺼 둔 채 만들어 첫 포즈 · 머티리얼을 준비한다. 선행: 시퀀서에 스폰 트랙(스폰 가능 바인딩)이 없다 — 지금은 이미 있는 오브젝트만 바인딩한다.

## P9 GPU 인스턴스 LOD 와 크로스페이드

지금은 컴포넌트마다 CPU 에서 고른다(`MeshLODSystem`). 단계가 바뀌면 배치 키가 달라져 그 프레임은 다시 나누고 정렬한다 — 단계가 자주 바뀌는 큰 씬에서는 비용이다.
GPU 컬링이 인스턴스마다 화면 크기로 단계를 고르고(`gpucull.hlsl` 이 단계별 간접 인자 구간에 압축), 단계 사이는 디더 크로스페이드(머티리얼 셰이더, 등장 디더와 같은 패턴)로 넘긴다. 컷 프레임과 대기 뷰는 크로스페이드를 끈다.
셰이더 변경이라 셰이더 담당이 쿠킹과 함께 한다. 본 LOD 를 메시 LOD 와 묶는 일은 [AnimationLodAndCrowd](AnimationLodAndCrowd.md) L4. HLOD 는 나중.

## P10 화면 크기 밉 요청과 그림자

- 밉 요청: 지금은 먼저 읽힌 텍스처부터 위쪽 밉을 업로드한다. 화면 크기(텍셀 밀도)로 필요한 밉을 정해 대기 뷰에서 보일 것을 먼저 업로드하고, 멀리 있는 것은 아래쪽 밉에 남기며 예산을 넘으면 위쪽 밉을 내린다(언리얼 텍스처 스트리머의 wanted mip · AddViewLocation).
  텍스처마다 그것을 쓰는 메시의 경계 · 머티리얼의 UV 밀도를 모아야 한다(렌더 수집이 머티리얼 원소를 모을 때가 그 위치다).
- 그림자: 대기 뷰 기준 캐스케이드를 미리 맞춘다.

## P11 확인

- 빌드 뒤 `AssetStreamingTest.PreloadSetWalksEveryPathWithinTheBudget` · `AssetStreamingTest.PinSetEvictsLeastRecentlyUsedOverBudget` · `AssetStreamingTest.PreloadSetPinsMeshesUntilReleased` · `PrefabPoolTest` ·
  `AnimationLODTest.PendingViewWarmsUnitsOfTheNextCut` · `MeshAssetTest.LODLevelsRoundTripAndFollowLODZero` · `MeshAssetTest.MeshLODSystemPicksLevelsByScreenSize` · `ModelImporterTest.LODRuleSimplifiesStaticMeshIntoLevels` ·
  `RenderPassGPUTest.AsyncTextureStreamsTopMipsAfterBase` 를 돌린다.
- 시퀀서 컷 트랙의 프리롤이 있는 시나리오로 컷 첫 프레임의 캐릭터 포즈가 튀지 않는지 캡처로 본다. LOD 모델은 `-gv_meshLodForce=N` 으로 단계마다 캡처해 단계가 그려지는지 본다.
