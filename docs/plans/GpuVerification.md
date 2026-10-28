# Linux 와 GPU 검증 구조의 남은 일

> 상태: 참고 · 선행: 없음 · 영역: quality · 진행: 해당없음

GPU 가 없는 환경과 배포본(Shipping)에서 검증이 거짓 실패하거나 조용히 건너뛰지 않게 하는 구조 정리다. 기능별 테스트 · 시나리오를 새 프레임워크를 만들려고 전부 지웠으므로 아래 테스트 · 시나리오 언급은 옛 구조를 가리키며, 새 틀이 정해지면 다시 쓴다. 셰이더 전 백엔드 확인, 요구 백엔드 정책, 소프트웨어 렌더러 묶음, 앱 러너의 건너뜀은 들어갔다. GPU 러너와 자체 호스트 러너는 하지 않기로 했고 사용자 결정 대기는 없다.

## 1. 정리한 것

| 영역 | 내용 | 상태 |
|---|---|---|
| 소프트웨어 렌더러 묶음 | GPU 없는 환경에서 소프트웨어 렌더러(`SW_RHI_SOFTWARE_ADAPTER=1`)로 도는 호스트 케이스(`Test/TestPolicy.json` 의 `softwareRenderCases`) | 들어감 |
| Shipping 건너뜀 선언 | 개발 빌드가 필요한 시나리오 · 원본 리소스를 읽는 케이스는 Shipping 에서 `Skipped` 로 돌려준다 | 들어감 |
| 시나리오 문턱의 출처 | `ExpectImage` 의 문턱에 `measured`(잰 값)와 `provisional`(어림값)을 가른다 | 들어감 |
| 종료 코드와 허용 로그 | 처리 계획 없는 Warning · Error 가 종료 코드 0 으로 지나가지 않게 한다 | 들어감 |
| 명령 없는 검증 항목 | `manual` 과 `need_command` 로 이유를 가른다 | 들어감 |

## 2. 소프트웨어 렌더러 묶음

- `RHIDeviceTest.DeviceCreationAllBackends` 가 런타임 없는 백엔드(리눅스의 DX)를 만들던 것을 `RHIAvailability` 로 거른다.
- 소프트웨어 렌더러 실행은 `--required_backends=` 가 있으면 그 목록만 세운다(`test::isBackendUnderTest`). 요구하지 않는 백엔드의 환경 탓 경고를 로그를 감싸 가리지 않고 막았다.
- 리눅스 llvmpipe 는 `GL_ARB_gl_spirv` 를 내지 않아 OpenGL 이 서지 못하므로 `softwareRequiredBackends.linux` 에서 `gl` 을 뺐다(`gl` 은 GPU PC 의 hostgpu 가 본다).

## 3. Shipping 검증의 건너뜀 선언

- 시나리오의 `<Launch variable="gv_…">` 나 `-gv_` 인자가 있는데 개발 빌드가 아니면 `AppTestUtil::runScenarioArgument` 가 App 을 띄우지 않고 `Skipped` 로 돌려주고 `[ PARTSKIP ]` 줄을 남긴다.
- 원본 리소스를 읽는 케이스 21 개(EngineTest 20 · EditorTest 1)는 `SW_TEST_REQUIRES_FEATURE( SourceResources )` 를 선언한다. 새 케이스가 `Resource/game/<게임>/` 원본이나 게임 컴포넌트를 읽으면 같은 선언을 붙인다(빠뜨리면 Shipping `ctest -L nogpu` 가 실패로 보고한다).

## 4. 시나리오 문턱의 근거

측정 전에 숫자를 시나리오에 적는 흐름이 있고 출처가 형식에 없어 어림값과 잰 값이 구분되지 않았다. 형식으로 막았다.

- 모든 `ExpectImage` 는 `measured="YYYY-MM-DD 백엔드:값 …"`(잰 값) 또는 `provisional="이유"`(어림값) 중 정확히 하나를 갖는다. 아니면 시나리오 읽기 오류다(`AutomationThresholdBasis`).
- 어림값은 틀리면 실패, 맞으면 통과(0)가 아니라 미확정(14, `Unconfirmed`)이다. 보고 JSON 의 `unconfirmed[]` 가 목록이다.
- 문턱 규칙(잰 값 기준): `atLeast` 는 잰 값의 70 % 아래, `atMost` 는 1.5 배 위(0 이 기준이면 0.001), 부호 있는 값은 0 과 잰 값 사이 절반쯤. **결함의 값은 `measured` 에 넣지 않는다**(한 백엔드만 다르면 결함의 증거이므로 고친 뒤 다시 잰다).
- 잰 값으로 옮긴 시나리오: `sky` · `vfxgpulights` · `useroptions` · `fog` · `realtimegi` · `modulepass` · `killcam` · `motionblur` · `clothshowcase` · `shippingframe` · `interactionhighlight`.

같은 GPU 확인에서 찾아 고친 결함:
- **dx12 `useroptions` 시야 거리**: D3D12 PSO 서술이 0 으로 시작해 `DepthClipEnable` 이 FALSE 였다 → TRUE 로 고침(`D3D12RHIResourceFactoryPipeline.cpp`). 같은 모양(0 으로 시작하는 서술)은 형식으로 못 막았다. 래스터라이저 칸을 한 함수가 한 번에 정하게 하는 정리가 남았다.
- **gl `vfxgpulights`**: GL 의 장면 깊이는 0 행이 화면 아래인데 `vfxsimulate` · `occlusiontiles` 는 D3D 처럼 0 행이 위라고 구해 입자 라이트가 사라졌다 → `depthhistory.hlsl` 이 GL 에서 행을 뒤집어 히스토리를 D3D 순서로 만든다. 네 백엔드가 같아졌고 문턱 완화로 가리지 않았다.
- **R32F 이미지 형식 불일치**: 컴퓨트 RW 슬롯이 `rgba32f` 로 고정이라 R32F 깊이 히스토리와 GL 이미지 형식이 어긋났다 → `SW_DECLARE_RW_TEXTURE2D` 가 GL 이미지 형식을 받는다.
- **`ShippingSceneMatchesGoldenImage`(dx12)**: 골든이 낡아 다시 떴다. 다른 백엔드의 Shipping 골든은 저장소에 없고, 필요하면 `build/Ninja-Shipping/Bin` 에서 `SW_UPDATE_GOLDEN=1` 로 더 뜬다.

## 5. 종료 코드와 처리하지 않은 로그

시나리오가 종료 코드 0 으로 끝나도 로그에 Warning · Error 가 남아 아무도 고치지 않았다. 로그 판정이 줄 문구의 부분 문자열이고 허용 항목에 처리 계획이 없어, 목록을 늘리는 쪽이 고치는 쪽보다 쌌기 때문이다.

| 코드 | 이름 | 뜻 |
|---|---|---|
| 0 | `Passed` | 단언이 맞고 처리하지 않은 Warning · Error 줄이 없다 |
| 10 | `Failed` | 단언 실패 |
| 11 | `LoadError` | 시나리오 · 허용 목록 읽기 오류 |
| 12 · 13 | `TimedOut` · `Skipped` | 시간 초과 · 이 기계에서 못 돎 |
| 14 | `Unconfirmed` | 단언은 맞았으나 어림값 문턱이 있다 |
| 15 | `UnhandledLog` | 단언은 맞았으나 허용 목록에도 `<ExpectLog>` 에도 없는 Warning · Error 가 남았다(14 보다 앞선다) |

- **허용 목록은 하나다**(`Resource/engine/automation/allowedlogs.json`). 항목마다 로그 id, 범위(`scenario` 나 `suite` 중 하나), 사유, 처리 계획 문서 이름이 모두 있어야 읽힌다. 비었거나 `TODO`, 와일드카드, 중복은 읽기 오류(11)다. 시나리오 실행기와 테스트 판정기(`CaseLogJudge`)가 같은 목록을 읽는다.
- **id 없는 줄은 목록으로 가릴 수 없다.** 소스에 id 를 붙인다. 시나리오가 일부러 만드는 줄만 `<ExpectLog contains>` 로 선언한다. id 없는 조립 문장 호출이 약 2,975 개라 한꺼번에 올리지 않고, 시나리오가 그 줄을 내는 순간 실행기가 15 로 세므로 그때 해당 호출에 id 를 붙인다.
- **래칫**: 목록에 있으나 더 나오지 않는 항목, 15 로 끝난 보고는 `scenario-logs --check` 가 실패로 센다. 초안 채우기는 `scenario-logs --regen`.
- 허용 목록은 지금 비어 있다(Warning · Error 를 남기는 시나리오는 15 로 끝난다). `plan` 칸이 가리키는 문서가 실제로 있는지는 기계가 확인하지 않는다.

## 6. 명령 없는 검증 항목

- 명령 없는 항목의 이유는 `manual`(사람 눈 · 귀 · 장비 · 외부 에셋 필요, 42 개)과 `need_command`(사람이 필요 없는데 명령을 못 만듦, 0 개)로 가른다. 일회성 항목은 명령이 있거나 `manual` 이어야 하고, 명령 없는 항목은 어느 문서의 `매니페스트 id` 줄이 가리켜야 한다.
- 상태가 `측정대기` 인 계획 문서는 돌릴 명령이 있는 항목을 하나는 가리켜야 한다(`findMeasureWaitViolations`). 사람 확인만 남은 문서는 `측정대기` 로 둘 수 없다.
- `result_doc` 이 인계 · 규칙 문서를 가리키거나 `N절` 번호의 제목이 문서에 없으면 `plans --check` 가 거절한다(`findDocReferenceProblems`).
- 통과한 일회성 항목은 지웠다.
- `shooter-crowd-measure` 는 `perf-compare --cases crowdShare --runs 3` 으로 닫았다. `paired-animation-two-process` 는 러너가 아니라 씬 저작이 선행이라 `manual`([Candidates-gameplay](Candidates-gameplay.md))로 분리했다.
