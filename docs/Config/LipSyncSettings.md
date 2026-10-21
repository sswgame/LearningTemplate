<!-- 생성 문서입니다. 손으로 고치지 말고 원본 코드를 고친 뒤 다시 만듭니다: py -3 Scripts/generate/GenerateConfigReference.py -->

# LipSyncSettings

[설정 색인](README.md) · [어디에 두나](../07_Configuration.md)

| | |
|---|---|
| 파일 | `Resource/engine/animation/lipsync.json` |
| 층 | 엔진 기본값 |
| 읽는 곳 | `LipSyncSettings::loadFromResource` (`FacialAnimationComponent::speak` · `App --import-lipsync`) |
| 언제 | 처음 말할 때 · 립싱크 임포트 |
| 배포본 | 엔진 팩에 실림 |
| 커밋 | 한다 |
| 참고 | 비즘 이름은 얼굴 리그(`*.facial.json`)의 `_listViseme` 와 맞춘다 |

JSON 키는 아래 필드 이름 그대로입니다(앞의 `_` 포함). 적지 않은 필드는 기본값입니다. 모르는 키나 읽지 못하는 값은 로드 오류입니다.

## 필드

분석 표(데이터, `engine/animation/lipsync.json`)입니다. 첫 비즘은 무음(sil)으로 씁니다. 파일은 리플렉션 구조체로 읽는다(`ConfigManager::readConfigJSON` — 모르는 키 · 읽지 못한 값 · 범위 밖 숫자는 오류). 칸 표는 PROPERTY 가 정본이다. 대역은 (낮은 Hz, 높은 Hz) 쌍이고, 비즘은 둘 이상이며 `_fallbackViseme` 은 그 가운데 하나여야 한다(`validate`).

원본: [`Source/Engine/Animation/Facial/LipSync.h`](../../Source/Engine/Animation/Facial/LipSync.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_listViseme` | `vector<LipSyncViseme>` | — |  |  | Visemes the analysis picks from (at least two; the first one is silence) |
| `_lowBandHz` | `float2` | `150.0, 600.0` |  | Hz | Low band pass range (low, high) |
| `_midBandHz` | `float2` | `600.0, 1600.0` |  | Hz | Mid band pass range (low, high) |
| `_highBandHz` | `float2` | `1600.0, 7000.0` |  | Hz | High band pass range (low, high) |
| `_frameRate` | `float32` | `30.0` | 0.0 ~ - | fps | Viseme track frames per second (greater than 0) |
| `_silenceRms` | `float32` | `0.02` | 0.0 ~ - |  | RMS below this is silence |
| `_fullRms` | `float32` | `0.25` | 0.0 ~ - |  | RMS at which the mouth is fully open (above the silence RMS) |
| `_sharpness` | `float32` | `8.0` | 0.0 ~ - |  | Sharpness of the band shape comparison (inverse softmax temperature) |
| `_formantWeight` | `float32` | `6.0` | 0.0 ~ - |  | Score a vowel viseme gains when the frame's measured formants match its _formantHz (voiced frames only) |
| `_unvoicedHighBandRatio` | `float32` | `0.5` | 0.0 ~ 1.0 |  | High band energy ratio above which a frame is unvoiced (hiss) and formants are not compared |
| `_fallbackViseme` | `hashed_string` | — |  |  | Viseme the amplitude drives when there is no track |

## `LipSyncViseme`

분석이 고르는 비즘 하나와 그 대역 형태(저, 중, 고 에너지 비율)입니다.

원본: [`Source/Engine/Animation/Facial/LipSync.h`](../../Source/Engine/Animation/Facial/LipSync.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_name` | `hashed_string` | — |  |  | Viseme name - the facial rig maps it to morph targets |
| `_bandShape` | `float3` | `0.0, 0.0, 0.0` |  |  | Energy ratio of the low, mid and high band (x, y, z) this viseme looks like |
| `_formantHz` | `float2` | `0.0, 0.0` |  | Hz | First and second formant of a vowel viseme (0,0: not a vowel - the band shape alone decides) |
