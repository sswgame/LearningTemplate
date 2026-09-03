# Test (자동화 테스트 스위트)

> **[🏠 위키 홈으로 돌아가기](../README.md)** | **[📖 서브시스템 목록](../docs/02_EngineSubsystems.md)**
> ---

엔진과 코어 프레임워크의 안정성을 보장하기 위한 자동화된 테스트 코드들이 모여있는 디렉터리입니다.
테스트는 각 역할과 종속성에 따라 4개의 메인 프로젝트와 1개의 프레임워크로 명확하게 분리되어 있습니다.

## 📁 테스트 프로젝트 구조

| 폴더명 | 테스트 성격 | 주요 특징 |
|---|---|---|
| **`CoreTest`** | 순수 코어 유닛 테스트 | 엔진(`Engine`) 라이브러리에 전혀 의존하지 않으며, `Math`, `String`, `DataStructure`, `Delegate` 등 가장 밑바닥 논리들을 매우 빠르게 검증합니다. |
| **`EngineTest`** | 엔진 코어/렌더링 유닛 테스트 | `GameObject`, `Scene`, `RHI`, `Material` 등 실제 엔진 객체들의 동작을 검증합니다. (GPU를 타지 않는 `nogpu` 필터링도 지원합니다) |
| **`ReflectionTest`** | 빌드 파이프라인(툴체인) 테스트 | 런타임 코드가 아닌, C++ 헤더를 분석하여 `*.gen.cpp`를 올바르게 자동 생성해 내는지 `ReflectionParser` 툴의 기능을 검증합니다. |
| **`SmokeTest`** | 런타임 모듈 통합 스모크 테스트 | 게임 DLL 핫 리로드(`LiveReloadManager`)나 RHI 모듈 동적 로드 등 시스템 전체가 런타임에 제대로 맞물려 돌아가는지를 검증합니다. |
| **`TestFramework`** | 테스트 공통 프레임워크 | 테스트 등록/실행을 조정하고, `TestContext`(결과 수집)와 `TestFilter`(CLI/glob 선택)를 재사용 가능한 구성요소로 제공합니다. |


## 🚀 테스트 실행 방법

CMake를 통해 구성(Configure)한 후, 다음과 같이 테스트를 실행할 수 있습니다.

### 모든 테스트 일괄 실행
```bash
# 빌드 디렉터리에서
ctest -C Debug --output-on-failure
```

### 특정 테스트만 골라서 실행 (Label 활용)
라벨은 `core`, `engine`, `reflection`, `module`, `unit`, `nogpu`, `lint`입니다. 루트 CMake는 `CheckEngineLayers`·`CheckSourceGlob`를 별도의 `lint` 라벨로 등록합니다.
```bash
# 예: GPU를 사용하지 않는 가벼운 테스트만 실행
ctest -C Debug -L "nogpu"
# 예: 순수 Core 관련 테스트만 실행
ctest -C Debug -L "core"
# 예: Python 기반 정적 lint만 실행
ctest -C Debug -L "lint"
```

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
