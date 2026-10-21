# 프리로딩 · LOD · 사전 준비

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-7 · 진행: 3/7

컷이나 레벨 전환에서 LOD · 텍스처 · 셰이더가 바뀌는 게 보이지 않게 미리 올리고 준비한다(로드맵 규모). 아래 셋이 들어갔다.

- 프리로드 묶음: `PreloadSet`(`Engine/Resource/PreloadSet.h`) — 경로 묶음을 동시 요청 상한(프레임 예산) 안에서 스트리밍 큐에 바이트 읽기로 걸고, 진행률 · 완료 · 실패 목록을 낸다.
  하는 일은 파일 읽기를 앞당기는 것(OS 캐시 · 팩 압축 해제)이다. 에셋 객체를 붙들어 두는 고정은 아래 P4.
- 프리팹 풀: `PrefabPool`(`GameFramework/Base/World/Lifetime/PrefabPool.h`) — 돌려받은 인스턴스를 끄고 쉬게 했다가 켜서 다시 내준다(상한 넘으면 지움, `prewarm` 으로 미리 만들어 꺼 둠).
- 컷 준비의 애니메이션 LOD: `CameraManagerComponent` 가 대기 뷰의 카메라를 프레임마다 `AnimationSystem::setPendingLODView` 로 넣는다 — 그 시점에서 보일 캐릭터가 컷 전부터 포즈를 만든다.

## P4 에셋 고정과 자동 수집

- 고정: 캐시가 약한 참조라 미리 읽은 에셋이 쓰기 전에 사라질 수 있다. 종류마다의 캐시(`MeshCache` · `TextureCache` · `MaterialCache` · `PrefabCache`)에서 `acquire` 한 `shared_ptr` 을 묶음이 쥐고(고정),
  묶음을 놓거나 메모리 예산을 넘으면 오래 안 쓴 것부터 놓는다(LRU). `PreloadSet` 에 "읽은 뒤 고정할 종류" 를 경로 확장자로 고르는 단계를 더한다.
- 자동 수집: 쿠킹 때 레벨 · 시퀀스 · 샷이 참조하는 프리팹(`PrefabAsset::collectReferencedPrefabPaths`)과 그 메시 · 머티리얼 · 텍스처를 모아 `<레벨>.preload.json` 으로 쓴다. 레벨 전환이 그것을 `PreloadSet` 으로 연다.
- 시퀀서 사전 스폰: 프리롤 동안 컷의 캐릭터를 `PrefabPool::prewarm` 처럼 꺼 둔 채 만들어 첫 포즈 · 머티리얼을 준비한다.

## P5 메시 LOD

설계: 임포트(`ModelImporter`)가 `meshopt_simplify` 로 단계를 만든다(비율 · 오차는 모델 임포트 설정 데이터). `.mesh` 에 LOD 구간(정점 시작 · 수 · 화면 크기 문턱)을 싣고 판을 올린다(옛 판은 읽을 때 LOD 0 하나).
GPU 컬링이 인스턴스마다 화면 크기로 단계를 고르고(`gpucull.hlsl` 의 간접 인자), 단계 사이는 디더 크로스페이드(머티리얼 셰이더). 컷 프레임과 대기 뷰는 그 시점의 단계로 바로 넘어간다(크로스페이드 끔).
본 LOD 를 메시 LOD 와 묶는 일은 [AnimationLodAndCrowd](AnimationLodAndCrowd.md) L4. HLOD 는 나중.

## P6 밉 스트리밍과 PSO 미리 만들기

- 텍스처 밉 스트리밍: 지금 `AssetStreamingQueue` 는 에셋 단위다. 낮은 밉부터 올리고 화면 크기로 높은 밉을 요청한다(대기 뷰에서 보일 것을 먼저 — 언리얼 streaming source · AddViewLocation). 텍스처 품질 설정(`gv_textureMipDrop`)은 읽을 때 위 밉을 버리는 것까지다.
- PSO 미리 만들기: 대기 뷰 절두체 안 머티리얼의 PSO 를 컷 전에 만든다(DX12 PSO 생성 히치). 다중 뷰 렌더로 컷 1~2 프레임 전에 대기 뷰를 저해상도로 숨겨 그려 업로드 · PSO 를 데운다.
- 그림자: 대기 뷰 기준 캐스케이드를 미리 맞춘다.

## P7 확인

- 빌드 뒤 `AssetStreamingTest.PreloadSetWalksEveryPathWithinTheBudget` · `PrefabPoolTest` · `AnimationLODTest.PendingViewWarmsUnitsOfTheNextCut` 를 돌린다.
- 시퀀서 컷 트랙의 프리롤이 있는 시나리오로 컷 첫 프레임의 캐릭터 포즈가 튀지 않는지 캡처로 본다.
