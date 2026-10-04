# Spline — 곡선 하나를 여러 곳이 나눠 쓴다

움직이는 발판(기믹 `Mover`) · 카메라 레일 · 길 · 레일 그라인드처럼 "곡선 위 거리 s 의 자리" 를 묻는 곳이 같은 곡선을 씁니다.
시험: `EngineTest` 의 `SplineTest`(`Test/EngineTest/TestSpline.cpp`), 무버는 `GimmickTest.MoverReachesEndAtArcLengthTime`.

| 타입 | 하는 일 |
|------|--------|
| `SplinePath` | 조절점 → 곡선(`SplineType` — `CatmullRom` · `Bezier` · `Linear`, 열림 · 닫힘), 호 길이 표, `sampleAtDistance` · `findClosest` · `sampleUniform` |
| `SplineComponent` | 조절점(소유 오브젝트 로컬)을 씬에 저장, 플레이 시작에 월드 곡선을 굳힌다(`getWorldPath`) |
| `ArcLengthUtil` | 누적 거리 표 → 구간 · 비율, 거리 감기/자르기. `SplinePath` 와 코스터 트랙(`CoasterTrack::sample`)이 함께 쓴다 |

- **거리는 호 길이입니다.** 구간마다 `samplesPerSegment` 개의 점으로 누적 현 길이 표를 짓고, 거리 → 곡선 매개변수(구간 + t)로 바꾼 뒤 그 매개변수에서
  곡선 식으로 자리 · 접선을 다시 구합니다(표의 점 사이를 직선으로 잇지 않는다). 그래서 같은 속도로 거리를 늘리면 곡선 위에서 같은 빠르기로 갑니다.
- **베지어 점 열**은 `[끝점, 손잡이, 손잡이, 끝점, …]` — 열린 곡선 3n+1 개, 닫힌 곡선 3n 개(마지막 손잡이 둘이 첫 끝점으로 돌아온다). 개수가 틀리면 짓지 않습니다.
- **열린 Catmull-Rom 의 양 끝**은 끝 점을 거울로 비춘 이웃을 씁니다(끝에서 곡선이 직선으로 빠진다).
- `SplineComponent` 의 월드 곡선은 **플레이 시작의 변환으로 굳습니다** — 곡선을 단 오브젝트가 그 곡선을 따라 움직여도(발판 하나짜리 프리팹) 길은 그대로입니다.
- 카트 트랙(`KartTrack`)은 거리를 **수평(XZ) 길이**로 재므로 이 곡선과 값이 달라 옮기지 않았습니다(옮기면 랩 · 순위 · 고스트 기록이 바뀐다).
