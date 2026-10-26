# 오디오 채널 레이아웃 계획 (2026-10-20)

> 상태: 측정대기 · 선행: 없음 · 영역: audio-ai-localization · 옛번호: D5-1 · 진행: 5/5

후보 `AudioEngineExtensions` 의 "출력: 5.1/7.1" 조각을 계획으로 올린 문서입니다(설계 검토 `design-reviewer`, 2026-10-20 반영). HRTF · 리눅스 출력 · 회절 · 컨볼루션 리버브는 [AudioEngineExtensions](AudioEngineExtensions.md) 가 이었습니다.

## 1. 왜 하는가

출력 형식이 48 kHz · 2채널 인터리브로 고정이었고 채널 수 상수를 거치지 않는 리터럴 `* 2` 가 믹서 · 보이스 · 이펙트 · 바이쿼드에 흩어져 상수만 바꾸면 컴파일은 되고 소리가 조용히 틀렸습니다. 그래서 "채널 수를 형식으로 싣는" 개편을 먼저 하고 그 위에 패너 · 이펙트 · 백엔드 협상 · 다채널 클립을 얹었습니다(U1 평면 블록, U2 패너, U3 다채널 이펙트, U4 백엔드 협상 · 설정 · 덤프, U5 다채널 클립 — 모두 끝).

## 2. 정한 것 (사용자 위임, 2026-10-20: 가장 좋은 방식을 고르기로 함)

| 결정 | 내용 | 이유 |
|---|---|---|
| 레이아웃 정본 | 장치 협상과 사용자 설정(`audio.speakerMode`). 믹서 그래프 XML 은 채널 중립 | 스피커 구성은 플레이어 기계의 사실이다. FMOD · Unity 도 같은 모양 |
| 최대 채널 | 8채널(7.1). 7.1.4 · 앰비소닉은 범위 밖 | 호출부 없는 설계를 만들지 않는다 |
| 스피커 모드 | `Auto`(장치 마스크)와 명시값. 장치가 감당하지 못하는 명시값은 오류로 멈춘다 | 폴백 금지. `Auto` 는 선언된 모드 |
| 내부 버퍼 | 평면(채널별 연속). 인터리브는 장치 경계와 클립에만 | 5절 Release 벤치로 확인한다. 스테레오 기준 느려지면 인터리브로 되돌린다 |
| 이펙트 | 종류 표에 지원 레이아웃을 선언하고 맞지 않으면 초기화 오류 | 폴백 금지 |
| 패너 | 레이아웃(채널 집합)과 패너(방향 → 게인)를 나눈다. 2D 소리는 앞 두 채널, 3D 는 쌍별 패닝, LFE 는 직접 패닝하지 않는다 | HRTF 가 같은 입력으로 같은 자리에 올라온다 |
| 다채널 클립 | ITU-R BS.775 명시 다운믹스로 스테레오로 줄인다(통째로 푼 클립 · 스트리밍 모두) | 앞 두 채널만 남기던 결함(센터 대사 소실)을 없앤다 |
| 버스별 레이아웃 | 이때는 하지 않았고 [AudioEngineExtensions](AudioEngineExtensions.md) 가 버스 데이터 `_channels`(`Output` · `Stereo`)로 이었다 | 그때는 호출부가 없었다 |

형식과 계약의 정본은 코드 헤더 주석: `AudioChannelLayout.h`, `AudioBlock.h`(평면 블록), `AudioPanner.h`, `AudioVoice.h`, `DSP/AudioEffect.h`, `IAudioService.h`(장치 협상 · `gv_audioDumpPath`), `AudioDownmix.h`. 장치 없는 서비스도 `setSpeakerMode` 하나로 레이아웃을 고른다(`createNull(layout)` 은 두지 않았다 — 길을 하나로). 알려진 위험(무관): 믹서 `applyCommands` 가 오디오 스레드에서 뮤텍스를 잡는다.

## 3. 나중에 하는 확인 (빌드 · 사람 귀)

| 확인 | 방법 |
|---|---|
| 단위 테스트 | `EngineTest --test_filter=AudioSurroundTest.*:AudioMixerTest.*:AudioSpatialTest.*:AudioDSPTest.*:AudioStreamTest.*` |
| 5.1 결정적 렌더 | `-gv_audioDumpPath=<파일>.wav` 로 Null(오프라인) 렌더를 남겨 채널별 에너지를 본다(스피커 모드 `surround51`) |
| 실제 장치 | 5.1 장치에서 배치(FL FR FC LFE BL BR), 이동 음원이 스피커 경계를 넘을 때 튀지 않는지, 서라운드 리버브 음색, 다채널 클립의 센터 대사 레벨 |
| XAudio2 | 장치 채널 수 · 마스크 로그(`Audio device: N channels`), 스테레오 장치에서 `surround51` 이 오류로 거절되는지 |

## 4. 측정

- 매니페스트 id `audio-bench-release`(Release `AudioBenchTest.*`, GPU 독점 — 평면 대 인터리브 전후 비교도 같은 실행). Stereo · 5.1 · 7.1 로 돌려 렌더 비용 비율을 본다(보이스 위주 워크로드라 리버브와 버스 처리량은 보이스 0 · 이펙트 켠 버스만으로 따로 잰다).
- 평면과 인터리브(U1): Debug 8회 최솟값이 전 312 / 349 / 470 ms, 후 291 / 356 / 471 ms(보이스 32 / 64 / 128)로 같다. **Release 전후 비교는 아직 못 했다**(기준 107 ms, 2026-10-15) — `EngineTest.exe --test_filter=AudioBenchTest.* --load_suites=only` 로 재고 스테레오 기준 느려졌으면 이 결정(평면)을 되돌린다. U2 가 보이스 믹스를 두 단계(소스 렌더 → 채널별 게인)로 바꾸고 로우패스를 패닝 앞으로 옮겼으므로 같은 벤치로 함께 본다.
- 실시간 스레드: 레이아웃은 장치를 닫은 뒤에만 바꾸고(`AudioEngine::setChannelLayout`) 렌더 중 할당은 0 이어야 한다.

## 5. 결정 대기

없음. 평면 버퍼 결정만 위 Release 벤치로 되돌릴 수 있다.
