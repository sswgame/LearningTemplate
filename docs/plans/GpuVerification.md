# Linux 와 GPU 검증 구조의 남은 일 (2026-10-18)

> 상태: 구현중 · 선행: 없음 · 영역: quality · 옛번호: C-41 · 진행: 1/2

C-41 의 순서는 결정 기록 5-12 `C-41` 행입니다. 네 단계 가운데 CI 상태 알림, 셰이더 전 백엔드 확인, 요구 백엔드 정책, 소프트웨어 렌더러 묶음은 들어갔습니다.
이 문서는 남은 한 가지를 구현 단위로 나눕니다. 앱 러너의 건너뜀(V1)은 `AppTestUtil::runAppOnBackend` 로 들어갔습니다. GPU 러너와 자체 호스트 러너는 하지 않는다는 결정은 그대로입니다.

## 1. CI 소프트웨어 렌더러 단계의 보고 전용 해제

### 1.1 지금 구조

`ci.yml` 의 `Test on software renderer` 단계는 `continue-on-error: true` 입니다. 실패는 주석(annotation)으로만 보이고 잡은 초록입니다.
`Test/TestPolicy.json` 의 `softwareRenderSeconds`(120)는 추정값입니다.

2026-10-20 확인(main 의 초록 실행 39163263777 · 39162795196 · 39160434493): 단계 시간은 Windows 2~4 초, Linux 6 초(설치 4~6 초)지만 세 번 모두 진 것입니다. `continue-on-error` 가 단계 결론을 success 로 보이게 할 뿐 주석 단계가 돌았으니(`outcome == 'failure'`) 초록이 아닙니다. 주석: Linux 는 `RHIDeviceTest.DeviceCreationAllBackends`(DX11 · DX12 백엔드 없음 로그를 예상 밖 로그로 침)와 `gl` 백엔드가 서지 못함, Windows 는 `BackendSweepVisitsEveryBackendThatStandsUp`(검증 레이어 없음 · GL 4.5 / KHR_debug 없음 등 16 줄을 예상 밖 로그로 침). 그래서 V2 는 해제하지 않았습니다. 케이스를 고치거나 의도한 로그를 `SW_TEST_DEFENSIVE_SCOPE` 로 감싼 뒤 다시 셉니다.

고친 것(빌드 · CI 미확인): 근본 원인은 두 가지였습니다. (1) `DeviceCreationAllBackends` 가 이 호스트에 런타임이 없는 백엔드(리눅스의 DX)를 그대로 만들었다 — `RHIAvailability` 로 거른다. (2) 소프트웨어 렌더러 실행이 요구하지 않는 백엔드(Windows 러너의 Vulkan · GL)까지 세워 보며 환경 탓 경고를 쏟았다 — `--required_backends=` 가 있으면 그 목록만 세운다(`test::isBackendUnderTest`, `RHITestDevice` 가 따른다). 로그를 감싸 가리지 않았습니다.
남은 것: 리눅스 `gl` 은 llvmpipe 에서 디바이스 초기화가 실패합니다(원인 로그를 못 읽었다 — 주석에 `[FAILED]` 줄만 있다). 설치 단계가 `--no-install-recommends` 라 GLX 쪽 mesa 패키지(`libglx-mesa0`)가 빠졌을 수 있다는 추정뿐입니다. 다음 main 실행에서 `Test on software renderer` 의 리눅스 로그를 읽어 원인을 정한 뒤 해제합니다.

### 1.2 해법

- `py -3 -m Scripts ci-jobs --annotations` 로 main 의 최근 실행에서 `swrender` 주석을 봅니다. Windows(WARP) 와 Linux(lavapipe, xvfb) 두 잡이 연속 세 번 이상 초록이면 `continue-on-error` 를 지웁니다.
- 같은 실행들에서 단계 시간을 읽어 `softwareRenderSeconds` 를 고칩니다. 가장 긴 값에 여유를 두고, 그 근거(실행 번호와 측정값)를 `_softwareRenderCasesReason` 에 적습니다.
- 초록이 아니면 지는 케이스를 고치는 것이 먼저입니다. 케이스를 목록에서 빼는 것은 소프트웨어 렌더러의 한계(예: WARP 픽셀 검증)일 때만 하고 이유를 적습니다.

## 2. 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| V2 | 지는 케이스 수정(들어감, 미확인) → 리눅스 `gl` 원인 확인 → CI 주석과 단계 시간 확인 뒤 보고 전용 해제, 초 수정 | `.github/workflows/ci.yml`, `Test/TestPolicy.json` | 없음(빌드 없음) | 다음 main CI 실행 | 필요합니다. CI 단계 시간을 세 번 이상 읽습니다 |

V2 는 판단이 거의 없어 `mech-worker` 에 맡길 수 있습니다.

## 3. 사용자 결정

없습니다.
