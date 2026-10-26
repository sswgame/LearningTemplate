# 에디터 시나리오 표

> 상태: 참고 · 선행: 없음 · 영역: editor · 옛번호: 없음 · 진행: 해당없음

에디터 기능마다 어느 자동화 시나리오가 확인하는지 적은 표입니다. 시나리오는 `Resource/engine/automation/editor/` 에 있고 `AppScenarioTest.EditorScenariosPassOn<백엔드>` 가
네 백엔드에서 돌립니다(`rhiswap/` 은 네 백엔드가 모두 있는 PC 에서 손으로 돌린다). 형식과 단계 · 탐침은 [Automation README](../../Source/Engine/Automation/README.md) 에 있습니다.

기능 하나만 보는 시나리오(단독)와, 기능을 잇거나 상태 경계를 넘는 흐름 시나리오(`flow*`)를 나눠 적습니다. 흐름 시나리오는 경계 전후 값을 `Remember` 로 기억해
`sameAs` 로 견줍니다 — 편집 상태 해시(`Editor.StateHash`), 씬 내용(`Editor.SceneHash`), 되돌리기 스택(`Editor.UndoState`), 선택(`Editor.SelectedNames`)입니다.

## 기능 × 시나리오

| 기능 | 단독 시나리오 | 흐름 · 경계 시나리오 |
|------|---------------|----------------------|
| 콘텐츠 브라우저 · 에셋 관리 | `contentbrowser` · `contentthumbnails` · `assetmanage` · `openasset` | `flowedit`(메시를 씬 뷰로 끌어 놓기) |
| 에셋 임포트 | `flowimport`(임포트 · 스폰 — 단독을 겸한다) | `flowimport`(임포트 → 씬에서 쓰기 → 저장 → 다시 열기) |
| 씬 · 계층 | `hierarchyedit` · `hierarchydrag` · `hierarchyfilter` · `hierarchyorder` · `addcomponent` · `hidelock` · `multiedit` | `flowselection` · `flowboundaries` |
| 인스펙터 | `inspectorhandle`(타입 핸들 선택기) · `scenereference`(씬 오브젝트 참조 선택기) · `inspectoredit` · `inspectorrename` · `inspectorcombo` · `propertyreset` · `colliderhandle` | `flowedit` · `flowprefab` · `flowselection` |
| 뷰포트 · 선택 | `viewportpick` · `billboardpick` · `viewportbasics` · `viewporthover` · `viewmodes` | `flowselection`(계층 선택 → 씬 뷰 외곽선) |
| 프리팹 | `prefabwindow`(프리팹 창 열기 · X 미저장 확인) · `flowprefab`(만들기 · 스폰 · 적용 — 단독을 겸한다) | `flowprefab`(적용 → 다른 인스턴스 갱신 · 덮어쓴 값 유지 · 되돌리기 · 저장 · 다시 열기) · `flowprefabnested`(자식 인스턴스 → 중첩 부품, 적용 → 다른 프리팹이 스폰한 부품 갱신 · 다시 열기) |
| 지형 브러시 | `terrainbrush` · `rhiswap/terrainedits` | `flowterrain`(편집 → 플레이 → 백엔드 교체 → 되돌리기) |
| 메시 모디파이어 | `meshmodifiers` | `flowedit`(배치 → 휘기 → 플레이 뒤 미리보기 유지) |
| 캐릭터 편집 | `charactereditor`(미리보기 탭의 꾸미기 슬라이더는 shooter3d `customization.xml` 의 Adventurer `Roughness` 슬라이더가 있어야 그려진다) · `retarget`(리타깃 프로필 창 — 위 구역을 접어 포즈 칸에 닿는다) | `game/shooter3d/automation/charactereditorscene`(외형 → 데이터 프리셋 내보내기 → 씬의 외형 컴포넌트가 그 id → 플레이 조립) |
| 타일맵 | `tilemapdatalayers`(데이터 레이어 칠하기 · 저장 왕복) | — |
| 에디터 시작 | `editorstartup`(`Editor.HasPanel` 로 패널을 등록하는 이미지마다 대표 패널을 id 로 묻는다 — 패널 수를 숫자로 박지 않는다) | — |
| 입력 맵 창 | `inputmapedit` · `inputmaptaps` · `panelclose`(문서 패널이 아닌 패널의 닫기 확인 · 창 크기를 바꾼 뒤에도 확인이 호스트 창 가운데에 뜬다) | — |
| UI 미리보기 · 디자이너 | `uipreview` · `uipreviewdesigner` · `uiscale` | — |
| 다중 월드 미리보기 | `previewscene` · `previewreopen`(머티리얼 · 프리팹 창을 연 채 8 번 여닫기, 렌더러 내리기 — 탐침 `Editor.ScenePreviewRenderers` 가 닫을 때 줄고 열 때 돌아오는지) | `previewswap`(창을 연 채 `SwapBackend other` → `start`, 교체 뒤 렌더러 수가 교체 앞과 같은지) |
| 머티리얼 · 셰이더 | `materialpreview` | — |
| 애니메이션 · 타임라인 | `animgraphedit` · `animgraphsave` · `sequencerkeys` · `curveedit` | — |
| 대화 · 기믹 그래프 | `dialoguegraph` · `dialoguegraphsave` · `gimmickgraph` · `graphaddnode` | — |
| 스케줄 편집(NPC 일정 디버그 창) | `game/harvestvalley/automation/scheduleeditor`(루트 `editor="true"` — 게임 팩 시나리오를 에디터로) | `scheduleeditor`(플레이 → 창 → NPC 고르기 → 시계 감기) |
| 서버 디버그 채널 | `netplay` · `netconditions` · `netprofiler` | — |
| 되돌리기 · 명령 스택 | `workflow` · `undosurvivesplay` | `flowboundaries` · `flowselection` · `flowprefab` · `flowterrain` · `flowedit` |
| 저장 · 로드 | `workflow` · `windowtitle` · `savedstate.1/.2` | `flowedit` · `flowprefab` |
| 플레이 인 · 아웃 | `workflow` · `playedit` · `sceneviewgameview` | `flowboundaries` · `flowedit` · `flowterrain` |
| 모듈 리로드 | `modules`(창) · 체인 사이 `chain/reload*` | `flowboundaries`(게임 모듈 · 에디터 모듈) |
| 백엔드 교체 | `rhiswap/swapbackends` · `rhiswap/removedevice`(손으로) | `flowboundaries` · `flowterrain`(`SwapBackend other` → `start`) |
| 레이아웃 저장 · 복원 | `defaultlayout` · `savedstate.1/.2` | `flowboundaries`(이름 붙인 레이아웃 저장 → 초기화 → 불러오기) |
| 라이트 프로브 굽기 | `lightprobebake`(볼륨 없음 · 굽기 전) · `lightprobebakecancel`(나눠 굽기 진행 탐침 · 상태줄 Cancel, 실행 미확인) | — |
| 설정 · 단축키 · 테마 | `preferences` · `shortcuts` · `projectconfigedit` · `commandpalette` | — |
| 프로파일러 · 디버그 | `profilercapture` · `profilertimeline` · `rendertargetview` · `debughudcontrols` · `outputlog` · `testrunner` | — |

## 시나리오 규칙 — 임시 자리

- **임시 파일**은 작업 폴더의 `Saved/Automation/<시나리오>/` 에 둔다(`asset.open` · `prefab.create` · `scene.saveAs`). 리소스 트리는 거절한다.
  에디터 저장은 없는 폴더를 만들지 않으므로(오류로 멈춘다), 캡처 · `asset.open` 이 폴더를 만들어 주지 않는 시나리오는 처음에 `asset.makeTempFolder Saved/Automation/<시나리오>` 를 부른다(`tilemapdatalayers`).
- **누를 위젯이 창의 보이는 영역 밖이면** 클릭 단계가 "outside the visible area" 로 알린다. 시나리오가 위 구역을 눌러 접거나(`retarget` 의 `retarget.section.<제목>`) 창 크기를 키워 닿게 한다.
  콤보 팝업은 기본 8 줄이라 뒤의 항목이 스크롤 아래로 가려진다 — 항목이 많은 콤보는 `ImGuiComboFlags_HeightLarge` 로 연다(`scenereference`). 레이블 글이 붙은 입력은 `EditorSelfTestMarks::noteInputFrame` 으로 입력 칸만 표식으로 남긴다(`inspector.method.arg.*`).
- **임포트한 에셋**은 리소스 id 가 있어야 씬이 쓰므로 리소스 트리 안의 임시 폴더 `editor/automationtemp/<시나리오>/` 에만 가져온다(`asset.import <원본> <폴더>`).
  이 폴더는 `.gitignore` 가 무시하고, 시나리오가 처음과 끝에 `asset.removeImportFolder` 로 지운다. 남은 파일이 있으면 같은 이름이 새 이름(`_1`)을 받아 뒤 단계가 어긋난다.
  원본은 작업 폴더 기준 경로나 리소스 id 이고, 모델 원본(glTF)의 변환은 에디터가 아니라 `App --import-models` 가 한다.
- **에디터가 쓰는 게임 데이터**(캐릭터 편집 창의 데이터 프리셋)도 같은 임시 폴더로 내보낸다. 창의 내보내기 폴더 칸에 `editor/automationtemp/<시나리오>` 를 적으면
  게임 데이터 폴더(`game/<팩>/data/appearance/presets/`)는 그대로이고, 내보내기가 실행 중인 게임의 외형 데이터에 그 파일을 올려 씬이 같은 실행에서 쓴다.
- **게임 팩의 에디터 창**은 그 팩의 `automation/` 시나리오 루트에 `editor="true"` 를 적는다. `AppScenarioTest` 가 `-game=<게임> -EnableEditor` 와 빈 에디터 상태 폴더로 돌린다.

## 시나리오로 못 하는 것

- **다른 씬의 프리팹 인스턴스.** Apply Overrides 는 지금 연 씬의 인스턴스(놓은 인스턴스와 다른 프리팹이 스폰한 중첩 부품)만 고친다. 닫힌 씬의 인스턴스는
  덮어쓴 값만 저장돼 있어 다시 열 때 새 프리팹 위에 얹힌다(유니티 · 언리얼도 닫힌 레벨은 로드할 때 받는다). 다른 바깥 프리팹 인스턴스에 새로 생긴 부품은 다시 열 때 생긴다.
- **에디터 UI 화면 확인의 범위.** 흐름 시나리오의 `screenshot` · `-gv_screenshot` 은 게임 · 씬 뷰 렌더 출력만 담는다. 패널은 `CaptureEditorUI` 로 찍는데,
  그 확인은 DX12 에서만 눈으로 봤다(네 백엔드가 같은 ImGui 그리기 목록을 그리므로 같다고 보지만 다른 백엔드의 그림은 보지 않았다).
