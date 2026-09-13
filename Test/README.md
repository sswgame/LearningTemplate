# Test (자동화 테스트 스위트)

> **[🏠 위키 홈으로 돌아가기](../README.md)** | **[📖 서브시스템 목록](../docs/02_EngineSubsystems.md)**
> ---

엔진과 코어 프레임워크의 안정성을 보장하기 위한 자동화된 테스트 코드들이 모여있는 디렉터리입니다.
테스트는 각 역할과 종속성에 따라 5개의 메인 프로젝트와 1개의 프레임워크로 명확하게 분리되어 있습니다.

## 📁 테스트 프로젝트 구조

| 폴더명 | 테스트 성격 | 주요 특징 |
|---|---|---|
| **`CoreTest`** | 순수 코어 유닛 테스트 | 엔진(`Engine`) 라이브러리에 전혀 의존하지 않으며, `Math`, `String`, `DataStructure`, `Delegate` 등 가장 밑바닥 논리들을 매우 빠르게 검증합니다. |
| **`EngineTest`** | 엔진 코어/렌더링 유닛 테스트 | `GameObject`, `Scene`, `RHI`, `Material` 등 실제 엔진 객체들의 동작을 검증합니다. (GPU를 타지 않는 `nogpu` 필터링도 지원합니다) |
| **`ReflectionTest`** | 빌드 파이프라인(툴체인) 테스트 | 런타임 코드가 아닌, C++ 헤더를 분석하여 `*.gen.cpp`를 올바르게 자동 생성해 내는지 `ReflectionParser` 툴의 기능을 검증합니다. |
| **`SmokeTest`** | 런타임 모듈 통합 스모크 테스트 | 게임 DLL 핫 리로드(`LiveReloadManager`)나 RHI 모듈 동적 로드 등 시스템 전체가 런타임에 제대로 맞물려 돌아가는지를 검증합니다. |
| **`EditorTest`** | 에디터 로직 유닛 테스트 | 커맨드 스택·선택·뷰포트 수학·문서 dirty 계약 등 `EditorModule` 의 UI 없는 부분을 검증합니다. ImGui 렌더링은 타지 않습니다. |
| **`TestFramework`** | 테스트 공통 프레임워크 | 테스트 등록/실행을 조정하고, `TestContext`(결과 수집)와 `TestFilter`(CLI/glob 선택)를 재사용 가능한 구성요소로 제공합니다. |


## 🚀 테스트 실행 방법

CMake를 통해 구성(Configure)한 후, 다음과 같이 테스트를 실행할 수 있습니다.

### 모든 테스트 일괄 실행
```powershell
ctest --test-dir build/Ninja-Debug --output-on-failure
```

### 특정 테스트만 골라서 실행 (Label 활용)
라벨은 `core`, `editor`, `engine`, `reflection`, `module`, `unit`, `nogpu`, `lint` 입니다.
`lint` 는 `sw_registerLintTests`(`cmake/Engine/AssetAndToolTargets.cmake`)가 등록하는 Python 검사
여덟입니다 — `CheckEngineLayers` · `CheckIncludeOrder` · `CheckResourceCasing` · `CheckCodeConventions` ·
`CheckSourceGlob` · `CheckDataFileReferences` · `CheckRenderOwnership` · `CheckTestSuites`.

> 이 수는 세어서 적지 말고 `ctest --preset Ninja-Debug-lint -N` 로 확인한다. 예전에 "여섯" 이라고
> 적힌 채 일곱이 돌고 있었다.

```powershell
# GPU 없이 도는 것만 (CI 와 같은 집합)
ctest --test-dir build/Ninja-Debug -L nogpu --output-on-failure
# 순수 Core 만
ctest --test-dir build/Ninja-Debug -L core
# Python 정적 lint 만 (프리셋도 있다)
ctest --preset Ninja-Debug-lint
```

> **`nogpu` 는 "CI 러너가 돌릴 수 있다"는 계약이다.** 실제 GPU·디스플레이·DXC 가 필요한 다섯 스위트
> (`RHIDeviceTest` · `RenderPassGpuTest` · `WindowTest` · `ShaderCompilerTest` · `LiveShaderTest`)를
> `EngineTest_NoGPU` 필터가 통째로 뺀다. 디바이스가 필요한 테스트를 새로 쓰면 **`RenderPassGpuTest` 에
> 넣는다** — 이름을 하나씩 필터에 적던 시절에는 새 테스트가 규칙을 비켜가 CI 가 나흘간 빨갛게 있었다.

### 스위트 이름 규칙 — `CheckTestSuites.py` 가 강제합니다

스위트 이름은 장식이 아닙니다. `EngineTest_NoGPU` 가 **스위트 이름으로** CI 가 못 돌리는 것을 걸러내고
`--test_filter` 도 스위트 단위로 고르므로, 이름이 흔들리면 필터가 흔들립니다. 규칙은 넷입니다.

1. 스위트 이름은 **`XxxTest`** — 대문자로 시작하고 `Test` 로 끝나며 밑줄이 없습니다.
   계층 접두어(`Core_` · `Engine_`)는 붙이지 않습니다. **실행 파일 이름이 이미 그 말을 합니다.**
2. 한 스위트는 **한 파일에만** 삽니다.
3. CI 가 못 돌리는 스위트는 자기 파일에 이유와 함께 마커를 답니다. 린트가 이 마커와 CMake 의
   `EngineTest_NoGPU` 필터를 **양방향으로** 대조하므로, 한쪽만 고치면 커밋이 막힙니다.

   ```cpp
   // SW_TEST_REQUIRES_HOST( WindowTest ): 진짜 창을 만든다. 헤드리스 CI 러너엔 디스플레이가 없다.
   ```
4. 그런 스위트가 있는 파일에는 **다른 스위트를 두지 않습니다.** 섞여 있으면 새 케이스를 옆 스위트에
   붙이기 쉽고, 그 순간 GPU 가 필요한 케이스가 CI 로 들어갑니다.

### 테스트 상태 정리

각 테스트가 종료되면 프레임워크가 비동기 씬 로드와 TaskManager를 정리합니다. 그 밖에 테스트가
직접 만든 것(코덱 등록·임시 파일·전역 설정)은 `SW_TEST_DEFER_CLEANUP` 으로 되돌립니다.

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
