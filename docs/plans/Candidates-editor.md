# 에디터 후보

> 상태: 후보 · 선행: 없음 · 영역: editor · 옛번호: B-4, D3-7, D3-9, D3-10 · 진행: 해당없음

에디터 패널, 에디터 플러그인, 에디터 시나리오의 착수 전 후보 목록입니다. 착수하면 절을 계획 문서로 옮기고 여기서 지웁니다. 이미 계획 문서가 있는 일은 [EditorUX](EditorUX.md)(UX 남은 결함)에 있습니다.

## 확인 대기

### EditorRealRunChecks
제목: 실기동으로 아직 확인하지 못한 것
옛번호: B-4
왜 못 하는가: 리눅스와 외부 프로그램 · 실기 입력이 있어야 확인된다. 풀리는 조건은 항목마다 다르다.
남은 것: 실제 RenderDoc 캡처(이 PC 에 RenderDoc 이 없다), 단언 대화상자의 세 단추(ImGui 가 아니라 OS `MessageBoxW` 이고 자동 실행에는 걸리지 않아 시나리오가 띄우지도 누르지도 못한다), Tracy 0.14.1 뷰어 연결(`localhost:8086` 직접 입력, 외부 앱).
시나리오로 만든 것: 스크린샷 토스트의 "Show in Explorer" 단추는 `editor.toastreveal`(실행은 통합이 처음 돈다)이 맡는다.
함정: RenderDoc 은 RHI 단계 맨 앞에서 로드하고 `ThirdParty/renderdoc` 에 원문 헤더를 두며 그 폴더의 `.clang-format` 은 `DisableFormat` 이다.

### EditorCaptureFollowUps
제목: 캡처로 확인하고 남은 것
옛번호: D3-9
왜 못 하는가: 둘 다 외부 조건이 있어야 한다.
남은 것:
- 녹화메뉴: 에디터 메뉴의 시나리오 녹화(`automation.toggleScenarioRecord`)는 자동화가 주입한 입력을 기록하지 않아(`0 steps`) 실제 입력으로만 확인된다(게임 실행의 `-scenario-record` 는 가상 입력도 적고 왕복 테스트가 있다). 풀리는 조건: 사람이 직접 입력.
- 빈상태창: 어빌리티 디버거 · 오디오 이벤트 · 리그 디버그 · 시퀀서 `RigSlot` 트랙 · UI 디자이너 앵커 프리셋은 빈 상태(안내 글)까지만 봤다. 쓰는 데이터(리그 · 오디오 이벤트 라이브러리)가 저장소에 없다. 풀리는 조건: 그 데이터가 들어올 때.

### PopupWindowCapture
제목: 팝업 창을 캡처 시나리오가 찍도록 캡처 영역을 플랫폼 창 합집합으로 넓히기
옛번호: 없음
왜 필요한가: 뷰포트 옵션 팝업은 `ViewportsEnable` 때문에 OS 창이 되는데 `CaptureWindow`(`AutomationWindowSteps.cpp`)는 주 창 클라이언트 사각형만 BitBlt 해서 팝업이 잘려 보인다. 팝업은 의도된 동작이라(Decisions 5-11) 캡처 도구의 빈틈이다.
착수 조건: 팝업을 캡처로 확인하는 시나리오가 생길 때.

### MeshModifierCaptureReview
제목: 메시 모디파이어 패널 캡처 세 장을 눈으로 확인
옛번호: D3-10
왜 필요한가: `GF_EditorPlugIn_MeshModifiers` 는 56 id 를 등록했고 시나리오 `editor.meshmodifiers` 가 `meshmodifiers/panel.png` · `gizmo.png` · `nested.png` 를 찍는다. 시나리오 통과(`app-scenarios-release`)는 캡처가 찍힌다는 것까지만 증명한다.
남은 것: — 사람이 PNG 세 장(패널 · 기즈모 · 중첩)을 본다.
못 하는 이유: 기준 그림이 없어 눈이 판정한다. 풀리는 조건: 기준 그림을 정해 `ExpectImage` 로 옮기면 항목과 절을 지운다.

## 기능

### PreviewWindowsRemaining
제목: 미리보기 씬 틀(EditorPreviewPanel)을 쓰는 나머지 툴 창
옛번호: D3-7
왜 필요한가: 다중 월드 틀 위에 머티리얼 · 프리팹 · VFX · 캐릭터 · 페어드 애니메이션 · 메시(`MeshPreviewPanel`, LOD 단계 고르기) · 애니메이션 클립(`AnimationClipPreviewPanel`) 창이 올랐다. 같은 모양(문서 패널 + 미리보기 씬 + 창 Undo)에 얹을 창이 더 남았다: 카메라 프리셋 · 래그돌 · 장르 도구. 창마다 독립이라 하나씩 계획 문서로 옮겨 착수한다.
착수 조건: 그 창의 편집 대상(`meadow.cameras.xml` 프리셋 보기)이 디자이너의 일이 되어 지금 길(인스펙터 · 콘텐츠 브라우저 썸네일)로 모자랄 때.
남은 것: 창마다 `EditorPreviewPanel` 파생 + 자산 종류 등록(`EditorAssetType` 표의 제목) + 미리보기 씬 스폰 + 시나리오 하나. `VfxPreviewPanel`(보기 전용, 약 230 줄)이 가장 작은 본보기다. 카메라 프리셋 창은 `EditorPreviewViewport` 가 오빗 카메라뿐이라 Fixed · Follow · Dolly 모드를 보이려면 뷰포트에 자유 카메라가 먼저 필요하다. 래그돌 창은 미리보기 정책의 물리(`_bPhysics`)와 `RagdollComponent` 의 스켈레톤 바인딩을 쓴다. 클립 창의 시나리오는 Empty 게임에 스켈레탈 애니메이터 프리팹이 없어 못 썼다(`testhead` 클립만 있다).
함정: 같은 파일을 두 편집기가 쥐지 않는다(카탈로그 편집기가 쥔 종류는 보기 전용). 닫기 확인 · 창 Undo 는 틀이 맡는다. 메시 · 클립 창은 만든 뒤 컴파일 · 실행을 해 보지 못했다(`meshpreview` 시나리오가 처음 돌려 볼 길이다).

### PreviewVertexPoolSharing
제목: 미리보기 렌더러의 정점 풀 · 머티리얼 버퍼 공유
옛번호: D3-7
왜 필요한가: 미리보기 씬마다 `FrameRenderer` 인스턴스 하나를 두고 PSO 캐시만 주 렌더러와 공유해, 정점 풀 · 머티리얼 그룹 버퍼가 렌더러마다 따로다. 풀을 나누면 bindless 인덱스 공간이 갈라지므로 메모리나 다시 열기 시간이 문제일 때만 값을 따져 본다.
착수 조건: 미리보기 창 여럿을 띄운 GPU 메모리나 창을 다시 여는 첫 프레임 시간이 눈에 띌 때.
남은 것: 창 여럿을 띄운 메모리와 창을 다시 여는 첫 프레임 시간을 공유 전 커밋과 견주어 정점 풀까지 나눌 값이 있는지 정한다(`previewreopen` 시나리오의 탐침 `Editor.ScenePreviewRenderers` 와 GPU 메모리). 한 명령으로 도는 케이스가 아직 없다.

### MeshModifierCpuPreviewCost
제목: 메시 모디파이어 CPU 미리보기의 비용 줄이기
옛번호: D3-10
왜 필요한가: GPU 프로그램 표에 없는 모디파이어가 스택에 있으면 미리보기가 매번 CPU 로 스택을 적용하고 새 메시(탄젠트 + 정점 풀 재업로드)를 만든다. 정점 갱신 API 는 렌더 스냅샷 수명 때문에 이 비용을 줄이지 못해 더하지 않았다.
착수 조건: 의 값(사람이 큰 메시를 열어 매개변수를 끌며 Release 프로파일러의 `Editor.MeshModifiers.*` 구간을 읽는다 — 그 입력 흐름을 한 명령으로 도는 케이스가 없다)에서 10 만 정점 끌기가 프레임을 16 ms 넘게 늘릴 때.
남은 것: `Editor.MeshModifiers.Apply` 가 크면 CPU 로 내려가는 모디파이어를 GPU 프로그램 표에 올린다(`MeshModifierGPUProgram`). `Editor.MeshModifiers.CpuMesh` 가 크면 미리보기 메시의 탄젠트 재계산을 건너뛰는 옵션(`Mesh::setVerticesWithTangents` 에 위치만 바뀐 탄젠트를 넘기는 길 포함)을 본다.
함정: 메시를 제자리에서 바꾸지 않는다 — 렌더 스냅샷이 옛 메시를 쥔다.
