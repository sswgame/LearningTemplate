# 지형 LOD 교체 · 지형 쓰기 · 식생 다시 계산의 비용

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 진행: 3/4

LOD 고리를 넘는 프레임에 청크 여럿의 메시를 한꺼번에 다시 만들던 비용(Release 최악 updateLODs 4 ~ 5 ms, GPUScene 수집 5 ms 로 GT 2 ms 기준 초과)과, 브러시 드래그 뒤 식생이 숲 전체를 다시 뿌리던 비용(쇼케이스 Debug 약 0.86 s)을 줄였다. 후보 [EnvironmentRemainders](EnvironmentRemainders.md) 의 "지형 LOD 교체 프레임 시간" · "지형 쓰기의 남은 것" 조각이다. 코드는 들어갔고 컴파일 · 테스트 · Release 측정이 남았다.

## 진행

| 단위 | 내용 | 상태 |
|---|---|---|
| T1 | 청크 메시를 워커로 나눠 만든다(`TerrainComponent::rebuildChunkMeshes`) | 코드 끝, 컴파일 확인 전 |
| T2 | 식생은 바뀐 곳만 다시 계산한다(`FoliageComponent::refreshFromTerrainEdits`) | 코드 끝, 컴파일 확인 전 |
| T3 | Release 측정과 예산 확정 | 남음 |
| T4 | 교체 프레임의 GPUScene 수집 — 메시만 바뀐 단일 인스턴스 배치는 다시 나누지 않는다 | 코드 끝(`GPUSceneTest.MeshSwapOfSingleInstanceBatchKeepsTheBatches` 통과), 컴파일 확인 전 |

첫 일: 컴파일 확인 뒤 T3 측정을 Release 에서 혼자 돈다.

## 설계 (계약은 코드 헤더 주석이 정본)

- **T1.** LOD 교체 · 쓰기 뒤 미룬 청크 · 처음 만들기가 `rebuildChunkMeshes` 한 함수로 지난다. 정점 만들기(높이장 읽기만)는 `engine::runParallel` 로 청크마다 나누고, 메시 생성과 배치 교체는 호출 스레드가 한다. 이음매는 같은 프레임 안에서 모두 맞춘다(프레임에 나눠 미루면 틈이 보인다). 렌더 스레드 정점 풀은 바뀐 메시의 구간만 올린다.
- **T2.** 배치를 후보마다의 표면 판정(`sampleCandidates`)과 최소 거리 이후(`resolveCandidates`)로 갈랐다. 지형이 알려 주는 편집 사각형(`findEditedArea`) 안의 후보만 다시 판정하고, 최소 거리는 앞 번호가 이기는 순서 규칙이라 연쇄로 바뀔 수 있어 늘 전부 다시 푼다. 결과는 처음부터 계산한 것과 비트까지 같고, 셀 내용 해시를 견줘 바뀐 셀만 새로 만든다.
  - 같은 모양 점검: `PropScatterComponent` 는 플레이 시작에 한 번 계산하고 지형 쓰기는 정지 상태에서만 열려 다시 계산할 계기가 없다.
  - 기각: 위치 순서와 무관한 최소 거리(우선순위 해시)는 최소 거리까지 지역 계산이 되지만 모든 씬의 배치가 달라져 골든을 다시 떠야 한다.
- **T4.** 지형 청크는 청크마다 인스턴스 배치 하나라 LOD 교체 프레임이 제자리 갱신(`GPUSceneBuilder::swapSingleInstanceBatchMeshes`)으로 끝난다. 나누기 항목의 메시 키도 함께 바꿔 낡은 포인터로 묶지 않게 했다.
  - 주의: `uploadInstanceAndVisibleBuffers` 는 "구간 없음" 을 전체 업로드로 읽으므로 교체 프레임은 인스턴스 버퍼를 통째로 올린다(T3 에서 RT 업로드가 보이면 "전부" 와 "없음" 을 나눈다).
  - 한계: 부분 수집 문턱(더티 4분의 1) 밖의 교체 프레임은 전체 수집, 여러 인스턴스가 한 메시를 쓰는 배치는 나누기가 맡는다. 메시를 배치 키에서 빼고 시작 오프셋을 인스턴스에 싣는 쪽(셰이더 · 간접 인자 변경)은 T3 에서 교체 프레임이 여전히 넘으면 후보로 올린다.

## T3 — 측정할 것

- `TerrainBenchTest.LODSweepWorstFrame`(Release 3 회): T1 뒤 updateLODs 최악이 GT 2 ms 안인지(워커 수에 달려 PC 를 함께 적는다).
- `ColliderRebuildAfterBrushWrite` · `WriteHeightsRectSizes` 를 Release 로 다시 재고 `kMaxChunkMeshRebuildsPerFrame`(넷)을 GT 2 ms 에 맞춘다.
- 식생: 쇼케이스에서 브러시 한 번 뒤 `Foliage: refreshed N of M cell batches` 로그의 시간(Debug 0.86 s 와 견준다). 최소 거리 풀이가 전부 다시 도는 몫이 크면 따로 잰다.
- 렌더 스레드 `RT.GPUScene.vertexPool` 의 교체 프레임 최대값, GT 구간 `build.partition` · `build.batches` · `build.meshSwap` · 횟수 `build.meshSwapCount`.
- 절차: Release, VSync 끔, 혼자 도는 시간대. `EngineTest` 를 빌드해 `--test_filter=TerrainBenchTest.*` 를 3 회. 표에 PC(코어 수)와 커밋을 함께 적는다.

## 확인하지 않은 것

- 빌드 · 테스트(`FoliageTest.TerrainEditRefreshesOnlyChangedCells` 와 `FoliageTest.*` · `TerrainTest.*` · `TerrainBenchTest.*`).
- 내비는 런타임 내비메시 타일만 다시 굽는다(에디터가 구운 내비 에셋은 다시 굽기 명령으로).
- 에디터 편집 모드에서도 틱이 남은 청크를 만드는지, 브러시 레이 전진이 걸음마다 `findTerrainAt` 으로 지형 목록을 훑는 비용(지형 수만큼, 측정 안 함).
