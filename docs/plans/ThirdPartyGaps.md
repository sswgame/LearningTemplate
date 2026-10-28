# 서드파티 빈자리

> 상태: 구현중 · 선행: 없음 · 영역: project · 진행: 5/7

사용자가 고르지 않은 후보 셋(리눅스 오디오 출력 · 업스케일 · 아랍어 셰이핑)을 닫는다. 리눅스 오디오는 이미 닫혔고, FSR 업스케일은 끝났다. 아랍어 셰이핑은 코드가 들어갔고 복합 문자 빌드(vcpkg 로 HarfBuzz · SheenBidi 설치)가 서는지 확인이 남았다.

## 진행

| 단위 | 내용 | 상태 |
|---|---|---|
| T1 | 셰이퍼 · 양방향 인터페이스 뒤 구현, 구성 옵션 · vcpkg 선택 기능 · ThirdParty 래퍼 | 끝(빌드 없이 작성) |
| A1 | 리눅스 오디오 출력 — 이미 `AlsaAudioBackend` 가 있어 닫음 | 끝 |
| T2 | 복합 문자 빌드 설치 · 빌드, 배포 고지(Apache 2.0) | 오버레이 포트 들어감, 설치 미확인 |
| T3 | 복합 빌드에서도 텍스트 테스트가 서도록 — 셰이퍼를 래스터라이저가 고른다, 실제 글꼴 테스트 `ComplexShapingTest` | 끝(기본 판) |
| T3b | 복합 판에서 연결형 기대(어두 · 어중 · 어말 글리프) 확인, 단순 판 기대와 갈리는 자리 정리 | 테스트 코드 작성, 돌리기 전 |
| F1 | 렌더 배율 — 뷰 풀이 `출력 × 사각형 × 배율` | 끝 |
| F2 | FSR 1(EASU + RCAS) Present 필터 | 끝 |

첫 일: `-DVCPKG_MANIFEST_FEATURES=complex-text -DSW_ENABLE_COMPLEX_TEXT=ON` 으로 설치 · 빌드를 해 보고(T2), 서면 복합 판에서 T3b 테스트를 돌린다.

## 정한 것

- **리눅스 오디오 — miniaudio 를 들이지 않는다.** `AlsaAudioBackend`(`libasound` 를 실행 중에 엶, PulseAudio · PipeWire 는 ALSA 플러그인으로)가 있고, 한 OS 에 출력 백엔드를 둘 두면 같은 일을 하는 길이 둘이 된다. ALSA 가 못 하는 요구(JACK · 장치 목록 · 핫플러그)가 생기면 그때 따진다.
- **아랍어 셰이핑 — HarfBuzz(셰이퍼) + SheenBidi(양방향 UAX #9 수준)를 엔진 인터페이스 뒤에 둔다.**
  - 셰이퍼는 면을 연 래스터라이저가 고른다(`IFontRasterizer::createShaper`). 복합 문자 빌드는 `HarfBuzzTextShaper`(면마다 `hb_font` 캐시, RTL 결과를 논리 순서로 뒤집음), 아니면 `SimpleTextShaper`.
  - 양방향은 수준만 바꾼다. `TextBidi::resolveLevels` 가 복합 빌드에서 SheenBidi, 아니면 단순 판이다. 재배열(L2) · 거울 문자(L4)는 같은 코드다.
  - **빌드 구성 `SW_ENABLE_COMPLEX_TEXT`(기본 꺼짐)가 고른다.** 실행 중에 라이브러리가 없다고 물러나는 길은 없다. 켜면 vcpkg 선택 기능 `complex-text` 가 필수이고 없으면 구성이 이유를 말하고 선다.
- **FSR 업스케일 — FSR 1(EASU + RCAS)을 Present 의 필터로 들였다.** 렌더 배율(`gv_renderScale`)이 화면 출력 뷰의 풀 크기를 줄이고 Present 가 출력 크기로 키우며, 업스케일러(`gv_upscaler`)가 그 필터다(off 선형, spatial 은 FSR 1). AMD 권고대로 톤맵 뒤 · UI 앞이라 톤맵 패스가 따로 있는 파이프라인(디퍼드)에서만 쓰고, Present 가 톤맵하는 포워드에서 켜면 오류를 한 번 남기고 선형으로 키운다. 셰이더는 ffx_fsr1.h 32 비트 판을 옮긴 것이고 MIT 고지는 셰이더 머리와 `ThirdParty/fidelityfx-fsr1/LICENSE.md` 에 있다.

## T2 — 복합 문자 빌드 설치

- 시도에서 `sheenbidi` 는 받아졌으나 `harfbuzz`(meson 포트)가 `lld-link` 를 PATH 에서 못 찾아 섰다(vcpkg 가 포트 빌드의 PATH 를 걸러 LLVM bin 이 빠진다).
- **정함:** "트리플릿이 아니라 오버레이 포트" 규칙대로 harfbuzz 만 CMake 로 짓는 오버레이 포트 `ThirdParty/harfbuzz/vcpkg-port/harfbuzz` 를 넣었다(같은 소스 · SHA512, 셰이퍼 코어만 — FreeType · ICU · GLib · 부분 집합 · 래스터 · 벡터 · GPU · 유틸리티 · 플랫폼 셰이퍼 백엔드 끔). 고지는 두 포트의 `copyright` 를 `GenerateThirdPartyNotices.py` 가 모은다.
- **남은 확인:** 오버레이 포트의 CMake 구성 · `vcpkg_cmake_config_fixup` 경로(업스트림 `CMakeLists.txt` 와 견줘 읽어 `CONFIG_PATH` · `<hb.h>` 는 맞음), `sheenbidi` 포트의 헤더 경로(`SheenBidi/SheenBidi.h`) · 임포트 타깃 이름(추측으로 적었다).
- T3b 의 테스트(`AlefJoinsFromBehChainAndBreaksTheChainAfterItself`)는 바 바 알리프 연결과 알리프 뒤 끊김을 보며, 복합 판 기대는 같은 테스트의 `#if` 분기다. 실제 글꼴은 아랍어 연결형 GSUB 를 가진 작은 글꼴 `swshapingtest.ttf`(생성기 `GenerateShapingTestFont.py`)다.
