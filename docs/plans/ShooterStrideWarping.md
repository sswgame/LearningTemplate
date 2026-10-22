# Shooter3D 발 미끄러짐 연결

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-6 · 진행: 1/2

앞으로 걷기 · 달리기는 재생 속도 매칭(`LocomotionSpeedUtil`, 범위 0.6 ~ 1.8)으로 맞추고, 범위를 넘는 몫은 리그 노드 `StrideWarping` 이 맡는다.
연결은 들어갔다: 몸 프리팹(`prefabs/player_body.prefab.xml`)의 `PoseModifierComponent` 가 `rigs/player_body.rig.xml`(노드 `Stride`)을 돌리고,
`ShooterAvatarComponent::applyStrideWarping` 이 프레임마다 `stride_scale`(`LocomotionSpeedUtil::computeStrideScale`) · 모델 공간 이동 방향을 넣는다. 노드가 없으면 오류를 한 번 남긴다.
뒷걸음 · 옆걸음도 같은 길이다(방향은 몸 요 기준 수평 속도). 남은 클립 쪽 일은 [ShooterCharacterIntegration](ShooterCharacterIntegration.md).

## S2 화면 확인

- 빌드 뒤 Shooter3D 를 돌려 빠른 이동(달리기 상한 위 · 뒷걸음)에서 발이 덜 미끄러지는지, 서 있을 때 포즈가 바뀌지 않는지(배율 1) 캡처로 본다.
- 리그가 몸 메시(외형이 묶은 스켈레톤)에 붙는 순서를 로그로 확인한다 — `isRigReady` 전 프레임은 넣지 않는다.
- 배율 범위(`min_scale` 0.6 · `max_scale` 1.6 · `max_pelvis_drop` 0.2)는 데이터라 화면을 보고 고친다.
