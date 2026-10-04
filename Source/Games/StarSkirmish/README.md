# StarSkirmish — 실시간 전략 시험 게임

`GF_RealTimeStrategy` 키트(유닛 카탈로그, 채취 · 보급 · 생산 대기열 · 테크, 일꾼 건설과 정제소, 전투 · 공격 이동, 흐름장 무리 이동, 전장의 안개,
끌어 고르기 · 부대, 행동 트리 AI)를 실제로 쓰는 스타크래프트 장르 1 대 1 입니다. 64×64 맵은 절차로 놓습니다 — 남서(파랑)와 북동(빨강)의 점 대칭 기지
(본진 · 일꾼 넷 · 광물 여덟 · 간헐천), 두 기지를 가르는 반대각선 절벽 능선과 그 사이 비탈길 셋, 고지 둘, 가운데 길목의 확장 광물 · 간헐천.
화면은 북쪽을 비스듬히 내려다보는 직교 카메라이고 유닛 · 건물 · 자원은 Kenney Space Kit
모델(`Resource/game/starskirmish/credits.md`), 땅 · 절벽은 내장 도형입니다.

## 빌드 · 실행

```powershell
cmake --preset Ninja-Debug-StarSkirmish        # 빌드 폴더 build/Ninja-Debug-StarSkirmish
cmake --build --preset Ninja-Debug-StarSkirmish
cd build/Ninja-Debug-StarSkirmish/Bin
./App.exe -dx12                           # 사람(파랑) 대 컴퓨터(빨강)
./App.exe -dx12 -gv_skirmishAutoPlay=1    # 컴퓨터(러시) 대 컴퓨터(운영) — 승패까지
```

## 조작(사람 대 컴퓨터)

| 키 | 하는 일 |
|----|---------|
| 방향키 · 휠 · Space | 카메라 이동 · 확대 · 고른 유닛으로 |
| 왼쪽 버튼 끌기 · 클릭 | 고르기(내 움직이는 유닛 먼저 — 최대 12) · Shift 로 더하기 |
| 오른쪽 버튼 | 적이면 공격, 광물 · 내 정제소면 채취(일꾼), 덜 지은 내 건물이면 이어 짓기, 빈 땅이면 무리 이동(건물은 집결지) · Shift 로 대기열 |
| A → 왼쪽 클릭 | 공격 이동 |
| S · H | 멈춤 · 제자리 |
| Q · W · E | 고른 건물의 첫째 · 둘째 · 셋째 생산(본진 → 일꾼, 병영 → 해병 · 화염방사병, 공장 → 벌처 · 탱크 · 골리앗, 우주공항 → 레이스) |
| B · N · G · Y · F · T · U | 고른 일꾼으로 커서 자리에 보급고 · 병영 · 정제소(가까운 간헐천) · 아카데미 · 공장 · 우주공항 · 벙커 |
| Ctrl + 숫자 · Shift + 숫자 · 숫자 | 부대 정하기 · 더하기 · 부르기 |
| − · = · P | 속도 ½ · ×2(0.25 – 8 배) · 멈춤 |
| F1 | 두 플레이어 형편을 로그로 |

자동 플레이에서는 WASD 도 카메라를 움직입니다. 사람 쪽 화면은 안개가 적 유닛을 가립니다(자원은 늘 보인다). 30 초마다
`[Skirmish] t=..s p0 workers .. army .. buildings .. minerals .. supply ../.. | p1 ...` 이, 한쪽이 건물을 모두 잃으면 `[Skirmish] game over at t=..s - player N wins` 가
남습니다. 커서는 활성 창 크기로 화면 → 땅을 바꾸니, 에디터의 게임 뷰처럼 창의 일부에 그릴 때는 조금 어긋날 수 있습니다.

모습: 파랑 · 빨강이 주인 색 — 모델에 곱해 입힌다. 건물은 격납고 · 발전기 · 안테나 · 포탑 · 착륙장(짓는 동안 솟는다), 일꾼은 채굴선, 병력은 종류마다
다른 우주선(공중은 떠 있고, 움직이는 쪽을 본다), 고르면 더 밝아집니다. 광물은 하늘색 결정(캘수록 낮아진다), 간헐천은 초록 분화구, 절벽은 갈색 벽입니다.

## 구조 — 씬 · 프리팹 · 컴포넌트

전장은 씬 하나(`Resource/game/starskirmish/maps/skirmish.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)입니다. 절벽은 판이 칠하는 절차 지형이라 디렉터가
플레이 시작에 세웁니다. 에디터에서 열면 카메라 · 해 · 땅 · 끌기 상자 · 디렉터가 보이고, Play 를 누르면 판이 열리며, Stop 은 플레이 전 씬으로 돌아갑니다.
`ThemeParkTycoon` 과 같은 모양입니다(`Source/Games/README.md` 레시피).

| 무엇 | 어디 |
|------|------|
| 카메라 · 해 · 땅 · 끌어 고르기 상자 · 디렉터 | 씬(엔티티) |
| 절벽 구간 · 유닛(건물 · 자원 포함) | 프리팹(`prefabs/cliff` · `unit`) — 디렉터가 런타임에 세우고 지운다(유닛 모델은 종류마다 디렉터가 건다) |
| 규칙 · 상태(채취 · 생산 · 전투 · 안개 · AI) | 키트의 보통 클래스 `RtsWorld` · `RtsAiController` · `RtsSelection` 과 판 규칙 `SkirmishMatch` — 씬 없이 시험한다 |
| 판 · 고르기 · 명령 · 생산 · 건설 · 부대 · 속도 · 알림 · 스폰 | `SkirmishDirectorComponent`(씬에 하나 — 언리얼 GameMode/GameState 자리) |
| 모습 | 뷰 `SkirmishUnitComponent`(유닛 하나 — 자리 · 지은 만큼 · 남은 자원만큼 · 움직인 쪽 · 편 색 · 고름) · `SkirmishDragComponent`(끌기 상자) — 디렉터를 **읽기만** 한다 |
| 카메라 | GameFramework 공용 `OrthoCameraRigComponent` — 방향키 · 휠, 초점을 맵 안에 묶고 Q/E 회전은 끈다. 사람 쪽은 WASD 를 끄고(글자 키가 명령), 자동 플레이는 디렉터가 맵 전체가 보이게 물러서며 WASD 를 켠다. 마우스는 리그의 `findGroundPoint` |
| 머티리얼 | `materials/ground` · `cliff` · `drag`(반투명). 편 색 · 자원 색 · 고름은 디렉터가 만들어 나눠 쓰는 머티리얼 인스턴스 |

**틱.** 디렉터는 `TickGroup::PrePhysics` 에서 입력 · 판 · 알림을 돌리고, 보일 유닛(사람 쪽은 안개에 가린 적을 뺀다)과 세운 모습을 견줘 새로 보인 · 사라진 유닛 자리를
쌓습니다. 틱 안에서는 오브젝트를 만들 수 없으므로 `executeOrDeferPostTick` 한 번으로 세우고 지웁니다(효과음도 그때). 뷰는 `PostUpdate` 에서 디렉터의 판을 읽어 자기
오브젝트만 맞춥니다. 커서는 리그의 시점으로 고르므로 리그가 그 프레임에 움직인 만큼은 한 프레임 늦습니다.

**핫 리로드 · 상태 저장.** 판은 런타임 상태라 리로드에서 처음부터 다시 섭니다(PROPERTY 만 남는다). 상태를 쓰기 전에 게임(`onBeforeStateSerialize`)이 디렉터가 세운
오브젝트를 걷고, 다시 만든 디렉터가 시작하며 다시 세웁니다.

## 파일 · 데이터

- `StarSkirmishGame` — 첫 씬을 열고, 상태 저장 전에 디렉터가 세운 것을 걷습니다.
- `SkirmishDirectorComponent` — 유닛 데이터 읽기 · 판 · 사람 입력(고르기 · 명령 · 생산 · 건설 · 부대) · 알림 · 스폰. 자동 플레이(`_bAutoPlay`,
  `-gv_skirmishAutoPlay=1` 도 켠다)면 컴퓨터 대 컴퓨터. 유닛 데이터 · 프리팹 경로 · 카메라 리그 · 구경 시점은 PROPERTY 입니다.
- `SkirmishUnitComponent` · `SkirmishDragComponent` — 뷰. 유닛 → 모델 표는 `SkirmishUnitComponent` 에 있습니다.
- `SkirmishMatch` — 절차 맵 · 시작 유닛 · AI(성향 셋: 러시 · 운영 · 사람 상대) · 정리 사냥(적 시작 지점에 닿아 노는 병력을 남은 적 건물로) · 상태 · 승패 로그.
  화면을 모르는 순수 규칙입니다.
- `Resource/game/starskirmish/maps/skirmish.scene.xml` · `prefabs/` · `materials/` — 엔진 직렬화기가 쓴 파일입니다(손으로 고치면 씬 · 프리팹 형식을 깨기 쉽다 — 에디터로).
- `Resource/game/starskirmish/data/units.xml` — 자원 셋 · 건물 아홉 · 유닛 일곱. 엔진 밖 하네스에서 확인했다 — AI 대 AI 는 422 초에 1 번(운영)이
  이기고, 사람이 아무것도 하지 않으면 284 초에 진다.
