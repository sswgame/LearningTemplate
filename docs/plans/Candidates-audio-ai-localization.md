# 오디오 · 현지화 · AI 후보

> 상태: 후보 · 선행: 없음 · 영역: audio-ai-localization · 옛번호: D5-1, D5-2, D5-3 · 진행: 해당없음

이 문서는 착수 전 후보의 목록이다. 착수하면 절을 계획 문서로 옮기고 여기서 지운다. 오디오 엔진, 현지화, NPC AI, 게임 로직 자동화가 범위다.

## 확인 대기

사람 귀 · 실기 장치가 있어야 해서(`manual`) 검증 묶음(`Test/QA/VerifyBatch.json`)이 돌리지 못하는 확인이다. 각 절의 `매니페스트 id` 줄이 항목의 주인이다. 사람이 끝내면 항목과 절을 지운다.

### AudioExtensionsByEar
제목: 소리 그래프 · 헤드폰 · 회절 · 컨볼루션의 귀 확인
옛번호: D5-1
왜 필요한가: 오디오 확장(결정 기록 5-6)의 단위 테스트(`AudioSurroundTest` · `AudioOcclusionTest` · `AudioConvolutionTest` · `AudioSceneTest` · `AudioGraphTest` · `AudioMixerTest` · `AudioStreamTest`)는 통과했지만 소리가 맞는지는 귀로 듣는다.
남은 것: 매니페스트 id `audio-extensions-by-ear` —
- 소리 그래프: 실제 그래뉼러 소스(빗소리 · 엔진음)로 밀도 · 흔들림 음색과 RTPC 반응.
- 헤드폰: 스피커 모드 `headphones` 로 이동 음원의 앞뒤 · 옆 구분, 머리 근처를 지나갈 때 튀지 않는지.
- 회절: 방 두 개 · 문 하나 장면에서 문을 돌아 오는 방향과 먹먹함(`_diffractionVolumeDb` · `_diffractionLowPassHz` 조정).
- 컨볼루션: 실제 임펄스 응답 클립으로 음색과 7.1 에서 블록당 비용(Release 프로파일).
못 하는 이유: 소리를 귀로 판정하고 재생된 소리를 읽는 탐침이 없다. 풀리는 조건: 판정할 수 있는 탐침이 생기면 `manual` 을 떼고 시나리오로 옮긴다.

### AudioSurroundRealDevice
제목: 5.1 실기 장치의 배치 · 센터 대사 · 장치 협상
옛번호: D5-1
왜 필요한가: 5.1 결정적 렌더(`-gv_audioDumpPath` Null 렌더)와 단위 테스트(`AudioSurroundTest`)는 통과했지만 실제 장치의 채널 배치는 장치가 있어야 한다.
남은 것: 매니페스트 id `audio-surround-real-device` — 5.1 장치에서 배치(FL FR FC LFE BL BR), 이동 음원이 스피커 경계를 넘을 때 튀지 않는지, 서라운드 리버브 음색, 다채널 클립의 센터 대사 레벨, XAudio2 장치 채널 수 · 마스크 로그(`Audio device: N channels`), 스테레오 장치에서 `surround51` 이 오류로 거절되는지.
못 하는 이유: 5.1 장치와 귀가 있어야 한다. 풀리는 조건: 5.1 장치가 생기면 사람이 한 번 본다.

### AudioAlsaRealDevice
제목: 리눅스 ALSA 출력의 실기 재생
옛번호: D5-1
왜 필요한가: 리눅스 클라이언트의 출력 백엔드(`Linux/AlsaAudioBackend`)가 `libasound.so.2` 를 실행 중 열어 쓴다. 컴파일은 CI 의 리눅스 잡이 보지만 재생은 장치가 있어야 한다.
남은 것: 매니페스트 id `audio-alsa-real-device` — `libasound.so.2` 가 있는 리눅스 기계에서 스테레오 · 5.1 재생, 없는 기계에서 오프라인 렌더로 서는지.
못 하는 이유: 소리 장치가 있는 리눅스 기계가 필요하다. 풀리는 조건: 리눅스 기계에서 사람이 한 번 본다.

## 기능

(지금 후보 없음. 현지화 문화권 데이터(D5-2)는 `--check-text` 를 테스트로 만들어 지웠고, NPC 일정 후속(D5-3)은 세이브 왕복 테스트를 더하고 지웠다 — `decisions/5-7_RuntimeUI.md` · `decisions/5-8_GameFramework.md`.)

## 조건부

### AudioPlanarBufferRevert
제목: 평면 오디오 버퍼를 인터리브로 되돌릴지
옛번호: D5-1
왜 필요한가: 내부 오디오 버퍼를 평면(채널별 연속)으로 정했다(결정 기록 5-6). Debug 8 회 최솟값은 평면 전 · 후가 같았지만(312 / 349 / 470 ms → 291 / 356 / 471 ms, 보이스 32 / 64 / 128) Release 전후 비교는 아직 못 했다. U2 가 보이스 믹스를 두 단계로 바꾸고 로우패스를 패닝 앞으로 옮겨 같은 벤치로 함께 본다.
착수 조건: 매니페스트 id `audio-bench-release`(`AudioBenchTest.*` Release — Stereo · 5.1 · 7.1 렌더 비용 비율)에서 스테레오 기준(107 ms, 2026-10-15)보다 느려지면 평면 결정을 되돌려 인터리브로 한다. 보이스 위주 워크로드라 리버브와 버스 처리량은 보이스 0 · 이펙트 켠 버스만으로 따로 잰다.
