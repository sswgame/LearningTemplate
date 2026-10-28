# WildWest 승마의 말 모델

> 상태: 측정대기 · 선행: 없음 · 영역: gameplay · 옛번호: D4-15 · 진행: 3/3

말 모델이 저장소에 없어 말과 기수가 큐브 · 캡슐 자리 표시였습니다(`Source/Games/WildWest`, 2026-10-16). KayKit · Kenney 에 말이 없습니다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| W1 | 말 모델 — Quaternius "Farm Animals"(CC0)의 `Horse.fbx` 를 FBX2glTF 로 바꾼 `models_raw/horse.glb`, 임포트 규칙 `WildWest_Horse`, 출처 `credits.md`. 클립 `armature_idle` · `armature_walk` · `armature_run`(접두어는 임포트가 붙인다). 자리 표시 말과 생성기는 지웠다 | 임포트 · 커밋 |
| W2 | 씬 연결 — 말 엔티티(`Bessie` · `Shadow`, `prairie.scene.xml`)가 `horse.mesh`(크기 0.4) + `SkeletalAnimatorComponent`(그래프 `data/horse.animgraph.xml` — 블렌드 스페이스가 Float `Gait` 로 Idle 0 · Walk 1 · Run 4 를 섞는다)를 쓰고 `_seatOffset="0,1.62,0"`(등 높이) | 데이터 작성(화면 미확인) |

## 남은 것

- 화면 확인): 시나리오 `mount.scenario.xml` 의 사진을 보고 말 크기(0.4 는 계산값) · 좌석 높이 · 걸음새를 눈으로 맞춘다. 모델에는 Trot · Canter 가 없어 Gait 2 · 3 은 Walk 와 Run 사이다.
  좌석 소켓(`_socketName`)은 외형 컴포넌트의 소켓이라 외형이 없는 말은 좌석 오프셋을 쓴다 — 기수가 말 몸통의 오르내림을 따르지 않는다.
