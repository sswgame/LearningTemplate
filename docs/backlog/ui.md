# 백로그: 런타임 UI

런타임 UI, 글자 렌더링, 접근성, DPI 의 남은 일입니다. 상태 태그의 뜻, 일을 고르는 순서, 새 항목을 쓰는 법은 [백로그 안내](README.md)에 있습니다.

- `B-8` [검증대기] **150 % 모니터 실물 확인** — 대체 테스트(`dpi.monitorScaleFollows`: 창 DPI 질의를 +0.5 로 바꿔 끼움 · WM_DPICHANGED)이 글자 · 여백 · 창 · 백버퍼를 본다.
  남은 것은 글자 래스터 선명도 — 150 % 모니터에서 에디터를 띄워 눈으로 한 번(2026-10-06 이 PC 의 기동 로그가 `Editor UI scale 1.5 (monitor DPI)` 였다).

- `C-8` [정리] **월드 공간 UI 를 대량으로 그리는 비용을 재고, 넘으면 GPU 로 옮긴다.** RTS 이름 · HP 바 2,000 개는 Release `App -gv_benchMeshes=2000 -gv_benchUiNameplates=2000 -gv_profileFrames=600` 으로 잰다
  (게임 스레드 `GT.UI.*`, 렌더 스레드 `RT.Canvas.*`). 게임 스레드가 0.5 ms 를 넘으면 위치 투영 · 컬링 · 간접 드로우를 컴퓨트로 옮긴다(메시의 GPU 커맨드 생성과 같은 구조; 언리얼은 대량
  머리 위 표시를 Niagara 로 권한다). 데미지 숫자처럼 시간 함수로만 움직이는 UI 의 GPU 애니메이션도 같은 측정으로 본다.
  2026-10-13 Release dx12 측정(이름표 2000): 게임 스레드 `GT.UI.Layout` p50 1.18 ms + `GT.UI.Paint` p50 1.97 ms = 약 3.1 ms 로 기준 0.5 ms 를 넘는다 — 옮기는 조건에 해당한다.

- `C-9` [정리] **글리프 SDF 를 컴퓨트로 만들지 재고 정한다.** 한글 장문을 처음 띄우는 프레임은 Release `App -gv_benchMeshes=100 -gv_benchUiHangulGlyphs=400 -gv_profileFrames=600`(`GT.Frame` · `RT.Canvas.Upload` 의 max),
  CPU 몫만은 `EngineTest --test_filter=GlyphBenchTest.*` 로 잰다. 히치가 4 ms 를 넘으면 FreeType 은 외곽선만 읽고 SDF 는 컴퓨트(점프 플러딩)로 만든다.
  2026-10-13 Release 측정: `GlyphBenchTest` 새 글리프 400 개 CPU 약 0.89 s(글리프당 약 2.2 ms). App `hangulGlyphs` 첫 프레임은 4 ms 넘는 히치가 드러나지 않았다 — 이 PC 에 시스템 한글 글꼴(`Noto Sans`)이 없어 글리프가 실제로 안 만들어졌을 수 있으니 글꼴을 갖춘 환경에서 다시 본다.

- `D6-1` [기능] **런타임 UI 전환의 남은 것**(`Engine/UI/README.md`). 상태별 브러시(단추 호버 · 누름 · 꺼짐)는 전환이 읽지 않고 기본 브러시를 쓴다.
  "키 사이 사건 말고 곡선 위 사건" 은 뜻이 불분명해 하지 않았다 — 쓰는 곳이 생기면 뜻을 정한다. 그림 캐시가 조상 변환에 자손까지 다시 칠하는 문제는 `D6-6`.

- `D6-2` [기능] **런타임 UI 입력의 남은 것**(`Engine/UI/README.md`). (2) 에디터 게임 뷰(Game 패널) 안의 포인터 좌표(창 픽셀 → 게임 뷰 렌더 타깃) — UI 가 게임 뷰에 그려지는 단계와 함께.
  (3) UI 행동 맵(`engine/input/ui.input.xml`)의 키 리바인딩 — 키 바인딩 창(`KeyRebindScreen`)은 있다, UI 맵을 설정 대상으로 두는 길(`UserSettingsTargets` 의 입력 맵이 게임 맵 하나)이 남았다.
  (4) 글 입력 칸의 운영체제 클립보드 — `UIClipboardAPI` 함수 표를 창 계층이 끼워야 한다(끼우기 전엔 엔진 안 버퍼 하나다).

- `D6-3` [기능] **런타임 UI 접근성의 남은 것.** (1) 음성 재생 쪽 자막 — 음성 이벤트(`GameSound`)에 자막 키를 실어
  `UISubtitleService::post` 로(오디오 키트의 모양을 보고 정한다 — 지금은 대화 러너만 보낸다).

- `D6-4` [기능] **옵션 · 일시정지 메뉴의 남은 것**(`Engine/UI/README.md`). (1) 일시정지 메뉴의 타이틀로 · 끝내기 단추는 전역 명령 표(`UISystem::getGlobalCommands`)에 같은 이름의 명령이 있을 때만 보인다 —
  등록하는 쪽(`Engine` 의 `QuitGame`, 게임 흐름 `GameInstanceBase` 의 `ReturnToTitle`)이 아직 없다. (5) 해상도 바꾸고 15 초 되돌림 ·
  키 바인딩 재시작 뒤 유지의 실기동 확인(Shooter3D · 패드)은 게임별 빌드 때.

- `D6-5` [기능] **알림 · 힌트 · 목표 마커의 남은 것**(`Engine/UI/README.md`). (2) 목표 마커를 퀘스트 키트의 목표 오브젝트에 붙이는 한두 줄 — 키트 쪽 목표 오브젝트 모양을 보고. (3) 코드가 올리는 알림 글
  ("Game saved" · "다시 시작하면 적용")은 글 그대로라 글 수집에 들지 않는다 — 코드 글 키(`SW_LOCTEXT` 꼴)로 바꿀 것. (4) 입력 힌트 위젯을 따로 두지 않았다 —
  리치 텍스트 태그 하나로 충분한지 게임 HUD(8-1)에서 본다.

- `D6-6` [기능] **런타임 UI 그리기 성능의 남은 것**(`Engine/UI/README.md` "성능"). 목표(`GT.UI` p50 0.3 ms) 안이다(측정값과 걷기 분해는 [검증과 측정](../08_Verification.md) 2절 "런타임 UI 벤치").
  더 줄일 후보 둘 — 패널마다 하위 출력 캐시(Slate Invalidation Panel)와, 조상의 `kTransform` 이면 자손까지 다시 칠하는 그림 캐시의 변환을 캐시 밖에서 곱하기 — 는 README 성능 절의
  세 측정(멈춘 화면 · 일부만 바뀜 · `-gv_benchUiOpacityPulse`)과 도입 기준으로 정한다. 창 크기 바꿈(전체 재배치) 한 프레임 · 첫 프레임 글리프 래스터화가 몇 프레임에 퍼지는지는 표 밖이다(워밍업 60 프레임이 버린다).

- `G-3` [조건부] **월드 공간 위젯의 입력 · 가려짐(runtime-ui 4-6 뒤)** — World 위젯은 그리기만 한다: 레이로 사각형을 맞혀 위젯 좌표로 사건을 보내는 길(언리얼
  `WidgetInteractionComponent`)이 없다. 화면 마커의 "벽에 가려지면 숨김" 은 레이캐스트 질의 · 깊이 버퍼 읽기 둘 다 없다. World 렌더 텍스처는 내용이 바뀔 때만 다시
  그리지만 화면에 안 보일 때도 칠하기는 돈다(보일 때만 칠하기 — 절두체 판정은 조건: 월드 위젯이 수십 개를 넘을 때).

- `G-9` [조건부] **스크린 리더**(위젯 접근성 이름 · 역할 → OS 내레이터 — 언리얼 Slate 접근성 · Xbox 접근성 지침 107): 런타임 UI 의 접근성은 글자 크기 · 자막 · 색각 · 고대비까지다.

- `G-10` [조건부] **MSDF 글리프**(직접 — 윤곽 모서리 칠하기 · 채널별 거리, 큰 글자의 모서리가 날카롭다) — 지금은 단일 채널 SDF(`Engine/Text/GlyphCache`, FreeType `sdf` 렌더러,
  결정 R2). 아틀라스 페이지가 R8 이라 MSDF 는 RGB 페이지 · 셰이더 median 이 함께 든다.

---

[◀ 백로그 안내](README.md)
