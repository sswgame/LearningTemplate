# 에디터 후보

> 상태: 후보 · 선행: 없음 · 영역: editor · 옛번호: B-3, B-4, D3-1.2, D3-5, D3-8, D3-9, D3-10 · 진행: 해당없음

에디터 패널, 에디터 플러그인, 에디터 시나리오의 착수 전 후보 목록입니다. 착수하면 절을 계획 문서로 옮기고 여기서 지웁니다. 이미 계획 문서가 있는 일은 [EditorMultiWorld](EditorMultiWorld.md)(다중 월드 · 툴 창)와 [EditorUX](EditorUX.md)(UX 남은 결함), [EditorFeatureGaps](EditorFeatureGaps.md)(지형 칠하기 · UI 디자이너 · 치트 · 타입 핸들 인스펙터의 남은 것)에 있습니다.

## 확인 대기

### EditorRestructureVerification
제목: 에디터 구조 개편 뒤 돌리지 않은 검증
옛번호: B-3
왜 필요한가: 층 게이트 · 의존 역전 · Panels 하위 폴더로 구조를 바꾼 3 · 4 단계 뒤에 아래를 돌리지 않았다. 결정은 [결정 기록](../09_Decisions.md) 5-11.
남은 것: 네 백엔드 에디터 시나리오(`AppTest --test_filter=*Editor*`), Shipping 빌드와 `-L hostgpu`, 게임 전부를 짓는 `Ninja-Debug` 빌드(ThemePark 밖의 에디터 확장 모듈). 배치 끝 검증에서 본다.

### EditorRealRunChecks
제목: 실기동으로 아직 확인하지 못한 것
옛번호: B-4
왜 필요한가: 에디터 작은 단위(E5 · G2 · G3 · H1)를 넣었지만 이 PC 에서 확인하지 못한 것이 남았다.
남은 것: 리눅스 X11 · dlopen 분기(CI), 실제 RenderDoc 캡처(이 PC 에 RenderDoc 이 없다), 단언 대화상자의 세 단추(모달), Tracy 0.14.1 뷰어 연결(주소 `localhost:8086` 을 직접 입력해 연결되는지),
배율 1.5 · 2 의 에디터, Dialogue Graph 첫 열기, 스크린샷 토스트의 "Show in Explorer" 단추(알림 매니저에 동작 단추가 없다), `screenshotbutton` 시나리오가 `Bin/Saved/Screenshots` 에 남기는 PNG, Engine README 개발 명령 줄의 경로.
함정: RenderDoc 은 RHI 단계 맨 앞에서 로드하고, `ThirdParty/renderdoc` 에 원문 헤더를 두며 그 폴더의 `.clang-format` 은 `DisableFormat` 이다.

### GraphSaveScenarioRun
제목: 그래프 패널 저장 시나리오를 실제로 돌려 확인하기
옛번호: D3-5
왜 필요한가: 임시 에셋으로 저장하는 길(`asset.open` · `asset.removeTemp` · 탐침 `Editor.TempAssetExists`, `Saved/Automation/` 밖과 리소스 트리는 거절)과 시나리오 둘(`dialoguegraphsave` · `animgraphsave`)은 있으나 실행하지 않았다.
남은 것: 연 직후 `Editor.DirtyDocumentCount` 가 0, `panel.focus` 뒤 `editor asset.save` 가 패널 문서를 저장, 스크립트가 보내는 `editor asset.save` 가 패널 포커스를 빼앗지 않음,
툴바 노드 추가 단추를 클릭할 수 있음(툴바 폭이 좁으면 잘린다), 대화 그래프 패널이 함께 열렸을 때 `Editor.GraphNodeCount` 가 읽는 패널.

### EditorCaptureFollowUps
제목: 캡처로 확인하고 남은 것
옛번호: D3-9
왜 필요한가: 시나리오 + `CaptureWindow` 로 DX12 에서 눈으로 확인한 에디터 보강 묶음에서 아래가 남았다(캡처: `D:\wisual\captures\`).
남은 것:
- 녹화메뉴: 에디터 메뉴의 시나리오 녹화(`automation.toggleScenarioRecord`)는 자동화가 주입한 입력을 기록하지 않아(`0 steps`) 실제 입력으로만 확인된다(게임 실행의 `-scenario-record` 는 가상 입력도 적고 왕복 테스트가 있다).
- 빈상태창: 어빌리티 디버거 · 오디오 이벤트 · 리그 디버그 · 시퀀서 `RigSlot` 트랙 · UI 디자이너 앵커 프리셋은 빈 상태(안내 글)까지만 봤다. 이 기능들을 쓰는 데이터(리그 · 오디오 이벤트 라이브러리)가 저장소에 없다. NPC 일정 디버그 창은 HarvestValley 마을 사람(`FarmVillageDirectorComponent`)으로 실물이 생겼지만 에디터에서 열어 보지 않았다.
- 포토모드: 포토 모드는 게임 HUD 를 그대로 둔다. 의도인지 확인.
- 팝업잘림: 뷰포트 옵션 팝업은 패널이 화면 오른쪽 끝이면 메인 창 밖으로 나가 클라이언트 캡처에 안 잡힌다.

### ContentBrowserAsyncWaits
제목: 콘텐츠 브라우저의 백그라운드 작업을 고정 프레임으로 기다리는 남은 곳
옛번호: 없음
왜 필요한가: 참조 역색인은 2026-10-23 에 "마지막 변경을 따라왔다"(`ContentBrowserReferenceSearch::isCurrent`)를 준비로 보고 보이는 패널에서 시계를 잡게 고쳤다(`assetmanage` 간헐 실패). 같은 모양이 둘 남았다.
- 폴더 목록: `contentthumbnails` 는 `content.open` 뒤 65 프레임에 `Editor.ThumbnailCacheCount` 를 단언한다. 목록은 워커 잡(`EditorFolderListingJob`)이라 워커가 밀리면(같은 때 도는 역색인 스캔) 고정 프레임이 모자랄 수 있다. 아직 진 적은 없다.
- 감시 지연: 에디터 밖의 쓰기(`<Scratch>` 복사)는 OS 파일 감시가 알려야 변경 번호가 오른다. 알림 전에는 낡은 색인이 맞다고 보인다(보통 ms, 스캔 하나보다 짧다). 실행기가 사본을 만든 뒤 감시가 그 변경을 본 것을 확인하는 울타리(감시 폴더에 표지 파일을 쓰고 그 사건을 기다리기)가 막는다.
- 스캔 비용: 전체 스캔이 Release 2 초, Debug 6~19 초다. 보이는 콘텐츠 브라우저에서 파일이 바뀔 때마다 그만큼 시계를 잡는다. 바뀐 파일만 다시 읽는 증분 갱신(언리얼 에셋 레지스트리)이면 줄어든다.
착수 조건: 폴더 목록 단언이 한 번이라도 지거나, 에디터 체인 시간이 시한에 닿을 때.

## 기능

### DataKindRegistry
제목: 엔진 데이터 종류의 레지스트리 이전
옛번호: D3-1.2
왜 필요한가: 에디터 로드맵 ⑦(카탈로그 편집기 뒤). 엔진 데이터 종류(장면 · 머티리얼 · 파이프라인 …)가 아직 `ResourceDataSchemaTest` 의 표에 있고, 카탈로그 밖(Engine 쪽) 열거자 이름 표는 `EnumNameTable` 로 모으지 않았다.
착수 조건 / 남은 것: `EnumNameTable` 은 `Core/String/EnumNameTable.h` 로 옮겼다. 다음은 로더가 `loadCatalog` 인 줄부터 레지스트리로 옮기는 것이다. 실제 걸림돌 셋과 단계적 대안은 [결정 기록](../09_Decisions.md) 5-11 "엔진 데이터 종류의 레지스트리 이전".
함정: 층 규칙은 걸림돌이 아니다(레지스트리는 티어 5 라 위 티어가 채울 수 있다). 영역 패널의 자리 — 엔진 영역은 EditorModule, 기반 GameFramework 영역은 `GF_EditorPlugIn_Base`(`Source/GameFramework/PlugIn/Editor/Base/`), 키트 영역은 `GF_EditorPlugIn_<이름>` 확장 모듈. 에디터에 이미 있는 것(전역 변수 표, 커맨드 팔레트, 핫 리로드, Undo, Play 와 Step, InputReplay, 기즈모, RenderTargetPanel, 프로파일러와 Tracy)은 다시 만들지 않는다.

### ScenarioRecorderAndInspectorGaps
제목: 시나리오 녹화 · 인스펙터 · 모듈 창의 남은 한계
옛번호: D3-8
왜 필요한가: 에디터 보강 묶음에서 들어간 기능마다 알려진 빈틈이 남았다. 녹화의 스틱 `ActionAxis`, 즐겨찾기 키의 선언 타입, 다중 선택의 "바뀐 것만 보기" 는 들어갔다(`ScenarioRecorder` · `InspectorFavorites` 헤더).
착수 조건 / 남은 것:
- 시나리오녹화: 더블클릭은 두 번째 `Press` 로 기록하고 재생은 원시 Down 을 두 번 낸다. 지금 원시 DoubleClick 이벤트를 읽는 쪽이 없다(입력 관리자는 Down, 입력 맵과 ImGui 는 간격으로 판정). 그 이벤트를 읽는 코드가 생기면 `VirtualInputScript` 에 `DoubleClick` 단계를 더한다. 합성 축(키 · D 패드)은 키 단계로 남는다 — 재생의 `ActionAxis` 가 합성 축은 키를 누르기만 하므로 키 단계와 같다.
- 인스펙터: 디버그 모드(유니티 Debug 인스펙터 — 숨은 필드는 리플렉션 범위 밖)는 하지 않았다. 즐겨찾기 키를 선언 타입으로 바꿔 예전에 파생 타입 이름으로 저장한 기반 프로퍼티 키는 더 맞지 않는다(사용자 파일, 다시 즐겨찾기한다).
- 모듈창: 모듈 설치 · 제거(파일 복사 · 삭제) · 외부 패키지 받기는 위험이 커서 뺐다. 확인 창이 패널 수준 ID 로 열려 셀프테스트 시나리오가 창 위치에 기댄다면 영향이 있을 수 있다.

### MeshModifierStack
제목: 메시 모디파이어 스택 플러그인의 남은 것
옛번호: D3-10
왜 필요한가: `GF_EditorPlugIn_MeshModifiers` 는 MegaFiers 류 변형 모음의 기능 이름 목록을 기준으로 56 id 를 등록했다. 구조 · 추가 방법 · 알려진 근사는 [모듈 README](../../Source/GameFramework/PlugIn/Editor/MeshModifiers/README.md).
착수 조건 / 남은 것:
- 셰이딩 미리보기 비용: 매개변수가 바뀌면 고른 모디파이어부터 CPU 로 한 번 돈다(앞부분은 캐시, 입력 상자를 GPU 가 못 세서). CPU 로 내려간 스택은 바뀔 때마다 정점 버퍼를 새로 만든다(렌더 스레드 사본과 겹치지 않게 메시째 바꾼다).
  큰 메시(10 만 정점 이상)에서 끌기 · 시간 재생 중 프레임 시간을 재고, 크면 엔진 `Mesh` 에 정점 갱신 API(렌더 스레드와 이중 버퍼)를 더한다. 구조는 모듈 README "셰이딩 미리보기".
- 텍스처표본기: 8 비트 원본도 float4 로 보관한다(4 배). 캐시 상한(512 MB, 1 초 넘게 안 쓴 것부터 내림)은 들어갔다. 파일 변경 감지는 1 초 지연이다.
- 패널캡처: 시나리오 `editor.meshmodifiers` 가 `meshmodifiers/panel.png` · `gizmo.png` · `nested.png` 를 찍는다. 아직 돌려 눈으로 보지 않았다.
함정: clang-format 20.1.8 로 줄 길이를 맞출 것(이 PC 는 21.1.1).
