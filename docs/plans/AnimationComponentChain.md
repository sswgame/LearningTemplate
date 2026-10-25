# 애니메이션 평가를 연결 성분 단위로 옮기기

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: 없음 · 진행: 표

`AnimationSystem` 은 "단계 먼저, 그 안에서 레벨" 순서로 레벨마다 `engine::runParallelByCost` 의 전체 대기를 해서, 의존 깊이가 L 이면 한 프레임에 최대 5L 번 기다린다(시간 단계 1 + 포즈 단계 넷, 각각 L 레벨).
이 문서는 의존으로 이어진 유닛을 연결 성분으로 모아 성분 안은 직렬로, 성분 사이는 한 번에 병렬로 돌려 전체 대기를 2 번(시간 단계 1 + 포즈 단계 합친 1)으로 줄이는 일을 단위로 나눈다.

## 1. 결정

| 물음 | 결정 | 이유 |
|---|---|---|
| 평가의 병렬 단위는 | 의존 간선으로 이어진 유닛의 연결 성분이다. 성분 안은 위상 순서로 **유닛 먼저**(한 유닛의 포즈 단계를 이어서) 직렬로 돌고, 성분 사이는 `runParallelByCost` 한 번이다 | 의존 깊이와 상관없이 전체 대기가 시간 단계 1 번과 포즈 단계 합친 1 번이다. 독립 캐릭터만 있는 장면(성분 N 개, 크기 1)은 지금과 같은 모양이다 |
| 간선의 뜻은 | 위 유닛의 **모든 단계**가 끝난 뒤에 아래 유닛이 시작한다(`AnimationDependencyEdge`). 성분 안 순서가 이 뜻을 지킨다 | 아래 유닛(리더 포즈를 따르는 부품, 리그 대상)은 위 유닛의 최종 포즈를 읽는다. 지금 순서는 단계마다 위 유닛이 먼저일 뿐이라 부품이 위 유닛의 부착 단계(체형 비율)와 후처리 단계(IK)를 적용하기 전 로컬 포즈를 복사할 수 있다(`SkeletalMeshComponent::copyLeaderPose`, `CharacterAppearanceProportionTask`) |
| 고리는 | 고리에 든 유닛과 그 아래 유닛은 등록 순서로 성분의 뒤에 직렬로 놓는다 | 고리는 데이터 오류이고 로그로 알린다. 순서를 강제하지 않되 같은 성분이라 서로 병렬로 돌지 않는다(레이스 없음) |
| 성분 안 직렬 실행에 `TaskManager` 의 의존 API 를 쓰나 | 쓰지 않는다. 성분 하나를 `runParallelByCost` 항목 하나로 돌린다 | 비용 기반 분배(결정 기록의 "애니메이션 평가는 비용 기반 분배")와 고정 청크 크기 기각을 그대로 지킨다. 성분이 크기 1 인 독립 유닛이 대부분이라 새 의존 그래프 실행기가 얻는 것이 없다 |
| 레벨은 | U3 까지 평가 순서이고, 전환 뒤에는 `getLevels` 의 진단용으로만 남기거나 지운다 | 아래 U4 |

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| U1 | 정합 테스트(`SkeletalAnimationTest.FollowerReadsLeaderFinalPoseAfterAttachmentPhase` · `PoseModifierTest.FollowerReadsLeaderPoseAfterRig`), 계측 카운터(`GT.Animation.levelCount` · `componentCount` · `maxComponentSize`), 벤치 변수(`-gv_benchCharacterFollowers` · `-gv_benchCharacterWeapon`) | 코드 작성(빌드 · 실행 미확인). 정합 테스트 둘은 현재 순서에서 실패해야 한다 |
| U2 | `rebuildLevels` 가 연결 성분을 함께 짓고(`AnimationComponentSet`, union-find + 성분별 위상 순서), `collectActiveUnits` 가 성분별 일하는 구간을 채운다. 아직 평가는 레벨을 따른다 | 코드와 `SkeletalAnimationTest.Components*` 테스트 작성(빌드 미확인) |
| U3 | `runPhase` 를 성분 단위로 옮긴다: 시간 단계는 따로 한 번, 포즈 단계 넷은 성분마다 유닛 먼저로 묶어 `runParallelByCost` 한 번. `_listActive` 와 동기 그룹 · 군중 순서가 달라지지 않는지 본다 | 대기 |
| U4 | 레벨 제거 여부를 정한다(`getLevels` 를 쓰는 테스트 · 진단 정리) | 대기 |

U1 의 벤치 변수로 재는 것: 몸 하나에 리더 포즈 부품 K 개(K = 0 · 2 · 4)와 같은 총 유닛 수를 독립 캐릭터로 채운 대조군(`-gv_benchCharacters` = N × (1 + K)), 그리고 무기(PoseModifier 대상)를 더해 L = 3 인 경우를 U3 전후 `GT.Animation.evaluate` 로 비교한다.
