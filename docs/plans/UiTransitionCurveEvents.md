# 전환의 곡선 위 사건

> 상태: 구현중 · 선행: 없음 · 영역: ui · 옛번호: D6-1 · 진행: 1/2

런타임 UI 전환의 "키 사이 사건 말고 곡선 위 사건" 은 뜻이 불분명해 미뤄 두었다. 착수 조건(쓰는 곳)은 사용자 지시(2026-10-22 "조건부도 구현")로 갈음했다.
뜻은 이렇게 정했다: 트랙 값이 문턱을 지날 때 화면에 명령을 보낸다. 시각에 붙는 `UIAnimationEvent` 는 곡선 · 키를 고치면 어긋나지만, 문턱은 곡선을 따라간다(불투명도가 0.5 를 넘을 때 소리, 크기가 1 로 돌아올 때 다음 연출).
단위가 끝나면 그 절을 지운다.

## 1. 단위

| 단위 | 내용 | 선행 |
|---|---|---|
| C2 | 빌드 · 테스트 | 없음 |

### C2 빌드 · 테스트

- `EngineTest --test_filter=UIAnimationTest.*`. 새 테스트는 `UIAnimationTest.CurveEventsFireWhenValueCrossesThreshold` 다.
- 문서 쓰기(`UIDocumentWriter`)는 빈 `<_listCurveEvent />` 를 빼므로 옛 문서는 다시 저장해도 바뀌지 않는다. 미리보기 패널의 저장 왕복으로 한 번 본다.

## 2. 끝난 단위의 확인 목록

- C1 곡선 위 사건: `UIAnimationTrack::_listCurveEvent`(`UIAnimationCurveEvent` — 문턱, 성분, 방향 `UICurveCrossing` Rising · Falling · Either, 명령). `UIAnimationPlayer::fireEvents` 가 시각 사건과 함께 `fireCurveEvents` 를 부른다.
  재생 구간을 키 시각마다 끊고(키 사이 곡선은 한 방향이다) 두 끝 값으로 지남을 본다. 지난 시각은 두 끝 사이를 선형으로 나눠 사건 순서에 쓴다. 거꾸로 재생은 재생 방향의 오르내림을 본다.
  실수가 아닌 트랙이나 없는 성분에 사건을 달면 경고하고 트랙째 뺀다(조용히 안 보내지 않는다). 움직임 줄이기 · 길이 0 은 끝까지 한 번에 가며 그 사이의 지남을 모두 보낸다.
