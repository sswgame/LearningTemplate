# 에디터 후보

> 상태: 후보 · 선행: 없음 · 영역: editor · 옛번호: B-4, D3-8, D3-9 · 진행: 해당없음

에디터 패널, 에디터 플러그인, 에디터 시나리오의 착수 전 후보 목록입니다. 착수하면 절을 계획 문서로 옮기고 여기서 지웁니다. 이미 계획 문서가 있는 일은 [EditorMultiWorld](EditorMultiWorld.md)(다중 월드 · 툴 창), [EditorUX](EditorUX.md)(UX 남은 결함), [EditorFeatureGaps](EditorFeatureGaps.md)(지형 칠하기 · UI 디자이너 · 치트 · 타입 핸들 인스펙터), [DataKindRegistry](DataKindRegistry.md), [MeshModifierPreviewCost](MeshModifierPreviewCost.md), [ContentBrowserWatchAndScan](ContentBrowserWatchAndScan.md)(콘텐츠 브라우저 감시 지연 · 스캔 비용)에 있습니다.

## 확인 대기

### EditorRealRunChecks
제목: 실기동으로 아직 확인하지 못한 것
옛번호: B-4
왜 못 하는가: 리눅스와 외부 프로그램 · 실기 입력이 있어야 확인된다. 풀리는 조건은 항목마다 다르다.
남은 것: 리눅스 X11 · dlopen 분기(CI 리눅스 잡), 실제 RenderDoc 캡처(이 PC 에 RenderDoc 이 없다), 단언 대화상자의 세 단추(모달이라 자동화가 누를 수 없다), Tracy 0.14.1 뷰어 연결(`localhost:8086` 직접 입력), 배율 1.5 · 2 의 에디터(`gv_editorUiScale` 로 눈으로), 스크린샷 토스트의 "Show in Explorer" 단추(알림 매니저에 동작 단추가 없다 — 기능 추가가 먼저).
함정: RenderDoc 은 RHI 단계 맨 앞에서 로드하고 `ThirdParty/renderdoc` 에 원문 헤더를 두며 그 폴더의 `.clang-format` 은 `DisableFormat` 이다.

### EditorCaptureFollowUps
제목: 캡처로 확인하고 남은 것
옛번호: D3-9
왜 못 하는가: 둘 다 외부 조건이 있어야 한다.
남은 것:
- 녹화메뉴: 에디터 메뉴의 시나리오 녹화(`automation.toggleScenarioRecord`)는 자동화가 주입한 입력을 기록하지 않아(`0 steps`) 실제 입력으로만 확인된다(게임 실행의 `-scenario-record` 는 가상 입력도 적고 왕복 테스트가 있다). 풀리는 조건: 사람이 직접 입력.
- 빈상태창: 어빌리티 디버거 · 오디오 이벤트 · 리그 디버그 · 시퀀서 `RigSlot` 트랙 · UI 디자이너 앵커 프리셋은 빈 상태(안내 글)까지만 봤다. 쓰는 데이터(리그 · 오디오 이벤트 라이브러리)가 저장소에 없다. 풀리는 조건: 그 데이터가 들어올 때.

### ContentBrowserAsyncWaits
제목: 콘텐츠 브라우저의 백그라운드 작업을 고정 프레임으로 기다리는 남은 곳
옛번호: 없음
왜 필요한가: 참조 역색인은 "마지막 변경을 따라왔다"(`ContentBrowserReferenceSearch::isCurrent`)를 준비로 보고 보이는 패널에서 시계를 잡게 고쳤다(`assetmanage` 간헐 실패, 폴더 목록의 고정 프레임 대기는 `contentthumbnails` 를 `WaitUntil` 로 바꿔 없앴다). 같은 모양이 남았다.
- 폴더 목록: `contentthumbnails` 는 `content.open` 뒤 65 프레임에 `Editor.ThumbnailCacheCount` 를 단언한다. 목록은 워커 잡(`EditorFolderListingJob`)이라 워커가 밀리면(같은 때 도는 역색인 스캔) 고정 프레임이 모자랄 수 있다. 아직 진 적은 없다.
- 감시 지연 · 스캔 비용은 [ContentBrowserWatchAndScan](ContentBrowserWatchAndScan.md) 로 옮겼다.
착수 조건: 폴더 목록 단언이 한 번이라도 지거나 에디터 체인 시간이 시한에 닿을 때.

### PopupWindowCapture
제목: 팝업 창을 캡처 시나리오가 찍도록 캡처 영역을 플랫폼 창 합집합으로 넓히기
옛번호: 없음
왜 필요한가: 뷰포트 옵션 팝업은 `ViewportsEnable` 때문에 OS 창이 되는데 `CaptureWindow`(`AutomationWindowSteps.cpp`)는 주 창 클라이언트 사각형만 BitBlt 해서 팝업이 잘려 보인다. 팝업은 의도된 동작이라(Decisions 5-11) 캡처 도구의 빈틈이다.
착수 조건: 팝업을 캡처로 확인하는 시나리오가 생길 때.

## 기능

### ScenarioRecorderAndInspectorGaps
제목: 시나리오 녹화의 남은 한계
옛번호: D3-8
왜 못 하는가: 원시 DoubleClick 이벤트를 읽는 코드가 아직 없다.
남은 것: 더블클릭은 두 번째 `Press` 로 기록하고 재생은 원시 Down 을 두 번 낸다(입력 관리자는 Down, 입력 맵과 ImGui 는 간격으로 판정).
풀리는 조건: 그 이벤트를 읽는 코드가 생기면 `VirtualInputScript` 에 `DoubleClick` 단계를 더한다. 합성 축(키 · D 패드)은 키 단계로 남는다.

### PreviewWindowsRemaining
제목: 미리보기 씬 틀(EditorPreviewPanel)을 쓰는 나머지 툴 창
옛번호: D3-7
왜 필요한가: 다중 월드 틀 위에 머티리얼 · 프리팹 · VFX · 캐릭터 · 페어드 애니메이션 창이 올랐다. 같은 모양(문서 패널 + 미리보기 씬 + 창 Undo)에 얹을 창이 더 남았다: 메시/모델(LOD 단계별 비교) · 애니메이션 클립 · 카메라 프리셋 · 래그돌 · 장르 도구. 창마다 독립이라 하나씩 계획 문서로 옮겨 착수한다.
착수 조건: 그 창의 편집 대상(`.mesh` 의 LOD 단계 확인, `meadow.cameras.xml` 프리셋 보기)이 디자이너의 일이 되어 지금 길(인스펙터 · 콘텐츠 브라우저 썸네일)로 모자랄 때.
남은 것: 창마다 `EditorPreviewPanel` 파생 + 자산 종류 등록(`EditorAssetType` 표의 제목) + 미리보기 씬 스폰 + 시나리오 하나. `VfxPreviewPanel`(보기 전용, 약 230 줄)이 가장 작은 본보기다.
함정: 같은 파일을 두 편집기가 쥐지 않는다(카탈로그 편집기가 쥔 종류는 보기 전용). 닫기 확인 · 창 Undo 는 틀이 맡는다.

### MeshModifierCpuPreviewCost
제목: 메시 모디파이어 CPU 미리보기의 비용 줄이기
옛번호: D3-10
왜 필요한가: GPU 프로그램 표에 없는 모디파이어가 스택에 있으면 미리보기가 매번 CPU 로 스택을 적용하고 새 메시(탄젠트 + 정점 풀 재업로드)를 만든다. 정점 갱신 API 는 렌더 스냅샷 수명 때문에 이 비용을 줄이지 못해 더하지 않았다([MeshModifierPreviewCost](MeshModifierPreviewCost.md) 2절).
착수 조건: 매니페스트 `mesh-modifier-preview-cost` 의 값에서 10 만 정점 끌기가 프레임을 16 ms 넘게 늘릴 때.
남은 것: `Editor.MeshModifiers.Apply` 가 크면 CPU 로 내려가는 모디파이어를 GPU 프로그램 표에 올린다(`MeshModifierGPUProgram`). `Editor.MeshModifiers.CpuMesh` 가 크면 미리보기 메시의 탄젠트 재계산을 건너뛰는 옵션(`Mesh::setVerticesWithTangents` 에 위치만 바뀐 탄젠트를 넘기는 길 포함)을 본다.
함정: 메시를 제자리에서 바꾸지 않는다 — 렌더 스냅샷이 옛 메시를 쥔다.

### BehaviorTreeLiveCommander
제목: 행동 트리 창에서 지휘관 AI 의 활성 노드 보기
옛번호: 없음
왜 필요한가: 행동 트리 창([DesignerDataToolsAudit](DesignerDataToolsAudit.md) D2)은 선택한 오브젝트의 `AIControllerComponent` 실행기만 읽는다. RTS 지휘관 AI(`RTSCommanderAI`)는 컴포넌트가 아니라 경기 상태(`SkirmishMatch`)가 쥐는 일반 클래스라 창이 닿지 못한다.
착수 조건: 컴포넌트에 안 붙은 행동 트리 실행기를 쓰는 게임이 둘 이상이 되어 트리를 고치며 활성 노드를 봐야 하는 일이 반복될 때.
남은 것: 실행기를 내놓는 작은 인터페이스(`IBehaviorTreeDebugSource`: 실행기 목록과 이름)를 GameFramework 에 두고 지휘관을 가진 컴포넌트(`SkirmishDirectorComponent`)가 구현해 창이 선택한 오브젝트에서 찾는다.
함정: 실행기 노드 번호는 깊이 우선 번호이므로 창에 연 파일과 실행기가 쥔 트리가 같은 파일일 때만 맞다 — 인터페이스가 파일 경로도 내놓아 다르면 표시하지 않는다.
