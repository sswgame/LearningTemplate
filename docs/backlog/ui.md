# 백로그: 런타임 UI

런타임 UI, 글자 렌더링, 접근성, DPI 의 남은 일입니다. 상태 태그의 뜻, 일을 고르는 순서, 새 항목을 쓰는 법은 [백로그 안내](README.md)에 있습니다.

- `G-13` [조건부] **화면 마커의 투영 · 컬링을 컴퓨트로 옮긴다** — 이름표 2,000 개의 남은 게임 스레드 몫 `GT.UI.Markers` p50 0.49 ~ 0.52 ms(마커마다 월드 위치 읽기 · 투영 · 앵커 쓰기,
  위치 · 배율 적용은 이미 GPU 앵커다 — [결정 기록](../09_Decisions.md) 5-7). 조건: 게임 HUD 에서 마커가 2,000 개를 넘거나 이 구간이 0.5 ms 를 꾸준히 넘을 때. 컴퓨트가 GPUScene 변환을 읽어
  앵커 표를 쓰는 모양이다(마커 ↔ GPU 인스턴스 대응이 필요하다). 데미지 숫자처럼 시간 함수로만 움직이는 UI 의 GPU 애니메이션도 같은 길이다.
  측정: Release `App -gv_benchMeshes=2000 -gv_benchUiNameplates=2000 -gv_profileFrames=600` 의 `GT.UI.Markers` · `GT.UI.Paint`.

- `C-9` [정리] **새 글리프 래스터화 히치를 없앤다(SDF 를 컴퓨트로 만들지 포함).** 2026-10-17 Release 측정(PC SW, AMD Family 25, `-lang=ko` — 영어 문화권 대체 글꼴 사슬에는 한글 글꼴이 없어 두부만 그려졌다):
  App `-lang=ko -gv_benchMeshes=100 -gv_benchUiHangulGlyphs=400` 탐침 프레임 `GT.Frame` max 784 ms(`GT.UI.Paint` 766 ms, `Text.GlyphsRasterized` 401), `RT.Canvas.Upload` max 4.6 ms.
  `GlyphBenchTest` 새 글리프 400 개 CPU p50 0.93 s(글리프당 약 2.3 ms, FreeType `sdf` 렌더러 — 외곽선에서 정확한 거리). 기준 4 ms 를 크게 넘는다.
  판단: SDF 만 컴퓨트(점프 플러딩)로 옮겨도 커버리지 래스터화(FreeType 보통 렌더)는 CPU 에 남아 400 개가 한 프레임이면 여전히 기준을 넘는다 — 둘 다 필요하다:
  (1) 새 글리프를 프레임 예산 안에서만 만들고 나머지는 다음 프레임(만들어지기 전엔 그리지 않음), (2) SDF 를 비트맵 → 거리 변환(FreeType `bsdf` 또는 컴퓨트 JFA)으로 바꿔 글리프당 비용을 줄인다.
  먼저 `bsdf` 의 글리프당 비용과 화질을 `GlyphBenchTest` 로 재서 컴퓨트가 필요한지 정한다. 탐침 오브젝트에 씬 컴포넌트가 없어 마커가 접혀 측정이 비어 있던 것은 고쳤다(`WidgetComponent` 가 시작할 때 오류를 남긴다).

- `D6-1` [기능] **런타임 UI 전환의 남은 것**(`Engine/UI/README.md`).
  "키 사이 사건 말고 곡선 위 사건" 은 뜻이 불분명해 하지 않았다 — 쓰는 곳이 생기면 뜻을 정한다. 그림 캐시가 조상 변환에 자손까지 다시 칠하는 문제는 `D6-6`.

- `D6-2` [기능] **런타임 UI 입력의 남은 것**(`Engine/UI/README.md`). (2) 에디터 게임 뷰(Game 패널) 안의 포인터 좌표(창 픽셀 → 게임 뷰 렌더 타깃) — UI 가 게임 뷰에 그려지는 단계와 함께.
  (4) 리눅스의 운영체제 클립보드 — Windows 만 끼웠다(`PlatformClipboard`, `App/RuntimeUIHost`). X11 은 CLIPBOARD 선택 소유와 `SelectionRequest` 응답이 창 메시지 루프에 있어야 하고
  (`Engine/Window/Linux/X11Clipboard.cpp` 는 `isAvailable` false), Wayland 는 `D1-6`. 그때까지 리눅스 UI 는 엔진 안 클립보드다.

- `D6-3` [기능] **런타임 UI 접근성의 남은 것.** (1) 음성 재생 쪽 자막 — 음성 이벤트(`GameSound`)에 자막 키를 실어
  `UISubtitleService::post` 로(오디오 키트의 모양을 보고 정한다 — 지금은 대화 러너만 보낸다).

- `D6-4` [기능] **옵션 · 일시정지 메뉴의 남은 것**(`Engine/UI/README.md`). (5) 키 바인딩이 재시작 뒤에도 유지되는지의 실기동 확인(Shooter3D · 패드) — 두 번 기동해야 하고
  사용자 설정 파일을 쓰므로 시나리오 하나로 못 한다(15 초 되돌림은 `engine/automation/displayrevert.scenario.xml` 이 본다). 타이틀 씬(`titleScene`)을 가진 게임이 아직 없어
  "타이틀로" 단추는 어느 게임에서도 접혀 있다 — 타이틀 화면을 만드는 게임이 생기면 그 게임으로 확인한다.

- `D6-5` [기능] **알림 · 힌트 · 목표 마커의 남은 것**(`Engine/UI/README.md`). (2) 목표 마커를 퀘스트 키트의 목표 오브젝트에 붙이는 한두 줄 — 키트 쪽 목표 오브젝트 모양을 보고.
  (4) 입력 힌트 위젯을 따로 두지 않았다 — 리치 텍스트 태그 하나로 충분한지 게임 HUD(8-1)에서 본다.

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
