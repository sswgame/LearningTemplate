# 5-7. 런타임 UI 와 글자

> 질문: 5-7. 런타임 UI 와 글자 영역에서 이미 정한 것은 무엇이고 근거는 무엇인가?
> 독자: 같은 제안을 다시 하려는 사람, 정한 이유를 찾는 사람.
> 결론: 결정 기록 5 절의 한 영역이다. 번호 체계와 쓰는 법은 [결정 기록](../09_Decisions.md) 5절에 있다.

- **저장소에는 CC0 라틴 글꼴만 둡니다**(2026-10-06, 사용자 결정). 한글과 CJK 는 OS 시스템 글꼴로 대체하고, 시스템 글꼴을 쓰는 테스트는 글리프가 있는지만 확인합니다.
- **글리프는 단일 채널 SDF(FreeType 내장)로 그립니다**(2026-10-06). 유니티 TextMeshPro, 언리얼 SDF 글꼴과 같습니다. MSDF 는 카탈로그 `_glyphField` 로 켜는 선택이고 기본은 SDF 입니다([MsdfGlyphs](../plans/MsdfGlyphs.md), 2026-10-22). FreeType 은 `vcpkg.json` 의 엔진 직접 의존입니다.
- **MSDF 는 직접 구현이고 카탈로그의 한 칸(`_glyphField`)이 고릅니다**(2026-10-22, [MsdfGlyphs](../plans/MsdfGlyphs.md)). 윤곽을 선분 · 베지어 간선으로 받아 모서리에서 채널을 갈라 칠하고(msdfgen 의 단순 칠하기), 채널별 가장 가까운 간선의 의사 거리를 RGB 에 담는다.
  알파에는 일반 SDF 를 같이 둔다(MTSDF, RGBA8 한 장). 아틀라스 페이지 형식이 모두 같아야 하므로 서비스 하나에 한 종류이고 기본은 SDF 다. 글리프당 CPU 비용이 SDF(FreeType, 약 2.3 ms)보다 크고 페이지 메모리가 네 배라 큰 글자의 모서리가 문제일 때만 켠다.
  겹치는 윤곽은 합치지 않는다(가변 폰트 글리프에서 거짓 선 가능). 셰이더는 글리프 사각형의 플래그(`kMultiChannelGlyph`)를 보고 RGB 중앙값을 쓴다.
- **백엔드 선택과 소유 객체의 서브시스템 판정**(2026-10-15, C-27 ⑤ 마감 — 코드를 다시 읽고 확정, 코드 변경 없음). 상용 엔진 비교는 모두 "~로 알고 있다" 로 기억에 의존한다.
  - **오디오는 이미 역할별 팩토리가 서브시스템 하나를 고른다.** `IAudioService::create()` 가 `SW_PLATFORM_WINDOWS` 와 `SW_WITH_CLIENT_CODE` 이면 `XAudio2AudioBackend`, 아니면 `NullAudioBackend` 를 돌려주고,
    `Engine` 는 전용 서버(`bDedicatedServer`)면 `createNull()` 을 골라 기동 단계에 `addSubsystem` 한다. 소비자는 구현 종류를 모르고 `IAudioService` 만 본다. 언리얼 오디오 믹서가 플랫폼별 `IAudioMixerPlatformInterface` 를 고르는 모양으로 알고 있다.
  - **물리는 갈아 끼우는 쌍이 아니라 함께 쓰는 둘이다.** `PhysicsSystem`(서브시스템 `Physics`) 하나가 Jolt(3D, `createScene3D`)와 Box2D(2D, `createScene2D`)를 같은 때에 쓴다. 한 역할에 구현이 둘이 아니라 역할이 둘이라 호스트 종류로 고를 것이 없다.
    그래서 팩토리나 서브시스템을 더 나누지 않는다. 언리얼은 PhysX · Chaos 처럼 한 역할의 구현을 모듈로 고르지만, 3D · 2D 를 함께 켜는 것은 Paper2D 가 Box2D 를 3D 물리와 따로 쓰는 것과 비슷하게 알고 있다.
  - **`ResourcePakManager` 는 서브시스템이 아니다.** `AssetManager`(서브시스템)가 `_pPackManager` 로 소유하고 `ResourceIO::getPackManager()` 로만 닿는다. 종료 때 `AssetManager::shutdown` 이 `unmountAll` 로 비우고(마운트를 부르는 곳은 엔진 코드에 없고 호스트 · 도구 · 테스트가 건다),
    자기만의 의존 순서나 별도 수명 범위가 없다. 서브시스템으로 올리면 `AssetManager` 와 둘 사이에 의존 칸만 늘 뿐이다. 판정 기준의 "소유한 서브시스템 안의 객체" 이고, RHI 부품과 같은 이유로 타입 있는 멤버로 둔다.
  - **`ServerServiceManager` 는 엔진 범위 서브시스템이 아니다.** 소유자가 엔진 루프가 아니라 `ServerApp`(호스트)이고, 수명이 엔진 기동 · 종료가 아니라 모듈 리로드 직전의 `stopAll` 과 리로드 뒤 `startAll` 로 되풀이된다.
    서버 서비스의 코드는 모듈(DLL)에 있어 모듈 언로드 전에 멈춰야 하므로, 엔진 범위 종료 순서가 아니라 호스트의 모듈 리로드 순서를 따른다. 전용 서버 호스트에만 있고 의존 서브시스템 선언으로 순서를 정할 것도 없다.
    **호스트 범위 서브시스템으로도 옮기지 않는다**(2026-10-15, 호스트 범위를 세우며 확인). 서브시스템의 한 쌍(`initializeSubsystem` · `shutdownSubsystem`)은 범위 수명에 한 번이고,
    되풀이하는 재기동은 `shutdownDependentsOf` → `restartStopped` 가 맡는데 이것은 "다른 서브시스템을 갈아 끼울 때 그에 기대는 것만" 내리고 다시 세우는 모양이다(RHI 백엔드 교체).
    서버 서비스는 반대로 **자기 자신**이 모듈 리로드마다 멈췄다 시작해야 하고(`onBeforeModuleReload` 의 `stopAll` → 다음 틱 `startAll`), 시작이 게임 인스턴스 · 서버 설정 · 지표 레지스트리가 선 뒤여야 한다.
    그것들은 호스트 범위가 초기화되는 시점(엔진 앞)보다 늦게 서므로 범위의 위상 순서로는 표현할 수 없다. 멈추는 자리도 범위 종료가 아니라 모듈 언로드 직전의 콜백이다.
    그래서 호스트가 `unique_ptr` 로 들고 모듈 리로드 · 종료 순서에서 직접 부르는 지금 모양이 맞다. 언리얼의 `FTickableGameObject` 서비스가 모듈 `ShutdownModule` 에서 수동으로 내려가는 것과 같은 자리로 알고 있다.
- **체력 바와 데미지 숫자는 화면 마커 위젯입니다**(2026-10-06). 값은 컴포넌트 코드가 직접 넣습니다. 마커는 오브젝트마다 하나라 뷰모델과 문서를 두면 마커 수만큼 생기기 때문이고, 언리얼의 위젯 컴포넌트 체력 바도 보통 그렇게 합니다.
- **데이터 바인딩은 뷰모델과 필드 통지입니다**(2026-10-06). 언리얼 5 MVVM 과 같은 구조이고, 알림은 필드마다의 단조 번호로 한 프레임에 한 번 반영합니다. 폴링 바인딩은 개발 편의용입니다.
- **UI 문서와 스타일은 모두 엔진 XML 입니다**(2026-10-06). 파서가 하나여서 학습하기 쉽습니다. 스타일이 정한 필드만 위젯 필드를 이기고, 정하지 않은 필드는 위젯의 기본 모습을 씁니다.
- **엔진이 기본 옵션 메뉴와 일시정지 메뉴 문서를 두고, 게임 프리셋이 덮어씁니다**(2026-10-06). 언리얼 CommonUI 와 Lyra 의 방식입니다. 메뉴는 UI 입력 맵의 `UI.Pause` 로 열리고, UI 행동 맵은 `UISystem` 이 소유하는 별도 맵입니다.
- **게임 정지는 시간 배율과 따로인 정지 요청 수입니다**(2026-10-06). 언리얼의 `SetGamePaused` 와 같습니다. UI, 페이드, 로딩 화면은 실제 경과 시간을 씁니다(유니티 `unscaledDeltaTime`).
- **접근성**(2026-10-06): 글자 크기는 배율이 줄여도 12 UI 단위 밑으로 내려가지 않습니다(Xbox 접근성 지침 101). 색각 보정은 Machado 2009 흉내 행렬과 오차 재분배이고 UI 전체에 겁니다.
- **스크린 리더는 접근성 트리 스냅샷과 플랫폼 브리지입니다**(2026-10-22, [ScreenReaderSupport](../plans/ScreenReaderSupport.md)). 위젯이 `describeAccessible` 로 역할 · 이름 · 값 · 상태를 내고(`accessibleName` 은 현지화 가능한 PROPERTY),
  `UIAccessibilityTree` 가 보이는 위젯만 읽는 순서로 스냅샷을 뽑는다. 브리지(`IUIAccessibilityBridge`)는 위젯 포인터를 쥐지 않고 (화면 핸들, 위젯 번호)로 스냅샷에서 노드를 찾는다 — 위젯이 사라지면 "요소 없음" 이다.
  Windows 는 UI Automation 제공자(`WM_GETOBJECT`, 내레이터)를 만들었고 리눅스 AT-SPI 는 두지 않았다. 스크린 리더가 붙어 있지 않으면 트리를 뽑지도 알리지도 않는다. 에디터는 창을 ImGui 가 써서 게임 UI 브리지를 끼우지 않는다.
  UIA 는 `ProviderOptions_UseComThreading` 으로 메인 스레드에서 처리한다. 언리얼 Slate 접근성(`FSlateAccessibleWidget` 을 플랫폼 접근성 API 에 잇는 구조)과 같은 모양으로 알고 있다.
- **월드 위젯 입력은 광선 → 위젯 좌표 → 위젯마다의 포인터 상태입니다**(2026-10-22, [WorldWidgetInput](../plans/WorldWidgetInput.md)). 입력을 켠(`_bAcceptsInput`) World 위젯 가운데 광선이 가장 가까이 맞힌 하나가 이벤트를 받고,
  위젯 컴포넌트마다 `UIPointerState` 를 가져 Enter · Leave 와 끌기가 화면 위젯과 같은 규칙으로 돈다. 물리 가림(`_bOccludedByPhysics`)은 물리 씬 광선 질의로 하고 깊이 버퍼는 읽지 않는다(렌더러 의존이 없다).
  언리얼 `WidgetInteractionComponent` 와 같은 일을 한다.
- **HUD 문서 경로는 플레이어 오브젝트의 컴포넌트 필드이고, 로딩 화면 문서는 팩의 `gamesettings.xml` 에 둡니다**(2026-10-06). 언리얼의 `AHUD` 가 GameMode 에 붙는 것처럼 씬마다 다른 HUD 를 쓸 수 있습니다.
- **UI 가 먹은 입력은 `UIInputMapView` 로 거릅니다**(2026-10-13). 입력 맵의 읽기 질의와 이름 · 뜻이 같고, 먹은 액션이거나 게임 입력이 막힌 상태면 "누르지 않음" 을 돌려줍니다.
  플레이어 조종자 · 명령 조종자 · 카메라는 입력 맵 읽기 한 줄만 바꿔 씁니다(언리얼 입력 소비 · 유니티 `EventSystem.IsPointerOverGameObject` 와 같은 자리).
- **화면 마커의 위치와 거리 배율은 GPU 가 화면 앵커로 적용합니다**(2026-10-17, 백로그 C-8 측정 후 결정). 마커 위젯은 피벗을 원점에 두고 한 번 배치 · 칠하고,
  사각형은 앵커 번호(`CanvasQuad::_anchor`)를 가진다. 마커가 움직이면 앵커 표(`CanvasFrameData::_listAnchor`, 사각형 버퍼 맨 앞 원소)만 바뀌어 다시 배치 · 칠하기 · 사각형 업로드가 없다.
  모든 화면 트리가 재사용이면 그리기 목록 이어 붙이기와 같은 내용 비교도 건너뛴다. 측정(PC SW, AMD Family 25 · Release dx12 · 이름표 2,000 · 600 프레임, 전후 바이너리를 번갈아 5 판씩 세 번, 가운데 값):
  `GT.UI.Layout + GT.UI.Paint` p50 3.15 ~ 3.4 ms → 0.004 ms, 새로 잰 `GT.UI.Markers`(투영 · 앵커 쓰기, 전에는 구간 밖) 0.49 ~ 0.52 ms, `GT.Frame` p50 4.19 → 0.92 ~ 1.18 ms,
  `RT.Canvas.Upload` p50 196 → 36 ~ 49 us, 벽시계 4.3 ~ 4.7 → 1.1 ~ 1.2 ms/프레임. 남은 게임 스레드 몫은 마커마다 월드 위치를 읽고 투영하는 일이고,
  `GT.UI.*` 합이 기준 0.5 ms 언저리다. 투영 · 컬링까지 컴퓨트로 옮기려면 GPU 가 마커의 월드 위치(GPUScene 변환)를 읽어야 해 [후보 문서](../plans/Candidates-ui.md) `G-13` 으로 둔다.
  앵커 아래에서는 자르기를 쓸 수 없다(가위는 대상 픽셀이다 — `UIPaintPass` 가 단언한다). 앵커가 따로 바인딩을 늘리지 않으려고 사각형 버퍼 원소 하나(rect 만 읽음)를 쓴다(SRV 슬롯 16 이 Vulkan 밴드를 꽉 채운다).

- **UI 전환의 곡선 위 사건은 트랙 값이 문턱을 지날 때 화면에 명령을 보냅니다**(2026-10-22, 전환의 곡선 위 사건). 시각에 붙는 `UIAnimationEvent` 는 곡선 · 키를 고치면 어긋나지만 문턱은 곡선을 따라갑니다(불투명도가 0.5 를 넘을 때 소리, 크기가 1 로 돌아올 때 다음 연출). `UIAnimationTrack::_listCurveEvent`(`UIAnimationCurveEvent` — 문턱 · 성분 · 방향 `UICurveCrossing` Rising · Falling · Either · 명령)를 `UIAnimationPlayer::fireCurveEvents` 가 처리합니다.
  재생 구간을 키 시각마다 끊어(키 사이 곡선은 한 방향) 두 끝 값으로 지남을 보고, 지난 시각은 두 끝 사이를 선형으로 나눠 사건 순서에 씁니다. 거꾸로 재생은 재생 방향의 오르내림을 봅니다. 실수가 아닌 트랙이나 없는 성분에 사건을 달면 경고하고 트랙째 뺍니다(조용히 안 보내지 않음). 움직임 줄이기 · 길이 0 은 끝까지 한 번에 가며 사이의 지남을 모두 보냅니다. `UIDocumentWriter` 는 빈 `<_listCurveEvent />` 를 쓰지 않아 옛 문서를 다시 저장해도 바뀌지 않습니다.

- **리눅스 개발 콘솔 오버레이는 Xft(fontconfig)로 그리고, 확인하기 어려운 "재시작 뒤" 확인은 두 번 기동 명령 · 단위 테스트로 옮깁니다**(2026-10-27, UI 후보 처리). 콘솔은 코어 X 글꼴(서버가 가진 글자만) 대신 `XftFontOpenName("monospace-11")` 로 열고, 기본 글꼴에 없는 글자는 fontconfig 에게 그 글자를 가진 글꼴을 물어 글자마다 그린다(없으면 건너뜀, 글꼴 자체가 안 열리면 콘솔을 세우지 않는다 — 폴백 없음). `libxft-dev`(MIT 계열, fontconfig MIT · FreeType FTL, 동적 링크) 가 새 시스템 의존성이다.
  키 다시 잡기의 재시작 유지는 `scenario-seq`(시나리오를 차례로 새 프로세스로) · `UserSettingsTest.KeyBindingSurvivesRestart` 로 자동화했고, 게임 쪽 "타이틀로" 등록은 `registerSessionCommands( UISystem& )` 가 UI 를 인자로 받게 해 가짜 설정으로 시험한다. 떼어 낸 창의 키 규칙은 `DetachedKeyForwarder` 로 패널에서 꺼냈다(OS 포커스 이동만 사람 몫). AT-SPI 브리지는 `SW_ENABLE_ATSPI`(libdbus-1, AFL-2.1/GPL-2.0+ 이중, 동적 링크) 뒤에 두고 기본은 끈다 — 지어 본 적이 없어 CI 에서 켜기 전까지 주 구성에 영향이 없게 한다.
- **출시 문화권의 번역 누락 · 낡음은 사람 눈이 아니라 `App --check-text` 헤드리스 테스트(`AppSmokeTest.TextCheckRunsHeadlessAndFindsNoMissingOrStaleTranslation`)가 매번 잡는다**(2026-10-27, 현지화 후보 정리). 일본어 화면 잘림 · RTL 배치의 눈 확인은 문화권을 바꾸는 시나리오 단계가 없어 자동화하지 않고 후보에서 뺀다(UI 자동 레이아웃 검사가 문화권을 따로 돌 수 있게 될 때 새로 연다).
- **UI 스타일 필드의 읽기 · 쓰기 · 같음 · 성분 수는 서술자 표(`UIStyleFieldTable`)의 오프셋 · 성분 수 칸에서 나옵니다**(2026-10-27, 감사 SD #6). 필드 하나에 switch 넷(성분 수 · 읽기 · 쓰기 · 같음)이 따로 있어 필드를 더할 때 넷을 맞춰 고쳐야 했습니다. 5-4 의 "필드마다 한 줄" 표에 `offsetof` 와 멤버 형식에서 나온 성분 수를 더했고(줄 하나에 멤버 이름을 한 번만 적음) 수치가 아닌 Font · Transition 만 `isSameValue` 가 따로 견주며 `static_assert` 가 그 둘뿐임을 지킵니다. 언리얼 `FProperty` 오프셋 · Godot 테마 항목 표와 같은 자리이고 리플렉션 `PropertyInfo` 를 런타임에 훑는 안은 프레임마다 도는 경로라 기각했습니다.
- **`UISystem` 의 그리기 목록 상태와 화면 마커는 값 멤버 `UICanvasPaintState` · `UIScreenMarkers` 가 듭니다**(2026-10-27, 감사 SD #5). 입력 라우팅(`UIInputRouter`) · 테마(`UIThemeController`) · 오프스크린(`UIOffscreenScreens`)과 같은 비가상 값 멤버 나누기이고 새 추상화는 두지 않았습니다. 그리기 쪽은 목록 둘 · 스크래치 · 내용 번호 · 지난 트리 순서 · 레이어 숨김 요청 수 10 칸, 마커 쪽은 화면 핸들 · 앵커 · 낸 앵커 · 빈 번호입니다. 화면 스택 순회 · 포커스 테두리 · 마커 화면 올리고 닫기는 화면 스택의 일이라 `UISystem` 에 남고 서비스 창구 getter 는 그대로 위임합니다. 재사용 판정 순서(프레임 조건 → 같은 트리 → `canReuseOutput`)는 `paintScreens` 본문 그대로입니다(언리얼 `FSlateApplication` 이 `FSlateDrawBuffer` · 렌더러 상태를 따로 드는 것과 같은 자리).
