# Shooter3D 발 미끄러짐 연결

> 상태: 측정대기 · 선행: 없음 · 영역: gameplay · 옛번호: D4-6 · 진행: 2/2

앞으로 걷기 · 달리기는 재생 속도 매칭(`LocomotionSpeedUtil`, 범위 0.6 ~ 1.8)으로 맞추고, 범위를 넘는 몫은 리그 노드 `StrideWarping` 이 맡는다.
연결은 들어갔다: 몸 프리팹(`prefabs/player_body.prefab.xml`)의 `PoseModifierComponent` 가 `rigs/player_body.rig.xml`(노드 `Stride`)을 돌리고,
`ShooterAvatarComponent::applyStrideWarping` 이 프레임마다 `stride_scale`(`LocomotionSpeedUtil::computeStrideScale`) · 모델 공간 이동 방향을 넣는다. 노드가 없으면 오류를 한 번 남긴다.
뒷걸음 · 옆걸음도 같은 길이다(방향은 몸 요 기준 수평 속도). 남은 클립 쪽 일은 [ShooterCharacterIntegration](ShooterCharacterIntegration.md).

화면 확인(S2)은 시나리오 `game/shooter3d/automation/stridewarping.scenario.xml` 로 만들었다. 프로브 `Shooter3D.StrideAppliedCount`(리그가 몸 메시에 묶여 값을 넣은 프레임 수 — 0 이면 `isRigReady` 전)와
`Shooter3D.StrideScale`(마지막으로 넣은 배율, 서 있으면 1)을 단언하고, 서 있는 사진과 뒷걸음 + 달리기 키 사진을 남긴다. 사진을 견줘 발이 덜 미끄러지는지 보고,
배율 범위(`min_scale` 0.6 · `max_scale` 1.6 · `max_pelvis_drop` 0.2, 데이터)를 화면을 보고 고치는 일이 남았다.

**남은 확인(통합에서 확인):** 매니페스트 id `shooter-stride-warping-scenario`. 끝나면 통합 담당이 이 문서를 닫는다.
