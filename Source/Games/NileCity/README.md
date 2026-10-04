# NileCity — 도시 건설 시험 게임

`GF_CityBuilder` 키트(물자 · 건물 · 집 단계 카탈로그, 노동 배분, 순회 일꾼, 농장 → 수레 → 창고 → 시장 → 상인 → 집 사슬, 집 진화 · 퇴화, 이민,
달 끝 세금 · 임금 · 소비, 해마다의 범람)를 실제로 쓰는 파라오 장르입니다. 땅은 절차로 칠합니다 — 남북으로 굽이치는 나일 강, 양쪽 범람원(농장은 여기에만),
동쪽 풀밭(도시 자리), 서쪽과 동쪽 끝의 사막 · 바위. 화면은 북쪽을 비스듬히 내려다보는 직교 카메라이고, 집 · 서비스 · 창고 · 시장 · 도로는
Kenney City Kit (Suburban) 모델(`Resource/game/nilecity/credits.md`), 땅 · 농장 · 조각상 · 일꾼은 내장 도형입니다. 집은 단계마다 더 큰 집 모델로 바뀌고
(빈 땅은 낮은 울타리), 서비스 건물 · 일꾼은 서비스 색을 입습니다.

## 빌드 · 실행

```powershell
cmake --preset Ninja-Debug-NileCity        # 빌드 폴더 build/Ninja-Debug-NileCity
cmake --build --preset Ninja-Debug-NileCity
cd build/Ninja-Debug-NileCity/Bin
./App.exe -dx12
./App.exe -dx12 -gv_nileAutoPlay=1      # 계획표대로 도로 고리 · 우물 · 농장 · 창고 · 시장 · 집 … 을 지으며 달마다 [Nile] month N pop P money M
```

## 조작

| 키 | 하는 일 |
|----|---------|
| WASD · 방향키 · 휠 | 카메라 이동 · 확대 |
| Q · E · Tab | 지을 것 고르기(도로 → `city.xml` 의 건물 순서) |
| R | 도로 고르기 |
| 왼쪽 버튼 | 커서 칸(노란 상자)에 짓기 — 도로는 누른 채 끌면 이어 깐다. 못 지으면 까닭(`Occupied` · `BadTerrain` · `NotEnoughMoney`)을 로그로 |
| 오른쪽 버튼 | 커서 칸의 건물 · 도로 허물기 |
| P | 자동 계획 켜기 · 끄기(`-gv_nileAutoPlay` 와 같은 계획표, 이어서) |
| Space · − · = | 멈춤 · 속도 ½ · 속도 ×2(0.25 – 8 배) |
| F1 | 도시 상태(인구 · 돈 · 일꾼 · 평균 집 단계 · 문화 · 비옥함)를 로그로 |

한 달은 게임 20 초이고 달이 끝날 때마다 `[Nile] month N pop P money M (net …, workers …, houses up …, avg level …, culture …%)` 이 남습니다.
해가 바뀌면 범람이 범람원 농장의 다음 한 해 비옥함(40 – 100 %)을 정합니다. 커서는 활성 창 크기로 화면 → 땅을 바꾸니, 에디터의 게임 뷰처럼 창의 일부에 그릴 때는
조금 어긋날 수 있습니다.

모습: 집은 단계가 오를수록 큰 집 모델로 바뀌며(빈 집은 낮은 울타리), 서비스 건물 벽은 물 파랑 · 종교 금색 · 오락 보라 · 의료 빨강 · 교육 청록 ·
세금 주황, 농장은 납작한 노란 녹색, 공방 · 창고 · 시장은 갈색 · 주황 기운의 건물, 정원은 나무, 광장은 화분, 조각상은 초록 원기둥입니다. 일꾼은 캡슐 — 서비스 일꾼은 그 서비스 색, 상인은 주황, 수레는 갈색입니다.

## 구조 — 씬 · 프리팹 · 컴포넌트

도시는 씬 하나(`Resource/game/nilecity/maps/nile.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)입니다. 땅은 계획기가 칠하는 절차 지형이라 씬에 두지 않고
디렉터가 플레이 시작에 세웁니다. 에디터에서 열면 카메라 · 해 · 커서 · 디렉터가 보이고, Play 를 누르면 도시가 서며, Stop 은 플레이 전 씬으로 돌아갑니다.
`ThemeParkTycoon` 과 같은 모양입니다(`Source/Games/README.md` 레시피).

| 무엇 | 어디 |
|------|------|
| 카메라 · 해 · 짓기 커서 · 디렉터 | 씬(엔티티) |
| 땅 구간 · 바위 · 도로 칸 · 건물(모델 · 색 상자) · 일꾼 | 프리팹(`prefabs/ground` · `road` · `modelbuilding` · `blockbuilding` · `walker`) — 디렉터가 런타임에 세우고 지운다 |
| 규칙 · 상태(노동 · 순회 일꾼 · 물자 사슬 · 집 진화 · 범람) | 키트의 보통 클래스 `CitySimulation` · `CityCatalog` — 씬 없이 시험한다 |
| 땅 모양 · 자동 계획 | `NileCityPlanner` — 화면을 모르는 순수 규칙 |
| 시뮬레이션 · 자동 계획 · 마우스 짓기/허물기 · 속도 · 달 로그 · 스폰 | `NileDirectorComponent`(씬에 하나 — 언리얼 GameMode/GameState 자리) |
| 모습 | 뷰 `NileBuildingComponent`(건물 하나 — 집 단계마다 모델) · `NileWalkerComponent`(일꾼 하나) · `NileCursorComponent`(커서) — 디렉터를 **읽기만** 한다 |
| 카메라 | GameFramework 공용 `OrthoCameraRigComponent` — WASD · 방향키 · 휠, 초점을 맵 안에 묶고 Q/E 회전은 끈다(회전 단계 0). 커서는 리그의 `findGroundPoint` |
| 머티리얼 | 팔레트 `materials/palette.material`(albedoMap = 키트 색 칸 텍스처) · 커서 `cursor.material`(반투명). 땅 · 건물 종류 · 일꾼 색은 디렉터가 만들어 나눠 쓰는 머티리얼 인스턴스 |

**틱.** 디렉터는 `TickGroup::PrePhysics` 에서 입력 · 자동 계획 · 시뮬레이션을 돌리고, 시뮬레이션과 세운 모습을 견줘 바뀐 도로 칸 · 건물 칸 · 모자란 일꾼 수를 쌓습니다.
틱 안에서는 오브젝트를 만들 수 없으므로 `executeOrDeferPostTick` 한 번으로 틱 뒤에 세우고 지웁니다(효과음도 그때). 건물 칸이 다른 건물로 다시 쓰이면 오브젝트를 바꿔
세우고, 집 단계 · 사람에 따른 모델 바꿈은 뷰가 `PostUpdate` 에서 합니다(모델은 디렉터가 미리 쥐고 있어 워커에서 파일을 읽지 않는다). 커서는 리그의 시점으로 고르므로
리그가 그 프레임에 움직인 만큼은 한 프레임 늦습니다.

**핫 리로드 · 상태 저장.** 도시(칸 · 건물 · 일꾼 · 돈 · 달력 · 난수)와 자동 계획 진행 · 속도 · 고른 도구는 PROPERTY 가 아니라 디렉터의 `writeState` 로 상태
스냅샷의 컴포넌트 섹션에 실려 넘어갑니다(`ComponentStateStore`). 상태를 쓰기 전에 게임 인스턴스(생성자의 `registerDirector` 한 줄 — `GameInstanceBase`)가 도시를 싣고 디렉터가 세운 오브젝트를 걷으며,
다시 만든 디렉터가 데이터를 읽은 뒤 도시를 되살리고 모습을 다시 세웁니다. 카탈로그에서 빠진 건물이 있으면 알리고 새 도시로 시작합니다.

## 파일 · 데이터

- `NileCityGame` — 첫 씬을 열고, 상태 저장 전에 디렉터가 세운 것을 걷습니다.
- `NileDirectorComponent` — 도시 데이터 읽기 · 시뮬레이션 · 자동 계획(`_bAutoPlay`, `-gv_nileAutoPlay=1` 도 켠다, P 는 실행 중에 켜고 끈다) · 입력 · 달 로그 · 스폰.
  도시 데이터 · 프리팹 경로 · 카메라 리그 · 시작 돈 · 순회 서비스 시간은 PROPERTY 입니다.
- `NileBuildingComponent` · `NileWalkerComponent` · `NileCursorComponent` — 뷰.
- `NileCityPlanner` — 절차 지형과 자동 계획표(세 구역 · 167 단계, 단계마다 돈 · 인구 문턱). 화면을 모르는 순수 규칙입니다.
- `Resource/game/nilecity/maps/nile.scene.xml` · `prefabs/` · `materials/` — 엔진 직렬화기가 쓴 파일입니다(손으로 고치면 씬 · 프리팹 형식을 깨기 쉽다 — 에디터로).
- `Resource/game/nilecity/data/city.xml` — 물자 넷 · 건물 열여덟 · 집 단계 여덟(Crude Hut … Spacious Homestead). 자동 계획으로 36 달을 엔진 밖
  하네스에서 돌려 확인했다 — 계획표의 모든 단계가 놓이고(건너뜀 0), 인구 ~400 · 집 대부분 Rough Cottage(4 단계)까지 오르며 돈이 쌓인다.
  인구가 그 근처에서 멈추는 것은 키트의 이민 규칙(일자리보다 실업이 25 % 넘으면 사람이 오지 않는다) 때문이다.
