# 패널을 id 글자로 여는 남은 호출

> 상태: 구현중 · 선행: 없음 · 영역: editor · 옛번호: 없음 · 진행: 2/3

패널 열기 요청은 패널 형식(`requestOpenPanel<T>()`)으로 바꿨지만(2026-10-20) 커맨드 · 뷰포트 · 플러그인 · 자체 테스트에 id 글자와 창 제목 글자로 여는 호출이 남아 있었다. 오타는 실행 중 false 로만 드러난다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| P1 | 패널 창구(`IEditorPanelHost::focusPanel` · `maximizePanel`)가 글자 대신 `EditorPanelID`(`Editor/Common/Workspace/EditorPanelID.h`, 명시 생성자)만 받는다. 코어 패널 id(`editorpanel::kSceneView` · `kGameView`)를 그 층에 두고 `SceneViewPanel::kPanelID` · `GameViewPanel::kPanelID` 가 그 상수를 쓴다 | 끝남(빌드 미확인) |
| P2 | GameFramework 에디터 플러그인 다섯 패널(`AdminToolPanel` · `GimmickGraphPanel` · `CharacterEditorPanel` · `MeshModifiersPanel` · `ParkLayoutPanel`)에 `kPanelID` 를 두고 등록 · 열기를 `setPanelOpen<T>()` 로. 자체 테스트의 창 제목 글자는 `SceneViewPanel::kPanelTitle` · `ContentBrowserPanel::kPanelTitle` 로 | 끝남(빌드 미확인) |
| P3 | 빌드와 에디터 자체 테스트(`sceneView.*` · `toolbar.*` · 플러그인 패널 테스트) 실행 | 남음 |

## 남은 것

- P3: Debug 빌드 뒤 `-EnableEditor` 자체 테스트로 확인한다.
- 같은 모양이 더 있다: 플러그인 자체 테스트의 `ImGui::FindWindowByName( "GM Tool" )` 같은 창 제목 글자. 패널 클래스에 `kPanelTitle` 을 두는 같은 방식으로 옮긴다. 실행 중 id 를 받는 `EditorPanelManager::setPanelOpen( string_view, bool )`(콘솔 인자 · 레이아웃)은 남긴다.
