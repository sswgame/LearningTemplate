# ThemeParkTycoon — 놀이공원 경영 시험 게임

`GF_ThemePark` 키트(코스터 트랙 빌더 · 열차 물리 · 시험 운행 평가 · 손님 경영 시뮬레이션)를 실제로 쓰는 롤러코스터 타이쿤 장르입니다.
화면은 비스듬히 내려다보는 아이소메트릭(직교) 카메라이고, Q/E 로 90° 씩 돌립니다. 손님은 행복도에 따라 초록 · 노랑 · 빨강 캡슐입니다.

## 빌드 · 실행

```powershell
cmake --preset Ninja-Debug -DSW_ACTIVE_GAME=ThemeParkTycoon
cmake --build --preset Ninja-Debug
cd build/Ninja-Debug/Bin
./App.exe -dx12
./App.exe -dx12 -gv_parkAutoBuild=1     # 돈이 모이는 대로 남은 것 중 가장 싼 놀이기구를 짓는다
```

## 조작

| 키 | 하는 일 |
|----|---------|
| WASD · 방향키 · 휠 | 카메라 이동 · 확대 |
| Q · E | 카메라 90° 돌리기 |
| Tab | 놀이기구 고르기(지은 것 · 안 지은 것 모두) |
| B | 고른 것을 짓기(지었으면 남은 것 중 가장 싼 것) — 코스터는 시험 운행 결과가 로그에 나온다 |
| O | 고른 놀이기구 열기 · 닫기 |
| [ · ] | 고른 놀이기구 표 값 −1 · +1(가치의 두 배가 넘으면 손님이 "너무 비싸다") |
| − · = | 입장료 −5 · +5(비쌀수록 손님이 덜 온다) |
| V | 고른 코스터의 맨 앞 차량에 타기 · 내리기(루프에서 뒤집히는 시점) |
| G · F1 | 손님 생각 요약 · 공원 상태를 로그로 |

처음에는 가장 싼 평지 놀이기구와 코스터 하나가 지어져 있습니다. 코스터의 흥분 · 강도 · 멀미는 시험 운행(`CoasterRideAnalyzer`)이 매기고 표 값의 기준이
됩니다 — 손님은 자기 강도 범위 밖의 놀이기구를 타지 않습니다.

## 파일 · 데이터

- `ThemeParkTycoonGame` — 데이터를 읽고 공원을 둡니다.
- `ParkWorld` — 배치 읽기 · 짓기 · 값 · 카메라와 레일 · 기둥 · 열차 · 평지 놀이기구 · 손님의 모습.
- `Resource/game/themepark/data/coasters.xml` — 코스터 레이아웃(조각 목록). 세 레이아웃은 회로가 닫히고 한 바퀴를 도는지, 평가가 어떻게 나오는지 엔진 밖
  하네스로 확인했다(Thunder Loop 흥분 6.2 · 강도 8.2, Camel Hills 7.1 · 7.6 · 에어타임, Little Dipper 2.6 · 5.7).
- `Resource/game/themepark/data/rides.xml` — 정문 · 시작 자금 · 평지 놀이기구(손으로 매긴 평가) · 코스터 배치.
