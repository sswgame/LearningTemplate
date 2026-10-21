# 오디오 엔진 확장 계획 (2026-10-21)

> 상태: 구현중 · 선행: AudioChannelLayout · 영역: audio-ai-localization · 옛번호: D5-1 · 진행: 6/8

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
| 소리 그래프 형식 (E5) | `*.audiograph.xml`(`AudioGraphDef`): 이름 붙은 노드(종류 enum) · 입력은 앞 노드 이름 목록(합) · 값은 이름과 값 목록(종류 표 `AudioGraphRegistry` 가 이름 · 범위 · 기본값). 노드는 사인 · 톱니 · 사각 · 삼각 · 잡음 · 그래뉼러 · 엔벨로프 · 로우/하이/밴드패스 · 게인. 출력은 모노 48 kHz, 길이 `_durationSeconds` 필수. | 이펙트 데이터와 같은 모양이라 검사 · 스냅샷 규칙을 한 벌로 둔다. 모노라 3D 패닝 · HRTF 가 그대로 걸린다. |
| 그래프 재생 길 (E5) | 이벤트 클립 경로에 그래프 파일을 적는다. 클립 캐시가 그래프를 굳혀 `AudioClipData::_pGraph` 로 들고, 보이스가 `AudioGraphInstance`(스트림 디코더)를 블록 길이 덩어리의 스트리밍 창으로 읽는다. | 컨테이너 · 피치 · 루프 · 페이드 · 가상화를 클립과 같은 길로 탄다(새 보이스 종류를 만들지 않는다). |
| 파라미터 갱신 (E5) | 노드 값에 게임 파라미터 + 곡선(필수)을 달면 엔진이 블록마다 그 에미터의 값을 넣는다. 로드한 라이브러리에 없는 파라미터를 읽는 그래프는 시작할 때 오류로 멈춘다. 되감기는 상태를 비우고 시각만 옮긴다. | 이벤트 RTPC 와 같은 값 · 같은 늦음(한 블록). 0 으로 읽는 폴백을 두지 않는다. |
| 그래프 편집 (E5 · E7) | 그래프 창이 파일 사본을 속성 그리드로 고치고 검사를 통과할 때만 저장한다. 엔진은 저장한 파일을 핫 리로드로 다시 굳힌다. 캔버스 편집은 E8. | 이벤트 창과 같은 저장 · 리로드 길이다. 형식이 리플렉션이라 그리드가 노드 편집기 노릇을 한다. |
| 버스별 레이아웃 | 버스 데이터 `_channels`: `Output`(출력 레이아웃) · `Stereo`. 두 채널 → 넓은 쪽은 FL FR, 넓은 쪽 → 두 채널은 BS.775. master 는 `Output` 이어야 한다. 기본 그래프의 `ui` 가 첫 사용처. | Wwise 버스 Channel Configuration 과 같다. 리버브 리턴처럼 채널 수에 비례하는 버스의 비용을 줄인다. |

형식과 계약의 정본은 코드 헤더 주석입니다: `AudioBinaural.h`, `Linux/AlsaAudioBackend.h`, `AudioRoomGraph.h` · `AudioSpatial.h`(`AudioPropagation`), `DSP/AudioFft.h` · `DSP/AudioEffect.cpp`(`AudioConvolutionEffect`),
`Graph/AudioGraphDef.h` · `Graph/AudioGraph.h`(소리 그래프), `AudioMixerDesc.h`(`AudioBusChannels`).

## 3. 구현 단위

| 단위 | 내용 | 파일 소유 | 상태 |
|---|---|---|---|
| E6 | 에디터 비용 측정: 이벤트 창(`AudioEventBrowserPanel`)의 게시 상태가 블록마다 인스턴스 × 보이스를 훑는 비용. 엔진이 블록마다 게시 시간을 재 `AudioEngineStats::_publishNanoseconds` · 최댓값으로 싣고, 이벤트 창 보이스 탭이 보이며, `AudioBenchTest.PublishCostPerInstance` 가 인스턴스 32 / 64 / 128 로 로그를 남긴다. | 측정 | 측정대기(병합 뒤 Release 벤치. 두 배에 네 배 가까이 늘면 보이스를 한 번만 훑어 인스턴스에 모은다) |
| E8 | 그래프 캔버스 편집(노드를 끌어 잇는 그래프 화면, `EditorNodeGraph`)과 노드 값의 실시간 미리 듣기(저장 없이). | 에디터 | 후보 |

끝난 단위: E1 바이노럴(HRTF), E2 리눅스 ALSA 백엔드, E3 회절, E4 컨볼루션 리버브, E5 소리 그래프(형식 · 실행기 · 노드 · 이벤트 길, 빌드 미확인), E7 그래프 창(Window > Audio Graph: 속성 그리드 편집 · 처리 순서 · 저장 · 미리 듣기)과 핫 리로드(`AudioClipCache::reloadClip`, 재생 중 보이스는 옛 그래프로 끝, 빌드 미확인), 버스별 레이아웃(빌드 미확인). 서버 호스트는 그래프 파일을 오디오로 빼고 읽지 않는다(`CookContract.json` Audio 종류).

## 4. 나중에 하는 확인 (빌드 · 사람 귀 · 다른 플랫폼)

| 확인 | 방법 |
|---|---|
| 단위 테스트 | `EngineTest --test_filter=AudioSurroundTest.*:AudioOcclusionTest.*:AudioConvolutionTest.*:AudioSceneTest.*:AudioGraphTest.*:AudioMixerTest.*:AudioStreamTest.*` |
| 소리 그래프 | 실제 그래뉼러 소스(빗소리 · 엔진음)로 밀도 · 흔들림 음색과 RTPC 반응(사람 귀), 128 보이스 그래프의 블록당 비용(Release `AudioBenchTest`). |
| 버스별 레이아웃 | 5.1 출력에서 `ui` 버스 소리가 FL FR 에만 나는지(`-gv_audioDumpPath` 채널별 에너지). |
| 헤드폰 | 스피커 모드 `headphones` 로 이동 음원의 앞뒤 · 옆 구분, 머리 근처를 지나갈 때 튀지 않는지(사람 귀). |
| 리눅스 | 리눅스 클라이언트 빌드에서 `AlsaAudioBackend.cpp` 컴파일, `libasound.so.2` 가 있는 기계에서 스테레오 · 5.1 재생, 없는 기계에서 오프라인으로 서는지. |
| 회절 | 방 두 개 · 문 하나 장면에서 문을 돌아 오는 방향과 먹먹함(데이터 `_diffractionVolumeDb` · `_diffractionLowPassHz` 조정). |
| 컨볼루션 | 실제 임펄스 응답 클립으로 음색, 7.1 에서 블록당 비용(Release 프로파일). |
