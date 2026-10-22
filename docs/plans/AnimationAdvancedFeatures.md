# 애니메이션 고급 기능

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-16 · 진행: 1/4

전신 IK, 리타게팅 도구, 애니메이션 압축 · 샘플링 최적화. 모션 매칭은 보류라 [MotionMatching](MotionMatching.md) 에 따로 있다.
처음에는 측정(A1)이 착수 순서를 정하기로 했으나, 구현할 수 있는 것은 측정을 기다리지 않고 한다는 사용자 지시(2026-10-22)로 A2 · A4 를 먼저 했다.

- A2 전신 IK(끝): 리그 노드 `FullBodyIK`(`RigIKNodes.cpp`) — 사슬 뿌리에서 목표까지 모자란 거리를 가중 평균해 골반을 옮기고(`max_pelvis_offset` 까지), 사슬마다 FABRIK 으로 붙인다.
  `RigAssetTest.FullBodyIKShiftsPelvisTowardWeightedEffectors`. 회전 제한 · 척추 굽힘은 하지 않는다(사슬에 척추 본을 넣는다).

## A1 비용 곡선 재기

- Release 에서 합성 리그 벤치를 본 수(20 · 60 · 150 · 300)와 유닛 수로 돌려 단계별 시간(`GT.Animation.*` — 샘플 · 블렌드 · 리그 노드 · 팔레트)을 표로 남긴다. 측정 규칙은 [검증과 측정](../08_Verification.md).
- `FullBodyIK` 노드의 비용(사슬 넷 · 골반 반복 4)을 같은 표에 더한다.

## A3 압축 · 샘플링

- 코덱별 디코드 비용(`AnimationCodec`)을 재고, 키 줄이기 오차 예산을 본 깊이로 나눈다(언리얼 ACL 의 계층 오차). 같은 프레임에 같은 클립 · 같은 시각을 묻는 유닛은 군중 묶음이 이미 나눈다.
  A1 표에서 샘플(디코드)의 몫이 클 때 한다 — 측정 없이 코덱을 바꾸면 크기 · 정밀도를 근거 없이 바꾸게 된다.

## A4 리타게팅 도구

- 창(`RetargetPanel`, 캐릭터 에디터 플러그인)이 프로필(`*.retarget.json`)의 두 스켈레톤 · 뿌리와 골반 짝 · 사슬 쌍 · 본 이름 테이블 · 이동 방법을 고치고, 런타임 리타기터로 묶어 본 점검
  (`RetargetProfileInspector` — 사슬 길이 비 · 짝지은 본 수)을 보이며, 묶이지 않는 프로필은 저장하지 않는다(`RetargetProfile::toJSON`, `RetargetTest.ProfileWritesBackAndInspectsAgainstSkeletons`).
- 창 화면: 2026-10-22 Release 에디터에서 명령 팔레트로 열고 "Open..." 으로 `knight_to_minion.retarget.json` 을 골라 `CaptureEditorUI` 로 봤다(스켈레톤 · 뿌리와 골반 · 선택 · 사슬 칸이 채워진다).
- 남은 것: 두 스켈레톤을 겹쳐 그리는 3D 미리보기와 기준 포즈(리타깃 포즈 — 대상 레퍼런스를 원본에 맞춰 돌린 포즈) 편집. 기준 포즈는 프로필 형식에 칸이 없어 형식(대상 본별 회전 덮어쓰기)부터 정한다.
  없는 경로로 연 문서는 지금 "읽지 못함" 으로 막힌다 — 새 프로필 만들기(빈 문서)도 남은 것이다.
