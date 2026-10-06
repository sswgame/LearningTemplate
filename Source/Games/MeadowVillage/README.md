# MeadowVillage — 키트 조립 시험 게임

농장(`GF_Farming`)과 생물 마을(`GF_CreatureLife`)을 한 씬에 섞는다. 씬의 `VillageState` 오브젝트에 **공유 상태 → 밭 디렉터 → 마을 디렉터** 순서로 붙어 있고(그 순서가 틱 순서),
돈 · 시계 · 퀘스트 일지 · 호감도(공유 평판의 세력 `creature.<종>`)는 공유 상태(`GameStateComponent`) 하나에 있다.

- 밭: 4 × 2 칸 순무. 하루(실제 2 분)가 넘어가면 자라고, 다 자란 칸은 거둬 공유 지갑에 판다(30 G). 시작 돈 20 G 는 새 판일 때만.
- 마을: 풀 두 칸(서식지 `meadow`)에 `sprout` 가 낮에 찾아와 부탁(`orchard` 서식지 하나)을 한다. 공유 지갑에 50 G 가 모이면 같은 틱에 나무 두 그루를 심어 부탁이 끝난다.
- 조작: Space 누르는 동안 8 배속(`Village.FastForward`), E 생물과 대화(`Town.Talk`, 하루 한 번) — 키는 `data/meadow.input.xml`. `-gv_meadowAutoPlay=1` 이면 늘 8 배속.
- 카메라는 씬의 고정 직교 카메라 — 디렉터 둘 다 카메라를 만지지 않는다(키트 조립 규칙: 카메라를 미는 것은 한 디렉터).
- 시험: `KitCompositionTest`(EngineTest — 같은 시나리오를 모의 디렉터로), `ArchitectureTest.LiveReloadOneOfTwoKitsCascadesIntoTheGameOnly`(SmokeTest).
