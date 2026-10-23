# 에디터 후보

> 상태: 후보 · 선행: 없음 · 영역: editor · 옛번호: B-4, D3-8, D3-9 · 진행: 해당없음

에디터 패널, 에디터 플러그인, 에디터 시나리오의 착수 전 후보 목록입니다. 착수하면 절을 계획 문서로 옮기고 여기서 지웁니다. 이미 계획 문서가 있는 일은 [EditorMultiWorld](EditorMultiWorld.md)(다중 월드 · 툴 창)와 [EditorUX](EditorUX.md)(UX 남은 결함), [EditorFeatureGaps](EditorFeatureGaps.md)(지형 칠하기 · UI 디자이너 · 치트 · 타입 핸들 인스펙터의 남은 것),
[DataKindRegistry](DataKindRegistry.md)(엔진 데이터 종류의 레지스트리 이전), [MeshModifierPreviewCost](MeshModifierPreviewCost.md)(메시 모디파이어 미리보기 비용), [ContentBrowserWatchAndScan](ContentBrowserWatchAndScan.md)(콘텐츠 브라우저 감시 지연 · 스캔 비용)에 있습니다.

## 확인 대기

### EditorRealRunChecks
제목: 실기동으로 아직 확인하지 못한 것
옛번호: B-4
왜 못 하는가: 리눅스와 외부 프로그램 · 실기 입력이 있어야 확인된다. 풀리는 조건은 항목마다 다르다.
남은 것: 리눅스 X11 · dlopen 분기(CI 리눅스 잡), 실제 RenderDoc 캡처(이 PC 에 RenderDoc 이 없다 — 설치되면), 단언 대화상자의 세 단추(모달이라 자동화가 누를 수 없다 — 실제 입력),
Tracy 0.14.1 뷰어 연결(주소 `localhost:8086` 을 직접 입력해 연결되는지 — 뷰어 실행), 배율 1.5 · 2 의 에디터(`gv_editorUiScale` 로 눈으로), 스크린샷 토스트의 "Show in Explorer" 단추(알림 매니저에 동작 단추가 없다 — 기능 추가가 먼저).
함정: RenderDoc 은 RHI 단계 맨 앞에서 로드하고, `ThirdParty/renderdoc` 에 원문 헤더를 두며 그 폴더의 `.clang-format` 은 `DisableFormat` 이다.

### EditorCaptureFollowUps
제목: 캡처로 확인하고 남은 것
옛번호: D3-9
왜 못 하는가: 둘 다 외부 조건이 있어야 한다.
남은 것:
- 녹화메뉴: 에디터 메뉴의 시나리오 녹화(`automation.toggleScenarioRecord`)는 자동화가 주입한 입력을 기록하지 않아(`0 steps`) 실제 입력으로만 확인된다(게임 실행의 `-scenario-record` 는 가상 입력도 적고 왕복 테스트가 있다). 풀리는 조건: 사람이 직접 입력.
- 빈상태창: 어빌리티 디버거 · 오디오 이벤트 · 리그 디버그 · 시퀀서 `RigSlot` 트랙 · UI 디자이너 앵커 프리셋은 빈 상태(안내 글)까지만 봤다. 이 기능들을 쓰는 데이터(리그 · 오디오 이벤트 라이브러리)가 저장소에 없다. 풀리는 조건: 그 데이터가 저장소에 들어올 때.

### ContentBrowserAsyncWaits
제목: 콘텐츠 브라우저의 백그라운드 작업을 고정 프레임으로 기다리는 남은 곳
옛번호: 없음
왜 필요한가: 참조 역색인은 2026-10-23 에 "마지막 변경을 따라왔다"(`ContentBrowserReferenceSearch::isCurrent`)를 준비로 보고 보이는 패널에서 시계를 잡게 고쳤다(`assetmanage` 간헐 실패). 같은 모양이 둘 남았다(폴더 목록의 고정 프레임 대기는 `contentthumbnails` 를 `WaitUntil` 로 바꿔 없앴다).
- 감시 지연: 에디터 밖의 쓰기(`<Scratch>` 복사)는 OS 파일 감시가 알려야 변경 번호가 오른다. 알림 전에는 낡은 색인이 맞다고 보인다(보통 ms, 스캔 하나보다 짧다). 실행기가 사본을 만든 뒤 감시가 그 변경을 본 것을 확인하는 울타리(감시 폴더에 표지 파일을 쓰고 그 사건을 기다리기)가 막는다.
- 스캔 비용: 전체 스캔이 Release 2 초, Debug 6~19 초다. 보이는 콘텐츠 브라우저에서 파일이 바뀔 때마다 그만큼 시계를 잡는다. 바뀐 파일만 다시 읽는 증분 갱신(언리얼 에셋 레지스트리)이면 줄어든다.
착수 조건: 감시 지연으로 색인 단언이 한 번이라도 지거나, 에디터 체인 시간이 시한에 닿을 때.
왜 필요한가: 참조 역색인은 2026-10-23 에 "마지막 변경을 따라왔다"(`ContentBrowserReferenceSearch::isCurrent`)를 준비로 보고 보이는 패널에서 시계를 잡게 고쳤다(`assetmanage` 간헐 실패). 같은 모양이 남았다.
- 폴더 목록: `contentthumbnails` 는 `content.open` 뒤 65 프레임에 `Editor.ThumbnailCacheCount` 를 단언한다. 목록은 워커 잡(`EditorFolderListingJob`)이라 워커가 밀리면(같은 때 도는 역색인 스캔) 고정 프레임이 모자랄 수 있다. 아직 진 적은 없다.
- 감시 지연 · 스캔 비용은 [ContentBrowserWatchAndScan](ContentBrowserWatchAndScan.md) 로 옮겼다.
착수 조건: 폴더 목록 단언이 한 번이라도 지거나, 에디터 체인 시간이 시한에 닿을 때.

## 기능

### ScenarioRecorderAndInspectorGaps
제목: 시나리오 녹화의 남은 한계
옛번호: D3-8
왜 못 하는가: 원시 DoubleClick 이벤트를 읽는 코드가 아직 없다.
남은 것: 더블클릭은 두 번째 `Press` 로 기록하고 재생은 원시 Down 을 두 번 낸다. 지금 원시 DoubleClick 이벤트를 읽는 쪽이 없다(입력 관리자는 Down, 입력 맵과 ImGui 는 간격으로 판정).
풀리는 조건: 그 이벤트를 읽는 코드가 생기면 `VirtualInputScript` 에 `DoubleClick` 단계를 더한다. 합성 축(키 · D 패드)은 키 단계로 남는다 — 재생의 `ActionAxis` 가 합성 축은 키를 누르기만 하므로 키 단계와 같다.

### PreviewWindowsRemaining
제목: 미리보기 씬 틀(EditorPreviewPanel)을 쓰는 나머지 툴 창
옛번호: D3-7
왜 필요한가: 다중 월드 틀은 단계 1 ~ 5 로 끝났고 머티리얼 · 프리팹 · VFX · 캐릭터 · 페어드 애니메이션 창이 그 위에 올랐다. 틀이 정한 모양(문서 패널 + 미리보기 씬 + 창 Undo)에 얹을 수 있는 창이 더 남았다: 메시/모델(LOD 단계별 비교) · 애니메이션 클립 · 카메라 프리셋 · 래그돌 · 장르 도구. 창마다 독립이라 하나씩 계획 문서로 옮겨 착수한다.
착수 조건: 그 창의 편집 대상(예: `.mesh` 의 LOD 단계 확인, `meadow.cameras.xml` 프리셋 보기)이 디자이너의 일이 되어 지금 길(인스펙터 · 콘텐츠 브라우저 썸네일)로 모자랄 때.
남은 것: 창마다 `EditorPreviewPanel` 파생 + 자산 종류 등록(`EditorAssetType` 표의 제목) + 미리보기 씬 스폰 + 시나리오 하나. `VfxPreviewPanel`(보기 전용, 약 230 줄)이 가장 작은 본보기다.
함정: 같은 파일을 두 편집기가 쥐지 않는다(카탈로그 편집기가 쥔 종류는 보기 전용). 닫기 확인 · 창 Undo 는 틀이 맡으므로 파생이 따로 만들지 않는다.
