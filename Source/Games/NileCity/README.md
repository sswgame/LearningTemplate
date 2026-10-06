# NileCity — 도시 건설 테스트 게임

## 이 게임으로 무엇을 배우나

파라오 장르의 도시 건설 게임입니다. `GF_CityBuilder` 키트를 실제로 씁니다. 키트에는 물자와 건물과 집 단계 카탈로그, 노동 배분, 순회 일꾼,
농장에서 수레와 창고와 시장과 상인을 거쳐 집으로 가는 물자 사슬, 집 진화와 퇴화, 이민, 월말 세금과 임금과 소비, 해마다의 범람이 있습니다.

- 폰이 없는 **명령형 디렉터**가 입력 맵 액션(`Nile.Place`, `Nile.Demolish`)으로 짓고 허무는 방법
- 커서 아래 땅을 고르는 방법(커서 화면 위치와 카메라 리그의 `findGroundPoint`)
- 절차 지형과 자동 계획을 화면을 모르는 순수 규칙(`NileCityPlanner`)으로 두는 방법
- 시뮬레이션과 지금 만들어 둔 모습을 비교해 바뀐 것만 다시 만드는 방법

땅은 절차로 칠합니다. 남북으로 굽이치는 나일 강, 양쪽 범람원(농장은 여기에만), 동쪽 풀밭(도시가 들어설 곳), 서쪽과 동쪽 끝의 사막과 바위가 있습니다.
화면은 북쪽을 비스듬히 내려다보는 직교 카메라입니다. 집과 서비스 건물과 창고와 시장과 도로는 Kenney City Kit 모델(`Resource/game/nilecity/credits.md`)이고,
땅과 농장과 조각상과 일꾼은 내장 도형입니다.

## 빌드와 실행

```powershell
cmake --preset Ninja-Debug-NileCity
cmake --build --preset Ninja-Debug-NileCity
cd build/Ninja-Debug-NileCity/Bin
./App.exe -dx12
./App.exe -dx12 -gv_nileAutoPlay=1      # 계획대로 도로, 우물, 농장, 창고, 시장, 집을 지으며 달마다 [Nile] month N pop P money M
./App.exe -dx12 -scenario=game/nilecity/automation/control.scenario.xml
```

`control.scenario.xml` 은 커서를 화면 가운데(`MousePosition`, 카메라 리그의 초점 셀)에 두고 패드 A(`Nile.Place`)로 도로를 깐 뒤 패드 B(`Nile.Demolish`)로 같은 셀을 허뭅니다.
로그에 `[Nile] demolished` 가 한 번 나오면 통과입니다. 마우스 버튼이 아니라 패드 버튼으로 누르는 것은, 디렉터가 장치가 아니라 액션을 읽는지 확인하기 위해서입니다.

## 조작

| 키 | 하는 일 |
|----|---------|
| WASD, 방향키, 휠(패드 D 패드 위아래) | 카메라 이동과 확대 |
| Q, E, Tab | 지을 것 고르기(도로, 그다음 `city.xml` 의 건물 순서) |
| R | 도로 고르기 |
| 왼쪽 버튼(패드 A, `Nile.Place`) | 커서 셀(노란 상자)에 짓기. 도로는 누른 채 끌면 이어서 깝니다. 못 지으면 까닭(`Occupied`, `BadTerrain`, `NotEnoughMoney`)을 로그로 |
| 오른쪽 버튼(패드 B, `Nile.Demolish`) | 커서 셀의 건물이나 도로 허물기 |
| P | 자동 계획 켜기와 끄기 |
| Space, −, = | 멈춤, 속도 ½, 속도 ×2(0.25배에서 8배) |
| F1 | 도시 상태를 로그로 |

키 배치는 `data/nile.input.xml` 에 있습니다. 한 달은 게임 시간 20초이고, 달이 끝날 때마다 인구와 돈과 일꾼과 집 단계를 담은 `[Nile] month …` 줄이 남습니다.
해가 바뀌면 범람이 범람원 농장의 다음 한 해 비옥함(40–100%)을 정합니다.

집은 단계가 오를수록 큰 집 모델로 바뀌고, 빈 땅은 낮은 울타리입니다. 서비스 건물과 서비스 일꾼은 서비스 색을 입습니다. 물은 파랑, 종교는 금색, 오락은 보라, 의료는 빨강, 교육은 청록, 세금은 주황입니다.

## 구조

도시는 씬 하나(`Resource/game/nilecity/maps/nile.scene.xml`)입니다. 땅은 계획기가 칠하는 절차 지형이라 씬에 두지 않고, 디렉터가 플레이 시작에 만듭니다.

| 무엇 | 어디 |
|------|------|
| 카메라, 해, 짓기 커서, 디렉터 | 씬 |
| 땅 구간, 바위, 도로, 건물, 일꾼 | 프리팹(`prefabs/`). 디렉터가 런타임에 만들고 지웁니다 |
| 규칙과 상태 | 키트의 `CitySimulation`, `CityCatalog` |
| 땅 모양과 자동 계획 | `NileCityPlanner` |
| 시뮬레이션, 자동 계획, 짓기와 허물기, 속도, 달 로그, 스폰 | `NileDirectorComponent` |
| 모습 | 뷰 `NileBuildingComponent`, `NileWalkerComponent`, `NileCursorComponent` |
| 카메라 | GameFramework `OrthoCameraRigComponent`. 초점을 맵 안에 묶고, Q/E 회전은 끕니다(회전 단계 0) |
| 머티리얼 | 팔레트 `materials/palette.material`, 커서 `cursor.material`(반투명). 땅과 건물 종류와 일꾼 색은 디렉터가 만드는 머티리얼 인스턴스 |

**입력.** 디렉터는 명령형 게임의 디렉터라서 입력 맵을 읽을 수 있는 몇 안 되는 파일입니다(`CheckControlBoundary` 의 허용 목록). 그래도 장치를 직접 묻지 않고 액션만 읽습니다.
장치를 묻는 것은 커서 화면 위치(`getMousePositionNormalized`) 하나이고, 그 위치를 리그의 `findGroundPoint` 로 땅 위의 셀로 바꿉니다.

**틱.** 디렉터는 `PrePhysics` 에서 입력, 자동 계획, 시뮬레이션을 돌리고, 시뮬레이션과 지금 만들어 둔 모습을 비교해 바뀐 도로와 건물과 모자란 일꾼 수를 쌓습니다.
틱 뒤에 그것을 만들고 지웁니다. 건물 셀이 다른 건물로 바뀌면 오브젝트를 바꿔 만들고, 집 단계에 따른 모델 교체는 뷰가 `PostUpdate` 에서 합니다.
모델은 디렉터가 미리 로드해 두므로 워커에서 파일을 읽지 않습니다.

**핫 리로드와 상태 저장.** 도시(셀, 건물, 일꾼, 돈, 달력, 난수)와 자동 계획 진행, 속도, 고른 도구가 디렉터의 `writeState` 로 실립니다.
카탈로그에서 빠진 건물이 있으면 알리고 새 도시로 시작합니다.

## 데이터

- `Resource/game/nilecity/data/city.xml` 에 물자 네 종류, 건물 열여덟, 집 단계 여덟이 있습니다.
- 자동 계획(세 구역, 167 단계, 단계마다 돈과 인구 조건)으로 36달을 엔진 밖 하니스에서 돌려 확인했습니다. 모든 단계가 놓이고, 인구가 400 근처, 집 대부분이 4단계(Rough Cottage)까지 오르며 돈이 쌓입니다.
  인구가 그 근처에서 멈추는 것은 키트의 이민 규칙 때문입니다. 일자리보다 실업이 25% 넘게 많으면 사람이 오지 않습니다.
- 디렉터의 PROPERTY 로 도시 데이터, 프리팹 경로, 카메라 리그, 시작 돈, 순회 서비스 시간을 정합니다.

## 함정과 주의

- **커서는 활성 창 크기로 화면 좌표를 땅으로 바꿉니다.** 에디터의 게임 뷰처럼 창의 일부에 그릴 때는 조금 어긋날 수 있습니다.
- **커서는 카메라 리그의 시점으로 고르므로, 리그가 그 프레임에 움직인 만큼 한 프레임 늦습니다.**

## 더 볼 곳

- [StarSkirmish](../StarSkirmish/README.md) — 같은 명령형 디렉터 구조의 RTS
- [GameFramework](../../GameFramework/README.md) — 조종 경계와 입력 맵
- [Games](../README.md) — 씬, 프리팹, 디렉터 구조
