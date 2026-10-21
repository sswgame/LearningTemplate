# FrontierTown — 키트 조립 둘째 테스트 게임

## 이 게임으로 무엇을 배우나

도시 키트(`GF_CityBuilder`)와 RTS 키트(`GF_RealTimeStrategy`)를 한 씬에 섞은 게임입니다. 격자를 쓰는 키트 둘이 **땅 하나**를 어떻게 나누는지를 보여 줍니다.

- 공유 상태(`SessionStateComponent`) 하나에 땅(`SessionStateSettings::_land*`), 지갑(두 통화), 시계를 두는 방법
- 두 키트가 같은 땅을 빌리는 방법(`CitySimulation::bindLand` · `RTSWorld::bindLand`) — 도시가 지은 칸에 RTS 는 짓지 못하고 유닛은 돌아간다
- 시계를 미는 디렉터를 하나로 정하는 방법(도시 디렉터만 입력과 시계를 만진다)

규칙은 Kits의 "키트 여럿을 한 게임에"에 있습니다. 첫 조립은 [MeadowVillage](../MeadowVillage/README.md)(땅 없이 돈 · 시계만 나눈다)입니다.

## 빌드와 실행

```powershell
cmake --preset Ninja-Debug
cmake --build --preset Ninja-Debug
cd build/Ninja-Debug/Bin
./App.exe -game=FrontierTown -dx12
./App.exe -game=FrontierTown -dx12 -gv_frontierAutoPlay=1    # 늘 빨리 감기
```

자동화 시나리오 `game/frontiertown/automation/month.scenario.xml` 이 공유 시계를 날 단위로 감아 판이 열리고 달 결산이 도는지 봅니다.
땅을 나누는 규칙은 EngineTest 의 `KitCompositionTest.CityAndSkirmish*` 가 같은 배치로 확인합니다.

## 조작

| 키 | 액션 | 하는 일 |
|----|------|---------|
| Space(누르는 동안) | `Frontier.FastForward` | 빨리 감기(`_fastTimeScale` 배) |

## 구조

씬은 `Resource/game/frontiertown/maps/frontier.scene.xml` 하나입니다. 씬의 `TownState` 오브젝트에 **공유 상태, 도시 디렉터, RTS 디렉터** 순서로 컴포넌트가 붙어 있고, 그 순서가 틱 순서입니다.

- **도시**(`FrontierCityDirectorComponent`)가 판을 엽니다 — 땅 32 × 32(칸 1 m), 새 판이면 Deben · Minerals 시작 돈. 첫 틱에 신전(4 × 4, 막힌 땅)과 도로(막히지 않은 땅)를 놓습니다.
  공유 시계의 하루가 도시의 한 달이고, 날이 넘어가면 달 결산을 합니다. 배치는 씬의 PROPERTY(`_landmark*` · `_road*`)입니다.
- **RTS**(`FrontierSkirmishDirectorComponent`)는 공유 지갑을 플레이어 0 에 빌려 줍니다. 새 판이면 일꾼과 해병을 놓고, 둘째 틱에 일꾼이 빈 땅(`_barracksX`, `_barracksY`)에 병영을 짓고
  해병이 신전 양쪽(`_patrolA` ↔ `_patrolB`)을 순찰합니다. 해병의 길은 도시 땅을 돌아갑니다.
- **카메라**는 씬의 고정 직교 카메라입니다. 두 디렉터 모두 카메라를 만지지 않습니다.
- **함정**: 땅은 놓기 전에 빌린다(이미 놓인 건물은 땅을 얻지 않는다). 도시의 첫 배치는 RTS 가 땅을 칠한 뒤에 들어오므로 RTS 는 땅 리비전으로 다시 칠한다.

## 더 볼 곳

- [NileCity](../NileCity/README.md) — 도시 키트 하나만 쓰는 게임
- [StarSkirmish](../StarSkirmish/README.md) — RTS 키트 하나만 쓰는 게임
