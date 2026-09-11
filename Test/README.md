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
여섯입니다 — `CheckEngineLayers` · `CheckIncludeOrder` · `CheckResourceCasing` · `CheckCodeConventions` ·
`CheckSourceGlob` · `CheckDataFileReferences`.

```powershell
# GPU 없이 도는 것만 (CI 와 같은 집합)
ctest --test-dir build/Ninja-Debug -L nogpu --output-on-failure
# 순수 Core 만
ctest --test-dir build/Ninja-Debug -L core
# Python 정적 lint 만 (프리셋도 있다)
ctest --preset Ninja-Debug-lint
```

> **`nogpu` 는 "GPU 없이 돌아간다"는 계약이다.** 실제 RHI 디바이스를 만드는 `RenderPassTest` 케이스들은
> `RenderPassGpuTest` 스위트로 갈라 두었고 `EngineTest_NoGPU` 필터가 그 스위트를 통째로 뺀다. 디바이스가
> 필요한 테스트를 새로 쓰면 **그 스위트에 넣는다** — 이름을 하나씩 필터에 적던 시절에는 새 테스트가
> 규칙을 비켜가 CI 가 나흘간 빨갛게 있었다.

### 테스트 상태 정리

각 테스트가 종료되면 프레임워크가 비동기 씬 로드와 TaskManager를 정리합니다. 테스트가 전역
오브젝트나 이벤트 구독을 만들었다면 `TestFixture`와 cleanup 등록을 함께 사용합니다.

```cpp
SW_TEST_CASE(MySuite, CreatesTemporaryObject)
{
	SW_TEST_FIXTURE(fixture);
	sw::GameObjectManager* pObjectManager = fixture.getObjectManager();

	// 테스트 오브젝트를 생성합니다.
	SW_TEST_DEFER_CLEANUP(
		SW_DELEGATE_LAMBDA(sw::Delegate<void()>, [pObjectManager]()
		{
			if ( pObjectManager != nullptr )
				pObjectManager->clear();
		}));
}
```

Cleanup은 등록한 역순으로 실행됩니다. ResourceManager 전체 shutdown이나 전역 Scene 초기화처럼
다른 테스트와 엔진 서비스에 영향을 주는 작업은 자동으로 수행하지 않습니다.
