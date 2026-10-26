# 월드 스트리밍(D1-7) — 설계와 구현 단위

> 상태: 측정대기 · 선행: 없음 · 영역: engine · 옛번호: D1-7 · 진행: 7/7

**남은 확인(통합에서 확인):** 매니페스트 id `world-streaming-tests`, `world-streaming-net-tests`, `world-streaming-hitch-measure`, `world-cells-panel-capture`, `cook-scenes-shipping`(3절 남은 일). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

결정의 정본은 [결정 기록](../09_Decisions.md) 5-12 의 `D1-7` 행이다. 이 문서는 그 결정을 구현 단위로 나눈다. `design-reviewer` 검토는 아직 받지 않았다(통합 때 받는다).

## 1. 결정 요약

- 월드는 `Scene` 을 유지하고 셀을 활성 씬에 **더해** 올린다. 셀마다 매니저를 두지 않는다(셀 사이 물리가 끊긴다). `World` 타입은 필요가 확인될 때 만든다.
- 편집은 한 씬으로 한다. 쿠킹이 오브젝트 위치와 격자 설정으로 엔티티를 셀로 나눠 같은 쿠킹본(`.scene.bin`)에 싣는다(셀마다 파일을 따로 두지 않는다 — W3). 셀은 위치로 계산하고 항상 올라 있어야 하는 오브젝트만 "공간 로드 안 함" 으로 표시한다.
- 셀을 넘는 참조는 같은 셀이나 항상 올라 있는 오브젝트만 허용한다. 그 밖은 **쿠킹 오류**로 멈춘다(조용히 null 로 바꾸지 않는다).
- 서버도 플레이어 주변 셀만 스트리밍한다(네트워크 관심 영역 `D7-8` 과 같은 격자). 첫 대상은 WildWest. HLOD 는 메시 LOD 뒤.
- 상용 엔진과의 대응: 언리얼 World Partition 의 런타임 격자(항상 올라 있는 액터는 "Is Spatially Loaded" 끔), 유니티의 추가 씬 로드(`LoadSceneMode.Additive`).

## 2. 선행 C-43 — 정점 · 모프 풀 부분 갱신 (끝)

구간 할당기(`Core/Container/SpanAllocator` — 이웃 합침 · 미룬 반납), `IRHIResourceFactory::updateVertexBufferRegions`(네 백엔드)와 `GPUMeshVertexPool::update` 가 바뀐 메시 구간만 쓰기, `GPUMeshMorphPool` 모프 구간도 같은 구조(칸 번호 고정, 스킨 구간은 모프 용량 뒤). 테스트 `SpanAllocatorTest` · `RHIDeviceTest.VertexBufferRegionUpdateReachesItsDraws` · `RenderPassGPUTest.VertexPoolUploadsOnlyChangedMeshes` · `MorphPoolUploadsOnlyChangedMeshes`.
핵심 규칙: DX12 · Vulkan 의 정점 버퍼는 CPU 가 직접 쓰는 메모리라 GPU 와 순서가 맞춰지지 않으므로 돌려받은 구간은 `kMaxFrameCountInFlight` 번의 `update` 뒤에야 다시 준다. 용량이 모자라거나 조각나면 1.5 배 새 버퍼로 다시 채운다(드물다).
남은 것: 스킨 **원본**(레스트 · 가중치 · 모프 차이)은 원본 집합이 바뀌면 통째로 올린다(캐릭터 종류 단위라 같은 캐릭터 사본의 스트리밍에는 걸리지 않는다). VAT 풀도 같은 모양인지 통합 때 확인한다.

### 측정 (병합은 끝났고 `08_Verification.md` 에 숫자가 아직 없다)

Release 에서(Debug 는 레이스 검출기가 과장한다) 셰이더를 먼저 굽고(`App.exe --cook-shaders`), 프로파일 구간 `RT.GPUScene.vertexPool` · `RT.Deform.rebuildMorphPool` 의 p50 · p99 와 집합이 바뀐 프레임의 최대값을 본다. 워크로드는 NileCity 에서 오브젝트를 스폰 · 제거하는 자동화 시나리오(또는 지형 LOD 교체가 일어나는 카메라 이동)를 풀 갱신 이전 커밋(전)과 main(후)에서 각 3 회, VSync 끔. 기대: 집합이 바뀐 프레임의 최대값이 "전체 정점 바이트" 비례에서 "바뀐 메시 바이트" 비례로 준다(바뀌지 않는 프레임은 같거나 약간 늘 수 있다 — 늘면 원인을 적는다). 결과는 [검증 기록](../08_Verification.md) 에 숫자로 남긴다.

## 3. D1-7 구현 단위 (W1 ~ W7 코드 모두 들어감)

| 단위 | 내용 | 확인 |
| --- | --- | --- |
| W1 | `Engine/Scene/WorldCellGrid` — 위치 → 셀, 로딩 반경 안의 셀, 셀을 넘는 참조 검사. 셀 크기 · 반경은 데이터(`WorldCellGridDef`), 기본값 없음 | `WorldCellGridTest` |
| W2 | 씬 루트의 `<WorldSettings>`(스트리밍 여부 · 격자 · 내리는 여유 · 프레임당 셀 수)와 `GameObject::_bAlwaysLoaded`. 씬 형식 판 2(옛 판 리더 없이 저장소의 씬 20 개를 `Scripts/dev/MoveSceneFormat.py` 로 다시 썼다), SCN1 판 4, 쿠커 버전 2 | `WorldStreamingTest.WorldSettingsRoundTripThroughXMLAndBinary` · `SceneWithoutUsableWorldSettingsIsRejected` |
| W3 | `SceneCooker::splitStreamingCells` — 계층 루트의 자리로 셀을 정하고 늘 올라 있거나 자리가 없는 엔티티는 기본 목록에 둔다. 참조는 저장 경로(`ObjectSaveOptions::_pOutListReferencedID`)로 모으고 셀을 넘으면 쿠킹 실패. 셀 파일을 따로 두지 않고 같은 쿠킹본에 싣는다(쿠킹 기록 · DDC 가 작업 하나에 출력 하나라 따로 굽힌 파일이 섞일 수 있다) | `CookerSplitsEntitiesIntoCells` · `CrossCellReferenceFailsTheCook` |
| W4 | `WorldStreamingSubsystem`(씬 서비스, `DestroyPost` 단계) — 원점 둘레의 셀을 가까운 순으로 프레임당 예산만큼 `Scene::instantiateCell` 로 짓고 반경 + 여유 밖은 `Scene::unloadCell` 로 내린다. 셀 바이트는 비동기 씬 로드 때 함께 올라와 있다 | `CellsFollowStreamingSources` |
| W5 | 서버 · 여러 원점 — `setExternalSources` 로 접속자마다 원점을 넣으면 셀 합집합. 서버 연결은 `MMOReplicator::feedStreamingSources`. 복제기의 관심 영역 해시는 `initialize` 가 받는 `WorldCellGrid` 의 칸이고 나가는 반경이 로딩 반경보다 크면 거절한다 | `NetMMOTest.ObserverPositionsAreTheStreamingSources` · `MMORefusesOutOfRangeSettings` |
| W6 | 에디터: 격자 오버레이(`gv_worldCellDebugDraw`), 개발 명령 `world.cells`, World Cells 창(`WorldCellsPanel` — `SceneCooker::previewStreamingCells` 로 셀마다 엔티티 수와 올림 · 유지 · 내림, 셀을 넘는 참조 목록, 줄을 누르면 그 오브젝트를 고른다) | `CellPreviewPointsAtSceneObjects`, `ux-reviewer` 캡처 |
| W7 | WildWest 초원(`prairie.scene.xml`)을 스트리밍 씬으로(셀 16 m, 반경 32 m, 여유 8 m, 프레임당 2 셀, 카메라 · 해 · 땅 · 기수 · 말 · AI · 목장은 늘 올라 있음) | 데이터만 |

남은 일은 확인뿐이다(통합에서 돌린다, 모두 빌드 미확인).
- 빌드와 테스트 — 매니페스트 id `world-streaming-tests`(`WorldStreamingTest`), `world-streaming-net-tests`(`NetMMOTest`). 씬 픽스처를 판 2 로 고친 `SceneTest` · `SceneAsyncTest` · `PrefabOverridesTest` 와 `GameObjectBenchTest`(`GameObject` 가 8 B 커졌다 — `_bAlwaysLoaded`)는 묶음의 `ctest-nogpu` 가 돈다. 리플렉션 헤더가 늘어 구성을 다시 돌린다.
- 쿠킹 — WildWest 초원이 셀로 나뉘는지(셀을 넘는 참조가 없는지), Shipping 에서 셀이 올라오는지: 매니페스트 id `cook-scenes-shipping`(`App.exe --cook-scenes`, 셀 수는 로그로).
- 측정 — Release 로 WildWest 를 돌리며 셀이 오르내리는 프레임의 `GT.Scene.tick.destroyPost` 최대값과 정점 풀 repack 횟수(0 이어야 한다): 매니페스트 id `world-streaming-hitch-measure`. 셀을 가로질러 걷는 시나리오가 아직 없다.
- W5: `MMOReplicator` 를 쓰는 게임 서버가 아직 없다 — 첫 MMO 게임이 틱마다 `replicator.update` 다음에 `feedStreamingSources( 씬의 WorldStreamingSubsystem )` 를 부르고 `initialize` 에 `getGrid()` 를 넘긴다(그때까지 `NetMMOTest` 가 연결을 본다).
- W6: World Cells 창의 캡처와 `uiaudit` 시나리오에 창이 잡히는지, 문구 열 개의 `--check-text`: 매니페스트 id `world-cells-panel-capture`.

충돌 후보: W2 · W3 은 오브젝트 · 씬 직렬화(feat14)와, C-43 은 그래픽스 브랜치(feat7, feat15)의 `GPUScene.cpp` · `FrameRendererCompute.cpp` · RHI 팩터리와 겹칠 수 있다.
