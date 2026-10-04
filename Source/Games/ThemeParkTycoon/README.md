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

## 구조 — 씬 · 프리팹 · 컴포넌트

공원은 씬 하나(`Resource/game/themepark/maps/park.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)입니다. 에디터에서 열면 고정 배치가 계층에 보이고,
Play 를 누르면 디렉터가 공원을 열며, Stop 은 플레이 전 씬으로 돌아갑니다.

| 무엇 | 어디 |
|------|------|
| 땅 · 해 · 카메라 · 정문 · 광장 매점/벤치/휴지통 · 나무 흩뿌리기 설정 · 디렉터 | 씬(엔티티) — 에디터에서 옮긴다 |
| 손님 · 코스터 차 · 레일 조각 · 기둥 · 승강장 · 놀이기구 입구 · 길 · 평지 놀이기구 | 프리팹(`prefabs/*.prefab.xml`) — 디렉터가 런타임에 스폰한다 |
| 규칙 · 상태(돈 · 손님 · 놀이기구 · 코스터 물리) | 키트의 보통 클래스(`ThemeParkSimulation` · `CoasterTrack` · `CoasterTrain` …) — 씬 없이 시험한다 |
| 규칙을 돌리고 스폰을 지시 | `ParkDirectorComponent`(씬에 하나 — 언리얼 GameMode/GameState 자리) |
| 엔티티의 모습 | 뷰 컴포넌트 `ParkGuestComponent` · `CoasterCarComponent` · `FlatRideComponent` — 디렉터를 **읽기만** 해 자기 메시를 맞춘다 |
| 카메라 · 나무 | GameFramework 공용 `OrthoCameraRigComponent` · `PropScatterComponent` |
| 모습 | 팔레트 머티리얼 `materials/palette.material`(albedoMap = 키트 색 칸 텍스처) · 잔디 `grass.material`. 실행 중 색(손님 기분 · 평지 놀이기구)만 머티리얼 인스턴스 색 |

**틱.** 디렉터는 `TickGroup::PrePhysics` 에서 시뮬레이션 · 입력 · 자동 짓기를 돌리고, 뷰와 카메라 리그는 `PostUpdate` 에서 그 결과를 읽습니다(그룹은 차례로
돈다). 같은 그룹의 오브젝트는 병렬이므로 뷰는 자기 오브젝트에만 쓰고, 디렉터의 목록은 첨자 대신 `data()` 로 읽습니다. 코스터에 타면(V) 디렉터가 PrePhysics 에서
리그에 원근 시점을 넣고, 리그가 PostUpdate 에서 그것을 씁니다. 틱 안에서는 오브젝트를 만들 수 없으므로 디렉터는 지은 것 · 효과음을 쌓아 두고
`executeOrDeferPostTick` 한 번으로 틱 뒤에 세웁니다. 뷰는 디렉터를 `GameObjectHandle` 로 들고 매 프레임 풉니다.

**핫 리로드 · 상태 저장.** 시뮬레이션은 런타임 상태라 리로드에서 처음부터 다시 섭니다(PROPERTY 만 남는다). 상태를 쓰기 전에 게임(`onBeforeStateSerialize`)이
디렉터가 세운 오브젝트를 걷고, 다시 만든 디렉터가 시작하며 다시 세웁니다. 나무는 흩뿌리기 컴포넌트가 핸들 목록(PROPERTY)으로 알아보고 한 벌 더 세우지 않습니다.

## 파일 · 데이터

- `ThemeParkTycoonGame` — 첫 씬을 열고, 상태 저장 전에 디렉터가 세운 것을 걷습니다.
- `ParkDirectorComponent` — 데이터 읽기 · 짓기(코스터는 시험 운행) · 입력 · 자동 짓기(`_bAutoBuild`, `-gv_parkAutoBuild=1` 도 켠다) · 로그 · 스폰. 프리팹 경로 ·
  손님 색 · 카메라 리그 · 정문(있으면 그 자리가 손님이 드나드는 정문)은 PROPERTY 입니다.
- `ParkGuestComponent` · `CoasterCarComponent` · `FlatRideComponent` — 손님 한 명 · 차 한 칸 · 평지 놀이기구 하나의 모습.
- `Resource/game/themepark/data/coasters.xml` — 코스터 레이아웃(조각 목록). 세 레이아웃은 회로가 닫히고 한 바퀴를 도는지, 평가가 어떻게 나오는지 엔진 밖
  하네스로 확인했다(Thunder Loop 흥분 6.2 · 강도 8.2, Camel Hills 7.1 · 7.6 · 에어타임, Little Dipper 2.6 · 5.7).
- `Resource/game/themepark/data/rides.xml` — 시작 자금 · 입장료 · 평지 놀이기구(손으로 매긴 평가) · 코스터 배치. 정문 자리는 씬의 정문이 있으면 그것을 쓴다.
- `Resource/game/themepark/maps/park.scene.xml` · `prefabs/` · `materials/` — 엔진 직렬화기가 쓴 파일입니다(손으로 고치면 씬 · 프리팹 형식을 깨기 쉽다 — 에디터로).
