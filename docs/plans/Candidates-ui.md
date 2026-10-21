# UI 후보

> 상태: 후보 · 선행: 없음 · 영역: ui · 옛번호: G-13, C-9, D6-4, D6-6, G-3, G-9, G-10 · 진행: 해당없음

이 문서는 착수 전 후보의 목록이다. 착수하면 절을 계획 문서로 옮기고 여기서 지운다. 런타임 UI, 글자 렌더링, 접근성, DPI 가 범위다.

## 정리

### GlyphRasterizationHitch
제목: 새 글리프 래스터화 히치
옛번호: C-9
왜 필요한가: 2026-10-17 Release 측정(PC SW, AMD Family 25, `-lang=ko`)에서 App `-gv_benchMeshes=100 -gv_benchUiHangulGlyphs=400` 탐침 프레임 `GT.Frame` max 784 ms(`GT.UI.Paint` 766 ms, `Text.GlyphsRasterized` 401), `RT.Canvas.Upload` max 4.6 ms. `GlyphBenchTest` 새 글리프 400 개 CPU p50 0.93 s(글리프당 약 2.3 ms, FreeType `sdf` 렌더러 — 외곽선에서 정확한 거리). 기준 4 ms 를 크게 넘는다.
남은 것: SDF 만 컴퓨트(점프 플러딩)로 옮겨도 커버리지 래스터화(FreeType 보통 렌더)는 CPU 에 남아 400 개가 한 프레임이면 여전히 기준을 넘는다 — 둘 다 필요하다.
- 프레임 예산: 새 글리프를 예산 안에서만 만들고 나머지는 다음 프레임에 만든다(만들어지기 전엔 그리지 않음).
- 글리프당 비용: SDF 를 비트맵 → 거리 변환(FreeType `bsdf` 또는 컴퓨트 JFA)으로 바꾼다. 먼저 `bsdf` 의 글리프당 비용과 화질을 `GlyphBenchTest` 로 재서 컴퓨트가 필요한지 정한다.
함정: 영어 문화권 대체 글꼴 사슬에는 한글 글꼴이 없어 `-lang=ko` 로 재야 한다(아니면 두부만 그려진다).

## 기능

### UiOptionsMenuVerification
제목: 옵션 · 일시정지 메뉴의 실기동 확인
옛번호: D6-4
왜 필요한가: `Engine/UI/README.md` 의 옵션 · 일시정지 메뉴 중 확인이 남았다.
남은 것:
- 키 바인딩이 재시작 뒤에도 유지되는지의 실기동 확인(Shooter3D · 패드): 두 번 기동해야 하고 사용자 설정 파일을 쓰므로 시나리오 하나로 못 한다(15 초 되돌림은 `engine/automation/displayrevert.scenario.xml` 이 본다).
- "타이틀로" 단추: 타이틀 씬(`titleScene`)을 가진 게임이 아직 없어 어느 게임에서도 접혀 있다. 타이틀 화면을 만드는 게임이 생기면 그 게임으로 확인한다.

### UiDrawPerformance
제목: 런타임 UI 그리기 성능의 남은 것
옛번호: D6-6
왜 필요한가: 목표(`GT.UI` p50 0.3 ms) 안이다(측정값과 걷기 분해는 [검증과 측정](../08_Verification.md) 2절 "런타임 UI 벤치"). 더 줄일 후보가 둘 있다.
남은 것:
- 패널마다 하위 출력 캐시(Slate Invalidation Panel).
- 조상의 `kTransform` 이면 자손까지 다시 칠하는 그림 캐시의 변환을 캐시 밖에서 곱하기.
두 후보는 `Engine/UI/README.md` "성능" 절의 세 측정(멈춘 화면 · 일부만 바뀜 · `-gv_benchUiOpacityPulse`)과 도입 기준으로 정한다. 창 크기 바꿈(전체 재배치) 한 프레임 · 첫 프레임 글리프 래스터화가 몇 프레임에 퍼지는지는 표 밖이다(워밍업 60 프레임이 버린다).

## 조건부

### MarkerComputeProjection
제목: 화면 마커의 투영 · 컬링을 컴퓨트로
옛번호: G-13
왜 필요한가: 이름표 2,000 개의 남은 게임 스레드 몫 `GT.UI.Markers` p50 0.49 ~ 0.52 ms(마커마다 월드 위치 읽기 · 투영 · 앵커 쓰기. 위치 · 배율 적용은 이미 GPU 앵커다 — [결정 기록](../09_Decisions.md) 5-7).
착수 조건: 게임 HUD 에서 마커가 2,000 개를 넘거나 이 구간이 0.5 ms 를 꾸준히 넘을 때.
남은 것: 컴퓨트가 GPUScene 변환을 읽어 앵커 표를 쓰는 모양이다(마커 ↔ GPU 인스턴스 대응이 필요하다). 데미지 숫자처럼 시간 함수로만 움직이는 UI 의 GPU 애니메이션도 같은 길이다.
함정: 측정은 Release `App -gv_benchMeshes=2000 -gv_benchUiNameplates=2000 -gv_profileFrames=600` 의 `GT.UI.Markers` · `GT.UI.Paint`.

### WorldWidgetInput
제목: 월드 공간 위젯의 입력 · 가려짐
옛번호: G-3
왜 필요한가: World 위젯은 그리기만 한다. 레이로 사각형을 맞혀 위젯 좌표로 사건을 보내는 길(언리얼 `WidgetInteractionComponent`)이 없다. 화면 마커의 "벽에 가려지면 숨김" 은 레이캐스트 질의 · 깊이 버퍼 읽기 둘 다 없다.
착수 조건: 월드 위젯에 입력이 필요해질 때(옛 계획 runtime-ui 4-6 뒤). 보일 때만 칠하기(절두체 판정)는 월드 위젯이 수십 개를 넘을 때.
함정: World 렌더 텍스처는 내용이 바뀔 때만 다시 그리지만 화면에 안 보일 때도 칠하기는 돈다.

### ScreenReaderSupport
제목: 스크린 리더
옛번호: G-9
왜 필요한가: 런타임 UI 의 접근성은 글자 크기 · 자막 · 색각 · 고대비까지다. 위젯 접근성 이름 · 역할을 OS 내레이터에 연결하는 길이 없다(언리얼 Slate 접근성 · Xbox 접근성 지침 107).
착수 조건: 접근성 요구(플랫폼 인증 등)가 생길 때.

### MsdfGlyphs
제목: MSDF 글리프
옛번호: G-10
왜 필요한가: 지금은 단일 채널 SDF(`Engine/Text/GlyphCache`, FreeType `sdf` 렌더러, 결정 R2)라 큰 글자의 모서리가 날카롭지 않다. MSDF(윤곽 모서리 칠하기 · 채널별 거리, 직접 구현)가 해결한다.
착수 조건: 큰 글자의 모서리 품질이 문제가 될 때.
함정: 아틀라스 페이지가 R8 이라 MSDF 는 RGB 페이지 · 셰이더 median 이 함께 든다.
