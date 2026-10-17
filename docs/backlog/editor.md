# 백로그: 에디터

에디터 패널, 에디터 플러그인, 에디터 시나리오와 UX 의 남은 일입니다. 상태 태그의 뜻, 일을 고르는 순서, 새 항목을 쓰는 법은 [백로그 안내](README.md)에 있습니다.

- `B-3` [검증대기] **에디터 구조 개편(층 게이트 · 의존 역전 · Panels 하위 폴더) 뒤에 돌리지 않은 검증.** 3 · 4 단계 뒤 네 백엔드 에디터 시나리오(`AppTest --test_filter=*Editor*`),
  Shipping 빌드와 `-L hostgpu`, 게임 전부를 짓는 `Ninja-Debug` 빌드(ThemePark 밖의 에디터 확장 모듈)를 배치 끝 검증에서 본다. 결정은 [결정 기록](../09_Decisions.md) 5-11.

- `B-4` [검증대기] **에디터 작은 단위(E5 · G2 · G3 · H1)와 실기동으로 아직 확인하지 못한 것.** 리눅스 X11 · dlopen 분기(CI), 실제 RenderDoc 캡처(이 PC 에 RenderDoc 이 없다),
  단언 대화상자의 세 단추(모달), Tracy 0.14.1 뷰어 연결(주소 `localhost:8086` 을 직접 입력해 연결되는지), 배율 1.5 · 2 의 에디터, Dialogue Graph 첫 열기,
  스크린샷 토스트의 "Show in Explorer" 단추(알림 매니저에 동작 단추가 없다), `screenshotbutton` 시나리오가 `Bin/Saved/Screenshots` 에 남기는 PNG, Engine README 개발 명령 줄의 경로.
  RenderDoc 은 RHI 단계 맨 앞에서 로드하고, `ThirdParty/renderdoc` 에 원문 헤더를 두며 그 폴더의 `.clang-format` 은 `DisableFormat` 이다.

- `C-37` [정리] **모델 층의 ImGui 사용 한 곳(`Common/Commands/Play/EditorScenarioRecordCommands.cpp`)을 EditorModel 안으로 들이기.** 모델 폴더(`Source/Editor/CMakeLists.txt` 의
  `listEditorModelDir`)는 ImGui 없이 컴파일되는 OBJECT 라이브러리 `EditorModel` 인데, 이 파일만 `#include <imgui.h>` 와 `ImGui::GetIO().DeltaTime`(녹화 프레임 간격)을 써서 예외 목록에 남아 있습니다.
  간격을 셸이 넘기거나 엔진 프레임 시간으로 바꾸면 예외 줄(`list(REMOVE_ITEM listEditorModelSource …)`)을 지웁니다.

- `D3-1` [기능] **에디터 로드맵 — 미룬 영역 패널을 그 영역 옆에.** 엔진 영역 패널은 EditorModule, 기반 GameFramework 영역은 `GF_EditorPlugIn_Base`(`Source/GameFramework/PlugIn/Editor/Base/`),
  키트 영역은 `GF_EditorPlugIn_<이름>` 확장 모듈에 둔다. 남은 것 —
  ⑦ 카탈로그 편집기 뒤: 엔진 데이터 종류(장면 · 머티리얼 · 파이프라인 …)는 아직 `ResourceDataSchemaTest` 의 표에 있고, 카탈로그 밖(Engine 쪽) 열거자 이름 표는 `EnumNameTable` 로 모으지 않았다.
  층 규칙은 걸림돌이 아니다(레지스트리는 티어 5 라 위 티어가 채울 수 있다) — 실제 걸림돌 셋과 단계적 대안은 [결정 기록](../09_Decisions.md) 5-11 "엔진 데이터 종류의 레지스트리 이전" 이다
  (첫 단계 들어감 2026-10-18: `EnumNameTable` 은 `Core/String/EnumNameTable.h` 로 옮겼다 — 다음은 D3-1.2, 로더가 `loadCatalog` 인 줄부터 레지스트리로). ⑧ 지형 칠하기 도구(`TerrainBrushTool`, 씬 뷰 Terrain 토글 — 올리기 · 내리기 · 평탄화 · 레이어 칠하기, 드래그 하나가 되돌리기 하나, 저장 단추)에
  높이장 크기 바꾸기 · 구멍 칠하기 · 스무드 브러시가 없다(쓰기 쪽 남은 것은 `D2-5` (7)).
  다중 월드 툴 창과 그 위의 미리보기 창은 `D3-7`, UI 문서 디자이너는 `D3-3` 이다.
  에디터에 이미 있는 것(전역 변수 표, 커맨드 팔레트, 핫 리로드, Undo, Play 와 Step, InputReplay, 기즈모, RenderTargetPanel, 프로파일러와 Tracy)은 다시 만들지 않는다.

- `D3-3` [기능] **UI 미리보기 패널의 디자이너 다음 단계**(`Editor/Panels/UIPreview/UIPreviewPanel`). 팔레트 끌어 놓기 · 지우기 · 부모 패널 종류별 슬롯 필드 편집(캔버스 · 상자 · 격자 · 공통) ·
  캔버스 앵커 프리셋 열둘 · 상자 순서 / 격자 칸 끌기 손잡이 · 탐침 `Editor.UIPreviewSlotEditCount` 는 들어갔다. 남은 것: 앵커 프리셋을 고른 직후 부모 여백이 있을 때 위젯이 약간 밀리는지 확인,
  손잡이의 가정(`D3-8` UI 디자이너 줄) 보완.

- `D3-4` [기능] **콘솔 · 치트의 남은 것** — 게임 · 키트의 치트 명령(무적 · 아이템 주기 · 돈 …)을 각 게임 · 킷에 `SW_DEV_COMMAND` 로 단다(지금 게임 · 키트 쪽 명령은 `GameNetSession` 하나뿐 — 레지스트리 · 콘솔 ·
  엔진 명령은 `Source/Engine/` "개발 콘솔"). 리눅스 오버레이(`X11DevConsoleWindow`)는 실기로 띄워 보지 않았고, X11 그리기는 `XDrawString`(Latin-1)이라 한글이 깨진다.
  어빌리티 · 판 상태 · Shooter3D 의 `cheat.*` 는 들어갔다(`AbilityDevCommands` · `SessionStateDevCommands` · `ShooterDevCommands`). **무적(`cheat.god`)은 못 달았다** — 어빌리티 시스템에
  무적 API 가 없고, Shooter3D 의 `Vitality::setInvulnerable` 은 private 멤버 `_vitality` 뒤에 있다. API 를 열면 단다. `cheat.heal` · `cheat.attribute` 는 적을 포함한 모든 어빌리티 시스템에 적용된다
  (플레이어만 가리는 길이 필요하면 태그나 팀을 본다). `Vitality` 를 직접 쓰는 다른 키트의 치트는 아직 없다.

- `D3-5` [기능] **그래프 패널 저장 시나리오를 실제로 돌려 확인하기.** 임시 에셋으로 저장하는 길(`asset.open` · `asset.removeTemp` · 탐침 `Editor.TempAssetExists`, `Saved/Automation/` 밖과 리소스 트리는 거절)과
  시나리오 둘(`dialoguegraphsave` · `animgraphsave`)은 들어갔다. 실행은 아직 안 했다 — 확인할 것: 연 직후 `Editor.DirtyDocumentCount` 가 0, `panel.focus` 뒤 `editor asset.save` 가 패널 문서를 저장,
  스크립트가 보내는 `editor asset.save` 가 패널 포커스를 빼앗지 않음, 툴바 노드 추가 단추를 클릭할 수 있음(툴바 폭이 좁으면 잘린다), 대화 그래프 패널이 함께 열렸을 때 `Editor.GraphNodeCount` 가 읽는 패널.

- `D3-7` [기능] **다중 월드 · 에디터 툴 창(로드맵, 카메라 4 단계와 한 덩어리).** 지금 `Engine` 는 활성 씬 하나만 틱하고 그리며, GPUScene 빌더 · 스냅샷도 하나다 —
  그래서 프리팹 격리는 활성 씬을 빌리고(씬이 더러우면 막는다) 머티리얼 미리보기는 메인 뷰포트의 선택 오브젝트에 입혀 보인다. ① 다중 월드(미리보기 월드를
  따로 살림, 월드마다 시간 · 틱 정책 · 조명 환경 · 물리 월드) ② 월드별 렌더(씬마다 GPUScene, 뷰 목록의 뷰가 "어느 월드 · 카메라 · 렌더 타깃" 을 고름,
  안 보이는 창은 안 그림 — 카메라 4 단계의 다중 뷰를 다중 월드 × 다중 뷰로) ③ 툴 창 틀(미리보기 월드를 가진 도킹 창, 자기 렌더 타깃 · 에디터 카메라 ·
  기즈모, 창마다 선택 · Undo 범위 — 지금은 에디터 전체에 하나, 문서 계약은 있는 것, 키트 · 게임이 등록하도록 1-4 의 C 확장 지점 위에) ④ 위에 올릴 창:
  프리팹(격리 월드) · 머티리얼(미리보기 구체) · 메시/모델(LOD 비교) · 애니메이션(스켈레톤 · 타임라인 · 압축 오차) · 카메라 프리셋(블렌드 미리보기) ·
  래그돌/물리 에셋(관절 한계) · 이펙트 · 장르 도구(코스터 트랙 · 리듬 차트). 언리얼 FPreviewScene · 애셋 에디터 툴킷, 유니티 PreviewRenderUtility · Prefab Stage.
  (2026-10-14 사용자 결정: 규모가 XL 이라 에디터 보강 묶음에서 뺐다 — 설계부터 따로 짠다.)
  계획: docs/plans/EditorMultiWorld.md
  **2026-10-15 단계 1~4 와 머티리얼 창이 들어갔다**(계획 문서의 각 "구현됨"). 남은 것:
  (1) **프리팹 창** — `EditorPrefabIsolationCommands` 가 활성 씬 대신 `EditorPreviewPanel` 의 씬에 프리팹을 스폰(더티 막기 제거, 중첩 격리 스택 `PrefabIsolationFrame` 의 의미 유지, 저장 경로 테스트).
  (2) **창 범위 선택의 나머지** — 창 선택(`EditorSelection::bindPreviewScope`)과 겨누는 선택 접근자(`editor::getFocusedSelection`), 인스펙터의 값 편집(창 스택)은 들어갔다.
  남은 것: 메인 선택을 직접 부르는 나머지 패널(계층 · 시퀀서 등 약 25 파일)을 접근자로 옮기기, 인스펙터의 구조 편집(컴포넌트 추가 · 제거)을 미리보기 씬에도 내기,
  미리보기 뷰포트의 클릭 피킹(기즈모는 창 선택에 놓인다 — `EditorViewportGizmo::draw` 가 선택을 받고 창은 `EditorPreviewGizmo` 를 든다).
  (3) **스카이** — 엔진에 스카이 컴포넌트가 없어 조명 환경 프리셋(`EditorPreviewEnvironment`)은 방향광 · 바닥뿐이다.
  (4) **풀 공유** — 미리보기 렌더러가 씬마다 머티리얼 · 정점 풀 · PSO 캐시를 따로 든다(결정 기록 5-11). 창 여럿을 띄운 메모리 · 첫 프레임 히치를 재고 필요하면 계획의 결정 2 대로 공유한다.
  (5) **미리보기 렌더러 내리기의 GPU 안전** — 창을 닫으면 렌더 스레드가 `FrameRenderer::shutdown` 으로 그 렌더러를 바로 내린다. 디바이스 해제 큐가 GPU 완료 뒤로 미루는 것에 기대며,
  네 백엔드에서 창을 연 채 여닫기를 반복하는 시나리오로 확인하지 않았다(DX12 · Vulkan 검증 레이어로 볼 것).
  (6) 미리보기 씬의 백엔드 교체(`-gv_rhiSwapAtFrame`) 확인 · 캐릭터 편집 창이 `EditorPreviewPanel` 을 이어받기(헤더 계약을 따른다).

- `D3-8` [기능] **에디터 보강 묶음(2026-10-14)에서 남긴 것.**
  - **시나리오 녹화**(`ScenarioRecorder` · `scenario.record` · `automation.toggleScenarioRecord`): 마우스 휠은 `Wheel` 단계로, 더블클릭은 두 번째 `Press` 로 기록한다. 조합 중 글자 · 포커스 · 포인터 출입 ·
    패드 연결은 시나리오 의미가 없어 건너뛴 수만 센다. 재생은 원시 Down 을 두 번 내므로 더블클릭 이벤트 자체를 보는 UI 코드는 재현되지 않는다(`DoubleClick` 단계가 필요하면 `VirtualInputScript` 를 고친다).
    액션의 단추(재생이 누를 그 장치의 첫 단추 바인딩)는 `ActionPress` · `ActionRelease` 로 쓴다. 축(스틱 · 합성 축)은 아직 `GamepadAxis` · 키 단계라 `ActionAxis` 로 쓰지 않는다.
  - **인스펙터**(`Panels/Inspector/`): 즐겨찾기(`InspectorFavorites` — 행 우클릭, 맨 위 Favorites 구역, `Saved/Editor/InspectorFavorites.txt` 에 저장)는 들어갔다. 키가 가장 파생된 타입 이름이라 기반 클래스에
    즐겨찾기한 프로퍼티는 파생 타입마다 따로 저장된다. 디버그 모드(유니티 Debug 인스펙터 — 숨은 필드는 리플렉션 범위 밖)는 하지 않았다. "바뀐 것만 보기" 는 다중 선택에서 주 선택 오브젝트의 값만 기준으로 거른다.
  - **모듈 창**(`ModulesPanel`): 종류 · 켜짐 필터, 상세(의존 · 역의존 · 오버라이드), 연쇄 영향 확인 창, 오버라이드 초기화는 들어갔다. 모듈 설치 · 제거(파일 복사 · 삭제) · 외부 패키지 받기는 없다 —
    위험이 커서 뺐다. 확인 창이 패널 수준 ID 로 열려 셀프테스트 시나리오가 창 위치에 기댄다면 영향이 있을 수 있다.
  - **UI 디자이너**(`D3-3`)의 상자 · 격자 손잡이는 들어갔다. 상자 순서 계산은 "가운데 좌표가 커지면 자식 번호도 커진다" 고 가정해 RTL · 음수 간격에서 거꾸로 나온다. 격자 줄은 자식 사각형으로 추정해
    걸친 칸(span)이 섞이고 빈 행 · 열로는 끌어 놓을 수 없다.

- `D3-9` [기능] **에디터 보강 묶음 — 캡처로 확인하고 남은 것(2026-10-15, 브랜치 `wt/visual`).** 시나리오 + `CaptureWindow` 로 DX12 에서 눈으로 확인한 것(결함 4건은 고침): 버그 리포트 창 ·
  인스펙터 보기 옵션(펼치기 · 접기 · 즐겨찾기 복원) · 하이어라키(행 메뉴 · 만들기 메뉴 · 걸러 보기) · 콘텐츠 브라우저 · 뷰포트 우클릭 메뉴(ThemePark 확장 항목) · UI 디자이너 슬롯 손잡이 · 입력 맵 패널(시간 · Negate · taps 칸,
  `inputmaptaps` 시나리오로 남김) · 모듈 창 상세 · 확인 창 · `LightingOnly` 4 백엔드(DX12 · DX11 · Vulkan · GL 이 Lit · LightingOnly 모두 바이트 동일) · 포토 모드 · 치트 명령(로그 응답) · E-11 리로드 로그.
  **남은 것**: (1) 지형 브러시 원 시각화 — 시각화 단추는 켜지지만 `envshowcase` 씬의 에디터 카메라에서 원이 안 보였다(마우스 레이가 지형에 안 닿는지 카메라가 지형 밑인지 가르지 못함, 카메라를 지형 위로 옮겨 다시 볼 것).
  (2) 에디터 메뉴의 시나리오 녹화(`automation.toggleScenarioRecord`)는 자동화가 주입한 입력을 기록하지 않아(`0 steps`) 실제 입력으로만 확인된다(게임 실행의 `-scenario-record` 는 가상 입력도 적고 왕복 테스트가 있다).
  (3) 어빌리티 디버거 · 오디오 이벤트 · 리그 디버그 · 시퀀서 `RigSlot` 트랙 · UI 디자이너 앵커 프리셋 — 빈 상태(안내 글)까지만 봤다. 저장소에 이 기능들을 쓰는 데이터(리그 · 오디오 이벤트 라이브러리)가 없다.
  NPC 일정 디버그 창은 HarvestValley 마을 사람(`FarmVillageDirectorComponent`)으로 실물이 생겼지만 에디터에서 열어 보지 않았다. (4) 시작 크래시는 `A-5` 로 옮겼다. (5) 포토 모드는 게임 HUD 를 그대로 둔다(의도인지 확인).
  (6) 뷰포트 옵션 팝업은 패널이 화면 오른쪽 끝이면 메인 창 밖으로 나가 클라이언트 캡처에 안 잡힌다. (7) 이 PC 의 파이썬이 git 출력을 못 받는 문제는 이 묶음 때 있었으나 이번 커밋 훅은 통과했다.
  캡처: `D:\wisual\captures\`.

- `D3-10` [기능] **메시 모디파이어 스택 플러그인(`GF_EditorPlugIn_MeshModifiers`, 2026-10-14)의 남은 것.** 구조 · 추가 방법 · 알려진 근사는 [모듈 README](../../Source/GameFramework/PlugIn/Editor/MeshModifiers/README.md).
  MegaFiers 류 변형 모음의 기능 이름 목록을 기준으로 56 id 를 등록했다.
  - **셰이딩 미리보기의 남은 것**(구조는 모듈 README "셰이딩 미리보기"): 매개변수가 바뀔 때마다 CPU 스택이 한 번 돈다(입력 상자를 GPU 가 못 세서). CPU 로 내려간 스택은 바뀔 때마다
    정점 버퍼를 새로 만든다. 둘 다 큰 메시에서 끌기 중 비용을 재지 않았다. 시간만 바뀌는 애니메이션도 미리보기 시간 칸을 움직일 때만 갱신된다.
  - **입력 창구 확장(2026-10-14, 들어감)**: 변형 전 위치(Displace Limits 의 `Clamp Displacement`), 회전 최소화 프레임(경로 계열), CPU 텍스처 표본기(Displace 계열 — `stb_image` 를 표본기 .cpp 에 정적으로 넣었다.
    남은 것: 확장 모듈이 쓸 수 있는 엔진의 공개 이미지 디코드 함수가 없어 중복 디코더를 품는다 → 엔진에 공개 디코드를 내보내면 합칠 수 있다. float4 텍셀이라 4096² 한 장이 256 MB, 메모리 상한 없음. 파일 변경 감지는 1 초 지연).
  - **패널 · 뷰포트(편집, 2026-10-14)**: 곡선 그래프 편집기(공용 `EditorCurveEditor` 어댑터) · 열 이름 있는 목록 표 · 참조 콤보 + 에셋 슬롯 · 구역 · 툴팁 · 단위 · 모디파이어 설명은 들어갔다. 뷰포트에서는 중심 이동 ·
    회전 기즈모, 반경 · 하한/상한 · 방향 자동 표시, 격자 제어점 클릭 · 끌기(대칭), 고른 모디파이어까지의 영향 미리보기("Stage" 토글)가 들어갔다. 남은 것:
    - 패널 · 기즈모는 시나리오의 이름표 클릭과 탐침으로만 확인했다. 이제 `<CaptureEditorUI>` 로 찍어 볼 수 있다(아직 찍지 않았다).
    - **들어감(2026-10-14)**: 공용 마우스 양보 창구 `EditorViewportMouseClaim`(뷰포트의 양보 판정 두 곳에 반영), 에셋 슬롯의 여러 확장자 필터(`EditorAssetFilter`, 쉼표 · 공백 · 탭 · 세미콜론)와 Point Cache(`.mfpc`) ·
      두 Displace 의 필터 채우기, 참조 걸러 보기의 파생 컴포넌트 판정(`TypeInfo::isDerivedFrom`), 회전 고리의 오일러 정확도(`applyRotationDelta`), 범위 · ffd2d 상자가 앞 모디파이어 결과를 반영.
    - **남은 한계**: 기즈모를 끄는 매 프레임 앞 모디파이어를 다시 걸어 비용이 든다. clang-format 20.1.8 로 줄 길이를 맞출 것(이 PC 는 21.1.1).
  - **정리**: 월드 공간 워프는 `World Warp` 모디파이어(씬 오브젝트 + 공유 `.mfwarp` 파일)로 들어갔다. 남은 것: 패널에서 묶음을 `.mfwarp` 로 내보내는 단추.

- `D3-12` [기능] **첫 UX 감사에서 남은 결함(2026-10-15).** 목록 · 우선순위 · 캡처 번호는 [에디터 UX 기반](../plans/EditorUX.md) 8-1 절이 원본이다. 큰 것부터:
  ① 기본 배치의 아래 도크(Output Log · Content Browser)가 낮아 내용이 거의 안 보인다(도크 최소 높이 · 기본 비율). ② 씬 뷰 통계 오버레이가 뷰포트 절반을 덮는다(접기 · 앵커).
  ③ 탭 · 표 열 · 콤보의 말줄임 · 툴팁 · 가로 스크롤 정책을 공용 위젯으로(Hierarchy · Profiler · Keyboard Shortcuts · Global Variables · Data Table · Project Settings).
  ④ 환경설정 · 사용자 설정의 표시 이름(내부 변수명이 보인다). ⑤ 라이트 · 고대비 토큰의 남은 대비(프리팹 이름색 · 로그 태그색 · 꺼진 토글 · 오류 칩) — `editor-theme check` 의 대비 쌍을 늘린다.
  ⑥ 빈 상태 문구(History · Catalog Editor · Multiplayer 로그 · Audio Mixer). ⑦ 되돌리기 용어 통일(Reset / Revert). ⑧ 판정 못 한 것: Tab 순서 · 포커스 표시 · 메뉴 단축키 표기는 입력 단계와 캡처를 묶은 시나리오가 필요하다.
  측정 지표 `maxClippedWindows` 는 메뉴 바 · 플레이 툴바의 작은 넘침(세로 +24, 가로 +8)을 아직 센다 — 넘친 양의 문턱을 정해 단언에 쓸 수 있게 한다.

---

[◀ 백로그 안내](README.md)

- `D3-13` [기능] **디자이너용 데이터 도구 점검.** 스크립팅 층을 두지 않으므로(결정 기록 2절) 코드 없이 로직 · 재질을 짜는 도구가 중요하다. 머티리얼 그래프, 상태 머신 · 행동 트리 편집기, 파티클 에디터(`D2-11` P2)가 있는지 현재 상태를 코드로 확인하고 빠진 것을 항목으로 쪼갠다. 확인 전에는 있다 없다를 단정하지 않는다.
