# StarSkirmish — 실시간 전략 테스트 게임

## 이 게임으로 무엇을 배우나

스타크래프트 장르의 1 대 1 실시간 전략 게임입니다. `GF_RealTimeStrategy` 키트를 실제로 씁니다.
키트에는 유닛 카탈로그, 채취와 보급, 생산 대기열과 테크, 일꾼 건설과 정제소, 전투와 공격 이동, 흐름장 무리 이동, 전장의 안개, 끌어 고르기와 부대, 행동 트리 AI 가 있습니다.

- 폰 없이 **명령 API** 로 움직이는 장르에서, 사람과 AI 커맨더가 같은 명령을 부르는 구조
- 마우스 버튼이 아니라 입력 맵 액션(`Skirmish.Select`, `Skirmish.Order`)으로 고르고 명령하는 방법
- 안개에 가린 적을 빼고 보이는 유닛만 모습으로 만드는 방법
- 컴퓨터 대 컴퓨터 자동 플레이로 승패까지 돌려 보는 방법

64 × 64 맵은 절차로 놓습니다. 남서(파랑)와 북동(빨강)에 점 대칭 기지가 있고(본진, 일꾼 네 명, 광물 여덟, 간헐천), 두 기지 사이를 반대각선 절벽 능선이 가르며 비탈길이 셋 있습니다.
고지 둘과 가운데 길목의 확장 광물도 있습니다. 화면은 북쪽을 비스듬히 내려다보는 직교 카메라이고, 유닛과 건물과 자원은 Kenney Space Kit 모델(`Resource/game/starskirmish/credits.md`)입니다.

## 빌드와 실행

```powershell
cmake --preset Ninja-Debug-StarSkirmish
cmake --build --preset Ninja-Debug-StarSkirmish
cd build/Ninja-Debug-StarSkirmish/Bin
./App.exe -dx12                           # 사람(파랑) 대 컴퓨터(빨강)
./App.exe -dx12 -gv_skirmishAutoPlay=1    # 컴퓨터(러시) 대 컴퓨터(운영), 승패까지
./App.exe -dx12 -scenario=game/starskirmish/automation/control.scenario.xml
```

`control.scenario.xml` 은 커서를 옮겨(`MousePosition`) 본진 근처를 패드 A 로 끌어 고르고, 빈 땅에 패드 B 로 명령합니다.
탐침 `Skirmish.SelectedCount` 가 1 이상이 되고 `Skirmish.SelectedMovingCount` 가 1 이상이면 통과입니다.
디렉터에 마우스 원시 조회가 남아 있으면 아무것도 고르지 못하므로, 이 시나리오가 디렉터가 액션을 읽는지 지킵니다.

## 조작(사람 대 컴퓨터)

| 키 | 하는 일 |
|----|---------|
| 방향키, 휠(패드 D 패드 위아래), Space | 카메라 이동, 확대, 고른 유닛으로 |
| 왼쪽 버튼(패드 A) 끌기나 클릭 | 고르기(내 움직이는 유닛 먼저, 최대 12). Shift 로 더하기 |
| 오른쪽 버튼(패드 B) | 적이면 공격, 광물이나 내 정제소면 채취, 덜 지은 내 건물이면 이어 짓기, 빈 땅이면 무리 이동. Shift 로 대기열 |
| A 를 누르고 왼쪽 클릭 | 공격 이동 |
| S, H | 멈춤, 제자리 |
| Q, W, E | 고른 건물의 첫째, 둘째, 셋째 생산 |
| B, N, G, Y, F, T, U | 고른 일꾼으로 커서 위치에 보급고, 병영, 정제소, 아카데미, 공장, 우주공항, 벙커 |
| Ctrl + 숫자, Shift + 숫자, 숫자 | 부대 정하기, 더하기, 부르기 |
| −, =, P | 속도 ½, ×2(0.25배에서 8배), 멈춤 |
| F1 | 두 플레이어의 형편을 로그로 |

키 배치는 `data/skirmish.input.xml` 에 있습니다. 고르기는 `Skirmish.Select`, 명령은 `Skirmish.Order`, 확대는 `Camera.Zoom` 액션입니다.
생산은 본진이 일꾼을, 병영이 해병과 화염방사병을, 공장이 벌처와 탱크와 골리앗을, 우주공항이 레이스를 만듭니다.

사람 쪽 화면은 안개가 적 유닛을 가립니다(자원은 늘 보입니다). 30초마다 두 플레이어의 일꾼, 병력, 건물, 광물, 보급을 담은 `[Skirmish] t=…` 줄이 남고,
한쪽이 건물을 모두 잃으면 `[Skirmish] game over at t=..s - player N wins` 가 남습니다.

## 구조

전장은 씬 하나(`Resource/game/starskirmish/maps/skirmish.scene.xml`)입니다. 절벽은 절차 지형이라 디렉터가 플레이 시작에 만듭니다.

| 무엇 | 어디 |
|------|------|
| 카메라, 해, 땅, 끌어 고르기 상자, 디렉터 | 씬 |
| 절벽 구간과 유닛(건물과 자원 포함) | 프리팹 `prefabs/cliff`, `unit`. 유닛 모델은 종류마다 디렉터가 겁니다 |
| 규칙과 상태 | 키트의 `RtsWorld`, `RtsAiCommander`, `RtsSelection` 과 이 게임의 규칙 `SkirmishMatch` |
| 고르기, 명령, 생산, 건설, 부대, 속도, 알림, 스폰 | `SkirmishDirectorComponent` |
| 모습 | 뷰 `SkirmishUnitComponent`(유닛 하나), `SkirmishDragComponent`(끌기 상자) |
| 카메라 | GameFramework `OrthoCameraRigComponent`. 사람 쪽은 글자 키가 명령이라 WASD 를 끄고, 자동 플레이는 맵 전체가 보이게 물러서며 WASD 를 켭니다 |
| 머티리얼 | `materials/ground`, `cliff`, `drag`(반투명). 편 색, 자원 색, 고름 표시는 디렉터가 만드는 머티리얼 인스턴스 |

`SkirmishMatch` 는 절차 맵, 시작 유닛, AI 성향 셋(러시, 운영, 사람 상대), 정리 사냥(적 시작 지점에 닿아 노는 병력을 남은 적 건물로 보냄), 승패 로그를 가진 순수 규칙입니다.

**틱.** 디렉터는 `PrePhysics` 에서 입력과 게임 진행과 알림을 돌리고, 보일 유닛(사람 쪽은 안개에 가린 적을 뺍니다)과 만들어 둔 모습을 비교해 새로 보인 유닛과 사라진 유닛을 쌓습니다.
틱 뒤에 그것을 만들고 지우며 효과음도 냅니다. 뷰는 `PostUpdate` 에서 디렉터를 읽어 자기 오브젝트만 맞춥니다.

**소리.** `StarSkirmishGame` 이 사운드 이벤트(`audio/starskirmish.audioevents.xml`)를 로드합니다. 알림(`Select`, `Built`, `Blocked`)은 `ui` 버스의 2D 소리이고, 부서진 유닛(`UnitDied`)은 그 위치에서 냅니다.
직교 카메라라서 리스너가 화면 평면(2D)이 되어 화면 좌우로 팬됩니다.

**핫 리로드와 상태 저장.** 월드의 유닛, 명령, 생산, 자원, 안개, 시간과 AI 의 생각 타이머와 공격 물결 수, 고름, 부대, 속도가 디렉터의 `writeState` 로 실립니다.
경로와 흐름장, 행동 트리 진행은 싣지 않습니다. 움직이던 유닛은 앞 명령의 경로를 다시 구하고, AI 는 트리를 처음부터 다시 고릅니다.

## 데이터

- `Resource/game/starskirmish/data/units.xml` 에 자원 셋, 건물 아홉, 유닛 일곱이 있습니다. 엔진 밖 하니스로 확인한 결과, AI 대 AI 는 422초에 1번(운영)이 이기고,
  사람이 아무것도 하지 않으면 284초에 집니다.
- 유닛 종류와 모델의 대응 테이블은 `SkirmishUnitComponent` 에 있습니다.
- 디렉터의 PROPERTY 로 유닛 데이터, 프리팹 경로, 카메라 리그, 구경 시점을 정합니다. 자동 플레이는 베이스의 `_bAutoPlay` 이고 `-gv_skirmishAutoPlay=1` 도 켭니다.

## 함정과 주의

- **커서는 활성 창 크기로 화면 좌표를 땅으로 바꿉니다.** 에디터의 게임 뷰처럼 창의 일부에 그릴 때는 조금 어긋날 수 있습니다.
- **커서는 카메라 리그의 시점으로 고르므로, 리그가 그 프레임에 움직인 만큼 한 프레임 늦습니다.**

## 더 볼 곳

- [NileCity](../NileCity/README.md) — 같은 명령형 디렉터 구조의 도시 건설
- [Kits](../../GameFramework/Kits/README.md) — `GF_RealTimeStrategy` 와 다른 키트
- [Games](../README.md) — 씬, 프리팹, 디렉터 구조
