# Test (자동화 테스트 스위트)

> **[🏠 위키 홈으로 돌아가기](../README.md)** | **[📖 서브시스템 목록](../docs/02_EngineSubsystems.md)**
> ---

엔진과 코어 프레임워크의 안정성을 보장하기 위한 자동화된 테스트 코드들이 모여있는 디렉터리입니다.
테스트는 각 역할과 종속성에 따라 7개의 메인 프로젝트와 1개의 프레임워크로 명확하게 분리되어 있습니다.
**타깃이 곧 경계다** — 어떤 의존성을 링크하느냐로 갈라 두었으므로, 새 테스트는 그 의존성이
이미 있는 타깃에 넣는다(없으면 작은 타깃을 하나 더 둔다. `EditorUiTest`·`AppTest` 가 그렇게 생겼다).

## 📁 테스트 프로젝트 구조

| 폴더명 | 테스트 성격 | 주요 특징 |
|---|---|---|
| **`CoreTest`** | 순수 코어 유닛 테스트 | `Source/Core` 만 시험합니다 — `Math`, `String`, `DataStructure`, `Delegate`, `Event`, `GlobalVariable` 등. 시험 파일은 Engine · GameFramework · Editor · Games · App 헤더를 include 하지 않고 `engine::` 서비스를 부르지 않습니다(`CheckTestSuites` 규칙 6 — 공용 `TestFramework` 가 Engine 을 링크하므로 include 경로로는 막을 수 없어 게이트가 지킵니다). 엔진 타입이 필요하면 지역 대역(이벤트 · `GlobalVariableManager`)을 쓰거나 `EngineTest` 에 둡니다. |
| **`EngineTest`** | 엔진 · 게임 프레임워크 유닛 테스트 | `GameObject`, `Scene`, `RHI`, `Material` 등 실제 엔진 객체들과, `GameFramework` · 장르 킷(`GF_Overworld` · `GF_TurnBattle` · `GF_ActionCombat` · `GF_Farming` · `GF_Shooter` · `GF_ThemePark` · `GF_Voxel`)을 함께 검증합니다 — 그 라이브러리들을 링크하는 실행 파일이 이것 하나라서입니다. 게임 서비스 바인딩 가드는 `EngineTest/GameTestUtil.h`. GPU 가 필요한 스위트는 `_HostOnly` 로 갈립니다(아래). |
| **`ReflectionTest`** | 빌드 파이프라인(툴체인) 테스트 | 런타임 코드가 아닌, C++ 헤더를 분석하여 `*.gen.cpp`를 올바르게 자동 생성해 내는지 `ReflectionParser` 툴의 기능을 검증합니다. |
| **`SmokeTest`** | 런타임 모듈 통합 스모크 테스트 | 게임 DLL 핫 리로드(`LiveReloadManager`)나 RHI 모듈 동적 로드 등 시스템 전체가 런타임에 제대로 맞물려 돌아가는지를 검증합니다. |
| **`EditorTest`** | 에디터 로직 유닛 테스트 | 커맨드 스택·선택·뷰포트 수학·문서 dirty 계약 등 `EditorModule` 의 UI 없는 부분을 검증합니다. ImGui 렌더링은 타지 않습니다. |
| **`EditorUiTest`** | ImGui 컨텍스트가 필요한 에디터 테스트 | `EditorTest` 는 **일부러 ImGui 를 링크하지 않는다** — 그 경계를 지키면서 컨텍스트만 있으면 도는 것(플랫폼 백엔드의 부분 초기화 수습 등)을 여기 담습니다. GPU·창이 필요한 것은 넣지 않습니다. |
| **`AppTest`** | App(런처) 로직 + **실기동 스모크** | `App` 은 실행 파일이라 링크할 라이브러리가 없어, **소스를 파일 단위로 가져와** 혼자 도는 정책을 검증합니다(`FrameTimeline`, `nogpu`). 여기에 더해 `AppSmokeTest` 가 **진짜 `App.exe` 를 네 백엔드 × 에디터 유무로 띄워** 종료 코드와 `[Error]` 를 봅니다(`hostgpu`) — `EngineLoop` 을 돌리는 유일한 자동 그물입니다. |
| **`TestFramework`** | 테스트 공통 프레임워크 | 테스트 등록/실행을 조정하고, `TestContext`(결과 수집)와 `TestFilter`(CLI/glob 선택)를 재사용 가능한 구성요소로 제공합니다. |


## 🚀 테스트 실행 방법

CMake를 통해 구성(Configure)한 후, 다음과 같이 테스트를 실행할 수 있습니다.

### 모든 테스트 일괄 실행
```powershell
ctest --test-dir build/Ninja-Debug --output-on-failure
```

> **직접 실행할 때는 작업 폴더가 `build/<preset>/Bin` 이어야 한다.** 테스트는 현재 폴더에서 위로 올라가며
> `Resource/` 를 찾는데, 그 탐색이 성공하는 자리가 `Bin` 이다(ctest 도 거기서 돌린다). **Shipping 은
> 바이너리가 `TestBin` 에 있지만 작업 폴더는 여전히 `Bin` 이다** — 배포용 `Bin` 에 테스트 바이너리와
> DXC 가 섞이지 않게 하려고 출력만 가른 것이다.
>
> ```powershell
> cd build/Ninja-Shipping/Bin
> ../TestBin/EngineTest.exe --test_filter=MaterialTest.*
> ```
>
> `TestBin` 에서 그냥 돌리면 리소스 루트를 못 찾는다. 예전엔 그 상태로 계속 달리다 세그폴트했다 —
> `SW_LOG_ASSERT` 는 배포본에서 사라지지 않지만 **브레이크 없이 로그만 남기고 진행** 하기 때문이다.
> 지금은 `ResourceUtil::initialize()` 를 쓰는 케이스가 전부 `SW_ASSERT_TRUE` 로 감싸 그 자리에서 실패한다.

### 특정 테스트만 골라서 실행 (Label 활용)
라벨은 `core`, `editor`, `engine`, `app`, `reflection`, `module`, `unit`, `nogpu`, `hostgpu`, `lint` 입니다.
`lint` 는 `Scripts/lint/gate/` 의 게이트와 `Scripts/lint/selftest/` 의 자기 검사 전부입니다. 목록은 어디에도 손으로 적지 않습니다 —
구성 시점에 `Scripts/lint/LintCatalog.py` 가 두 폴더를 훑고 `Scripts/generate/GenerateLintTargets.py` 가 CTest 항목을 만듭니다
(`sw_registerLintTests`, `cmake/Engine/AssetAndToolTargets.cmake`). 지금 무엇이 도는지는 `ctest --preset Ninja-Debug-lint -N` 으로 봅니다.

```powershell
# GPU 없이 도는 것만 (CI 와 같은 집합)
ctest --test-dir build/Ninja-Debug -L nogpu --output-on-failure
# 순수 Core 만
ctest --test-dir build/Ninja-Debug -L core
# Python 정적 lint 만 (프리셋도 있다)
ctest --preset Ninja-Debug-lint
```

> **`nogpu` 는 "CI 러너가 돌릴 수 있다"는 계약이다.** 실제 GPU·디스플레이·DXC 가 필요한 스위트는 자기 파일에서
> `SW_TEST_REQUIRES_HOST( 스위트, "이유" )` 로 선언하고, `HOST_SPLIT` 으로 등록한 실행 파일(`EngineTest` · `AppTest`)이
> 그 선언으로 ctest 항목을 가른다 — `<타깃>_NoGPU` 는 `--host_suites=exclude`, `<타깃>_HostOnly` 는 `--host_suites=only`.
> 지금 선언된 것은 `EngineTest --test_list` 끝에 이유와 함께 찍힌다. 디바이스가 필요한 테스트를 새로 쓰면
> **`RenderPassGpuTest` 에 넣고** 창 + 디바이스는 `test::RHITestDevice`(`Test/EngineTest/RHITestDevice.h`)로 세운다 — 스코프를
> 벗어나면 내려가므로 단언으로 일찍 빠져도 다음 케이스에 남지 않는다. (이름을 하나씩 필터에 적던 시절에는 새 테스트가
> 규칙을 비켜가 CI 가 나흘간 빨갛게 있었다.)

> **호스트 스위트의 케이스는 예상 밖 `[Error]` 로그 하나로 진다.** 검증 레이어 · 드라이버 오류는 Error 로그로만 남고 단언은 통과할 수
> 있기 때문이다. 일부러 거절 경로를 부르는 자리는 `SW_TEST_DEFENSIVE_SCOPE( "이유" )` 로 감싼다(그 안의 Error 는 표식이 붙어 세지 않는다).
> 원인이 엔진에 있고 아직 못 고친 Error 는 그 스위트 파일에 `SW_TEST_KNOWN_ERROR_LOG( 스위트, "문구", "이유" )` 로 적는다 — 그 스위트 ·
> 그 문구만 견디고, 실행 요약이 견딘 줄 수를 찍으며, 한 번도 안 나오면 지우라고 알린다. 고쳤으면 그 줄을 지운다.

> **골든 이미지** — `AppSmokeTest.BenchFrameMatchesGoldenImage` 가 벤치 큐브 장면(`-W=256 -H=144 -gv_benchMeshes=8 -gv_benchAnimate=0`,
> 불투명 · 투명 두 장)을 백엔드마다 찍어 `Test/AppTest/Golden/<장면>_<백엔드>.ppm.z`(Zlib `CompressionStream` 으로 감싼 PPM)와
> 채널 차이 2 까지 비교한다. 기준은 백엔드마다 하나다(윈도우 드라이버가 그린 것 — 리눅스는 `.linux` 를 붙인 이름을 따로 뜨고, 없으면
> 경고만 남긴다). 배포본은 링크한 백엔드 하나의 기준과 비교한다. 그림이 **의도해서** 바뀌었으면
> `SW_UPDATE_GOLDEN=1` 로 그 케이스를 돌려 기준을 다시 뜨고, 바뀐 `.ppm.z` 를 같은 커밋에 넣는다. 진 그림은 `build/<프리셋>/GoldenDiff` 에 남는다.

### 이름만으로 돌리기 — `RunTests.py`

케이스 하나를 돌리려면 세 가지를 알아야 했다: 그 스위트가 **어느 실행 파일**에 사는지, 작업 폴더가 **`Bin`** 이어야 한다는 것,
Shipping 은 실행 파일이 **`TestBin`** 에 있다는 것. `Scripts/dev/RunTests.py` 가 셋을 대신 안다 — 실행 파일마다
`--test_list` 로 물어 패턴이 고르는 케이스가 있는 것만, `Bin` 에서 돌린다.

```powershell
py -3 -m Scripts test SceneTest.*                                    # = py -3 Scripts/dev/RunTests.py SceneTest.*
py -3 -m Scripts test "SceneTest.*,ResourceTest.Ensure*" --list      # 어느 실행 파일에 어떤 케이스가 있나
py -3 -m Scripts test ResourceTest.* --preset Ninja-Shipping
py -3 -m Scripts test ProcessTest.* --repeat 20 --shuffle            # 아래 두 플래그로 넘어간다
```

이 스크립트가 모르는 인자(`--host_suites=all` 같은)는 실행 파일에 그대로 간다. 고른 케이스가 어디에도 없으면 1 로 끝난다
(오타 난 패턴이 "통과" 로 보이지 않게).

### 간헐 실패 · 순서 의존 찾기 — 되풀이와 섞기

| 플래그 | 하는 일 |
| --- | --- |
| `--test_repeat=N` (`--gtest_repeat=N`) | 고른 케이스를 N 번 되풀이한다. 진 케이스는 몇 번째 회차였는지 함께 찍힌다. |
| `--test_shuffle` (`--gtest_shuffle`) | 스위트 순서와 스위트 안의 케이스 순서를 섞는다(gtest 와 같다 — 스위트는 붙어 있다). 쓴 씨앗을 맨 앞에 찍는다. |
| `--test_shuffle=<씨앗>` (`--gtest_random_seed=<씨앗>`) | 그 순서를 다시 만든다. 섞어서 진 실행은 끝에 다시 돌릴 플래그를 찍는다. |

끝 요약에는 오래 걸린 케이스 다섯이 찍힌다(`Slowest cases:`) — 테스트가 느려지는 것은 조용히 일어난다.

```powershell
cd build/Ninja-Debug/Bin
./CoreTest.exe --test_filter=ProcessTest.* --test_repeat=50   # 간헐 실패
./EngineTest.exe --test_shuffle                               # 앞 케이스가 남긴 상태에 기대는 테스트
./EngineTest.exe --test_shuffle=81234                         # 진 순서 다시 만들기
```

### 구성마다 도는 케이스 수가 다르다

`ctest` 는 어느 구성에서든 똑같이 "Passed" 라고만 말한다. 실제로 도는 양은 이렇게 다르다
(2026-10-01 실측 — 등록된 케이스 / 그중 스킵, 호스트 스위트 포함):

| 실행 파일 | Debug | Shipping |
| --- | ---: | ---: |
| CoreTest | 328 / 4 | 328 / 10 |
| EngineTest | 726 / 0 | 720 / 3 |
| ReflectionTest | 132 / 0 | 132 / 8 |
| **SmokeTest** | **36 / 1** | **2 / 0** |
| EditorTest | 78 / 0 | 78 / 0 |
| EditorUiTest | 2 / 0 | 2 / 0 |
| **AppTest** | **6 / 0** | **5 / 0** |

SmokeTest 가 36 → 2 가 되는 것은 **의도된 것이다.** 핫 리로드와 모듈 백그라운드 컴파일은 Dev 에만 있고,
Shipping 스모크는 정적 `fillGameAPI` 경로만 본다(`Test/SmokeTest/CMakeLists.txt` 참고). AppTest 가 6 → 5 인 것도
같은 이유다 — 에디터 실기동 케이스는 배포본에 에디터가 없어 아예 컴파일되지 않는다.
스킵도 구성을 탄다. **어느 구성에서나 스킵되는 것은 "자식 역할" 케이스다** — 환경 변수가 없으면 스스로 빠지고, 다른 케이스가
자기 자신을 자식 프로세스로 띄울 때만 돈다(`CrashReportTest.ChildProcessCrashesAsRequested` · `TestFrameworkTest.ChildRoleEchoesOrHangs` ·
`ModuleApiTest.SharedModuleChildKeepsItsRegistrations`). Debug 의 CoreTest 는 둘이 더 빠진다 — `SW_ASSERT` 가 프로세스를 세우는
구성이라, 단언이 사라진 구성에서만 뜻이 있는 방어 경로 검사다. Shipping 은 `SW_LOG_*` 가 컴파일에서 빠져 로그 검사가,
ReflectionTest 는 Dev 전용 진단 경로와 배포본에 없는 메타데이터 검사가 빠진다.

**의도한 축소와 사고를 가르는 선은 하나다: 스위트가 통째로 비면 실패한다.**
필터로 고른 스위트의 케이스가 **전부 스킵되면** 그 실행은 아무것도 검증하지 않은 것이므로 프레임워크가
실패로 돌린다. DXC 가 사라지거나 구운 셰이더가 없어지면 예전에는 조용히 초록이었다. 정말 그래도 되는
실행이면 `--allow_empty_suite` 를 준다.

### 스위트 이름 규칙 — `CheckTestSuites.py` 가 강제합니다

스위트 이름은 장식이 아닙니다. CI 가 못 돌리는 것은 **스위트 단위로** 선언되어 빠지고 `--test_filter` 도
스위트 단위로 고르므로, 이름이 흔들리면 그 둘이 흔들립니다. 스위트 규칙은 넷이고, 같은 게이트가 둘을 더 봅니다 —
`EditorTest` 가 손으로 나열한 Editor 소스가 살아 있고 ImGui 를 include 하지 않는지, `CoreTest` 가 엔진을 직접 쓰지 않는지(위 표).

1. 스위트 이름은 **`XxxTest`** — 대문자로 시작하고 `Test` 로 끝나며 밑줄이 없습니다.
   계층 접두어(`Core_` · `Engine_`)는 붙이지 않습니다. **실행 파일 이름이 이미 그 말을 합니다.**
2. 한 스위트는 **한 파일에만** 삽니다.
3. CI 가 못 돌리는 스위트는 자기 파일에서 이유와 함께 선언합니다. **이 한 줄이 분류의 전부입니다** — CMake 에
   스위트 이름을 적는 곳이 없습니다. 린트는 선언이 효력이 있는지만 봅니다: 그 스위트가 그 파일에 있는가,
   그 폴더의 실행 파일이 `HOST_SPLIT` 으로 등록되는가.

   ```cpp
   // 진짜 창을 만든다. 헤드리스 CI 러너엔 디스플레이가 없다.
   SW_TEST_REQUIRES_HOST( WindowTest, "creates a real OS window; headless CI runners have no display" );
   ```
4. 그런 스위트가 있는 파일에는 **다른 스위트를 두지 않습니다.** 섞여 있으면 새 케이스를 옆 스위트에
   붙이기 쉽고, 그 순간 GPU 가 필요한 케이스가 CI 로 들어갑니다.

### 단언

`SW_EXPECT_*` 는 실패를 기록하고 계속 가고, `SW_ASSERT_*` 는 기록하고 그 케이스(또는 그 함수)를 끝냅니다 — 뒤 줄이
앞 줄의 결과에 기대는 자리(널 포인터를 바로 쓰는 자리)에만 ASSERT 를 씁니다. 전부 한 뼈대(`SW_TEST_CHECK_IMPL` ·
`SW_TEST_CHECK_EQUAL_IMPL`)를 지나고, **실패 경로는 바깥 함수**(`test::reportFailure` · `reportNotEqual` …)라 단언 자리에는
비교와 호출 하나만 남습니다. 새 단언은 그 뼈대로 한 줄입니다.

자기 자신을 자식 프로세스로 다시 띄워 케이스 하나만 돌릴 때(프로세스마다 한 번뿐인 상태 · 프로세스를 죽이는 일)는
`test::runThisExecutableAsChild`(`TestFramework/TestChildProcess.h`)를 씁니다 — 환경 변수를 띄우는 동안만 걸고, 출력을 모으고,
**시한을 넘기면 자식을 죽입니다.** 시한 없이 기다리면 멈춘 자식 하나가 실행 파일 전체를 CTest 시한까지 세워 둡니다.

단언 자체를 시험할 때는 `test::ScopedFailureCapture` 로 실패를 가로챕니다(gtest 의 `EXPECT_FATAL_FAILURE` 와 같은 일).
`Test/CoreTest/TestTestFramework.cpp` 가 매크로마다 "실패를 하나 남기는가 · 무엇이라 찍는가 · ASSERT 가 멈추는가" 를 봅니다.

### 테스트 상태 정리

각 테스트가 종료되면 프레임워크가 비동기 씬 로드와 TaskManager를 정리합니다. 그 밖에 테스트가
직접 만든 것(코덱 등록·전역 설정)은 `SW_TEST_DEFER_CLEANUP` 으로 되돌립니다.

**임시 파일은 정리하지 않습니다 — 프레임워크가 합니다.** `test::makeTempPath( "x.xml" )` 는
`<임시 폴더>/sw_<pid>/<스위트_케이스>/x.xml` 을 주고, 케이스가 끝나면(정리 함수가 돈 뒤) 그 케이스 폴더를 통째로
지웁니다. 엔진이 옆에 구운 `.bin` · `.meta` 도 같이 지워집니다. 파일 이름이 뜻을 갖는 경우(팩 이름이 우선순위를
정한다)는 `test::makeTempDirectory( "packs" )` 안에 원래 이름으로 둡니다. 못 지우면(파일을 연 채로 둔 경우) 그 케이스가
집니다. **실행 파일 폴더(`Bin`)나 소스 트리에 쓰지 않습니다** — 같은 `Bin` 을 쓰는 다른 프로세스와 부딪치고, 단언으로
일찍 빠지면 남습니다. 소스 트리의 `Resource/` 에 꼭 써야 하면(리소스 루트로만 풀리는 경로) 만들기 **전에**
`SW_TEST_DEFER_CLEANUP` 으로 지우는 것을 걸어 둡니다(`ResourceTest.ConfigurableResourcePriorityAndDlcSupport`).

```cpp
// Test/CoreTest/TestCompression.cpp 에서 실제로 쓰는 모양.
sw::CompressionCodecRegistry& registry  = *sw::CompressionCodecRegistry::getActive();
const bool                    bHadCodec = registry.isCodecRegistered( sw::CompressionCodecType::Custom );

registry.registerCodec( sw::make_unique<XorTestCodec>() );
SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [bHadCodec]()
{
    if ( bHadCodec == false )
        sw::CompressionCodecRegistry::getActive()->unregisterCodec( sw::CompressionCodecType::Custom );
} ) );
```

Cleanup은 등록한 역순으로 실행됩니다. ResourceManager 전체 shutdown이나 전역 Scene 초기화처럼
다른 테스트와 엔진 서비스에 영향을 주는 작업은 자동으로 수행하지 않습니다.

**오브젝트는 지역 `GameObjectManager` 로 만듭니다.** 전역 활성 씬을 빌리면 테스트끼리 상태가 샙니다.
예전엔 그 용도의 `TestFixture` 가 프레임워크에 있었지만 **쓰는 테스트가 하나도 없어서** 지웠습니다
(이 예제만 그것을 가리키고 있었습니다).
