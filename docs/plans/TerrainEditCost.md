# 지형 LOD 교체 · 지형 쓰기 · 식생 다시 계산의 비용 (2026-10-21)

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 옛번호: D2-5.6, D2-5.7 · 진행: 3/4

후보 [TerrainWaterVegetationRemainders](Candidates-graphics.md#terrainwatervegetationremainders)의 "지형 LOD 교체 프레임 시간"과 "지형 쓰기의 남은 것" 조각을 계획으로 올린 문서입니다. 강 경로 · 안개 · 물 굴절 · 지오모프는 후보에 남깁니다.
지형 쓰기의 결정은 [결정 기록](../09_Decisions.md) 5-5, 측정값은 [검증과 측정](../08_Verification.md) 2절 "지형 LOD 교체" · "지형 높이 쓰기" 입니다.

## 1. 왜 하는가

- LOD 고리를 넘는 프레임은 청크 여럿의 메시를 한꺼번에 다시 만든다. Release 최악이 updateLODs 4.0 ~ 5.1 ms, GPUScene 수집 5.0 ~ 5.2 ms 로 기준(GT 2 ms)을 넘었다.
- 브러시 드래그가 끝나면 식생이 숲 전체를 다시 뿌렸다(쇼케이스 Debug 약 0.86 s).
- 쓰기 측정은 Debug 뿐이었다.

## 2. 들어간 것 (계약은 코드 헤더 주석이 정본)

- **T1 청크 메시를 워커로 나눠 만든다**(`TerrainComponent::rebuildChunkMeshes`). LOD 교체 · 쓰기 뒤 미룬 청크 · 처음 만들기가 모두 이 한 함수로 지난다. 정점 만들기(높이장 읽기만)는 `engine::runParallel` 로 청크마다 나누고, 메시 생성과 배치 교체(레지스트리 더티 표시)는 호출 스레드가 한다. 이음매는 같은 프레임 안에서 모두 맞춘다(청크를 프레임에 나눠 미루면 틈이 보인다). 렌더 스레드 정점 풀은 이미 바뀐 메시의 구간만 올린다(`GPUMeshVertexPool`, C-43).
- **T2 식생은 바뀐 곳만 다시 계산한다**(`FoliageComponent::refreshFromTerrainEdits`). 배치를 두 단계로 갈랐다 — 후보마다의 표면 판정(`PlacementScatter::sampleCandidates`)과 최소 거리 이후(`resolveCandidates`). 식생은 레이어마다 후보 판정을 들고, 지형이 알려 주는 편집 사각형(`TerrainComponent::findEditedArea` — 높이 · 스플랫 세대를 청크마다 적는다) 안의 후보만 다시 판정한다(`resampleCandidates`). 최소 거리는 앞 번호가 이기는 순서 규칙이라 연쇄로 바뀔 수 있어 늘 전부 다시 푼다. 그래서 결과는 처음부터 계산한 것과 비트까지 같다. 셀마다 내용 해시를 견줘 바뀐 셀의 배치만 새로 만든다.
  - 같은 모양 점검(2026-10-21): `PropScatterComponent` 는 플레이 시작에 한 번 계산하고, 지형 쓰기는 정지 상태에서만 열려(`EditorSessionPolicy::areSceneEditsAllowed`) 세운 동안 표면이 바뀌지 않는다. 다시 계산할 계기가 없어 부분 계산을 붙이지 않았다(헤더 주석에 조건을 적었다). 디퍼드의 `sprite2dlit` 은 `Transparent` 머티리얼이라 G버퍼를 지나지 않고 포워드 투명 패스가 2D 빛을 직접 계산하므로 `ForwardOnlyOpaque` 로 옮길 것이 없다.
  - 위치 순서와 무관한 최소 거리(우선순위 해시)로 바꾸면 최소 거리까지 지역 계산이 되지만, 모든 씬의 식생 · 소품 배치가 달라져 골든 이미지를 다시 떠야 하므로 하지 않았다.

## 3. 남은 단위

### T3 Release 측정과 예산

- `TerrainBenchTest.LODSweepWorstFrame`(Release, 3 회): T1 뒤 updateLODs 최악이 GT 2 ms 안인지. 워커 수에 달려 있으므로 PC 를 함께 적는다.
- `TerrainBenchTest.ColliderRebuildAfterBrushWrite` · `WriteHeightsRectSizes` 를 Release 로 다시 재고 `TerrainComponentInternal::kMaxChunkMeshRebuildsPerFrame`(넷)을 GT 2 ms 에 맞춘다.
- 식생 부분 계산: 쇼케이스에서 브러시 한 번 뒤 `Foliage: refreshed N of M cell batches` 로그의 시간(Debug 0.86 s 와 견준다). 최소 거리 풀이가 전부 다시 도는 몫이 크면 그 비용을 따로 잰다.
- 렌더 스레드 `RT.GPUScene.vertexPool` 의 교체 프레임 최대값(App 프로파일 표, 벤치에는 없다).

- 절차(Release, VSync 끔, 혼자 도는 시간대): `cmake --build build/Ninja-Release --target EngineTest` 뒤 `cd build/Ninja-Release/Bin && ../TestBin/EngineTest.exe --test_filter=TerrainBenchTest.* --load_suites=only --host_suites=all` 를 3 회. 표에 PC(코어 수)와 커밋을 함께 적는다.
- App 프로파일 표에서 볼 구간: `GT.GPUScene.build.partition` · `GT.GPUScene.build.batches` · 새 `GT.GPUScene.build.meshSwap`(T4 의 길)과 횟수 `GT.GPUScene.build.meshSwapCount`, `RT.GPUScene.vertexPool`. 교체 프레임은 `meshSwapCount` 가 0 이 아닌 프레임이다.
- 결과는 [검증과 측정](../08_Verification.md) 2절 "지형 LOD 교체" 에 숫자로 남기고 `kMaxChunkMeshRebuildsPerFrame` 을 고친다.

### T4 교체 프레임의 GPUScene 수집 — 들어감(빌드 미확인)

- 메시만 바뀐 불투명 후보(`DrawCandidate::hasSameBatchKeyExceptMesh`)는 그 배치가 인스턴스 하나면 다시 나누지 않고 배치의 메시만 바꾼다(`GPUSceneBuilder::swapSingleInstanceBatchMeshes`). 지형 청크는 청크마다 인스턴스 배치 하나라 LOD 교체 프레임이 제자리 갱신으로 끝난다. 나누기 항목의 메시 키도 함께 바꿔 다음 전체 방출이 낡은 포인터(해제 뒤 같은 주소에 새 메시가 올 수 있다)로 묶지 않게 했다. 계약은 헤더 주석이 정본이다.
- 테스트 `GPUSceneTest.MeshSwapOfSingleInstanceBatchKeepsTheBatches` — 실행 미확인.
- 남은 것: 부분 수집 문턱(더티 4분의 1) 밖의 교체 프레임(청크가 적은 씬)은 여전히 전체 수집이다. 여러 인스턴스가 한 메시를 쓰는 배치의 메시 교체는 나누기가 맡는다. 메시를 배치 키에서 빼고 시작 오프셋을 인스턴스에 싣는 쪽(셰이더 · 간접 인자 변경)은 T3 측정에서 교체 프레임이 여전히 넘으면 후보로 올린다.

## 4. 확인하지 않은 것

- 빌드 · 테스트를 돌리지 않았다. 새 테스트 `FoliageTest.TerrainEditRefreshesOnlyChangedCells` 와 기존 `FoliageTest.*` · `TerrainTest.*` · `TerrainBenchTest.*` 를 돌려야 한다.
- 내비는 런타임 내비메시 타일만 다시 굽는다(에디터가 구운 내비 에셋은 다시 굽기 명령으로).
- 에디터 편집 모드에서도 틱이 남은 청크를 만드는지(브러시 한 번은 쓰기에서 끝난다). 브러시 레이 전진이 걸음마다 `findTerrainAt` 으로 지형 목록을 훑는 비용(지형 수만큼, 측정 안 함).
