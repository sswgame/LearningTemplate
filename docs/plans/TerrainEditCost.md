# 지형 LOD 교체 · 지형 쓰기 · 식생 다시 계산의 비용 (2026-10-21)

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 옛번호: D2-5.6, D2-5.7 · 진행: 3/4

후보 `TerrainWaterVegetationRemainders`(지금은 [EnvironmentRemainders](EnvironmentRemainders.md))의 "지형 LOD 교체 프레임 시간"과 "지형 쓰기의 남은 것" 조각을 계획으로 올린 문서입니다. 지형 쓰기의 결정은 [결정 기록](../09_Decisions.md) 5-5, 측정값은 [검증과 측정](../08_Verification.md) 2절 "지형 LOD 교체" · "지형 높이 쓰기" 입니다.

## 1. 왜 하는가

LOD 고리를 넘는 프레임은 청크 여럿의 메시를 한꺼번에 다시 만든다(Release 최악 updateLODs 4.0 ~ 5.1 ms, GPUScene 수집 5.0 ~ 5.2 ms 로 기준 GT 2 ms 초과). 브러시 드래그가 끝나면 식생이 숲 전체를 다시 뿌렸다(쇼케이스 Debug 약 0.86 s). 쓰기 측정은 Debug 뿐이었다.

## 2. 들어간 것 (계약은 코드 헤더 주석이 정본)

- **T1 청크 메시를 워커로 나눠 만든다**(`TerrainComponent::rebuildChunkMeshes`). LOD 교체 · 쓰기 뒤 미룬 청크 · 처음 만들기가 이 한 함수로 지난다. 정점 만들기(높이장 읽기만)는 `engine::runParallel` 로 청크마다 나누고 메시 생성과 배치 교체는 호출 스레드가 한다. 이음매는 같은 프레임 안에서 모두 맞춘다(프레임에 나눠 미루면 틈이 보인다). 렌더 스레드 정점 풀은 바뀐 메시의 구간만 올린다(`GPUMeshVertexPool`, C-43).
- **T2 식생은 바뀐 곳만 다시 계산한다**(`FoliageComponent::refreshFromTerrainEdits`). 배치를 후보마다의 표면 판정(`PlacementScatter::sampleCandidates`)과 최소 거리 이후(`resolveCandidates`)로 갈랐다. 지형이 알려 주는 편집 사각형(`TerrainComponent::findEditedArea` — 높이 · 스플랫 세대를 청크마다) 안의 후보만 다시 판정하고(`resampleCandidates`), 최소 거리는 앞 번호가 이기는 순서 규칙이라 연쇄로 바뀔 수 있어 늘 전부 다시 푼다 — 결과는 처음부터 계산한 것과 비트까지 같다. 셀마다 내용 해시를 견줘 바뀐 셀의 배치만 새로 만든다. 같은 모양 점검: `PropScatterComponent` 는 플레이 시작에 한 번 계산하고 지형 쓰기는 정지 상태에서만 열려(`EditorSessionPolicy::areSceneEditsAllowed`) 다시 계산할 계기가 없다. 위치 순서와 무관한 최소 거리(우선순위 해시)로 바꾸면 최소 거리까지 지역 계산이 되지만 모든 씬의 식생 · 소품 배치가 달라져 골든을 다시 떠야 해서 하지 않았다.
- **T4 교체 프레임의 GPUScene 수집**(빌드 미확인): 메시만 바뀐 불투명 후보(`DrawCandidate::hasSameBatchKeyExceptMesh`)는 그 배치가 인스턴스 하나면 다시 나누지 않고 배치의 메시만 바꾼다(`GPUSceneBuilder::swapSingleInstanceBatchMeshes`). 지형 청크는 청크마다 인스턴스 배치 하나라 LOD 교체 프레임이 제자리 갱신으로 끝난다. 나누기 항목의 메시 키도 함께 바꿔 낡은 포인터로 묶지 않게 했다. `GPUSceneTest.MeshSwapOfSingleInstanceBatchKeepsTheBatches` 통과(2026-10-22 — 인스턴스 원소에는 메시가 없어 더티 구간은 0 개이고 바뀐 것은 배치 표 `_bCPUDirty`). 주의: `GPUScene::uploadInstanceAndVisibleBuffers` 는 "구간 없음" 을 전체 업로드로 읽으므로 교체 프레임은 인스턴스 버퍼를 통째로 올린다(T3 측정에서 RT 업로드가 보이면 "전부" 와 "없음" 을 나눈다). 남은 것: 부분 수집 문턱(더티 4분의 1) 밖의 교체 프레임(청크가 적은 씬)은 전체 수집, 여러 인스턴스가 한 메시를 쓰는 배치의 메시 교체는 나누기가 맡는다. 메시를 배치 키에서 빼고 시작 오프셋을 인스턴스에 싣는 쪽(셰이더 · 간접 인자 변경)은 T3 에서 교체 프레임이 여전히 넘으면 후보로 올린다.

## 3. 남은 단위

### T3 Release 측정과 예산

매니페스트 id `terrain-bench-release`.
- `TerrainBenchTest.LODSweepWorstFrame`(Release, 3 회): T1 뒤 updateLODs 최악이 GT 2 ms 안인지(워커 수에 달려 PC 를 함께 적는다).
- `TerrainBenchTest.ColliderRebuildAfterBrushWrite` · `WriteHeightsRectSizes` 를 Release 로 다시 재고 `TerrainComponentInternal::kMaxChunkMeshRebuildsPerFrame`(넷)을 GT 2 ms 에 맞춘다.
- 식생 부분 계산: 쇼케이스에서 브러시 한 번 뒤 `Foliage: refreshed N of M cell batches` 로그의 시간(Debug 0.86 s 와 견준다). 최소 거리 풀이가 전부 다시 도는 몫이 크면 따로 잰다.
- 렌더 스레드 `RT.GPUScene.vertexPool` 의 교체 프레임 최대값(App 프로파일 표).
- 절차(Release, VSync 끔, 혼자 도는 시간대): `cmake --build build/Ninja-Release --target EngineTest` 뒤 `cd build/Ninja-Release/Bin && ../TestBin/EngineTest.exe --test_filter=TerrainBenchTest.* --load_suites=only --host_suites=all` 를 3 회. 표에 PC(코어 수)와 커밋을 함께 적는다. App 프로파일 표에서 볼 구간: `GT.GPUScene.build.partition` · `build.batches` · `build.meshSwap` 과 횟수 `build.meshSwapCount`(교체 프레임은 0 이 아닌 프레임), `RT.GPUScene.vertexPool`. 결과는 [검증과 측정](../08_Verification.md) 2절 "지형 LOD 교체" 에 숫자로 남기고 `kMaxChunkMeshRebuildsPerFrame` 을 고친다.

## 4. 확인하지 않은 것

- 빌드 · 테스트를 돌리지 않았다(새 `FoliageTest.TerrainEditRefreshesOnlyChangedCells` 와 `FoliageTest.*` · `TerrainTest.*` · `TerrainBenchTest.*`).
- 내비는 런타임 내비메시 타일만 다시 굽는다(에디터가 구운 내비 에셋은 다시 굽기 명령으로).
- 에디터 편집 모드에서도 틱이 남은 청크를 만드는지(브러시 한 번은 쓰기에서 끝난다). 브러시 레이 전진이 걸음마다 `findTerrainAt` 으로 지형 목록을 훑는 비용(지형 수만큼, 측정 안 함).
