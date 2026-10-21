# 서드파티 빈자리

> 상태: 구현중 · 선행: 없음 · 영역: project · 옛번호: G-11 · 진행: 2/6

2026-10-05 후보 중 사용자가 고르지 않은 셋(리눅스 오디오 출력 · 업스케일 · 아랍어 셰이핑)입니다. 2026-10-22 사용자 결정으로 착수 조건을 갈음했습니다.

## 1. 정한 것

- **리눅스 오디오 출력 — miniaudio 를 들이지 않습니다.** 후보를 적은 뒤 `AlsaAudioBackend`(`Engine/Audio/Linux/`, `libasound` 를 실행 중에 엶 — PulseAudio · PipeWire 는 ALSA 플러그인으로)가 들어와 빈자리가 닫혔습니다. 한 OS 에 출력 백엔드를 둘 두면 같은 일을 하는 길이 둘이 되므로 더하지 않습니다. ALSA 가 못 하는 요구(JACK · 장치 목록 · 핫플러그)가 생기면 그때 ALSA 백엔드를 바꿀지 따집니다.
- **아랍어 셰이핑 — HarfBuzz(셰이퍼) + SheenBidi(전체 UAX #9 수준)** 를 엔진 인터페이스 뒤에 둡니다.
  - 셰이퍼는 `ITextShaper::createDefault()` 가 빌드에 따라 고릅니다. 복합 문자 빌드는 `Text/HarfBuzz/HarfBuzzTextShaper.cpp`(면마다 `hb_font` 캐시, 같은 폰트 바이트 `findFaceBytes` 를 읽기 전용으로 빌림, RTL 결과를 논리 순서로 뒤집음), 아니면 `SimpleTextShaper` 입니다. `TextLayoutEngine` 은 `unique_ptr<ITextShaper>` 를 듭니다.
  - 양방향은 수준만 바꿉니다 — `TextBidi::resolveLevels` 가 복합 문자 빌드에서 `Text/SheenBidi/SheenBidiLevels.cpp` 를, 아니면 `resolveSimpleLevels` 를 부릅니다. 재배열(L2) · 거울 문자(L4)는 같은 코드입니다.
  - 고르는 것은 빌드 구성 `SW_ENABLE_COMPLEX_TEXT`(기본 꺼짐)입니다. 실행 중에 라이브러리가 없다고 물러나는 길은 없습니다. 켜면 vcpkg 선택 기능 `complex-text`(`-DVCPKG_MANIFEST_FEATURES=complex-text`)가 필수이고, 없으면 구성이 이유를 말하고 섭니다.
  - 격리 게이트: `lint/SharedRules.py` 의 라이브러리 표에 HarfBuzz · SheenBidi 줄, 제품 이름 표에 두 낱말을 더했습니다.
- **FSR 업스케일 — 설계만 했습니다.** 렌더러는 아직 `gv_renderScale` 을 읽지 않습니다(`UserSettingsVariables.h`). 업스케일러를 꽂으려면 그 앞 단계가 먼저입니다(아래 F1 ~ F3).

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| T1 | 셰이퍼 · 양방향 인터페이스 뒤 구현, 구성 옵션 · vcpkg 선택 기능 · ThirdParty 래퍼 · 격리 표, 테스트 `TextShaperTest.DefaultShaperIsChosenByTheBuild` | 끝(빌드 없이 작성) |
| A1 | 리눅스 오디오 출력: 이미 `AlsaAudioBackend` — 닫음(위 1절) | 끝 |
| T2 | main 에서 `-DVCPKG_MANIFEST_FEATURES=complex-text -DSW_ENABLE_COMPLEX_TEXT=ON` 으로 설치 · 빌드(포트 이름 `sheenbidi` · 헤더 경로 `SheenBidi/SheenBidi.h` · 임포트 타깃 이름이 맞는지 확인 — 추측으로 적었다), 배포 고지(Apache 2.0) | 남음 |
| T3 | 복합 문자 빌드의 테스트: 가짜 래스터라이저는 폰트 바이트가 없어 셰이핑하지 않는다 — 텍스트 배치 · 양방향 테스트가 실제 폰트(아랍어 · 히브리어)로 도는 판, 단순 판 기대값(`TestTextBidi`)과 갈리는 자리 정리 | 남음 |
| F1 | 렌더 배율: 장면 패스 첨부를 `gv_renderScale` 크기로(지금 `_resolutionDivisor` 는 1 · 2 · 4 정수뿐 — 연속 배율의 크기 정책이 필요), 뷰포트 · TAA 히스토리 크기 | 남음 |
| F2 | 업스케일 패스 종류(톤맵 앞, 배율 크기 → 출력 크기)와 FSR 1(EASU + RCAS, MIT) 셰이더를 서드파티 고지와 함께 들이기, `gv_upscaler` 가 고름 | 남음 |
