# 애니메이션 평가를 연결 성분 단위로 옮기기

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: 없음 · 진행: 표

`AnimationSystem` 은 "단계 먼저, 그 안에서 레벨" 순서로 레벨마다 `engine::runParallelByCost` 의 전체 대기를 해서, 의존 깊이가 L 이면 한 프레임에 최대 5L 번 기다렸다(시간 단계 1 + 포즈 단계 넷, 각각 L 레벨).
이 문서는 의존으로 이어진 유닛을 연결 성분으로 모아 성분 안은 직렬로, 성분 사이는 한 번에 병렬로 돌려 전체 대기를 2 번(시간 단계 1 + 포즈 단계 합친 1)으로 줄이는 일을 단위로 나눈다.

## 1. 결정

| 물음 | 결정 | 이유 |
|---|---|---|
| 평가의 병렬 단위는 | 포즈 단계는 의존 간선으로 이어진 유닛의 연결 성분이다. 성분 안은 위상 순서로 **유닛 먼저**(한 유닛의 포즈 단계 넷을 이어서) 직렬로 돌고, 성분 사이는 `runParallelByCost` 한 번이다. 시간 단계는 성분과 상관없이 일하는 유닛 위로 `runParallelByCost` 한 번이다 | 의존 깊이와 상관없이 전체 대기가 시간 단계 1 번과 포즈 단계 합친 1 번이다. 독립 캐릭터만 있는 장면(성분 N 개, 크기 1)은 예전과 같은 모양이다. 시간 단계는 자기 유닛만 읽어(동기 그룹 · 군중은 시간 단계 뒤 게임 스레드) 순서 제약이 없다 |
| 간선의 뜻은 | 위 유닛의 **모든 단계**가 끝난 뒤에 아래 유닛이 시작한다(`AnimationDependencyEdge`). 성분 안 순서가 이 뜻을 지킨다 | 아래 유닛(리더 포즈를 따르는 부품, 리그 대상)은 위 유닛의 최종 포즈를 읽는다. 옛 순서는 단계마다 위 유닛이 먼저일 뿐이라 부품이 위 유닛의 부착 단계(체형 비율)와 후처리 단계(IK)를 적용하기 전 로컬 포즈를 복사할 수 있었다(`SkeletalMeshComponent::copyLeaderPose`, `CharacterAppearanceProportionTask`) |
| 고리는 | 고리에 든 유닛과 그 아래 유닛은 등록 순서로 성분의 뒤에 직렬로 놓는다(`AnimationComponentSet::hasCycle`) | 고리는 데이터 오류이고 로그로 알린다. 순서를 강제하지 않되 같은 성분이라 서로 병렬로 돌지 않는다(레이스 없음) |
| 성분 안 직렬 실행에 `TaskManager` 의 의존 API 를 쓰나 | 쓰지 않는다. 성분 하나를 `runParallelByCost` 항목 하나로 돌린다 | 비용 기반 분배(결정 기록의 "애니메이션 평가는 비용 기반 분배")와 고정 청크 크기 기각을 그대로 지킨다. 성분이 크기 1 인 독립 유닛이 대부분이라 새 의존 그래프 실행기가 얻는 것이 없다 |
| 레벨은 | 지웠다(`getLevels` · `hasDependencyCycle` · 레벨 칸 표 · `GT.Animation.levelCount`). 순서가 필요한 곳은 `getComponents()`(평가 순서 그대로)를 본다 | 평가가 레벨을 더 쓰지 않아 남기면 평가와 다른 순서를 진단에 보이게 된다 |
| 리타깃은 원본의 무엇을 읽나 | 원본의 **최종 포즈**(후처리 · 부착까지)다. 기본 포즈까지만 읽는 별도 간선 종류를 두지 않는다 | 아래 3절 |
| 게임 스레드 순회 순서(`beginAnimationFrame` · 동기 그룹 · 군중 · `finishAnimationFrame` · 되감기 기록)는 | 성분 순서(`getActiveComponents().getUnits()`)다. 레벨 순서(레벨 0 전부, 다음 레벨…)에서 바뀐 것이다 | 아래 3절 |
| 단계별 시간은 | 단계마다의 벽시계 구간(`GT.Animation.phaseBasePose` · `phaseAttachment` · `phasePostProcess` · `phaseSkinPalette`)을 지우고, 포즈 단계 합친 벽시계 구간 `GT.Animation.phasePose` 와 워커가 단계마다 누적한 CPU 시간(나노초)의 프레임 합 카운터 `GT.Animation.phase<단계>CpuNanos` 를 둔다. `phaseTime` 은 벽시계 구간 그대로다 | 단계가 유닛 안에서 이어서 돌아 단계 사이에 전체 대기가 없다. 카운터는 청크마다 지역 배열에 모아 한 번 더해(유닛마다 공유 칸을 두드리지 않는다) 계측이 켜져 있을 때만 시계를 읽는다. 스레드를 더한 값이라 벽시계보다 클 수 있고 단계의 몫(비율)을 읽는 데 쓴다 |

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| U1 | 정합 테스트(`SkeletalAnimationTest.FollowerReadsLeaderFinalPoseAfterAttachmentPhase` · `PoseModifierTest.FollowerReadsLeaderPoseAfterRig`), 계측 카운터(`componentCount` · `maxComponentSize`), 벤치 변수(`-gv_benchCharacterFollowers` · `-gv_benchCharacterWeapon`) | 코드 작성(빌드 · 실행 미확인). 정합 테스트 둘은 옛 순서에서 실패해야 하고 지금 순서에서 통과해야 한다 |
| U2 | 연결 성분을 짓는다(`AnimationComponentSet`, union-find + 성분별 위상 순서) | 코드와 `SkeletalAnimationTest.Components*` 테스트 작성(빌드 미확인) |
| U3 | `runPhase` 를 성분 단위로 옮긴다: 시간 단계는 유닛 위로 한 번, 포즈 단계 넷은 성분마다 유닛 먼저로 묶어 `runParallelByCost` 한 번. 게임 스레드 순회는 성분 순서, 되감기 훅 순회는 `AnimationOrderedList::forEach` | 코드 작성(빌드 · 실행 미확인). 새 테스트 `SkeletalAnimationTest.PosePhasesRunUnitFirstInsideComponentAfterEveryTimePhase` · `RetargetTest.ComponentFollowsSourceFinalPoseAfterPostProcess` |
| U4 | 레벨 제거(`getLevels` · `hasDependencyCycle` · `_listLevelSlot` · `levelCount` 카운터). 외톨이 해제 지름길은 성분 목록에서 유닛 하나짜리 성분을 뺀다(`AnimationComponentSet::removeSingletons`). `getLevels` 를 쓰던 테스트는 성분 API 로 옮기고, 성분 테스트와 겹치던 `SystemLevelsMatchFreshBuildAfterUnregister` 는 지웠다 | 코드 작성(빌드 · 실행 미확인) |

후보(단위로 세우지 않음): 큰 성분(리더 하나에 수십 유닛)이 직렬 임계 경로가 되면 성분 안의 형제 유닛(같은 위 유닛만 가진 부품들)을 다시 병렬로 나눈다. `GT.Animation.maxComponentSize` 와 4절의 측정으로 필요가 보일 때 착수한다.

## 3. 판정과 순서 변화

**리타깃이 최종 포즈를 읽게 되는 것은 의도에 맞는다.** 옛 주석(`PoseRetargetComponent` "원본은 기본 포즈를 끝냈다 — 후처리 리그는 아직, 리타깃은 애니메이션만 옮긴다")은 순서의 부산물을 의도로 적은 것이었다 — 의존은 "원본이 먼저 끝나야 한다" 로만 선언됐고(`setSource`) 어떤 단계까지인지 말하지 않았다. 리타기터는 원본의 로컬 포즈 전체를 읽으므로 "애니메이션만" 을 가르는 입력은 없다.
최종 포즈를 읽는 쪽이 낫다: (1) 언리얼 `Retarget Pose From Mesh` 도 원본 메시 컴포넌트의 평가가 끝난 포즈를 읽는다. (2) 원본의 IK 가 원본 월드의 발을 디디므로 대상이 같은 모양을 따르는 것이 화면에서 원본과 어긋나지 않는다. (3) "BasePose 까지" 로 구분하려면 원본이 기본 포즈 단계 끝에 포즈를 한 장 더 떠 두는 사본이 필요하고(유닛 먼저 순서에서는 원본의 후처리가 이미 덮은 뒤다), 그 사본의 비용을 낼 근거(원본 후처리가 대상에 해로운 실제 사례)가 없다.
남은 위험은 둘이고 둘 다 읽기만 했다. 원본에 체형 비율(`CharacterAppearanceProportionTask`)이 있으면 비율이 적용된 본 이동이 대상에 옮겨질 수 있다. 스프링 본(후처리)이 원본에서 흔들린 모양이 대상의 자기 스프링과 겹쳐 두 번 흔들릴 수 있다. 사례가 나오면 그때 간선 종류("기본 포즈까지")를 더하고 원본에 사본을 둔다.

**게임 스레드 순회 순서가 바뀐다.** 옛 `_listActive` 는 레벨 순서(레벨 0 의 일하는 유닛 전부, 그다음 레벨 1 …)였고, 지금은 성분 순서(성분은 가장 앞선 등록 순서 순, 성분 안은 위 유닛이 먼저)다. 의존이 없는 장면은 둘 다 등록 순서라 같다. 의존이 있는 장면의 차이:
- 동기 그룹(`synchronizeGroups`): 그룹은 처음 나온 플레이어 순서로, 그룹 안은 플레이어 순서로 처리한다. 순서가 달라지는 것은 서로 다른 성분의 의존 유닛이 같은 그룹에 든 때뿐이고, 영향은 가중치가 같은 리더 후보의 동률 깨기(`AnimationSyncGroup::synchronize` 가 앞선 것을 리더로 고른다)뿐이다.
- 군중(`updateCrowd`): 성분 안은 위 유닛이 먼저라 리더 부품 가입(`joinLeaderCrowdBucket` 이 리더의 묶음을 읽는다)의 전제는 지켜진다. 묶음 목록의 순서가 달라질 수 있으나 묶음은 순서로 결과가 갈리지 않는다고 읽었다.
- `finishAnimationFrame` · 되감기 기록: 성분 안은 위 유닛이 먼저라 아래 유닛이 위 유닛의 결과를 이어 받는 성질은 그대로다. 되감기 트랙은 유닛별이라 기록 순서가 결과에 끼지 않는다.
- 훅이 등록 · 해제를 부를 수 있는 순회는 `forEach` 로 돈다(LOD 클라이언트의 `applyAnimationLOD` · `applyRewindState` · `collectDebugState`). 유닛 순회(`_componentActive`)는 해제로 바뀌지 않는다 — 해제는 `_componentAll` 만 건드리고 활성 목록은 다음 `collectActiveUnits` 가 다시 짓는다.

**선언하지 않은 교차 유닛 읽기는 레이스가 된다.** 옛 순서는 "모든 유닛의 기본 포즈가 끝난 뒤 부착 · 후처리" 라는 전역 장벽이 있어 의존을 선언하지 않고 다른 유닛의 기본 포즈 결과를 부착 · 후처리 단계에서 읽어도 우연히 맞았다. 지금은 장벽이 없다. 읽은 범위(`AnimationPhase::Attachment` · `PostProcess` 를 구현한 작업 — 래그돌 · 얼굴 · 로코모션 워핑 · 체형 비율 · 리그 — 의 단계 함수가 다른 유닛을 해석하는 곳)에서는 선언 없는 교차 읽기를 찾지 못했다. 리그의 `space` 대상은 의존 없이 지난 프레임의 자기 본을 읽고, 외부 대상은 `bLive` 일 때만 이번 프레임 값을 읽으며 그때 의존을 건다. 새 작업은 `IAnimationPhaseTask` 의 계약(자기 유닛과 의존으로 선언한 위 유닛만 읽는다)을 지켜야 한다.

## 4. 재는 것(통합이 돌린다)

모두 `GT.Animation.evaluate` · `phaseTime` · `phasePose` · `componentCount` · `maxComponentSize` · `phase<단계>CpuNanos` 를 읽는다. 통합에서 벤치(`-gv_benchCharacterFollowers` · `-gv_benchCharacterWeapon`)로 돌린다.
- K 대조군: 몸 하나에 리더 포즈 부품 K 개(K = 0 · 2 · 4) 대 같은 총 유닛 수를 독립 캐릭터로 채운 대조군(`-gv_benchCharacters` = N × (1 + K)). 독립 캐릭터 대조군은 성분 N(1 + K) 개 크기 1 이라 병렬도가 가장 크고, K 군은 성분 N 개 크기 1 + K 다. 두 평가 시간의 차이가 성분 안 직렬화의 비용이다. K = 0 은 U3 전후가 같아야 한다(회귀 점검).
- 무기(`-gv_benchCharacterWeapon=1`)를 더해 L = 3 인 경우: U3 전(`wt/anim1`)의 `GT.Animation.evaluate` 와 U3 후를 비교한다. 이전 빌드의 `levelCount` 와 U3 후의 `componentCount` · `maxComponentSize` 를 같이 적는다 — 레벨 카운터는 이 변경으로 사라진다.
- 포크-조인 5 → 2: 전체 대기 횟수가 단계 5 × L 에서 2 로 준 효과. 대기 횟수 카운터는 두지 않았으므로 L = 1(K = 0) 과 L = 3 의 `evaluate` 차를 U3 전 · 후로 대조한다 — 전에는 L 이 늘면 시간이 단계 수에 비례해 늘고, 후에는 성분 크기에 비례해 늘어야 한다. 직렬 임계 경로가 병렬도 이득을 넘는 지점(성분 크기 대 스레드 수)이 위 후보의 착수 기준이다.
