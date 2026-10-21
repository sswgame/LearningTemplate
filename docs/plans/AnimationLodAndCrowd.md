# 애니메이션 LOD 와 군중 공유

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-6 · 진행: 2/4

애니메이션 LOD(`AnimationLOD.h` — 리그 노드도 화면 크기 `min_screen_size` 로 꺼진다)와 군중 공유(`AnimationCrowd.h`)에 이어 아래가 들어갔다.

- 작업의 포즈 몫을 형식으로 나눴다: `IAnimationPhaseTask::getPoseRole`(순수 가상, `AnimationTaskPoseRole::Writes` · `ReadsOnly`). 묶음과 나누는 조건은 "포즈를 쓰는 작업이 하나"다
  (`SkeletalMeshComponent::findSolePoseWriter`). 외형의 소켓 작업(`CharacterAppearanceSocketTask`)은 `ReadsOnly` 라 나누기를 막지 않고, 묶음의 포즈를 읽는다(`getModelSpaceTransforms`).
  리더를 따르는 부품은 리더의 `getLocalPose()` 가 묶음의 포즈라 그대로 따른다(묶음은 포즈 단계 앞에 평가된다).
- 섞기: 반복 클립 둘 사이의 페이드는 두 묶음(나가는 · 들어오는 클립)의 포즈를 유닛 가중치로 섞는다(`AnimationCrowdMode::Blended`, 클립 샘플 없음, 사본 풀의 메시로 스킨).
  애니메이터가 페이드를 요청에 싣는다(`AnimationSharedPoseRequest::_pTargetClip` · `_targetTime` · `_blendWeight`). inertialization 중이면 나누지 않는다.
- Shooter3D 적(`prefabs/skeleton.prefab.xml`)에 `_bShareCrowdPose` 를 켰다.

## L3 확인

- 빌드 뒤 `AnimationCrowdTest`(새 `ReadOnlyTasksKeepSharingAndReadTheBucketPose`, 바뀐 `CrossfadeBlendsTwoBucketsThenRejoins`)와 애니메이션 · 포즈 모디파이어 테스트를 돌린다.
- Shooter3D 를 돌려 프로파일 카운터(`GT.Animation.crowdSharedUnits` · `crowdBlendedUnits` · `crowdSoloUnits`)로 적이 묶음과 나누는지, 캡처로 무기 · 투구 소켓이 몸을 따라가는지 본다.
  외형이 몸 메시를 다시 입히면 묶음 메시와 다투는지(머티리얼 · 메시가 바뀌는지)를 본다.

## L4 본 LOD 를 메시 LOD 와 묶기

- 메시 LOD([PreloadAndLod](Candidates-gameplay.md#preloadandlod) 의 메시 LOD)가 들어오면 본 LOD 단계를 메시 LOD 단계에 맞춘다(낮은 메시 LOD 에서 쓰지 않는 본은 평가하지 않는다).
