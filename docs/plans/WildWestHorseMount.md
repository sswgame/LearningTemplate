# WildWest 승마의 말 모델

> 상태: 측정대기 · 선행: 없음 · 영역: gameplay · 진행: 2/2

말과 기수가 큐브 · 캡슐 자리 표시였다. Quaternius "Farm Animals"(CC0)의 말 모델로 바꾸고 씬에 연결했다(KayKit · Kenney 에는 말이 없다).

## 진행

| 단위 | 내용 | 상태 |
|---|---|---|
| W1 | 말 모델 — `horse.glb`(FBX2glTF 변환), 임포트 규칙 `WildWest_Horse`, 출처 `credits.md`. 클립 idle · walk · run. 자리 표시 말은 지움 | 끝 |
| W2 | 씬 연결 — `Bessie` · `Shadow` 가 `horse.mesh`(크기 0.4) + `SkeletalAnimatorComponent`(`horse.animgraph.xml`, `Gait` 로 Idle 0 · Walk 1 · Run 4 블렌드), `_seatOffset="0,1.62,0"` | 데이터 끝, 화면 미확인 |

## 남은 일

첫 일: `mount.scenario.xml` 의 사진으로 말 크기(0.4 는 계산값) · 좌석 높이 · 걸음새를 눈으로 맞춘다.

- 모델에 Trot · Canter 가 없어 Gait 2 · 3 은 Walk 와 Run 사이다.
- 좌석 소켓(`_socketName`)은 외형 컴포넌트의 소켓이라 외형 없는 말은 좌석 오프셋을 쓴다. 그래서 기수가 말 몸통의 오르내림을 따르지 않는다.
