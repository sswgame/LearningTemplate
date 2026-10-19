# 오디오 채널 레이아웃 계획 (2026-10-20)

> 상태: 구현중 · 선행: 없음 · 영역: audio-ai-localization · 옛번호: D5-1 · 진행: 1/5

후보 `AudioEngineExtensions`(`Candidates-audio-ai-localization.md`)의 "출력: 5.1/7.1" 조각을 계획으로 올린 문서입니다. HRTF 와 리눅스 출력 백엔드는 이 위에 얹을 후속이라 후보에 남깁니다. 설계 검토(`design-reviewer`, 2026-10-20)를 반영했습니다.

## 1. 왜 하는가

출력 형식이 48 kHz · 2채널 인터리브로 고정이었습니다. 채널 수 상수(`kChannelCount`)를 쓰는 곳은 8곳뿐인데, 상수를 거치지 않는 리터럴 `* 2` 가 믹서 · 보이스 · 이펙트 · 바이쿼드에 더 많이 흩어져 있었습니다. 상수만 바꾸면 컴파일은 되고 소리가 조용히 틀립니다. 그래서 "채널 수를 형식으로 싣는" 개편(U1, 끝)을 먼저 하고 그 위에 패너 · 이펙트 · 백엔드를 얹습니다.

## 2. 정한 것 (사용자 위임, 2026-10-20: 가장 좋은 방식을 고르기로 함)

| 결정 | 내용 | 이유 |
|---|---|---|
| 레이아웃 정본 | 장치 협상과 사용자 설정. 믹서 그래프 XML 은 채널 중립으로 둔다. | 스피커 구성은 플레이어 기계의 사실이다. FMOD · Unity 도 같은 모양이다. |
| 최대 채널 | 8채널(7.1). 7.1.4 · 앰비소닉은 범위 밖. | 호출부 없는 설계를 만들지 않는다. |
| 설정 `audio.speakerMode` | `Auto`(장치 마스크 감지)와 명시값(`Stereo` · `Surround51` · `Surround71`)을 받는다. 장치가 지원하지 못하는 명시값은 오류로 멈춘다. 몰래 낮추지 않는다. | 폴백 금지. `Auto` 는 폴백이 아니라 선언된 모드다. |
| 내부 버퍼 | 평면(채널별 연속). 기존 인터리브 클립 · 장치 경계에서만 변환한다. | 채널별 램프 · 피크 · 이펙트 상태가 N 에 무관한 단일 루프가 된다. U1 의 벤치로 확인하고, 스테레오 기준 느려지면 인터리브로 되돌린다. |
| 이펙트 | 종류 표에 "지원 채널"을 선언한다. 맞지 않으면 조용히 건너뛰지 않고 초기화 오류로 멈춘다. | 폴백 금지. |
| 패너 | 레이아웃(채널 집합)과 패너(방향 → 게인)를 분리한다. 보이스는 방향(방위 · 고도 · 퍼짐)을 내고, 스피커 패너와 이후의 HRTF 패너가 같은 입력을 받는다. LFE 는 직접 패닝하지 않는다. | HRTF 가 같은 자리에 올라온다. |
| 다채널 클립(U5) | 이번 계획에 넣는다(U1 뒤). | `AudioClip.cpp:81` 이 3채널 이상에서 앞 두 채널만 복사해 센터 대사와 서라운드가 사라지는 기존 결함이다. |
| 버스별 레이아웃 | 하지 않는다. 리버브 비용이 8채널에서 부담되면 그때 최소형을 둔다. | 지금 호출부가 없다. |

## 3. 현재 구조 요약 (검토 결과, 줄 번호는 착수 때 다시 확인)

- **U1 로 바뀐 것.** 버스 입력 · 보이스 작업 버퍼 · 믹서 출력은 채널 수를 싣는 평면 블록(`AudioBlock.h` 의 `AudioBlockView` · `AudioBlockBuffer`)이고, 채널 수는 `AudioChannelLayout`(`AudioChannelLayout.h`)에서만 나온다. `audio::kChannelCount` 는 없어졌다. `AudioMixer::initialize` · `AudioEngine::initialize` 가 레이아웃을 받고(필수 인자), 인터리브는 `AudioEngine::render` 의 장치 경계와 클립에만 남는다. 믹서 그래프 데이터 `AudioMixerDesc` 에는 여전히 채널 정보가 없다(채널 중립).
- **U1 이 스테레오 전용으로 남긴 자리(후속 단위가 지운다).** ① `AudioEngine::initialize` 는 스테레오가 아닌 레이아웃을 거절한다(보이스가 앞 두 채널에만 더하므로 — U2 가 방향 패너로 푼다). ② `AudioMixer::initialize` 는 스테레오가 아닌 레이아웃에서 이펙트가 있는 버스를 거절한다(`IAudioEffect::process` 가 스테레오 블록만 처리 — U3 가 지원 채널 표로 바꾼다). ③ 이펙트 내부 상태와 바이쿼드는 `audio::kStereoChannelCount` 를 쓴다(U3). ④ `IAudioServiceInternal::kOutputLayout` 이 스테레오로 고정이고 XAudio2 는 엔진의 레이아웃으로 장치를 연다(U4 가 협상으로). ⑤ 클립 읽기는 인터리브 1 · 2채널 그대로(U5).
- 전수 확인: 음악(`AudioMusic*`, `AudioEngineMusic.cpp`) · 룸 그래프(`AudioRoomGraph`) · 이벤트 · 공간화 · 스트리밍에는 출력 채널 수에 기댄 `* 2` 가 없었다(남은 `* 2` 는 16비트 바이트 조립과 리버브 프리딜레이 슬롯 계산이다).
- 팬은 -1 ~ 1 의 스칼라(`Source/Engine/Audio/AudioSpatial.cpp:81-98`)이고 보이스가 L/R 게인 둘로 바꾼다(`AudioVoice.cpp:123-142`).
- 백엔드는 `IAudioService::openOutput()/closeOutput()` 둘이 전부(`IAudioService.h:169-171`). XAudio2 는 `CreateMasteringVoice(kChannelCount, kSampleRate)`(`XAudio2AudioBackend.cpp:325-343`). 팩토리는 Windows 클라이언트만 XAudio2, 나머지는 Null.
- 스트리밍은 1 · 2채널만 받는다(`AudioStream.cpp:190`).
- 확인하지 못한 것: 음악(`AudioMusic*`, `AudioEngineMusic.cpp`)과 룸 그래프(`AudioRoomGraph`)의 스테레오 가정. U1 착수 때 `rg "\* 2"` 로 전수 확인한다.
- 알려진 위험: 믹서 `applyCommands` 가 오디오 스레드에서 뮤텍스를 잡는다(`AudioEngine.cpp:612-614`). 이번 일과 무관하지만 실시간 스레드 위험으로 기록한다.

## 4. 구현 단위

| 단위 | 내용 | 파일 소유 | 소리 없이 하는 검증 | 사람 귀 |
|---|---|---|---|---|
| U2 | 패너: 스칼라 pan 을 방향으로. 스테레오는 현재와 같은 등전력, 5.1 · 7.1 은 쌍별 패닝. | `AudioSpatial*` · `AudioVoice*` | 에너지 보존, 방위 스윕 연속성(게인 점프 없음), 스테레오 회귀, 레이아웃별 행렬 | 이동 음원이 스피커 경계를 넘을 때 |
| U3 | 이펙트 N채널화와 종류 표의 지원 채널. | `DSP/*` | 채널 독립성, 지원 밖 조합이 초기화 오류 | 서라운드 리버브 음색 |
| U4 | 백엔드 협상(`openOutput` 이 형식을 돌려줌), XAudio2 채널 마스크, `audio.speakerMode`, `createNull(layout)`, 다채널 WAV 덤프. | `IAudioService*` · `XAudio2*` · `NullAudioBackend.h` | Null 로 5.1 결정적 렌더, 덤프 파일 분석 | 실제 5.1 장치의 배치(FL FR FC LFE BL BR) |
| U5 | 다채널 클립 읽기: 앞 두 채널 자르기를 없애고 명시 다운믹스 행렬(ITU 0.707)을 둔다. 스트리밍도 같이. | `AudioClip*` · `AudioStream*` · `AudioClipDecoder*` | 행렬 단위 테스트, 3채널 이상 읽기 | 센터 대사 레벨 |

순서: U1(끝) 뒤에 U2 · U3 · U5 는 병렬, U4 는 독립. 단위 U1(평면 블록 형식, 동작 불변)은 끝났다 — 스테레오 렌더 해시가 전후 같았고(Debug), 형식은 `AudioBlock.h` · `AudioChannelLayout.h` 의 헤더 주석이 정본이다.

## 5. 측정

- Release `AudioBenchTest` 를 Stereo · 5.1 · 7.1 로 돌려 렌더 비용 비율을 본다. 보이스 위주 워크로드라 리버브와 버스 처리량은 보이스 0 · 이펙트 켠 버스만으로 따로 잰다.
- 평면과 인터리브 비교(U1): Debug `AudioBenchTest` 8회 최솟값이 전 312 / 349 / 470 ms, 후 291 / 356 / 471 ms(보이스 32 / 64 / 128)로 같다. **Release 전후 비교는 아직 못 했다**(기준 107 ms, 2026-10-15) — Release 빌드를 허락받은 작업이 `EngineTest.exe --test_filter=AudioBenchTest.* --load_suites=only` 로 재고, 스테레오 기준 느려졌으면 이 결정(평면)을 되돌린다.
- 실시간 스레드: 레이아웃은 장치를 닫은 뒤에만 바꾸고, 렌더 중 할당은 0 이어야 한다(버스 입력을 최대 8채널로 미리 잡거나 초기화 때 크기를 확정한다).

## 6. 결정 대기

- 없음. 위 결정은 사용자가 "제일 좋은 방식"으로 위임해 정했다. 평면 버퍼 결정만 위 Release 벤치로 되돌릴 수 있다.
