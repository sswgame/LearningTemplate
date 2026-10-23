# 상호작용 대상의 공유 공간 격자

> 상태: 측정대기 · 선행: 없음 · 영역: gameplay · 옛번호: G-4 · 진행: 3/3

하는 쪽(`InteractorComponent`)은 등록된 대상 목록을 한 번 돌았습니다(대상 16 · 10k 오브젝트에서 +2~6 us). 2026-10-22 사용자 결정으로 착수 조건(하는 쪽 · 대상이 모두 수천)을 갈음했습니다.

## 1. 정한 것

- 씬당 하나 `InteractionSpatialIndex`(`ISceneFrameSystem`, `GameFramework/Base/Gameplay/Interaction`)가 `FrameSystems` 단계(PrePhysics 틱 앞, 게임 스레드)에서 공간(3D · 2D)마다 `SpatialHashGrid2D` 를 맞춥니다. 셀 = 그 프레임 대상들의 최대 도달 거리입니다.
- 하는 쪽이 **둘 이상인 공간만** 맞춥니다(`kMinInteractorCountForGrid`). 하나면 목록을 도는 비용과 격자를 맞추는 비용이 같기 때문입니다. 맞추지 않은 공간은 예전처럼 목록을 돕니다.
- 평면: 3D 는 XZ, 2D 는 XY. 평면 거리는 공간 거리보다 길지 않아 반경 = 최대 도달 거리 원 질의는 닿는 대상을 빠뜨리지 않습니다. 판정(`considerCandidate`)은 두 길이 같은 함수로 현재 위치에 합니다.
- 위치는 물리 앞 값이라, 그 프레임 물리로 바깥에서 들어온 대상은 한 프레임 늦게 후보가 됩니다.
- 사용자 수는 하는 쪽의 `onBeginPlay` · `onEndPlay` · `setSpace` 가 셉니다. 마지막이 빠지면 매니저에서 뗍니다.

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| I1 | 색인 · 하는 쪽 연결 · 테스트 `InteractionTest.SharedSpatialGridServesSeveralInteractors` | 끝 |
| I2 | Debug 빌드(`all EngineTest`, 경고 0) · `InteractionTest.*` 16 개 통과(2026-10-23) | 끝 |
| I3 | `TestInteractionBench` 에 하는 쪽 여럿 × 대상 수천 케이스(`InteractionBenchTest.SharedGridTickWithManyInteractors` — 하는 쪽 1 · 8 · 64 × 대상 2,000 · 5,000)를 더했다 | 코드 끝, Release 측정은 통합(빌드 미확인) |

확인 대기: 매니페스트 id `interaction-bench`. 그 항목이 `InteractionBenchTest.*` 를 Release 로 돌려 하는 쪽 1(목록 순회)과 8 · 64(격자)의 틱 표본을 견준다. 하는 쪽 수로 나눈 비용이 격자에서 더 낮으면 이 문서를 닫고, 아니면 `kMinInteractorCountForGrid` 를 올린다.
