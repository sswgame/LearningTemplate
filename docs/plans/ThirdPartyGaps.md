# 서드파티 빈자리

> 상태: 구현중 · 선행: 없음 · 영역: project · 옛번호: G-11 · 진행: 4/6

2026-10-05 후보 중 사용자가 고르지 않은 셋(리눅스 오디오 출력 · 업스케일 · 아랍어 셰이핑)입니다. 2026-10-22 사용자 결정으로 착수 조건을 갈음했습니다.

## 1. 정한 것

- **리눅스 오디오 출력 — miniaudio 를 들이지 않습니다.** 후보를 적은 뒤 `AlsaAudioBackend`(`Engine/Audio/Linux/`, `libasound` 를 실행 중에 엶 — PulseAudio · PipeWire 는 ALSA 플러그인으로)가 들어와 빈자리가 닫혔습니다. 한 OS 에 출력 백엔드를 둘 두면 같은 일을 하는 길이 둘이 되므로 더하지 않습니다. ALSA 가 못 하는 요구(JACK · 장치 목록 · 핫플러그)가 생기면 그때 ALSA 백엔드를 바꿀지 따집니다.
- **아랍어 셰이핑 — HarfBuzz(셰이퍼) + SheenBidi(전체 UAX #9 수준)** 를 엔진 인터페이스 뒤에 둡니다.
  - 셰이퍼는 `ITextShaper::createDefault()` 가 빌드에 따라 고릅니다. 복합 문자 빌드는 `Text/HarfBuzz/HarfBuzzTextShaper.cpp`(면마다 `hb_font` 캐시, 같은 폰트 바이트 `findFaceBytes` 를 읽기 전용으로 빌림, RTL 결과를 논리 순서로 뒤집음), 아니면 `SimpleTextShaper` 입니다. `TextLayoutEngine` 은 `unique_ptr<ITextShaper>` 를 듭니다.
  - 양방향은 수준만 바꿉니다 — `TextBidi::resolveLevels` 가 복합 문자 빌드에서 `Text/SheenBidi/SheenBidiLevels.cpp` 를, 아니면 `resolveSimpleLevels` 를 부릅니다. 재배열(L2) · 거울 문자(L4)는 같은 코드입니다.
  - 고르는 것은 빌드 구성 `SW_ENABLE_COMPLEX_TEXT`(기본 꺼짐)입니다. 실행 중에 라이브러리가 없다고 물러나는 길은 없습니다. 켜면 vcpkg 선택 기능 `complex-text`(`-DVCPKG_MANIFEST_FEATURES=complex-text`)가 필수이고, 없으면 구성이 이유를 말하고 섭니다.
  - 격리 게이트: `lint/SharedRules.py` 의 라이브러리 표에 HarfBuzz · SheenBidi 줄, 제품 이름 표에 두 낱말을 더했습니다.
- **FSR 업스케일 — FSR 1(EASU + RCAS)을 Present 의 필터로 들였습니다.** 렌더 배율(`gv_renderScale`)은 화면 출력 뷰의 풀 크기를 줄이고(`RenderViewCollector::applyUserRenderScale`),
  Present 가 출력 크기로 키운다. 업스케일러(`gv_upscaler`, graphics.upscaler)는 그 필터다 — off 선형, spatial 은 FSR 1(`FrameRendererUpscale.cpp`, `fsr1.hlsl`).
  AMD 권고대로 톤맵 뒤 · UI 앞이라 톤맵 패스가 따로 있는 파이프라인(디퍼드)에서만 쓰고, Present 가 톤맵하는 파이프라인(포워드 후처리 체인)에서 켜면 오류를 한 번 남기고 선형으로 키운다.
  셰이더는 ffx_fsr1.h 32 비트 판을 엔진 셰이더 규칙으로 옮긴 것이고 MIT 고지는 셰이더 머리와 `ThirdParty/fidelityfx-fsr1/LICENSE.md`(서드파티 고지 파일이 모은다)에 있다.

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| T1 | 셰이퍼 · 양방향 인터페이스 뒤 구현, 구성 옵션 · vcpkg 선택 기능 · ThirdParty 래퍼 · 격리 표, 테스트 `TextShaperTest.DefaultShaperIsChosenByTheBuild` | 끝(빌드 없이 작성) |
| A1 | 리눅스 오디오 출력: 이미 `AlsaAudioBackend` — 닫음(위 1절) | 끝 |
| T2 | main 에서 `-DVCPKG_MANIFEST_FEATURES=complex-text -DSW_ENABLE_COMPLEX_TEXT=ON` 으로 설치 · 빌드(포트 이름 `sheenbidi` · 헤더 경로 `SheenBidi/SheenBidi.h` · 임포트 타깃 이름이 맞는지 확인 — 추측으로 적었다), 배포 고지(Apache 2.0) | 남음 |
| T3 | 복합 문자 빌드의 테스트: 가짜 래스터라이저는 폰트 바이트가 없어 셰이핑하지 않는다 — 텍스트 배치 · 양방향 테스트가 실제 폰트(아랍어 · 히브리어)로 도는 판, 단순 판 기대값(`TestTextBidi`)과 갈리는 자리 정리 | 남음 |
| F1 | 렌더 배율: 이미 들어와 있었다 — 뷰 풀(모든 첨부 · TAA · 깊이 히스토리)이 `출력 × 사각형 × 배율` 이다(`FrameRendererUtil::computeViewPoolExtent`, `ensureTransientResources`). 연속 배율은 첨부 나눗수가 아니라 풀 크기로 받는다 | 끝 |
| F2 | FSR 1(EASU + RCAS) — Present 단계의 필터(톤맵 뒤, 위 1절), `gv_upscaler` 가 고르고 `gv_upscalerSharpness` 가 RCAS 세기, 테스트 `RenderPassGPUTest.SpatialUpscalerReplacesTheLinearPresent`(네 백엔드 같은 값). 셰이더는 패치로 넘겨 병합 때 쿠킹한다 | 끝 |
