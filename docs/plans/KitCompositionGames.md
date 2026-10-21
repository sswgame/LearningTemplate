# 키트 조립의 두 번째 테스트 게임과 땅 쓰기

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-4 · 진행: 1/2

틀 · 키트 전부 · 조립 테스트 둘(`KitCompositionTest`)과 `MeadowVillage` 에 이어, 둘째 조립(도시 + RTS)의 테스트 게임 `FrontierTown`(`Source/Games/FrontierTown`, 팩 `game/frontiertown`)이 들어갔다.
규칙은 `Source/GameFramework/Kits/` "키트 여럿을 한 게임에"이다. 공유 상태가 땅을 연다(`FrontierTownData::makeStateSettings` 의 `_landWidth` · `_landHeight`). 도시와 RTS 디렉터가 각자 키트의 `bindLand` 로 같은 땅을 원점 (0, 0) 에 빌린다.
배치는 `KitCompositionTest.CityAndSkirmish*` 와 같다(신전 (4, 4) · 도로 (0..15, 10) · 병영 (20, 20) · 해병 (2.5, 6.5) ↔ (10.5, 6.5)). 그래서 땅을 나누는 규칙은 그 테스트가 단언하고, 게임은 판을 열고 도는 흐름과 모습을 보인다.
함정: 땅은 놓기 전에 빌린다(이미 놓인 건물은 땅을 얻지 않는다). SRPG 는 전투 끝에 `releaseLand`. 공원 놀이기구 발자국은 상태 바이트에 싣지 않는다 — 지은 칸은 땅이 든다.

## K2 빌드와 실행 확인

- `Ninja-Debug` 전체 빌드(게임 폴더는 모두 컴파일된다)로 `FrontierTown` 의 컴파일을 확인한다. 작업자는 빌드하지 않았다.
- `App.exe -game=FrontierTown -dx12 -scenario=game/frontiertown/automation/month.scenario.xml` 이 0 으로 끝나는지, `AppScenarioTest.EveryGamePackScenarioPassesOnEveryBackend` 가 네 백엔드에서 이 팩을 통과하는지 본다.
- 캡처로 신전 · 도로 판, 병영이 서는 것, 해병이 신전을 돌아 오가는 것을 본다. 로그에 `[Frontier] ... refused` 오류가 없어야 한다(배치가 땅과 맞지 않으면 남는다).
- QA 골든(`Test/QA/Golden/FrontierTown`)은 아직 없다 — `Test/QA/Games.json` 에 줄만 넣었다.
