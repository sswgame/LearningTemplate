# 에디터 후보

> 상태: 후보 · 선행: 없음 · 영역: editor · 옛번호: B-3, B-4, D3-1, D3-1.2, D3-3, D3-4, D3-5, D3-8, D3-9, D3-10, D3-13 · 진행: 해당없음

에디터 패널, 에디터 플러그인, 에디터 시나리오의 착수 전 후보 목록입니다. 착수하면 절을 계획 문서로 옮기고 여기서 지웁니다. 이미 계획 문서가 있는 일은 [EditorMultiWorld](EditorMultiWorld.md)(다중 월드 · 툴 창)와 [EditorUX](EditorUX.md)(UX 남은 결함)에 있습니다.

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
- 지형브러시원: 시각화 단추는 켜지지만 `envshowcase` 씬의 에디터 카메라에서 원이 안 보였다. 마우스 레이가 지형에 안 닿는지 카메라가 지형 밑인지 가르지 못했다. 카메라를 지형 위로 옮겨 다시 볼 것.
- 녹화메뉴: 에디터 메뉴의 시나리오 녹화(`automation.toggleScenarioRecord`)는 자동화가 주입한 입력을 기록하지 않아(`0 steps`) 실제 입력으로만 확인된다(게임 실행의 `-scenario-record` 는 가상 입력도 적고 왕복 테스트가 있다).
- 빈상태창: 어빌리티 디버거 · 오디오 이벤트 · 리그 디버그 · 시퀀서 `RigSlot` 트랙 · UI 디자이너 앵커 프리셋은 빈 상태(안내 글)까지만 봤다. 이 기능들을 쓰는 데이터(리그 · 오디오 이벤트 라이브러리)가 저장소에 없다. NPC 일정 디버그 창은 HarvestValley 마을 사람(`FarmVillageDirectorComponent`)으로 실물이 생겼지만 에디터에서 열어 보지 않았다.
- 포토모드: 포토 모드는 게임 HUD 를 그대로 둔다. 의도인지 확인.
- 팝업잘림: 뷰포트 옵션 팝업은 패널이 화면 오른쪽 끝이면 메인 창 밖으로 나가 클라이언트 캡처에 안 잡힌다.

### PanelIDLiterals
제목: 패널을 id 글자로 여는 남은 호출
옛번호: 없음
왜 필요한가: 패널 열기 요청은 패널 형식(`requestOpenPanel<T>()`)으로 바꿨지만(2026-10-20) 같은 모양이 남았다. `EditorPlayCommands.cpp` 의 `focusPanel( "scene_view" )` · `"game_view"`(Common 이 Panels 를 모른다),
GameFramework 에디터 플러그인의 `setPanelOpen( "admin.tool" )` · `"gimmick_graph"` · `"charactereditor.main"`, `EditorSelfTestDevTools.cpp` 의 `ImGui::SetWindowFocus( "Scene" )`(창 제목 글). 오타는 실행 중 false 로만 드러난다.
남은 것: 같은 모듈이면 `setPanelOpen<T>()` 로, 층이 막히면 id 상수를 아래 층(패널 호스트 창구)으로 내린다.

### DesignerDataToolsAudit
제목: 디자이너용 데이터 도구 점검
옛번호: D3-13
왜 필요한가: 스크립팅 층을 두지 않으므로([결정 기록](../09_Decisions.md) 2절) 코드 없이 로직 · 재질을 짜는 도구가 중요하다.
착수 조건 / 남은 것: 머티리얼 그래프, 상태 머신 · 행동 트리 편집기, 파티클 에디터(`D2-11` P2)가 있는지 현재 상태를 코드로 확인하고 빠진 것을 항목으로 쪼갠다. 확인 전에는 있다 없다를 단정하지 않는다.

## 기능

### DataKindRegistry
제목: 엔진 데이터 종류의 레지스트리 이전
옛번호: D3-1.2
왜 필요한가: 에디터 로드맵 ⑦(카탈로그 편집기 뒤). 엔진 데이터 종류(장면 · 머티리얼 · 파이프라인 …)가 아직 `ResourceDataSchemaTest` 의 표에 있고, 카탈로그 밖(Engine 쪽) 열거자 이름 표는 `EnumNameTable` 로 모으지 않았다.
착수 조건 / 남은 것: `EnumNameTable` 은 `Core/String/EnumNameTable.h` 로 옮겼다. 다음은 로더가 `loadCatalog` 인 줄부터 레지스트리로 옮기는 것이다. 실제 걸림돌 셋과 단계적 대안은 [결정 기록](../09_Decisions.md) 5-11 "엔진 데이터 종류의 레지스트리 이전".
함정: 층 규칙은 걸림돌이 아니다(레지스트리는 티어 5 라 위 티어가 채울 수 있다). 영역 패널의 자리 — 엔진 영역은 EditorModule, 기반 GameFramework 영역은 `GF_EditorPlugIn_Base`(`Source/GameFramework/PlugIn/Editor/Base/`), 키트 영역은 `GF_EditorPlugIn_<이름>` 확장 모듈. 에디터에 이미 있는 것(전역 변수 표, 커맨드 팔레트, 핫 리로드, Undo, Play 와 Step, InputReplay, 기즈모, RenderTargetPanel, 프로파일러와 Tracy)은 다시 만들지 않는다.

### TerrainBrushTools
제목: 지형 칠하기 도구의 남은 기능
옛번호: D3-1
왜 필요한가: `TerrainBrushTool`(씬 뷰 Terrain 토글 — 올리기 · 내리기 · 평탄화 · 레이어 칠하기, 드래그 하나가 되돌리기 하나, 저장 단추)에 높이장 크기 바꾸기 · 구멍 칠하기 · 스무드 브러시가 없다.
착수 조건 / 남은 것: 쓰기 쪽 남은 것은 `D2-5` (7). 브러시 원이 보이지 않는 문제는 [EditorCaptureFollowUps](#editorcapturefollowups) 의 지형브러시원.

### UIPreviewDesigner
제목: UI 미리보기 패널 디자이너의 다음 단계
옛번호: D3-3
왜 필요한가: `Editor/Panels/UIPreview/UIPreviewPanel` 의 디자이너는 팔레트 끌어 놓기 · 지우기 · 부모 패널 종류별 슬롯 필드 편집 · 캔버스 앵커 프리셋 열둘 · 상자 순서 / 격자 칸 끌기 손잡이 · 탐침 `Editor.UIPreviewSlotEditCount` 까지 있다.
착수 조건 / 남은 것: 앵커 프리셋을 고른 직후 부모 여백이 있을 때 위젯이 약간 밀리는지 확인. 손잡이의 가정 보완 — 상자 순서 계산이 "가운데 좌표가 커지면 자식 번호도 커진다" 고 가정해 RTL · 음수 간격에서 거꾸로 나온다. 격자 줄은 자식 사각형으로 추정해 걸친 칸(span)이 섞이고 빈 행 · 열로는 끌어 놓을 수 없다.

### GameCheatCommands
제목: 콘솔 · 치트의 남은 것
옛번호: D3-4
왜 필요한가: 게임 · 키트의 치트 명령(무적 · 아이템 주기 · 돈 …)을 각 게임 · 킷에 `SW_DEV_COMMAND` 로 달아야 한다. 지금 게임 · 키트 쪽 명령은 `GameNetSession` 하나뿐이다(레지스트리 · 콘솔 · 엔진 명령은 `Source/Engine/` "개발 콘솔"). 어빌리티 · 판 상태 · Shooter3D 의 `cheat.*` 는 있다(`AbilityDevCommands` · `SessionStateDevCommands` · `ShooterDevCommands`).
착수 조건 / 남은 것:
- 리눅스 오버레이(`X11DevConsoleWindow`)를 실기로 띄워 보지 않았다. X11 그리기는 `XDrawString`(Latin-1)이라 한글이 깨진다.
- Shooter3D 는 어빌리티 시스템이 아니라 `Vitality` 라 `cheat.god` 이 닿지 않는다(`Vitality::setInvulnerable` 이 private 멤버 `_vitality` 뒤). `Vitality` 를 직접 쓰는 다른 키트의 치트도 아직 없다.
함정: 무적(`cheat.god`)은 어빌리티 시스템의 태그(`State.Invulnerable`, 치트는 하위 `.Cheat`)로 플레이어 조종 폰에만 건다. `cheat.heal` · `cheat.attribute` 는 적을 포함한 모든 어빌리티 시스템에 적용된다(플레이어만 가리는 길이 필요하면 태그나 팀을 본다).

### ScenarioRecorderAndInspectorGaps
제목: 시나리오 녹화 · 인스펙터 · 모듈 창의 남은 한계
옛번호: D3-8
왜 필요한가: 에디터 보강 묶음에서 들어간 기능마다 알려진 빈틈이 남았다.
착수 조건 / 남은 것:
- 시나리오녹화: 마우스 휠은 `Wheel` 단계로, 더블클릭은 두 번째 `Press` 로 기록한다. 조합 중 글자 · 포커스 · 포인터 출입 · 패드 연결은 시나리오 의미가 없어 건너뛴 수만 센다. 재생은 원시 Down 을 두 번 내므로 더블클릭 이벤트 자체를 보는 UI 코드는 재현되지 않는다(`DoubleClick` 단계가 필요하면 `VirtualInputScript` 를 고친다). 액션의 단추는 `ActionPress` · `ActionRelease` 로 쓰지만 축(스틱 · 합성 축)은 아직 `GamepadAxis` · 키 단계라 `ActionAxis` 로 쓰지 않는다.
- 인스펙터: 즐겨찾기(`InspectorFavorites`, `Saved/Editor/InspectorFavorites.txt`)의 키가 가장 파생된 타입 이름이라 기반 클래스에 즐겨찾기한 프로퍼티는 파생 타입마다 따로 저장된다. 디버그 모드(유니티 Debug 인스펙터 — 숨은 필드는 리플렉션 범위 밖)는 하지 않았다. "바뀐 것만 보기" 는 다중 선택에서 주 선택 오브젝트의 값만 기준으로 거른다.
- 모듈창: 모듈 설치 · 제거(파일 복사 · 삭제) · 외부 패키지 받기는 위험이 커서 뺐다. 확인 창이 패널 수준 ID 로 열려 셀프테스트 시나리오가 창 위치에 기댄다면 영향이 있을 수 있다.
함정: UI 디자이너 손잡이의 가정은 [UIPreviewDesigner](#uipreviewdesigner) 에 있다.

### TypedHandleInspectorGaps
제목: 타입 핸들 인스펙터 선택기(TypedComponents 3.8)의 남은 빈틈
옛번호: 없음
왜 필요한가: 선택기는 `PROPERTY` 의 타입 핸들만 막습니다. 같은 "타입 없이 정수나 글로 핸들을 넣는" 길이 아래에 남았습니다.
착수 조건 / 남은 것:
- `set<TComponentHandle<T>>`: `operator<` 와 `std::hash` 가 없어 컴파일되지 않는데 리플렉션 파서는 이 모양을 통과시킵니다. 파서에서 막거나 `TComponentHandle` 에 순서 · 해시를 둡니다(막는 쪽이 형식으로 닫는 길).
- `FUNCTION` 인자: CallInEditor 에서 타입 핸들 인자는 아직 정수 입력입니다(`drawMethodArgFor`).
- Reset to Default 가 기본 인스턴스의 핸들 글을 어떻게 적용하는지 확인하지 못했습니다.
- `FloatCurvePropertyDrawer.cpp` 에도 `PropertyInfo` 손 복사가 하나 남았습니다(커브 값이라 핸들과 무관). 엔진 쪽 `PropertyInfo` 에 `makeElementProperty` 를 두면 손 복사 자체가 사라집니다.
- 화면 확인: 콤보 · `(none)` · 되돌리기 · Paste 비활성(`gpu-verifier`).

### MeshModifierStack
제목: 메시 모디파이어 스택 플러그인의 남은 것
옛번호: D3-10
왜 필요한가: `GF_EditorPlugIn_MeshModifiers` 는 MegaFiers 류 변형 모음의 기능 이름 목록을 기준으로 56 id 를 등록했다. 구조 · 추가 방법 · 알려진 근사는 [모듈 README](../../Source/GameFramework/PlugIn/Editor/MeshModifiers/README.md).
착수 조건 / 남은 것:
- 셰이딩 미리보기: 매개변수가 바뀔 때마다 CPU 스택이 한 번 돈다(입력 상자를 GPU 가 못 세서). CPU 로 내려간 스택은 바뀔 때마다 정점 버퍼를 새로 만든다. 둘 다 큰 메시에서 끌기 중 비용을 재지 않았다. 시간만 바뀌는 애니메이션도 미리보기 시간 칸을 움직일 때만 갱신된다. 구조는 모듈 README "셰이딩 미리보기".
- 텍스처표본기: 확장 모듈이 쓸 수 있는 엔진의 공개 이미지 디코드 함수가 없어 중복 디코더(`stb_image` 를 표본기 .cpp 에 정적으로 넣음)를 품는다. 엔진에 공개 디코드를 내보내면 합칠 수 있다. float4 텍셀이라 4096² 한 장이 256 MB 이고 메모리 상한이 없다. 파일 변경 감지는 1 초 지연이다.
- 패널캡처: 패널 · 기즈모는 시나리오의 이름표 클릭과 탐침으로만 확인했다. `<CaptureEditorUI>` 로 찍어 볼 수 있으나 아직 찍지 않았다.
- 기즈모비용: 기즈모를 끄는 매 프레임 앞 모디파이어를 다시 걸어 비용이 든다.
함정: clang-format 20.1.8 로 줄 길이를 맞출 것(이 PC 는 21.1.1).
