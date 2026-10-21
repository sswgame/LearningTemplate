# 애니메이션 LOD 와 군중 공유의 남은 것

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-6 · 진행: 3/4

애니메이션 LOD(`AnimationLOD.h` — 리그 노드도 화면 크기 `min_screen_size` 로 꺼진다)와 군중 공유(`AnimationCrowd.h`)는 들어갔다. 이 문서는 그 위에 남은 것이다(후보 `Candidates-gameplay.md` 에서 옮겼다).

## 1. 결정

| 물음 | 결정 | 이유 |
|---|---|---|
| 읽기만 하는 작업(외형 소켓)이 붙은 유닛도 나누나 | 나눈다. 작업마다 `IAnimationPhaseTask::getPoseAccess`(순수 가상, `Writes` · `ReadsOnly`)를 정하고, 포즈를 쓰는 작업이 꼭 하나일 때 그 작업에만 묻는다 | 예전에는 "작업 수 = 1" 로 대신 셌다 — 읽기 작업이 생길 때마다 공유가 조용히 꺼졌다. 기본값을 두지 않아 새 작업이 반드시 정한다 |
| 리더를 따르는 부품(옷 · 머리)은 | 리더가 묶음을 나누면 리더 묶음과 같은 클립 · 속도 · 변형 칸의 자기 묶음(`AnimationCrowd::joinFollowerBucket`)에 든다. 자기 포즈를 쓰는 작업이나 모프가 있으면 혼자다 | 리더 포즈가 클립 하나이므로 본 이름으로 옮긴 포즈와 같은 클립을 같은 시각에 샘플한 포즈가 같다. 칸은 리더를 따라 옮긴다(붙들면 부품이 몸과 다른 위상으로 돈다) |
| 섞는 중인 유닛은(언리얼 Animation Sharing 의 블렌드 액터) | 반복 클립 둘 사이의 페이드는 섞기 묶음(`joinBlendBucket`)을 나눈다. 키는 출발 상태 · 칸 · 들어가는 클립 · 페이드 길이 · 시작 프레임이다. 가중치는 멤버가 프레임마다 정한다 | 같은 프레임에 같은 전이를 시작한 유닛만 가중치가 같다. 반복하지 않는 클립 · 블렌드 스페이스로 가는 페이드, 끊긴 페이드, 레이어는 여전히 혼자다 |
| Shooter3D 군중 켜기 | 적 프리팹(`skeleton.prefab.xml`)의 `_bShareCrowdPose`. 외형의 스킨드 부품은 몸의 값을 따른다(`CharacterAppearanceComponent::syncParts`) | 값의 정본은 몸 프리팹이다 |

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| C1 | 작업의 포즈 접근 형식(`AnimationPoseAccess`)과 읽기 작업이 붙은 유닛의 공유 | 코드와 `AnimationCrowdTest.ReaderTasksAndLeaderFollowersShareBuckets` 작성(빌드 미확인) |
| C2 | 리더를 따르는 부품의 묶음(`joinFollowerBucket`), Shooter3D 적 군중 켜기 | 코드 · 데이터 작성(빌드 · 화면 미확인) |
| C3 | 섞기 묶음(`joinBlendBucket`, `SkeletalAnimatorComponent::describeSharedPose` 의 페이드 요청) | 코드와 `AnimationCrowdTest.CrossfadeSharesABlendBucketThenRejoins` 작성(빌드 미확인) |
| C4 | 메시 LOD 가 생기면 본 LOD 를 메시 LOD 와 묶기 | 착수 조건: 메시 LOD([PreloadAndLod](PreloadAndLod.md) P5) |

## 남은 것

- C1 · C2 확인: Shooter3D autoplay 에서 프로파일 카운터 `GT.Animation.crowdSharedUnits` 가 적 수만큼 오르고 `crowdSoloUnits` 가 쫓는 동안 0 근처인지, 적 무기 · 방패(소켓 부품)가 손을 따라가는지 스크린샷으로 본다. 휘두름 · 움찔 클립(반복하지 않음)으로 갈 때 혼자로 내려가는 것은 맞다.
- C3 확인: 섞기 묶음이 끝나 들어간 클립의 묶음으로 갈 때 반 칸 안쪽의 시각 차이가 튀어 보이는지(혼자 → 묶음과 같은 크기). 튀면 섞기 묶음의 들어가는 클립 시각을 다음 묶음의 칸 시각에 맞추는 길을 본다.
- 측정: 적 30 마리에서 `GT.Animation` 전후(Release, 혼자 돌 때).
