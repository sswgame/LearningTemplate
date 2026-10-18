# Linux 와 GPU 검증 구조의 남은 일 (2026-10-18, 백로그 C-41)

C-41 의 순서는 결정 기록 5-12 `C-41` 행입니다. 네 단계 가운데 CI 상태 알림, 셰이더 전 백엔드 확인, 요구 백엔드 정책, 소프트웨어 렌더러 묶음은 들어갔습니다.
이 문서는 남은 두 가지를 구현 단위로 나눕니다. GPU 러너와 자체 호스트 러너는 하지 않는다는 결정은 그대로입니다.

## 1. 앱 시나리오 러너의 건너뜀

### 1.1 지금 구조와 뿌리

요구 백엔드가 서지 못하면 지는 판정은 `test::reportUnusableBackend` 한 곳에 있고, `RHITestDevice` 와 `TestAppSmoke` 의 `runApp` 이 이 함수를 지납니다.
그런데 앱을 띄우는 다른 러너는 종료 코드를 직접 봅니다. `TestAppScenario.cpp` 의 다섯 곳(118, 200, 289, 363, 391 줄)과 `TestAppUI.cpp:282` 가 `AppTestUtil::isSkippedExitCode` 로 "건너뜀" 을 판정하고 로그만 남깁니다.
그래서 GPU 가 있는 PC 에서 Vulkan 이 사라져도 이 러너들은 그 백엔드를 건너뛴 채 초록입니다.

뿌리는 종료 코드를 해석하는 함수가 공개되어 있어서 러너마다 정책을 다시 정할 수 있다는 것입니다. 정책 함수를 하나 더 부르라고 고치면, 다음에 생기는 러너가 또 빠뜨릴 수 있습니다.

### 1.2 해법

- `AppTestUtil` 에 앱 실행 하나의 결과를 돌려주는 함수 `[[nodiscard]] AppRunOutcome runAppOnBackend( backend, arguments, ... )` 를 둡니다. 결과는 `enum class AppRunOutcome { Passed, Failed, BackendUnusable }` 입니다.
- 이 함수 안에서 건너뜀 종료 코드를 보면 `reportUnusableBackend` 를 부릅니다. 그 백엔드가 요구 백엔드이면 테스트 실패를 기록하고 `Failed` 를, 아니면 `BackendUnusable` 을 돌려줍니다.
- `isSkippedExitCode` 는 `AppTestUtil.cpp` 안으로 숨깁니다. 러너는 종료 코드를 직접 해석할 수 없으므로 정책을 우회할 길이 없어집니다.
- `TestAppSmoke` 의 `reportUnusableBackendSwitch` 도 같은 함수로 합칩니다.
- "아무 시나리오도 돌지 못했다" 는 건너뜀(`SW_TEST_SKIP`)은 모든 백엔드가 `BackendUnusable` 일 때만 남습니다. 요구 백엔드가 하나라도 있으면 그 전에 이미 실패입니다.

대안: 러너마다 `reportUnusableBackend` 호출을 더하는 방법은 기각합니다. 위 뿌리를 그대로 둡니다. 린트로 `isSkippedExitCode` 호출을 막는 방법도 두지 않습니다. 함수를 숨기면 형식으로 막힙니다.

## 2. CI 소프트웨어 렌더러 단계의 보고 전용 해제

### 2.1 지금 구조

`ci.yml` 의 `Test on software renderer` 단계는 `continue-on-error: true` 입니다. 실패는 주석(annotation)으로만 보이고 잡은 초록입니다.
`Test/TestPolicy.json` 의 `softwareRenderSeconds`(120)는 추정값입니다.

### 2.2 해법

- `py -3 -m Scripts ci-jobs --annotations` 로 main 의 최근 실행에서 `swrender` 주석을 봅니다. Windows(WARP) 와 Linux(lavapipe, xvfb) 두 잡이 연속 세 번 이상 초록이면 `continue-on-error` 를 지웁니다.
- 같은 실행들에서 단계 시간을 읽어 `softwareRenderSeconds` 를 고칩니다. 가장 긴 값에 여유를 두고, 그 근거(실행 번호와 측정값)를 `_softwareRenderCasesReason` 에 적습니다.
- 초록이 아니면 지는 케이스를 고치는 것이 먼저입니다. 케이스를 목록에서 빼는 것은 소프트웨어 렌더러의 한계(예: WARP 픽셀 검증)일 때만 하고 이유를 적습니다.

## 3. 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| V1 | `runAppOnBackend` 와 `AppRunOutcome`, `isSkippedExitCode` 숨김, 세 러너 이동 | `Test/AppTest/`(`AppTestUtil.*`, `TestAppScenario.cpp`, `TestAppUI.cpp`, `TestAppSmoke.cpp`) | 없음 | `-L hostgpu` 전체. 요구 백엔드를 이 PC 에 없는 이름으로 준 실행이 지는지 확인 | 없음 |
| V2 | CI 주석과 단계 시간 확인 뒤 보고 전용 해제, 초 수정 | `.github/workflows/ci.yml`, `Test/TestPolicy.json` | 없음(빌드 없음) | 다음 main CI 실행 | 필요합니다. CI 단계 시간을 세 번 이상 읽습니다 |

V1 은 테스트 코드만 바꾸므로 `worker` 하나로 충분합니다. V2 는 판단이 거의 없어 `mech-worker` 에 맡길 수 있습니다.

## 4. 사용자 결정

없습니다.
