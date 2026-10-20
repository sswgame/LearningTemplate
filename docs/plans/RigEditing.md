# 후처리 리그 편집 저장과 기즈모

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-6 · 진행: 2/3

후처리 리그(`PoseModifierComponent`)의 시퀀서 `RigSlot` 트랙(`setSlotWeight` 에 키 찍기)과 리그 디버그 창(`RigDebugPanel`)은 들어갔다. 이 문서는 창에서 고친 값을 에셋에 남기는 길과 대상 본 기즈모를 다룬다.

## 1. 결정

| 물음 | 결정 | 이유 |
|---|---|---|
| 창에서 고친 값을 어디에 쓰나 | 리그 에셋(`*.rig.json`)의 노드 `weight` 와 새 공통 키 `enabled`(적지 않으면 켜짐) | 값의 정본은 데이터다. 런타임 인스턴스의 복제본만 바꾸면 다시 읽을 때 사라진다 |
| 파일을 어떻게 고치나 | `RigAsset::rewriteNodeSettings` — JSON 문서에서 이름의 노드만 `weight` · `enabled` 를 바꾸고 다른 키는 그대로 둔 뒤, 결과를 런타임 로더로 다시 읽어 규칙을 본다 | 리그 전체 쓰기(노드 종류마다 키 쓰기)는 노드 종류가 늘 때마다 쓰기 · 읽기가 어긋날 자리를 만든다. 고칠 키만 바꾸면 쓰는 쪽이 노드 종류를 몰라도 된다 |
| `enabled` 를 언제 쓰나 | 끈 노드이거나 파일에 이미 있을 때만 | 켜진 노드마다 `"enabled": true` 가 붙으면 파일이 시끄럽다. "적지 않음 = 켜짐" 은 형식이 정한 값이다 |

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| R1 | 리그 디버그 창 "Save to rig" — `RigAsset::rewriteNodeSettings`, 공통 키 `enabled` | 코드와 `RigAssetTest.RewriteNodeSettingsKeepsOtherKeys` 작성(빌드 · 테스트 미확인) |
| R2 | 노드 대상 본 기즈모 편집 — 대상(`RigTargetDef`)의 `translation` · `rotation` 오프셋을 뷰포트 기즈모로 옮기고 같은 되쓰기로 저장 | 아래 "남은 것" |
| R3 | `Engine/Sequencer` 가 `Engine/Character` 헤더를 include 하는지(레이어 검사) | 확인함 — 지금 include 가 없다(`git grep`, 2026-10-21) |

## 남은 것

- R2: 대상 오프셋 기즈모. 리그 디버그 창은 메인 씬을 보는 도구 창이라 뷰포트 기즈모(`EditorViewportGizmo`)를 대상 본의 월드 자리에 놓고, 끌기 결과를 대상의 부모 본 공간 오프셋으로 바꿔 `rewriteNodeSettings` 와 같은 방식(대상 키만 바꾸는 되쓰기)으로 저장한다.
- 화면 확인: 리그 디버그 창에서 가중치 · 켜짐을 바꾸고 "Save to rig" 뒤 파일 감시가 다시 읽어도 값이 남는지(Shooter3D `knight.rig.json`, `-EnableEditor`).
