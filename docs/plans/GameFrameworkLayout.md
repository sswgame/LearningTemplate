# GameFramework 폴더 재배치 계획

`Source/GameFramework` 는 약 930 개 파일이다. `Base` 에는 한 단계에 폴더 24 개가 나란히 있고, 키트는 장르 그룹 아래 폴더 하나씩이라 한 키트 안은 평평하다.
이동은 `git mv` + include 일괄 치환이고, 헤더가 옮겨지면 reconfigure 로 코드젠을 다시 만든다. 키트끼리는 서로 모르고(`Kits/README.md`) 기반 → 키트 방향도 그대로 둔다.

## 측정한 현재 상태 (2026-10-10)

| 위치 | 파일 수 | 문제 |
|---|---|---|
| `Base/` | 24 개 폴더가 같은 단계 | `Online` 89 · `Gimmick` 31 · `Appearance` 29 · `Framework` 28 · `AI` 27 · `Control` 25 · `Combat` 24 · … `GameState` 2 · `Quest` 4. 묶는 층이 없어 무엇이 토대이고 무엇이 기능인지 목록에서 안 보인다 |
| `Base/Framework` | 루트에 28 | 게임 인스턴스 · 세이브 · 문자열 · 소리 · 자동 저장 · 로딩 화면 · 화면 전환이 한 폴더에 평평하다 |
| `Base/Utility` | 루트에 18 | 타이머(`Countdown` · `TimerQueue` · `FixedStepTimer`) · 난수 · 격자 · 노이즈 · 레이 · 방향 · 이벤트 버퍼가 섞인 통. 이 중 상당수는 Core 로 내려갈 후보(중복 정리 보고) |
| `Base/World` | 루트에 20 | 날씨 · 시계 · 중력 · 땅 등록부 · 산포 · 플래그 · 질의가 한 폴더 |
| `Base/Control` · `Input` · `Movement` · `Vehicle` · `Navigation` | 25 · 6 · 6 · 16 · 12 | "조종 → 이동" 한 흐름이 다섯 폴더로 갈라져 있다 |
| `Base/Online` + `Kits/Online` | 89 + 172 | GameFramework 의 28% 가 온라인. 같은 서비스(예: Account)가 `Kits/Online/Account`(클라이언트) · `Kits/Online/Server/Account`(서버, 28)로 나뉘고 `Base/Online/{Service,Store,…}` 가 공통 |
| 키트 14 개 | 폴더 안이 평평(루트 10~16 파일, 하위 폴더 0) | `CardGame` 한 키트에 Klondike · Matgo · Poker · Uno · Hwatu 가 들어 있다 — 키트 하나가 게임 여럿 |
| 키트 그룹 | `Rpg` 에 `Overworld`(타일 월드 라이브러리 성격), `Simulation` 에 `Voxel`, `Storage`(SqlStore) | 장르 키트와 기능 키트가 섞였다 |

## 단계

### 1. Base 를 층으로 묶는다 — 끝남(2026-10-10)
```
Base/Online/      Online                                                        (층 0 — Core 만 본다)
Base/Foundation/  Utility · Data · Framework                                    (층 1)
Base/World/       World · Spline                                                (층 2)
Base/Actor/       Input · Movement · Navigation · Combat · Camera · AI · Control (층 3)
Base/UI/          UI                                                            (층 4)
Base/Gameplay/    Inventory · Progression · Match · Ability · Interaction · Quest · Appearance · Gimmick · GameState · Vehicle (층 5)
```
- 첫 안(Foundation 에 GameState, Actor 에 Appearance · Vehicle, World · UI 를 맨 위)은 include 를 재 보니 성립하지 않았다. GameState 는 인벤토리 · 퀘스트 · 월드를,
  AI · 기믹 · 상호작용은 월드를, 어빌리티는 데미지 숫자(UI)를, 외형은 인벤토리를, 탈것은 전투를 본다. 그래서 World 를 아래로, UI 를 Gameplay 아래로 내렸다.
- 층 방향과 층 안 폴더 순서는 `CheckGameFrameworkLayers` 의 `_kBaseLayer` · `_kBaseFolderOrder` 가 지킨다. 이동은 `Scripts/dev/MoveGameFrameworkLayout.py --step 1`.

### 2. 큰 평평한 폴더를 안쪽으로 나눈다 (낮은 위험)
- `Framework`: `Save`(SaveGame · Autosave · ComponentStateStore) · `Flow`(GameInstanceBase · LoadingScreenController · ScreenTransitionManager) · `Presentation`(GameSound · GameStrings · MaterialTintCache).
- `World`: `Environment`(WeatherSystem · WorldClock · GravityComponent) · `Land`(LandRegistry · AreaGraph · PropScatter) · 나머지.
- `Utility`: 타이머 · 난수 · 격자는 중복 정리 단계에서 Core 로 내린 뒤 남은 것만.
- `Control`(루트 25) · `Combat`(24): 하위 폴더 후보는 파일 이름을 보고 정한다.

### 3. 키트 안을 나눈다 (낮은 위험, 키트별)
- 루트 10 파일 이상인 키트 14 개에 `Catalog/` · `State/` · `Component/` 같은 공통 하위 폴더 규칙을 정한다(전부 같은 이름). 파일이 6 개 이하인 키트는 평평하게 둔다.
- `CardGame` 은 게임별 하위 폴더(`Klondike` · `Matgo` · `Poker` · `Uno`)로. 키트를 쪼갤지는 별도 판단(키트마다 DLL 이 하나라 쪼개면 DLL 이 늘어난다 — Bin 정리와 충돌) — 폴더만 나눈다.

### 4. 키트 그룹을 성격으로 다시 나눈다 (결정 필요)
- 장르 키트(`Action` · `Casual` · `Horror` · `Rpg` · `Simulation` · `Strategy`)와 기능 키트(`Online` · `Network` · `Storage` · `Rpg/Overworld` · `Simulation/Voxel`)를 같은 `Kits/` 아래에 두되 그룹 이름으로 구분되게: `Kits/Genre/<그룹>/` · `Kits/Service/<그룹>/` 후보.
- 모듈 이름(`GF_<키트>`)은 바뀌지 않는다. 바뀌는 것은 경로뿐이라 매니페스트 탐색(`Kits/CMakeLists.txt` 글롭)이 그대로 동작하는지만 확인.

### 5. 온라인 짝 맞추기
- `Kits/Online/<서비스>/`(클라이언트)와 `Kits/Online/Server/<서비스>/`(서버)를 서비스마다 한 폴더 아래 `Client/` · `Server/` 로 묶는 안 — `Kits/README.md` 가 이미 `Kits/<그룹>/Client/<키트>/` · `Server/<키트>/` 꼴을 허용하므로 규칙 변경 없이 옮길 수 있다. `Base/Online` 의 `Service` · `Store` · `Config` 등은 그대로.

## 건드리지 않는 것
- 키트끼리 서로 모른다는 규칙, 모듈 이름, 키트 DLL 병합(핫 리로드 단위 유지).

## 순서와 위험

| 단계 | 규모 | 위험 | 확인 |
|---|---|---|---|
| 1 Base 층 | 큼(경로 일괄 치환) | 중간 | 전 프리셋 컴파일 · `CheckGameFrameworkLayers` · 코드젠 재생성 |
| 2 평평한 폴더 | 중간 | 낮음 | 컴파일 |
| 3 키트 안 | 중간 | 낮음 | 키트별 컴파일 · `KitCompositionTest` |
| 4 키트 그룹 | 작음 | 낮음(경로만) | 매니페스트 탐색 · 모듈 순서 시험 |
| 5 온라인 짝 | 중간 | 낮음 | 컴파일 · 온라인 시험 |

엔진 분할 계획의 0-3(Engine 폴더 재배치) 뒤에 한다. Core · Engine 쪽 경로 이동이 끝난 뒤라야 include 치환이 한 번씩만 일어난다.
