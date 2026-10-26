# 애니메이션 LOD 와 군중 공유의 남은 것

> 상태: 측정대기 · 선행: 없음 · 영역: gameplay · 옛번호: D4-6 · 진행: 4/4

애니메이션 LOD(`AnimationLOD.h` — 리그 노드도 화면 크기 `min_screen_size` 로 꺼진다)와 군중 공유(`AnimationCrowd.h`)는 들어갔고 이 문서는 그 위에 남은 것이다.

## 1. 결정

| 물음 | 결정 | 이유 |
|---|---|---|
| 읽기만 하는 작업(외형 소켓)이 붙은 유닛도 나누나 | 나눈다. 작업마다 `IAnimationPhaseTask::getPoseAccess`(순수 가상, `Writes` · `ReadsOnly`)를 정하고 포즈를 쓰는 작업이 꼭 하나일 때 그 작업에만 묻는다 | 예전에는 "작업 수 = 1" 로 대신 셌다 — 읽기 작업이 생길 때마다 공유가 조용히 꺼졌다. 기본값을 두지 않아 새 작업이 반드시 정한다 |
| 리더를 따르는 부품(옷 · 머리)은 | 리더가 묶음을 나누면 리더 묶음과 같은 클립 · 속도 · 변형 칸의 자기 묶음(`AnimationCrowd::joinFollowerBucket`)에 든다. 자기 포즈를 쓰는 작업이나 모프가 있으면 혼자다 | 리더 포즈가 클립 하나이므로 본 이름으로 옮긴 포즈와 같은 클립을 같은 시각에 샘플한 포즈가 같다. 칸은 리더를 따라 옮긴다(붙들면 부품이 몸과 다른 위상으로 돈다) |
| 섞는 중인 유닛은(언리얼 Animation Sharing 의 블렌드 액터) | 반복 클립 둘 사이의 페이드는 섞기 묶음(`joinBlendBucket`)을 나눈다. 키는 출발 상태 · 칸 · 들어가는 클립 · 페이드 길이 · 시작 프레임, 가중치는 멤버가 프레임마다 정한다 | 같은 프레임에 같은 전이를 시작한 유닛만 가중치가 같다. 반복하지 않는 클립 · 블렌드 스페이스로 가는 페이드, 끊긴 페이드, 레이어는 혼자다 |
| 어떤 때만 쓰는 작업의 포즈 접근 선언은 | 프레임마다 읽는 값이고(시간 단계 뒤), 작업마다 "쓸 것이 있는가" 판정 하나를 선언과 쓰기가 함께 쓴다: 후처리 리그 `RigInstance::canWritePose`, 방향 보정 `isOrientingPose`, 리타깃 `isRetargeting`, 얼굴 `isDrivingFace`, 체형 비율(비율 목록이 빔), 래그돌은 깨어 있을 때만 `Writes`. 애니메이터는 늘 `Writes`, 소켓 부착은 늘 `ReadsOnly`. 읽기만 한다고 선언한 작업이 포즈 · 모프를 쓰면 Debug 단언(`SkeletalMeshComponent::runTasks`)이 잡는다 | 래그돌이 늘 `Writes` 라 군중 공유가 0 이 된 일과 같은 모양이 다른 작업에 있었다. 반대 방향("쓴다고 하고 안 쓴다")은 단언으로 가를 수 없어 판정을 하나로 합치고 `AnimationPoseAccessTest` 로 막는다. 커브로 움직이는 리그 노드 가중치는 포즈 단계에서야 정해져 "쓸 수 있다" 로 센다 |
| Shooter3D 군중 켜기 | 적 프리팹(`skeleton.prefab.xml`)의 `_bShareCrowdPose`. 외형의 스킨드 부품은 몸의 값을 따른다(`CharacterAppearanceComponent::syncParts`) | 값의 정본은 몸 프리팹이다 |

## 2. 단계 (모두 코드 작성, 빌드 · 화면 미확인)

| 단계 | 내용 | 상태 |
|---|---|---|
| C1 | 작업의 포즈 접근 형식(`AnimationPoseAccess`)과 읽기 작업이 붙은 유닛의 공유(`AnimationCrowdTest.ReaderTasksAndLeaderFollowersShareBuckets`) | 코드 · 테스트 작성 |
| C2 | 리더를 따르는 부품의 묶음, Shooter3D 적 군중 켜기 | 코드 · 데이터 작성 |
| C3 | 섞기 묶음(`SkeletalAnimatorComponent::describeSharedPose` 의 페이드 요청, `CrossfadeSharesABlendBucketThenRejoins`) | 코드 · 테스트 작성 |
| C4 | 본 LOD 를 메시 LOD 와 묶기(`MeshAssetTest.SkinnedLODLevelsRoundTripAndSkinInstanceKeepsThem`) | 코드 · 테스트 작성 |

## C4 가 한 것

스킨드 메시가 LOD 를 받게 하고 고른 메시 단계가 본 LOD 단계를 정하게 했다.
- 임포트: 스킨드 모델도 LOD 규칙(`makeLODLevels( …, skinBoneCount, … )`)을 받는다 — 단계마다 정점에 스킨(`MeshLODLevel::_listSkinVertex`)을 싣는다. 모프 타깃이나 VRM 구간이 있는 모델의 LOD 규칙은 임포트 오류다(구간과 LOD 는 함께 쓰지 못한다).
- 파일: `.mesh` 에 선택 덩어리 `LSKN`(낮은 단계의 스킨 — `LODS` 뒤, 단계 수 · 정점 수가 맞아야 한다). LOD 없는 파일은 바이트까지 같다.
- 메시: `Mesh::setLODs` 가 스킨드 메시를 받는다(단계마다 정점과 같은 길이의 스킨, 본 번호가 본 수 안). 스킨을 먼저 설정해야 하고 `setSkin` 은 LOD 를 뗀다. `createSkinInstance` 사본도 LOD 를 이어받는다.
- 스키닝: `GPUMeshMorphPool` 이 LOD 0 뒤에 낮은 단계의 레스트 정점과 가중치를 이어 올려 한 번에 변형한다(결과 구간은 `getDrawVertexCount` 길이, `MeshComponent::findDrawSection` 은 GPU 모프가 켜진 메시도 낮은 단계 범위를 그린다).
- 고르기: `MeshLODSystem` 은 `SkeletalMeshComponent` 도 고른다. `IAnimationLODClient::findMeshLODIndex` 를 `AnimationSystem::updateLOD` 가 읽어 `_boneLODLevel` 을 `min( 메시 단계, 본 LOD 단계 수 - 1 )` 로 정한다(화면 크기 임계값의 정본은 메시 단계 하나, 메시 단계는 FlushEnd 에 골라져 본 LOD 는 한 프레임 늦는다).
- 한계: 외형이 구운 메시(`createBakedMesh`)와 병합 메시(`mergeMeshes`)는 LOD 를 갖지 않는다 — 몸에 LOD 를 쓰려면 병합을 끄고 베이크하지 않는 차림이어야 한다.

## 남은 것

- 포즈 접근 선언 감사(통합에서 확인): 매니페스트 id `pose-access-test`(`AnimationPoseAccessTest` — 작업마다 쉬면 묶음 · 쓰면 혼자 · 다시 쉬면 묶음, 거짓 읽기 작업의 단언. 읽기 작업 단언이 기존 군중 · 리그 · 리타깃 · 얼굴 테스트의 실제 구현을 잡지 않는지도 같은 묶음에서 본다). 체형 비율 작업의 "비율이 있을 때 쓴다" 쪽은 외형 조립이 등록해야 해서 이 테스트가 닿지 않는다.
- C4 확인(통합에서 확인): 매니페스트 id `skinned-lod-mesh-test` · `skinned-lod-gpu-check`(앞은 `MeshAssetTest` 필터 — `.mesh` 왕복 · 사본 · 거절, 뒤는 LOD 규칙을 건 스킨드 모델을 `--import-models` 로 다시 임포트해 먼 거리 스크린샷에서 낮은 단계가 스키닝되어 그려지는지, 본 LOD 단계가 메시 단계를 따르는지 눈으로 본다 — GPU 필요).
- C1 · C2 · C3 · 측정 확인(통합에서 확인): 매니페스트 id `shooter-crowd-scenario` · `shooter-crowd-measure`(앞은 자동 플레이로 `Animation.CrowdSharedUnits` · `CrowdSoloUnits` 탐침을 기록하고 사진 — 적 무기 · 방패가 손을 따라가는지, 섞기 묶음이 끝날 때 위상이 튀는지 본다; 튀면 섞기 묶음의 들어가는 클립 시각을 다음 묶음의 칸 시각에 맞추는 길을 본다. 뒤는 적 30 마리에서 `GT.Animation` 전후, Release, 혼자 돌 때). 첫 실행(2026-10-25)은 `CrowdSharedUnits` 0 · `CrowdSoloUnits` 4 로 졌다 — 래그돌(`RagdollAnimationBinding`)이 포즈를 쓰는 작업이라 혼자 평가했다. 래그돌을 깨어 있을 때만 `Writes` 로 고쳤다(wt/gfxfix, `AnimationCrowdTest.TaskThatWritesOnlyWhileActiveBlocksSharingOnlyThen`). 빌드 · GPU 대기: 시나리오를 다시 돈다. 휘두름 · 움찔 클립(반복하지 않음)으로 갈 때 혼자로 내려가는 것은 맞다.
