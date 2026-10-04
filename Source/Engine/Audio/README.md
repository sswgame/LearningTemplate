# 오디오 (Audio)

엔진의 오디오는 **플랫폼과 무관한 소프트웨어 믹서**(`AudioEngine`)와, 그것을 장치로 흘려 보내는 얇은 백엔드로 나뉩니다.
믹서 · 보이스 · 버스 그래프는 전부 우리 코드이고(서드파티 오디오 라이브러리 없음), 백엔드는 출력 장치를 열고 닫는 일만 합니다.

| 무엇 | 자리 |
|---|---|
| 서비스(게임이 보는 창구) — 재생 · 음악 · 볼륨 · 음소거 · 믹서 교체 | `IAudioSystem` |
| 엔진 — 명령 큐 · 보이스 칸 · 블록 렌더 · 게시 상태 | `AudioEngine` |
| 버스 그래프 데이터(`*.audiomixer.xml`) | `AudioMixerDesc` |
| 버스 그래프 실행(페이더 · 음소거/솔로 · 센드) | `AudioMixer` |
| 보이스(재생 위치 · 피치 리샘플 · 팬 · 페이드 · 가상 진행) | `AudioVoice` |
| 클립(float 샘플)과 경로 → 클립 캐시 · 비동기 디코드 | `AudioClip` |
| WAV · OGG 디코드(stb_vorbis) | `AudioClipDecoder` · `AudioVorbisDecode.cpp` |
| 버스 이펙트(바이쿼드 · 컴프레서 · 리미터 · 리버브 · 딜레이)와 이름 → 종류 등록부 | `Dsp/AudioEffect` |
| 바이쿼드 계수(RBJ cookbook) · 스테레오 상태 · 크기 응답 | `Dsp/AudioBiquad` |
| 공간화 — 리스너 · 에미터 · 감쇠 프리셋 · 팬 · 도플러 · 가림 · 가림 질의 창구 | `AudioSpatial` |
| 형식 상수 · dB 변환 · 결정적 난수 · 버스 이름 | `AudioTypes.h` |
| Windows 출력(XAudio2 스트리밍 보이스) · MP3 대체 디코더(Media Foundation) | `Windows/XAudio2System` |
| 장치 없는 출력 | `NullAudioSystem` |

## 형식과 스레드

- 믹서는 **48 kHz · 스테레오 · float32 교차** 하나로 돕니다. 클립은 읽을 때 float 로 풀고(3 채널 이상은 앞 두 채널), 재생할 때 피치와 함께 선형 보간으로
  리샘플합니다.
- 렌더는 **블록**(`audio::kBlockFrameCount` = 256 프레임, 5.3 ms) 단위입니다. 블록을 시작할 때 쌓인 명령을 적용하고, 볼륨 · 팬은 블록 안에서 샘플마다
  램프합니다(지퍼 잡음 없음). 장치 버퍼 길이와 블록 길이는 상관없습니다(`render` 가 잘라 이어 줍니다).
- **게임 스레드는 명령만 쌓고**(`playClip` · `stop` · `setBusUserVolume` …) **오디오 스레드만 엔진 상태를 만집니다.** 오디오 스레드는 장치 콜백
  (XAudio2 의 버퍼 끝 콜백)이고, 장치가 없으면 게임 스레드의 `IAudioSystem::update` 가 흐른 시간만큼 렌더합니다 — 재생이 끝나는 시각 · 가상 보이스는
  장치가 있을 때와 같이 흐릅니다. 게임 스레드가 읽는 것(`isPlaying` · `getStats`)은 블록마다 게시한 사본입니다.
- **결정적입니다.** 같은 명령 · 같은 씨앗(`setRandomSeed`)이면 같은 샘플이 나옵니다. 그래서 오디오 시험은 장치 없이 `AudioEngine` 을 만들고
  `render` 로 버퍼에 렌더해 숫자(평균 · RMS · 피크 · 한 주파수의 진폭 · 첫 소리 프레임)로 잽니다(`Test/EngineTest/AudioTestUtil.h`).

## 버스 그래프 (믹서)

언리얼 서브믹스 · Wwise 버스 계층 · FMOD 그룹 버스의 자리입니다. 데이터는 `engine/audio/default.audiomixer.xml`(없으면 `AudioMixerDesc::makeDefault`),
게임은 `IAudioSystem::loadMixer` 로 바꿉니다(살아 있는 보이스는 같은 이름의 버스로 옮겨 갑니다).

```xml
<AudioMixerDesc _maxVoiceCount="256" _maxRealVoiceCount="64" _inaudibleDb="-60">
	<_listBus>
		<AudioBusDesc _name="master" />
		<AudioBusDesc _name="sfx" _parent="master" _volumeDb="-3">
			<_listSend><AudioSendDesc _bus="reverb" _levelDb="-14" _bPreFader="false" /></_listSend>
		</AudioBusDesc>
	</_listBus>
</AudioMixerDesc>
```

- 루트는 `master` 하나입니다. 부모 · 센드 대상은 이름으로 적고, 모르는 이름 · 겹친 이름 · 고리(부모와 센드를 간선으로 본 위상 정렬이 실패)는 **읽기 오류**입니다.
- 처리 순서는 위상 순서(보내는 버스 먼저)입니다. 버스마다: 입력(보이스 · 자식 · 센드의 합) → 프리 페이더 센드 → 페이더 → 포스트 페이더 센드 → 부모.
- **페이더 게인 = 데이터 볼륨(dB) × 사용자 볼륨 × 스냅샷 오프셋(dB) × 음소거/솔로.** 사용자 볼륨은 설정 메뉴(`audio.busVolume` 의 `master` · `music` ·
  `sfx` · `voice` · `ambient` · `ui`)가 `IAudioSystem::setBusVolume` 으로 넣고, 그래프를 바꿔도 이름으로 남습니다. 음소거(`setMute`)는 master 한 곳입니다.
- 솔로: 하나라도 솔로면 솔로 버스의 조상(신호가 지나는 길)과 자손만 소리를 냅니다(DAW · Wwise 와 같음).

## 이펙트

버스마다 인서트 체인(`AudioBusDesc::_listEffect`)이 있고, 센드 · 페이더 앞에서 순서대로 돕니다. 종류는 `AudioEffectRegistry` 의 표 한 줄씩이고
데이터는 종류 이름과 파라미터 이름으로만 고릅니다 — 모르는 종류 · 파라미터 · 같은 버스에서 겹친 이펙트 이름은 읽기 오류입니다.

| 종류 | 파라미터(기본) | 메모 |
|---|---|---|
| `LowPass` · `HighPass` · `BandPass` · `Peaking` · `LowShelf` · `HighShelf` | `frequencyHz` · `q`(0.707) · `gainDb` | RBJ cookbook 2 차. LowPass 20 kHz · HighPass 10 Hz 는 건너뜀(스냅샷이 내려 쓰는 필터를 평소엔 공짜로) |
| `Compressor` | `thresholdDb`(-18) · `ratio`(4) · `attackMs`(10) · `releaseMs`(120) · `kneeDb`(6) · `makeupDb`(0) | 피드 포워드, 스테레오 링크 피크, 소프트 니, smooth decoupled peak 검출 |
| `Limiter` | `ceilingDb`(-1) · `releaseMs`(60) · `lookaheadMs`(2) | 미리 보기 브릭월: 필요 게인의 창(L+1) 최솟값을 L 상자 평균으로 다듬고 소리를 L 늦춘다 — 출력이 천장을 넘지 않는다 |
| `Reverb` | `roomSize`(0.7) · `damping`(0.5) · `wet`(0.33) · `dry`(0) · `width`(1) · `preDelayMs`(0) | Freeverb(콤 8 + 올패스 4, 채널 벌림 23) — 48 kHz 로 늘린 길이 |
| `Delay` | `timeMs`(250) · `feedback`(0.35) · `wet`(0.5) · `dry`(1) · `dampingHz`(20000) | 피드백 경로에 한 극 로우패스 |

기본 그래프는 master 에 리미터(-1 dBFS), `reverb` 리턴 버스에 리버브(dry 0)를 두고 sfx · voice · ambient 가 센드로 보냅니다.
이펙트 이름(`AudioEffectDesc::_name`, 비우면 종류 이름)은 스냅샷이 파라미터를 바꿀 때 씁니다.

## 공간화

보이스가 에미터(`_emitterId`)와 감쇠 프리셋(`_attenuation`, 믹서 데이터의 `_listAttenuation` — Wwise Attenuation ShareSet · 언리얼 Sound Attenuation)을
가지면 블록마다 리스너에 대해 공간화합니다. 둘 중 하나라도 없으면 2D(공간화 없음)입니다.

- **리스너** `setListener( index, AudioListenerState )` — 최대 4 개(분할 화면). 여럿이면 보이스마다 가장 크게 들리는 리스너로 잽니다(언리얼 · Wwise 의
  "가장 가까운 리스너"). 리스너를 섞어 내는 다중 리스너 믹스는 없습니다.
- **팬** — 3D(`World3D`): 수평면 위 방향의 오른쪽 성분(오른쪽 = 위 × 앞, 왼손 좌표). 고도는 팬에 쓰지 않습니다. 리스너에 아주 가까우면(최소 거리의 절반 안)
  가운데로 모읍니다. 모노는 등전력(가운데 -3 dB, L² + R² 일정), 스테레오는 밸런스. **HRTF(바이노럴) · 서라운드 출력은 없습니다** — 출력은 스테레오입니다.
- **2D**(`Screen2D`): 팬 = 가로 거리 / 화면 반폭(`_screenHalfWidth`), 세로는 팬에 쓰지 않음(고도 없음), 거리는 XY 거리(Z 는 레이어라 무시).
- **거리 감쇠** — `Linear` · `Inverse`(OpenAL inverse clamped) · `InverseSquare` · `Custom`(거리, dB 점). 공기 흡수는 (거리, Hz) 점의 보이스 로우패스.
- **도플러** — 비 = (c − v_리스너·u) / (c − v_소스·u), u 는 소스 → 리스너, 속도는 c/2 안으로, 비는 [0.5, 2] 로 묶고 프리셋의 세기(`_dopplerFactor`, 0 = 끔)로 줄입니다.
- **가림(occlusion)** — 게임 스레드가 0..1 을 재서(`IAudioOcclusionQuery`, 엔진 기본은 물리 씬 레이캐스트) `setEmitterOcclusion` 으로 넣고, 엔진은
  `_occlusion._smoothingSeconds` 로 따라가며 볼륨(`_volumeDb` × 값)과 로우패스(20 kHz → `_lowPassHz`, 로그 축)로 바꿉니다. 프리셋의 `_bOcclusion` 이 끄면 무시.
  가림 · 막힘(obstruction)을 따로 두지 않습니다 — 한 값입니다.

## 재생

```cpp
IAudioSystem* pAudio = game::getService<IAudioSystem>();
pAudio->play( "game/x/sounds/click.ogg", hashed_string( "ui" ) );   // 버스를 고른다(비우면 sfx)
pAudio->playMusic( "game/x/music/theme.ogg" );                       // music 버스 루프, 다른 곡이면 0.25 초 크로스페이드
AudioEngine& engine = pAudio->getEngine();                           // 클립 · 이벤트 · 파라미터 · 스냅샷 · 음악 · 공간화
```

## 플랫폼

- **Windows**: `XAudio2System` — 마스터링 보이스 + float32 스테레오 48 kHz 소스 보이스 하나. 512 프레임 버퍼 셋을 돌려 쓰고, 버퍼가 끝날 때마다
  XAudio2 처리 스레드에서 다음 버퍼를 렌더해 제출합니다(지연 ≈ 21 ms + 블록). 장치를 못 열면 오프라인으로 돕니다(로그 `offline - no device`).
- **Linux**: 출력 백엔드가 없습니다 — `NullAudioSystem`(오프라인 렌더). 엔진 코드는 플랫폼 분기가 없습니다.
- 공통 디코더(WAV · OGG)가 못 읽는 형식(MP3 · ADPCM WAV)은 백엔드의 대체 디코더(`AudioClipStore::setFallbackDecoder`, Windows 는 Media Foundation)가 풉니다.
