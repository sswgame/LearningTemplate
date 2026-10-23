# 후처리 리그 편집 저장과 기즈모

> 상태: 측정대기 · 선행: 없음 · 영역: gameplay · 옛번호: D4-6 · 진행: 3/3

**남은 확인(통합에서 확인):** `RigAssetTest.*` 와 리그 디버그 창 · 대상 기즈모 화면(아래 "남은 것"). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

후처리 리그(`PoseModifierComponent`)의 시퀀서 `RigSlot` 트랙(`setSlotWeight` 에 키 찍기)과 리그 디버그 창(`RigDebugPanel`)은 들어갔다. 이 문서는 창에서 고친 값을 에셋에 남기는 길과 대상 본 기즈모를 다룬다.
세 단계의 코드는 다 썼다. 남은 것은 빌드 · 화면 확인이다(아래). 확인이 끝나면 이 문서를 지운다.

## 1. 결정

| 물음 | 결정 | 이유 |
|---|---|---|
| 창에서 고친 값을 어디에 쓰나 | 리그 에셋(`*.rig.xml`)의 노드 `weight` 와 새 공통 키 `enabled`(적지 않으면 켜짐), 대상 `translation` · `rotation` | 값의 정본은 데이터다. 런타임 인스턴스의 복제본만 바꾸면 다시 읽을 때 사라진다 |
| 파일을 어떻게 고치나 | `RigAsset::rewriteSettings`(노드 · 대상 목록을 한 묶음 `RigSaveParams` 로) — JSON 문서에서 이름의 노드 · 대상만 그 키를 바꾸고 다른 키는 그대로 둔 뒤, 결과를 런타임 로더로 다시 읽어 규칙을 본다 | 리그 전체 쓰기(노드 종류마다 키 쓰기)는 노드 종류가 늘 때마다 쓰기 · 읽기가 어긋날 자리를 만든다. 고칠 키만 바꾸면 쓰는 쪽이 종류를 몰라도 된다. 노드용 · 대상용 함수를 따로 두지 않는다(같은 읽기 · 검사 · 다시 읽기) |
| `enabled` 를 언제 쓰나 | 끈 노드이거나 파일에 이미 있을 때만 | 켜진 노드마다 `"enabled": true` 가 붙으면 파일이 시끄럽다. "적지 않음 = 켜짐" 은 형식이 정한 값이다 |
| 대상 기즈모가 무엇을 옮기나 | 자기 유닛 대상(본 · 소켓)만. 부모 공간 = 데이터 오프셋의 역 × 인스턴스의 합친 오프셋(`RigInstance::tryGetOwnTargetBinding`) × 본 모델 × 유닛 월드 | 바깥 대상(다른 유닛 · 오브젝트)의 부모 공간은 호스트(`PoseModifierComponent`)가 프레임마다 넣는 값이라 창이 되짚을 수 없다 — 그 오브젝트를 옮긴다 |
| 기즈모가 어디 그려지나 | 뷰포트 시각화 "Rig Target"(`SW_EDITOR_VISUALIZER`, 창과 한 파일) — 고른 대상이 있는 동안 뷰포트 마우스를 가져간다(`EditorViewportMouseClaim`) | 창은 도구 창이라 뷰포트를 그리지 않는다. 마우스를 가져가야 선택 기즈모 · 클릭 선택과 겹치지 않는다 |
| 언제 저장하나 · 되돌리기 | 끌기를 놓을 때 한 번 파일에 쓰고, 파일 글 전후를 되돌리기 하나로 기록한다 | 끄는 동안 쓰면 파일 감시가 프레임마다 다시 읽는다. 인스턴스 값이 아니라 파일이 바뀌므로 되돌리기도 파일 글이다 |

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| R1 | 리그 디버그 창 "Save to rig" — 노드 되쓰기, 공통 키 `enabled` | 코드와 `RigAssetTest.RewriteNodeSettingsKeepsOtherKeys` 작성(빌드 · 테스트 미확인) |
| R2 | 대상 오프셋 기즈모 — 창의 대상 표("Gizmo" 칸) · 시각화 `rig_target_gizmo` · `RigAsset::rewriteSettings` 의 대상 키 · `RigInstance::tryGetOwnTargetBinding` | 코드와 `RigAssetTest.RewriteTargetOffsetRoundTrips` 작성(빌드 · 테스트 · 화면 미확인) |
| R3 | `Engine/Sequencer` 가 `Engine/Character` 헤더를 include 하는지(레이어 검사) | 확인함 — 지금 include 가 없다(`git grep`, 2026-10-21) |

## 남은 것

- 테스트: `EngineTest --test_filter=RigAssetTest.*`.
- 화면 확인(`-EnableEditor`, Shooter3D `arena.scene.xml` 플레이): 플레이어 몸을 고르고 리그 디버그 창에서 가중치 · 켜짐을 바꾸고 "Save to rig" 뒤 파일 감시가 다시 읽어도 값이 남는지(`rigs/shooter_knight.rig.xml`).
- 화면 확인(대상 기즈모): 자기 유닛 대상이 있는 리그(벤치 `knight_cape.rig.xml` 의 `Shoulders` 는 다른 유닛이라 안 되고, 본 · 소켓 대상이 자기 유닛인 리그)를 고르고 "Gizmo" 를 켜 이동 · 회전으로 끈 뒤 놓으면 파일의 `translation` · `rotation` 이 바뀌고 다시 읽은 자리에 기즈모가 남는지, Ctrl+Z 가 파일을 되돌리는지. 창 캡처는 `-gv_screenshotEditor`.
