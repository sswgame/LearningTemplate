# 애니메이션 평가를 연결 성분 단위로 옮기기

> 상태: 측정대기 · 선행: 없음 · 영역: gameplay · 옛번호: 없음 · 진행: 표

`AnimationSystem` 은 "단계 먼저, 그 안에서 레벨" 순서로 레벨마다 `engine::runParallelByCost` 의 전체 대기를 해서 의존 깊이가 L 이면 한 프레임에 최대 5L 번 기다렸다. 이 문서는 의존으로 이어진 유닛을 연결 성분으로 모아 성분 안은 직렬로, 성분 사이는 한 번에 병렬로 돌려 전체 대기를 2 번(시간 단계 1 + 포즈 단계 합친 1)으로 줄이는 일이다.

## 1. 결정

| 물음 | 결정 | 이유 |
|---|---|---|
| 평가의 병렬 단위는 | 포즈 단계는 의존 간선으로 이어진 유닛의 연결 성분이다. 성분 안은 위상 순서로 **유닛 먼저**(한 유닛의 포즈 단계 넷을 이어서) 직렬, 성분 사이는 `runParallelByCost` 한 번. 시간 단계는 일하는 유닛 위로 한 번 | 의존 깊이와 상관없이 전체 대기가 2 번이다. 독립 캐릭터만 있는 장면(성분 N 개, 크기 1)은 예전과 같은 모양이다. 시간 단계는 자기 유닛만 읽어 순서 제약이 없다 |
| 간선의 뜻은 | 위 유닛의 **모든 단계**가 끝난 뒤 아래 유닛이 시작한다(`AnimationDependencyEdge`) | 아래 유닛(리더 포즈를 따르는 부품, 리그 대상)은 위 유닛의 최종 포즈를 읽는다. 옛 순서는 부품이 위 유닛의 부착 · 후처리 단계(체형 비율 · IK) 전 로컬 포즈를 복사할 수 있었다(`SkeletalMeshComponent::copyLeaderPose`, `CharacterAppearanceProportionTask`) |
| 고리는 | 고리에 든 유닛과 그 아래 유닛은 등록 순서로 성분 뒤에 직렬로 놓는다(`AnimationComponentSet::hasCycle`) | 고리는 데이터 오류이고 로그로 알린다. 같은 성분이라 서로 병렬로 돌지 않는다(레이스 없음) |
| 성분 안 직렬 실행에 `TaskManager` 의 의존 API 를 쓰나 | 쓰지 않는다. 성분 하나를 `runParallelByCost` 항목 하나로 돌린다 | 비용 기반 분배와 고정 청크 크기 기각(결정 기록)을 지킨다. 성분이 크기 1 인 독립 유닛이 대부분이라 새 의존 그래프 실행기가 얻는 것이 없다 |
| 레벨은 | 지웠다(`getLevels` · `hasDependencyCycle` · 레벨 칸 표 · `GT.Animation.levelCount`). 순서가 필요한 곳은 `getComponents()` | 남기면 평가와 다른 순서를 진단에 보이게 된다 |
| 리타깃은 원본의 무엇을 읽나 | 원본의 **최종 포즈**(후처리 · 부착까지). 기본 포즈까지만 읽는 별도 간선 종류를 두지 않는다 | 아래 3절 |
| 단계별 시간은 | 단계별 벽시계 구간을 지우고 포즈 단계 합친 구간 `GT.Animation.phasePose` 와 워커가 단계마다 누적한 CPU 시간 카운터 `GT.Animation.phase<단계>CpuNanos` 를 둔다. `phaseTime` 은 그대로 | 단계가 유닛 안에서 이어 돌아 단계 사이에 전체 대기가 없다. 카운터는 청크마다 지역 배열에 모아 한 번 더하고(공유 칸을 두드리지 않음) 계측이 켜져 있을 때만 시계를 읽는다. 스레드를 더한 값이라 벽시계보다 클 수 있고 단계의 비율을 읽는 데 쓴다 |

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| U1 | 정합 테스트(`SkeletalAnimationTest.FollowerReadsLeaderFinalPoseAfterAttachmentPhase` · `PoseModifierTest.FollowerReadsLeaderPoseAfterRig`), 계측 카운터(`componentCount` · `maxComponentSize`), 벤치 변수(`-gv_benchCharacterFollowers` · `-gv_benchCharacterWeapon`) | 끝남 |
| U2 | 연결 성분을 짓는다(`AnimationComponentSet`, union-find + 성분별 위상 순서) | 끝남 |
| U3 | `runPhase` 를 성분 단위로: 시간 단계는 유닛 위로 한 번, 포즈 단계 넷은 성분마다 유닛 먼저로 묶어 `runParallelByCost` 한 번. 게임 스레드 순회는 성분 순서, 되감기 훅 순회는 `AnimationOrderedList::forEach` | 끝남 |
| U4 | 레벨 제거. 외톨이 해제 지름길은 유닛 하나짜리 성분을 뺀다(`removeSingletons`) | 끝남 |

U1 ~ U4 는 통합 L(2026-10-25)에서 빌드하고 Debug `nogpu` · `race` 로 확인했다(선언 없는 교차 유닛 읽기는 `race` 에서 걸리지 않았다).
후보(단위로 세우지 않음): 큰 성분(리더 하나에 수십 유닛)이 직렬 임계 경로가 되면 성분 안의 형제 유닛(같은 위 유닛만 가진 부품들)을 다시 병렬로 나눈다. `GT.Animation.maxComponentSize` 와 4절 측정으로 필요가 보일 때 착수.

## 3. 판정과 순서 변화

**리타깃이 최종 포즈를 읽는 것은 의도에 맞는다.** 옛 주석("원본은 기본 포즈를 끝냈다 — 후처리 리그는 아직")은 순서의 부산물을 의도로 적은 것이었다. 최종 포즈가 낫다: (1) 언리얼 `Retarget Pose From Mesh` 도 평가가 끝난 포즈를 읽고, (2) 원본의 IK 가 원본 월드의 발을 디디므로 대상이 같은 모양을 따라야 어긋나지 않으며, (3) "BasePose 까지" 로 가르려면 원본이 기본 포즈 단계 끝에 사본을 한 장 더 떠야 하는데 그 비용을 낼 사례가 없다. 남은 위험(읽기만 했다): 원본에 체형 비율이 있으면 비율이 적용된 본 이동이 대상에 옮겨질 수 있고, 스프링 본이 대상의 자기 스프링과 겹쳐 두 번 흔들릴 수 있다. 사례가 나오면 "기본 포즈까지" 간선 종류를 더한다.

**게임 스레드 순회 순서가 바뀐다.** 옛 `_listActive` 는 레벨 순서였고 지금은 성분 순서(가장 앞선 등록 순, 성분 안은 위 유닛이 먼저)다. 의존이 없는 장면은 같다. 동기 그룹(`synchronizeGroups`)은 서로 다른 성분의 의존 유닛이 같은 그룹일 때만 달라지고 영향은 동률 리더 후보 깨기뿐이다. 군중(`updateCrowd`)은 성분 안 위 유닛이 먼저라 리더 부품 가입(`joinLeaderCrowdBucket`)의 전제가 지켜진다. `finishAnimationFrame` · 되감기 기록은 유닛별이라 순서가 결과에 끼지 않는다. 훅이 등록 · 해제를 부를 수 있는 순회는 `forEach` 로 돈다(`applyAnimationLOD` · `applyRewindState` · `collectDebugState`). 유닛 순회(`_componentActive`)는 해제로 바뀌지 않는다(해제는 `_componentAll` 만 건드리고 활성 목록은 다음 `collectActiveUnits` 가 다시 짓는다).

**선언하지 않은 교차 유닛 읽기는 레이스가 된다.** 옛 순서의 "모든 기본 포즈가 끝난 뒤 부착 · 후처리" 전역 장벽이 사라졌다. 읽은 범위(`Attachment` · `PostProcess` 단계 작업 — 래그돌 · 얼굴 · 로코모션 워핑 · 체형 비율 · 리그)에서는 선언 없는 교차 읽기를 찾지 못했다(리그의 `space` 대상은 지난 프레임의 자기 본, 외부 대상은 `bLive` 일 때만 이번 프레임 값을 읽으며 의존을 건다). 새 작업은 `IAnimationPhaseTask` 계약(자기 유닛과 의존으로 선언한 위 유닛만 읽는다)을 지켜야 한다.

## 4. 재는 것(통합이 돌린다)

`GT.Animation.evaluate` · `phaseTime` · `phasePose` · `componentCount` · `maxComponentSize` · `phase<단계>CpuNanos` 를 읽는다. 벤치는 `-gv_benchCharacterFollowers` · `-gv_benchCharacterWeapon`.
- K 대조군: 몸 하나에 리더 포즈 부품 K 개(K = 0 · 2 · 4) 대 같은 총 유닛 수를 독립 캐릭터로 채운 대조군(`-gv_benchCharacters` = N × (1 + K)). 두 평가 시간의 차이가 성분 안 직렬화의 비용이다. K = 0 은 U3 전후가 같아야 한다(회귀 점검).
- 무기(`-gv_benchCharacterWeapon=1`)를 더해 L = 3: U3 전(`wt/anim1`)의 `evaluate` 와 U3 후를 비교하고 이전 `levelCount` 와 U3 후 `componentCount` · `maxComponentSize` 를 같이 적는다.
- 포크-조인 5 → 2: 대기 횟수 카운터는 없으므로 L = 1(K = 0) 과 L = 3 의 `evaluate` 차를 U3 전 · 후로 대조한다(전에는 단계 수에 비례, 후에는 성분 크기에 비례). 직렬 임계 경로가 병렬도 이득을 넘는 지점(성분 크기 대 스레드 수)이 위 후보의 착수 기준이다.
