# 백로그: 저장소 규칙, 서드파티, 리소스

코딩 규칙과 이름, 주석 문체, 서드파티, 게임 리소스처럼 한 영역에 속하지 않는 남은 일입니다. 상태 태그의 뜻, 일을 고르는 순서, 새 항목을 쓰는 법은 [백로그 안내](README.md)에 있습니다.

- `C-11` [정리] **규칙 예외 감사(2026-10-07)에서 정한 셋의 적용** — 두 플랫폼 공통 명시적 경고 목록, `(void)` 이유 주석은 `[[nodiscard]]` 실패 가능 함수에만,
  출처를 모르는 리소스 교체.
  결정과 근거는 [결정 기록](../09_Decisions.md) 5-2.

- `C-16` [정리] **6 차 주석 작업(새 문체로 다시 쓰기)에서 처리하지 못한 구역.** 같은 방식으로 `py -3 -m Scripts worktree-make c6<이름>` 후 이어서 한다.
  Engine 은 Graphics · Renderer 의 13KB 넘는 큰 파일 약 100 개(작은 파일은 끝났다), Object · Scene · Resource · Serialization · Spatial · Common 대부분, UI 의 `Console`, Compression · Observability · Profiling · Telemetry 전부다.
  `SceneNavigation.h` · `GameObjectManager.h` 는 충돌 때문에 다시 쓰지 못했다.
  Editor 는 주석 25 줄 이상인 약 45 개(`Common/GUI`, `Commands`, `Panels/Animation` · `TileMap` · `Viewport` 등)와 그보다 짧은 파일이다. `EditorIconGlyphs.h` 는 생성 파일이라 제외한다.
  GameFramework 는 Base 의 짧은 헤더 약 230 개와 .cpp · .inl, Kits/Feature 의 옛 어휘(꼬리표, 그새, 거두다, 짓다)가 남은 약 35 개(Network · Online · Voxel 등), Kits/Feature/Online 의 Account 밖 폴더다.
  Core 는 Math, File, Process, Delegate, Memory, Diagnostics, CommandLine, Container, Task, Network 나머지, LogSink, String 이다. RuntimeAPI 는 손대지 않았다. Games 는 Empty 벤치 씬 일부와 VoxelCraft 전체다.
  '시험' → '테스트' 는 `Scripts/**` · `Test/**` · `docs/Config/**` 약 300 곳(2026-10-13 셈)과 셰이더 주석 2 곳(`waterwaveprobe.hlsl` · `lighting2d.hlsli` — 바꾸면 `App --cook-shaders` 로 다시 쿠킹)이 남았다.

- `C-17` [정리] **다음 이름 점검**(2026-10-13 이름 점검에서 남긴 것). 기준은 [AGENTS.md](../../AGENTS.md) "Class suffixes" 절과 [결정 기록](../09_Decisions.md) 5-1 이다. 아직 보지 않은 것:
  ① `Context` 접미어 타입이 무엇을 담는지, ② `*Util` 정적 클래스 중 한 타입의 메서드가 된 것이 나은 것,
  ③ `Unit` 계열(812 회), ④ 남은 지역 · 멤버 변수 단독 `value` 약 50 곳. `Settings` · `Config` · `Def` 기준은 AGENTS.md "Type suffixes".

- `C-18` [정리] **100 줄 넘는 함수 정리.** 분해는 총량을 줄이지 않는다. 중복을 먼저 없애고, 그래도 문제면 본다. 목록이 필요하면 여러 줄 시그니처를 중괄호 깊이로 측정하는 스크립트로
  뽑는다(단순 정규식은 틀린다).

- `C-57` [정리] **하위 번호가 백로그에 정의되지 않은 항목을 본문 기준으로 정리한다.** 결정 기록 · 계획 문서 · 커밋이 `D7-5.10` · `D7-5.3` · `D7-5.6` · `D5-1.*` · `D2-5.4~5.8` · `D3-8.6` 처럼
  하위 번호로 가리키는데 백로그 본문에는 그 번호가 없다(①② 같은 원 숫자만 있다). 본문의 조각에 하위 번호를 붙이거나 가리키는 쪽을 본문의 말로 바꾼다 — 린트 `CheckBacklog` 가 하위 번호까지 보게 할지는 그 뒤에 정한다.

- `G-11` [조건부] **서드파티 빈자리(2026-10-05 후보 중 사용자가 고르지 않은 것).** 리눅스 오디오 출력 없음(`XAudio2AudioBackend` 만, 리눅스는 `NullAudioBackend`) → miniaudio(퍼블릭 도메인/MIT-0) ·
  `gv_renderScale` 을 읽는 업스케일 없음 → AMD FidelityFX FSR(MIT) · 아랍어 셰이핑 없음(양방향은 `Engine/Text/TextBidi` 단순판 — 포개진 방향 제어 문자 ·
  숫자 앞뒤 기호 규칙 미지원) → HarfBuzz(MIT) + SheenBidi(Apache 2.0)
  (꽂을 자리는 `Engine/Text/ITextShaper` — 런 하나, 같은 글꼴 바이트 `IFontRasterizer::findFaceBytes` 로 `hb_face` 를 만든다. SheenBidi 는 `TextBidi` 를 대신한다). 들이면 Jolt · Recast · Tracy 처럼
  엔진 인터페이스 뒤 + 격리 게이트, vcpkg 변경은 main 에서 먼저.

- `H-1` [보류] **(보류 — 사용자 결정 "안정화된 뒤 개발") 게임별 CC0 리소스 배치 + SD 메카 테스트 게임 MechArena.** 라이선스는 **CC0 급만**(출처 표기 의무 · 재배포 금지가 붙은 것은
  넣지 않는다 — 내려받기 스크립트 우회도 안 함). 확인한 원본: VRoid 알파판 샘플 D · E · F · G · 남녀 기본(OpenGameArt `vroid-studio-cc0-models`, VRM 0.x — 툰 셰이더 ·
  VRM 임포트는 있음) · KayKit(GitHub 사용자 `KayKit-Game-Assets` 의 `*-1.0` 저장소, LICENSE.txt CC0 — Space Base · Restaurant · City Builder · Dungeon Remastered ·
  Furniture · Halloween) · 음악 OpenGameArt CC0 `jaoan`(고토) · `menu-music-2`(도장풍 루프).
  원본 배치함(2026-10-17 `res3`, 씬 · 코드 연결은 `D4-17`): StarSkirmish ← Space Base · Shooter3D ← Dungeon. NileCity ← City Builder 는 실제로 쓰지 못했다.
  남은 배치안: ThemePark ← Restaurant · Halloween · HarvestValley ← jaoan(음악만; 일본 시골 모델은 Quaternius 를 쓰지 않아 비어 있다) ·
  AbilityArena ← VRoid + menu-music-2. 프리팹 · 데이터 연결까지(안 쓰면 에셋 검증 `orphans`). 건담풍 사람형 메카는 CC0 로 쓸 만한 것이 없다(Quaternius 메카는 동물이 탄
  보행기, OGA Shock Bot 은 리얼풍) → 파이썬으로 SD 메카 부품 메시를 만들어 부품마다 뼈 하나에 붙이고 KayKit 사람형 뼈대 이름에 맞춰 KayKit 애니메이션(CC0)을 쓰는 생성기 +
  MechArena 테스트 게임(`GF_MechArena` 키트는 있고 테스트만 씀). 건담 고유 요소(V 안테나 · 얼굴 마스크 · 흰 · 파랑 · 빨강 · 노랑 배색)는 피한다.
  탈것: 말(`MountMovementComponent` · 서부극 `WesternHorseMountComponent`) · 차(`ArcadeVehicleComponent`)는 기본 도형(캡슐 말 · 상자 차)으로만 테스트했다 —
  CC0 말 메시 · 걸음새 애니메이션(Gait 0..4 · Turn)과 차 모델을 찾으면 서부극 · 카트 테스트 게임에 붙인다.
  카트 키트(`Kits/Genre/Casual/KartRacing`)의 플레이어 경로를 `ArcadeVehicleComponent::toVehicleInput`(의도 → 모터 입력)으로 바꾸는 것은 카트 테스트 게임이 생길 때
  (지금 키트는 InputMap 을 읽지 않고 `KartRace::resolveInput` 이 이미 "같은 모터에 다른 조종자" 다).
  물리 차(`WheeledVehicleComponent`)는 바퀴 메시를 바퀴 자세(조향 · 회전 · 서스펜션)로 옮기지 않는다 — 차 모델이 생기면 바퀴 자식 이름 표와 `getVehicleWheelPose` 창구를 더한다.

---

[◀ 백로그 안내](README.md)
