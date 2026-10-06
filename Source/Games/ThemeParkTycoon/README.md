# ThemeParkTycoon — 놀이공원 경영 테스트 게임

## 이 게임으로 무엇을 배우나

롤러코스터 타이쿤 장르의 게임입니다. `GF_ThemePark` 키트의 코스터 트랙 빌더, 열차 물리, 시험 운행 평가, 손님 경영 시뮬레이션을 실제로 씁니다.
이 게임은 **씬, 프리팹, 디렉터, 뷰 컴포넌트로 게임을 나누는 구조의 본보기**입니다. 다른 테스트 게임도 같은 구조이므로, 새 게임을 만들기 전에 이 게임을 먼저 읽으면 좋습니다.

- 고정 배치는 씬에, 런타임에 생기는 것은 프리팹에 두는 방법
- 규칙을 키트의 보통 클래스로 두고 디렉터가 돌리는 방법
- 뷰 컴포넌트가 디렉터를 읽기만 해서 병렬 틱에서도 안전한 이유
- 디렉터의 상태 데이터로 핫 리로드를 넘기는 방법

화면은 비스듬히 내려다보는 직교 카메라이고 Q 와 E 로 90도씩 돌립니다. 손님은 행복도에 따라 초록, 노랑, 빨강 캡슐입니다.

## 빌드와 실행

```powershell
cmake --preset Ninja-Debug-ThemeParkTycoon
cmake --build --preset Ninja-Debug-ThemeParkTycoon
cd build/Ninja-Debug-ThemeParkTycoon/Bin
./App.exe -dx12
./App.exe -dx12 -gv_parkAutoBuild=1     # 돈이 모이는 대로 남은 것 중 가장 싼 놀이기구를 짓는다
./App.exe -dx12 -scenario=game/themepark/automation/shadow.scenario.xml
```

`shadow.scenario.xml` 은 고정 카메라와 고정 해, 자동 건설을 끈 상태에서 직교 카메라가 화면 끝까지 그림자를 받는지 봅니다.
화면 띠와 나무 줄과 가운데 영역의 어두운 픽셀 비율을 단언하고, 측정값은 보고 JSON 과 `[Scenario] metric` 로그 줄에 남습니다.
`ctest --test-dir build/Ninja-Debug-ThemeParkTycoon -R AppTest_HostOnly` 가 이 시나리오를 네 백엔드로 돌립니다.

## 조작

| 키 | 하는 일 |
|----|---------|
| WASD, 방향키, 휠(패드 D 패드 위아래) | 카메라 이동과 확대 |
| Q, E | 카메라 90도 돌리기 |
| Tab | 놀이기구 고르기(지은 것과 안 지은 것 모두) |
| B | 고른 것 짓기(이미 지었으면 남은 것 중 가장 싼 것) |
| O | 고른 놀이기구 열기와 닫기 |
| [, ] | 고른 놀이기구 표 값 −1, +1 |
| −, = | 입장료 −5, +5 |
| V | 고른 코스터의 맨 앞 차량에 타기와 내리기 |
| G, F1 | 손님 생각 요약, 공원 상태를 로그로 |

키 배치는 `Resource/game/themepark/data/park.input.xml` 에 있습니다.
처음에는 가장 싼 평지 놀이기구와 코스터 하나가 지어져 있습니다. 코스터를 지으면 시험 운행(`CoasterRideAnalyzer`)이 흥분, 강도, 멀미를 매겨 로그에 남기고, 그 값이 표 값의 기준이 됩니다.
손님은 자기 강도 범위 밖의 놀이기구를 타지 않고, 표 값이 가치의 두 배를 넘으면 "너무 비싸다"고 합니다. 입장료가 비쌀수록 손님이 덜 옵니다.

## 구조

공원은 씬 하나(`Resource/game/themepark/maps/park.scene.xml`)입니다. 에디터에서 열면 고정 배치가 계층에 보이고, Play 를 누르면 디렉터가 공원을 열며, Stop 은 플레이 전 씬으로 돌아갑니다.

| 무엇 | 어디 |
|------|------|
| 땅, 해, 카메라, 정문, 광장 시설, 나무 흩뿌리기 설정, 디렉터 | 씬 |
| 손님, 코스터 차, 레일 조각, 기둥, 승강장, 놀이기구 입구, 길, 평지 놀이기구 | 프리팹(`prefabs/`). 디렉터가 런타임에 스폰합니다 |
| 규칙과 상태(돈, 손님, 놀이기구, 코스터 물리) | 키트의 `ThemeParkSimulation`, `CoasterTrackBuilder`, `CoasterTrain` |
| 규칙을 돌리고 스폰을 지시 | `ParkDirectorComponent` |
| 엔티티의 모습 | 뷰 `ParkGuestComponent`, `CoasterCarComponent`, `FlatRideComponent` |
| 카메라와 나무 | GameFramework `OrthoCameraRigComponent`, `PropScatterComponent` |
| 머티리얼 | 팔레트 `materials/palette.material`(키트 색 텍스처), 잔디 `grass.material`. 실행 중 바뀌는 색만 머티리얼 인스턴스 |

**틱.** 디렉터는 `PrePhysics` 그룹에서 시뮬레이션과 입력과 자동 건설을 돌리고, 뷰와 카메라 리그는 `PostUpdate` 그룹에서 그 결과를 읽습니다. 그룹은 차례로 돕니다.
같은 그룹의 오브젝트는 병렬이므로 뷰는 자기 오브젝트에만 쓰고, 디렉터의 목록은 첨자 대신 `data()` 로 읽습니다.
코스터에 타면(V) 디렉터가 `PrePhysics` 에서 리그에 원근 시점을 넣고, 리그가 `PostUpdate` 에서 그것을 씁니다.
틱 안에서는 오브젝트를 만들 수 없으므로 디렉터는 지을 것과 효과음을 쌓아 두고 틱 뒤에 만듭니다. 뷰는 디렉터를 `GameObjectHandle` 로 가지고 매 프레임 찾습니다.

**핫 리로드와 상태 저장.** 놀이기구 운행, 손님, 돈, 평가, 난수, 입장료, 지은 배치, 코스터 열차의 위치와 속도는 디렉터의 `writeState` 로 상태 스냅샷에 실립니다.
코스터 트랙은 배치 데이터에서 다시 만듭니다. 배치 데이터가 바뀌어 지은 것을 맞출 수 없으면 알리고 새 공원으로 시작합니다.
나무는 흩뿌리기 컴포넌트가 핸들 목록(PROPERTY)으로 알아보므로 한 번 더 만들지 않습니다.

## 데이터

- `Resource/game/themepark/data/coasters.xml` 은 코스터 레이아웃(조각 목록)입니다. 세 레이아웃이 회로가 닫히고 한 바퀴를 도는지 엔진 밖 하니스로 확인했습니다.
  Thunder Loop 는 흥분 6.2와 강도 8.2, Camel Hills 는 7.1과 7.6(에어타임), Little Dipper 는 2.6과 5.7 입니다.
- `Resource/game/themepark/data/rides.xml` 은 시작 자금, 입장료, 평지 놀이기구(손으로 매긴 평가), 코스터 배치입니다. 씬에 정문이 있으면 그 위치가 손님이 드나드는 정문입니다.
- 디렉터의 PROPERTY 로 프리팹 경로, 손님 색, 카메라 리그, 정문을 정합니다. 자동 건설은 베이스의 `_bAutoPlay` 이고 `-gv_parkAutoBuild=1` 도 켭니다.

## 함정과 주의

- **씬, 프리팹, 머티리얼 파일은 엔진 직렬화기가 쓴 것입니다.** 손으로 고치면 형식을 깨기 쉬우므로 에디터로 고칩니다.

## 더 볼 곳

- [Games](../README.md) — 이 구조를 일반화한 "새 게임 = 씬 + 프리팹 + 디렉터와 뷰 컴포넌트"
- [Kits](../../GameFramework/Kits/README.md) — `GF_ThemePark` 와 다른 키트
