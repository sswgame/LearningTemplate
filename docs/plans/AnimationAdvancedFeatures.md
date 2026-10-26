# 애니메이션 고급 기능

> 상태: 측정대기 · 선행: 없음 · 영역: gameplay · 옛번호: D4-16 · 진행: 4/4

전신 IK, 리타게팅 도구, 애니메이션 압축 · 샘플링 최적화. 모션 매칭은 보류라 [MotionMatching](MotionMatching.md) 에 따로 있다.
처음에는 측정(A1)이 착수 순서를 정하기로 했으나, 구현할 수 있는 것은 측정을 기다리지 않고 한다는 사용자 지시(2026-10-22)로 구현을 먼저 했다. 남은 것은 실행 확인과 측정값을 읽는 일이다.

- A2 전신 IK(끝): 리그 노드 `FullBodyIK`(`RigIKNodes.cpp`) — 사슬 뿌리에서 목표까지 모자란 거리를 가중 평균해 골반을 옮기고(`max_pelvis_offset` 까지), 사슬마다 FABRIK 으로 붙인다.
  `RigAssetTest.FullBodyIKShiftsPelvisTowardWeightedEffectors`. 회전 제한 · 척추 굽힘은 하지 않는다(사슬에 척추 본을 넣는다).
- A1 비용 곡선(코드 끝): 합성 리그 벤치(`SyntheticRigBenchTest.EvaluateByBonesCharactersAndMode`)에 모드 `own+fullbody` 를 더했다 — 몸 사슬 넷(`leg_*_00` · `arm_*_00` 줄)을 골반 `root` 에 묶고
  목표를 사슬 끝 앞으로 0.3 m 밀어 골반 반복 4 가 끝까지 돈다. 단계별 시간은 프로파일 구간 `GT.Animation.phaseTime`(벽시계)과 포즈 단계의 CPU 시간 합 카운터 `GT.Animation.phaseBasePoseCpuNanos`(샘플 · 블렌드) · `phaseAttachmentCpuNanos` · `phasePostProcessCpuNanos`(리그 노드) · `phaseSkinPaletteCpuNanos`(팔레트)로 읽는다(포즈 단계 합친 벽시계는 `phasePose`; 이유는 [결정 기록 5-6](../decisions/5-6_ObjectsInput.md)). 카운터는 스레드를 더한 나노초라 프레임당 값을 유닛 수로 나눠 읽는다.
  표(본 수 20 · 60 · 150 · 300 × 유닛 수 × 모드)는 Release 에서 혼자 돌려 `docs/08_Verification.md` 에 남긴다. 측정 규칙은 [검증과 측정](../08_Verification.md).
- A3 압축 · 샘플링(코드 끝): 키 줄이기 오차 예산을 본 깊이로 나누는 일(언리얼 ACL 의 계층 오차)은 ACL 이 이미 한다 — 코덱이 트랙의 부모(`_listTrackParent`)를 넘기고 가상 정점(`_shellDistance`)의 모델 공간 오차를 잰다
  (`ACLAnimationCodec::compress`). 남은 것은 코덱별 디코드 비용이고, 케이스 `SyntheticRigBenchTest.DecodeCostByCodec` 가 Raw · ACL · ACL 절반 마스크(본 LOD)의 호출당 ns · 압축 크기 · 압축률 · 최대 오차를 찍는다.
  A1 표에서 샘플(디코드)의 몫이 클 때만 코덱 설정을 바꾼다 — 측정 없이 바꾸면 크기 · 정밀도를 근거 없이 바꾸게 된다.
- A4 리타게팅 도구(끝): 창(`RetargetPanel`, 캐릭터 에디터 플러그인)이 프로필(`*.retarget.xml`)의 두 스켈레톤 · 뿌리와 골반 짝 · 사슬 쌍 · 본 이름 테이블 · 이동 방법을 고치고,
  런타임 리타기터로 묶어 본 점검(`RetargetProfileInspector`)을 보이며, 묶이지 않는 프로필은 저장하지 않는다(`RetargetTest.ProfileWritesBackAndInspectsAgainstSkeletons`).
  - 3D 미리보기: 창이 `EditorPreviewPanel` 이고 원본(파랑) · 대상 레퍼런스(회색) · 리타깃 포즈를 얹은 대상(주황)을 뷰포트 위에 선으로 겹쳐 긋는다(`EditorProjectedLineRenderer`).
  - 기준 포즈(리타깃 포즈): 프로필의 `target_pose`(대상 본 + 부모 공간 도)를 창에서 고치고(끄는 동안 미리보기가 바뀌고 놓으면 되돌리기 한 단계), 런타임 묶기(`PoseRetargeter::initialize`)와 미리보기가
    같은 길(`RetargetPoseUtil::applyTargetPose`)로 대상 레퍼런스에 얹는다. `RetargetTest.TargetPoseTurnsTheTargetReferenceAndRoundTrips`.
  - 새 프로필: 경로 칸에 리소스 id 를 적고 New 를 누르면 없는 파일은 빈 문서(`ToolAssetLoadResult::Missing`)로 열린다(`EditorDocumentPanel::requestOpenDocument` 를 파생 창이 쓰게 열었다).

**남은 확인(통합에서 확인):** 매니페스트 id `animation-cost-curve`(리타깃 테스트와 에디터 리타깃 시나리오는 통합 Q 에서 통과했다). 측정값(A1 표 · A3 디코드 비용)을 읽어 [검증과 측정](../08_Verification.md) 2절에 남기고 샘플의 몫이 크면 코덱 설정을 정한다. 확인이 끝나면 통합 담당이 이 문서를 닫는다.
