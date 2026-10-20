# 오디오 엔진 확장 계획 (2026-10-21)

> 상태: 구현중 · 선행: AudioChannelLayout · 영역: audio-ai-localization · 옛번호: D5-1 · 진행: 4/6

후보 `AudioEngineExtensions`(`Candidates-audio-ai-localization.md`)를 착수하며 옮긴 문서입니다. 5.1/7.1 출력은 계획 [AudioChannelLayout](AudioChannelLayout.md) 이 먼저 했고, 이 문서는 그 위에 얹는 출력 · 공간 · 이펙트 확장입니다.

## 1. 왜 하는가

오디오 엔진(`Source/Engine/Audio`)은 출력이 스피커 패닝뿐이고(헤드폰 방향 단서 없음), 리눅스 클라이언트는 출력 백엔드가 없어 늘 오프라인 렌더였으며, 옆방 소리가 문을 돌아 오지 않고 벽을 뚫고 곧게 들렸고, 잔향은 알고리즘 리버브뿐이었습니다. 절차적 소리 그래프(MetaSounds 급)도 없습니다.

## 2. 정한 것 (작업자 판단, 2026-10-21 — 사용자가 "묻지 말고 상용 엔진과 우리 엔진이 발전할 방향으로" 위임)

| 결정 | 내용 | 이유 |
|---|---|---|
| HRTF 자리 | 레이아웃 `Binaural`(두 채널) + 스피커 모드 `Headphones`. 3D 모노 소리만 보이스의 HRTF 상태가 두 귀 신호를 만들고, 2D 소리 · 스테레오 클립은 그대로. `Auto` 는 헤드폰을 고르지 않는다. | Wwise 의 Headphones 패닝 규칙과 같다. 장치는 헤드폰인지 알려 주지 않는다. |
| HRTF 모델 | 측정 데이터(SOFA) 없이 Brown · Duda 구 머리 모델(ITD + 1 차 머리 그림자). | 데이터 자산과 라이선스 없이 앞뒤 · 옆 단서를 낸다. 고도 · 귓바퀴는 방향에 고도가 들어올 때 함께. |
| 리눅스 출력 | ALSA `default` 장치(PulseAudio · PipeWire 플러그인 포함)를 `libasound.so.2` 실행 중 열기로. 스트림 스레드가 `snd_pcm_writei`. | 링크 의존성을 늘리지 않고 없는 기계에서도 뜬다. vcpkg 를 바꾸지 않는다. |
| 회절 | 방 · 포털 그래프가 길(겉보기 위치 · 회절)을 낸다. 질의 결과를 `AudioPropagation` 하나로 바꾸고 포털 위치를 필수로. | Wwise Rooms & Portals 의 Diffraction 과 같은 모양. 가림 float 하나로는 길의 모양을 실을 수 없다. |
| 컨볼루션 리버브 | 이펙트 종류 `ConvolutionReverb`, 임펄스 응답은 이펙트 데이터의 `DataPath _impulse`(필수). 균일 분할 overlap-save, 3 초 상한. | 자원 경로는 데이터가 정본이다. 비용이 응답 길이에 비례해 상한을 둔다. |

형식과 계약의 정본은 코드 헤더 주석입니다: `AudioBinaural.h`, `Linux/AlsaAudioBackend.h`, `AudioRoomGraph.h` · `AudioSpatial.h`(`AudioPropagation`), `DSP/AudioFft.h` · `DSP/AudioEffect.cpp`(`AudioConvolutionEffect`).

## 3. 구현 단위

| 단위 | 내용 | 파일 소유 | 상태 |
|---|---|---|---|
| E5 | 절차적 · 그래뉼러 소리 그래프(MetaSounds 급). 노드 그래프 데이터(`*.audiograph.xml`), 오디오 스레드 그래프 실행기, 그래뉼러 · 오실레이터 · 엔벨로프 · 필터 노드, 이벤트에서 그래프 소리를 고르는 길. | `Source/Engine/Audio/Graph/*`(새) | 설계 전. 착수 때 노드 형식 · 파라미터 갱신 경로 · 에디터 편집을 먼저 정한다. |
| E6 | 에디터 비용 측정: 이벤트 창(`AudioEventBrowserPanel`)의 게시 상태가 블록마다 인스턴스 × 보이스를 훑는 비용. | 측정 | 측정대기 |

끝난 단위: E1 바이노럴(HRTF), E2 리눅스 ALSA 백엔드, E3 회절, E4 컨볼루션 리버브.

## 4. 나중에 하는 확인 (빌드 · 사람 귀 · 다른 플랫폼)

| 확인 | 방법 |
|---|---|
| 단위 테스트 | `EngineTest --test_filter=AudioSurroundTest.*:AudioOcclusionTest.*:AudioConvolutionTest.*:AudioSceneTest.*` |
| 헤드폰 | 스피커 모드 `headphones` 로 이동 음원의 앞뒤 · 옆 구분, 머리 근처를 지나갈 때 튀지 않는지(사람 귀). |
| 리눅스 | 리눅스 클라이언트 빌드에서 `AlsaAudioBackend.cpp` 컴파일, `libasound.so.2` 가 있는 기계에서 스테레오 · 5.1 재생, 없는 기계에서 오프라인으로 서는지. |
| 회절 | 방 두 개 · 문 하나 장면에서 문을 돌아 오는 방향과 먹먹함(데이터 `_diffractionVolumeDb` · `_diffractionLowPassHz` 조정). |
| 컨볼루션 | 실제 임펄스 응답 클립으로 음색, 7.1 에서 블록당 비용(Release 프로파일). |
