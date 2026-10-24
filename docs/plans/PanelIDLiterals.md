# 패널을 id 글자로 여는 남은 호출

> 상태: 측정대기 · 선행: 없음 · 영역: editor · 옛번호: 없음 · 진행: 3/3

**남은 확인(통합에서 확인):** 매니페스트 id `panel-id-literals-p3`(`Test/QA/VerifyBatch.json`). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

패널 열기 요청은 패널 형식(`requestOpenPanel<T>()`)으로 바꿨지만(2026-10-20) 커맨드 · 뷰포트 · 플러그인 · 자체 테스트에 id 글자와 창 제목 글자로 여는 호출이 남아 있었다. 오타는 실행 중 false 로만 드러난다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| P1 | 패널 창구(`IEditorPanelHost::focusPanel` · `maximizePanel`)가 글자 대신 `EditorPanelID`(`Editor/Common/Workspace/EditorPanelID.h`, 명시 생성자)만 받는다. 코어 패널 id(`editorpanel::kSceneView` · `kGameView`)를 그 층에 두고 `SceneViewPanel::kPanelID` · `GameViewPanel::kPanelID` 가 그 상수를 쓴다 | 끝남 |
| P2 | GameFramework 에디터 플러그인 다섯 패널(`AdminToolPanel` · `GimmickGraphPanel` · `CharacterEditorPanel` · `MeshModifiersPanel` · `ParkLayoutPanel`)에 `kPanelID` 를 두고 등록 · 열기를 `setPanelOpen<T>()` 로. 자체 테스트의 창 제목 글자는 `SceneViewPanel::kPanelTitle` · `ContentBrowserPanel::kPanelTitle` 로 | 끝남 |
| P3 | 같은 모양 마저 옮기기 — 자체 테스트가 `ImGui::FindWindowByName` 에 넘기던 창 제목 글자(`"Hierarchy"` · `"Game"` · `"Inspector"` · `"Scene"` · `"Park Layout"` · `"GM Tool"` · `"Gimmick Graph"`)를 각 패널의 `kPanelTitle`(`getPanelTitle()` 도 같은 상수)로. 실행 중 id 를 받는 `EditorPanelManager::setPanelOpen( string_view, bool )`(콘솔 인자 · 레이아웃)은 남긴다 | 끝남(코드). 2026-10-23 Debug `full-build` · `EditorTest` 474 개 통과는 이 옮김 앞의 값이라, 옮긴 뒤의 빌드와 에디터 자체 테스트 실행은 확인 대기: 매니페스트 id `panel-id-literals-p3` |

## 남은 것

- 실행: `-EnableEditor` 자체 테스트(`sceneView.*` · `toolbar.*` · 플러그인 패널 테스트)는 GPU 로 앱을 띄워야 해서 통합이 돌린다(2026-10-23 Debug `full-build` 경고 0 · `EditorTest` 474 개는 통과했다). 창 제목 상수는 이전 글자와 같은 값이라 동작은 같다.
