# 에디터 시나리오 표

에디터 기능마다 어느 자동화 시나리오가 확인하는지 적은 표입니다. 시나리오는 `Resource/engine/automation/editor/` 에 있고 `AppScenarioTest.EditorScenariosPassOn<백엔드>` 가
네 백엔드에서 돌립니다(`rhiswap/` 은 네 백엔드가 모두 있는 PC 에서 손으로 돌린다). 형식과 단계 · 탐침은 [Automation README](../../Source/Engine/Automation/README.md) 에 있습니다.

기능 하나만 보는 시나리오(단독)와, 기능을 잇거나 상태 경계를 넘는 흐름 시나리오(`flow*`)를 나눠 적습니다. 흐름 시나리오는 경계 전후 값을 `Remember` 로 기억해
`sameAs` 로 견줍니다 — 편집 상태 해시(`Editor.StateHash`), 씬 내용(`Editor.SceneHash`), 되돌리기 스택(`Editor.UndoState`), 선택(`Editor.SelectedNames`)입니다.

## 기능 × 시나리오

| 기능 | 단독 시나리오 | 흐름 · 경계 시나리오 |
|------|---------------|----------------------|
| 콘텐츠 브라우저 · 에셋 관리 | `contentbrowser` · `contentthumbnails` · `assetmanage` · `openasset` | `flowedit`(메시를 씬 뷰로 끌어 놓기) |
| 에셋 임포트 | 없음(아래 "시나리오로 못 하는 것") | 없음 |
| 씬 · 계층 | `hierarchyedit` · `hierarchydrag` · `hierarchyfilter` · `hierarchyorder` · `addcomponent` · `hidelock` · `multiedit` | `flowselection` · `flowboundaries` |
| 인스펙터 | `inspectoredit` · `inspectorrename` · `inspectorcombo` · `propertyreset` · `colliderhandle` | `flowedit` · `flowprefab` · `flowselection` |
| 뷰포트 · 선택 | `viewportpick` · `billboardpick` · `viewportbasics` · `viewporthover` · `viewmodes` | `flowselection`(계층 선택 → 씬 뷰 외곽선) |
| 프리팹 | `flowprefab`(만들기 · 스폰 · 적용 — 단독을 겸한다) | `flowprefab`(적용 → 다른 인스턴스 갱신 · 덮어쓴 값 유지 · 되돌리기 · 저장 · 다시 열기) |
| 지형 브러시 | `terrainbrush` · `rhiswap/terrainedits` | `flowterrain`(편집 → 플레이 → 백엔드 교체 → 되돌리기) |
| 메시 모디파이어 | `meshmodifiers` | `flowedit`(배치 → 휘기 → 플레이 뒤 미리보기 유지) |
| 캐릭터 편집 | `charactereditor` | 없음(아래) |
| 입력 맵 창 | `inputmapedit` · `inputmaptaps` | — |
| UI 미리보기 · 디자이너 | `uipreview` · `uipreviewdesigner` · `uiscale` | — |
| 다중 월드 미리보기 | `previewscene` | — |
| 머티리얼 · 셰이더 | `materialpreview` | — |
| 애니메이션 · 타임라인 | `animgraphedit` · `animgraphsave` · `sequencerkeys` · `curveedit` | — |
| 대화 · 기믹 그래프 | `dialoguegraph` · `dialoguegraphsave` · `gimmickgraph` · `graphaddnode` | — |
| 스케줄 편집(NPC 일정 디버그 창) | 없음(아래) | 없음 |
| 서버 디버그 채널 | `netplay` · `netconditions` · `netprofiler` | — |
| 되돌리기 · 명령 스택 | `workflow` · `undosurvivesplay` | `flowboundaries` · `flowselection` · `flowprefab` · `flowterrain` · `flowedit` |
| 저장 · 로드 | `workflow` · `windowtitle` · `savedstate.1/.2` | `flowedit` · `flowprefab` |
| 플레이 인 · 아웃 | `workflow` · `playedit` · `sceneviewgameview` | `flowboundaries` · `flowedit` · `flowterrain` |
| 모듈 리로드 | `modules`(창) · 체인 사이 `chain/reload*` | `flowboundaries`(게임 모듈 · 에디터 모듈) |
| 백엔드 교체 | `rhiswap/swapbackends` · `rhiswap/removedevice`(손으로) | `flowboundaries` · `flowterrain`(`SwapBackend other` → `start`) |
| 레이아웃 저장 · 복원 | `defaultlayout` · `savedstate.1/.2` | `flowboundaries`(이름 붙인 레이아웃 저장 → 초기화 → 불러오기) |
| 설정 · 단축키 · 테마 | `preferences` · `shortcuts` · `projectconfigedit` · `commandpalette` | — |
| 프로파일러 · 디버그 | `profilercapture` · `profilertimeline` · `rendertargetview` · `debughudcontrols` · `outputlog` · `testrunner` | — |

## 흐름 시나리오가 찾아 고친 결함 (2026-10-15)

- 백엔드 교체와 에디터 모듈 리로드 뒤 되돌리기 스택 · 선택 · 미저장 표시가 사라졌다(`flowboundaries`). 새 에디터 인스턴스가 지금 씬을 새 씬으로 보고 비웠다.
- Apply to Prefab 이 같은 씬의 다른 인스턴스에 퍼지지 않았고 되돌리기에도 남지 않았다(`flowprefab`).
- 에디터 문구 표(`editor.locproject.json`)가 한 번도 올라가지 않았다(`flowprefab` 의 `ExpectLog`).
- 저장하지 않은 지형 편집이 플레이 시작과 정지에서 사라졌고, 백엔드 교체가 지형 붓질 되돌리기를 버렸다(`flowterrain`).
- 메시를 씬 뷰로 끌어 놓으면 배치되지 않고 메시 문서만 열렸다(`flowedit`).
- 에디터 모듈만 다시 올려도 게임 인스턴스가 내려가 그 뒤 씬 저장이 막혔다(`flowboundaries` 끝의 저장 — 체인에서 뒤 시나리오의 저장이 줄줄이 졌다).

## 시나리오로 못 하는 것

- **에셋 임포트.** 콘텐츠 브라우저의 Import 는 OS 파일 대화상자로 원본을 고르고 리소스 트리에 복사한다(`EditorAssetFileCommands::importFiles`). 무인 실행에서 대화상자를 닫을 수 없고,
  임시 경로(`Saved/Automation`)로 가져오면 리소스 id 가 없어 씬이 그 에셋을 쓸 수 없다. 모델 원본(glTF)의 변환은 에디터가 아니라 `App --import-models` 가 한다.
- **캐릭터 편집 → 저장 → 씬 사용.** 캐릭터 편집 창의 프리셋은 플레이어 세이브 형식(`UserAppearancePresetStore`)이고, 씬의 `CharacterAppearanceComponent` 는 데이터 프리셋 id
  (`data/appearance/presets.xml`)만 가리킨다. 창에서 저장한 것을 씬이 쓰는 길이 엔진에 없다.
- **스케줄 편집.** NPC 일정 디버그 창은 일정 시스템이 있는 게임(HarvestValley)에서만 내용이 있다. 에디터 시나리오 체인은 기본 게임(Empty)으로 돌고, 게임 팩 시나리오는 에디터 없이 돈다.
