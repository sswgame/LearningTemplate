<!-- 생성 문서입니다. 손으로 고치지 말고 원본 코드를 고친 뒤 다시 만듭니다: py -3 Scripts/generate/GenerateConfigReference.py -->

# EngineConfig

[설정 색인](README.md) · [어디에 두나](../07_Configuration.md)

| | |
|---|---|
| 파일 | `Config/Engine/EngineConfig.json` |
| 층 | 엔진 기본값 |
| 읽는 곳 | `ConfigManager::ensureConfig<EngineConfig>` (`EngineLoop` Config 단계) |
| 언제 | 기동 · 에디터 핫 리로드(창 크기 · 백엔드처럼 기동 때만 쓰는 값은 다음 실행부터) |
| 배포본 | configure 때 exe 에 구워 넣음(`ShippingHostDefaults.h`) — 디스크의 파일은 읽지 않는다 |
| 커밋 | 한다 |

JSON 키는 아래 필드 이름 그대로입니다(앞의 `_` 포함). 적지 않은 필드는 기본값입니다. 모르는 키나 읽지 못하는 값은 로드 오류입니다.

## 필드

`Config/Engine/EngineConfig.json` 이 담는 엔진 기동 설정입니다.

원본: [`Source/Engine/Config/EngineConfig.h`](../../Source/Engine/Config/EngineConfig.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_window` | `WindowConfig` | — |  |  | 창·백엔드 설정 |
| `_maxFrameDeltaTime` | `float32` | `0.1` | 0.001 ~ - | s | 한 프레임이 인정하는 최대 가변 델타(초)입니다. 디버거 정지 같은 긴 멈춤을 잘라 냅니다. |
| `_fixedDeltaTime` | `float32` | `1.0 / 60.0` | 0.001 ~ - | s | 고정 주기 한 스텝의 길이(초)입니다. 기본은 60Hz 입니다. 고정 스텝 값의 출처는 이것 하나다 — 게임 `fixedUpdate` 와 씬 물리가 같이 쓴다(물리는 `PhysicsSettings::_subStepCount` 로 이 스텝을 나눈다, 유니티 `Time.fixedDeltaTime` 과 같다). |
| `_maxFixedStepPerFrame` | `uint32` | `6` | 1 ~ - |  | 한 프레임이 돌릴 수 있는 고정 스텝 수의 상한입니다. 상한을 넘긴 남은 시간은 버립니다. 남기면 느린 프레임이 더 많은 스텝을 불러 더 느려지는 악순환이 됩니다. FixedTimestep 이 이 값을 적용합니다. |
| `_listResourcePriority` | `vector<string>` | `game, common, engine, editor` |  |  | 리소스 팩 탐색 우선순위(앞이 먼저) |

## `WindowConfig`

주 창과 기본 렌더링 백엔드 설정입니다. 창 제목은 게임 프리셋(`GameConfig::_windowTitle`)이 정합니다.

원본: [`Source/Engine/Config/EngineConfig.h`](../../Source/Engine/Config/EngineConfig.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_clearColor` | `string` | `0.12 0.15 0.18 1.0` |  |  | 백버퍼 클리어 색(공백 또는 쉼표로 구분한 RGBA) |
| `_width` | `uint32` | `1280` | 1 ~ - |  | 클라이언트 영역 너비(픽셀) |
| `_height` | `uint32` | `720` | 1 ~ - |  | 클라이언트 영역 높이(픽셀) |
| `_defaultRHI` | `RHIBackend` | `DirectX12` |  |  | 명령줄이 고르지 않았을 때 쓸 백엔드(기본값은 쿠킹 표 `default_rhi_backend`) |
| `_bVSync` | `bool` | `false` |  |  | 수직 동기화 여부 |

## 열거 `RHIBackend`

| 값 | 설명 |
|---|---|
| `DirectX11` | Direct3D 11 |
| `DirectX12` | Direct3D 12 (Bindless) |
| `Vulkan` | Vulkan 1.3 (Bindless) |
| `OpenGL` | OpenGL 4.5+ |
