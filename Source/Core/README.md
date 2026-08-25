# Core (코어 유틸리티)

엔진의 가장 밑바닥(Foundation)에 해당하는 정적 라이브러리(STATIC) 모듈입니다.
문자열, 로그, 파일, 델리게이트, 컨테이너 래퍼, 동시성, 메모리 진단이 여기 있습니다. `Handle/`은 없습니다.

## 디렉터리
- **Memory/**: `alignedAlloc`, `LinearAllocator`, `FrameArenaAllocator`(+ `FrameDoubleBuffer`), `MemoryProfiler`(누수 검사 포함), `CallStackCapture`
- **Concurrency/**: `LockFreeObjectPool`, `LockFreeQueue`, `ConcurrentQueue`, `WorkStealingDeque`, `DeadlockDetector`, `DataRaceDetector`
- **Container/**: `DynamicBitset` · **String/** · **File/** · **Event/** · **Delegate/**

## 빌드 모델
- 소스는 `Core_objects`(OBJECT)에서 **한 번만** 컴파일됩니다.
- `Core` STATIC = 그 OBJECT 아카이브 → `Tools/ReflectionParser`가 직접 링크합니다.
- `Engine`는 동일 OBJECT를 링크해 Dev에서 `Engine.dll`로 foundation 심볼을 export합니다 (App/Editor는 `SW_IMPORTS`로 dllimport).

## 핵심 규칙
- **독립성 유지**: `Core`는 `Engine`이나 `GameFramework`, `Game` 폴더의 코드에 **절대 의존해서는 안 됩니다.**
- **어디서나 쓰임**: ReflectionParser에도 직접 링크되므로 무거운 GPU/에디터 의존성은 피해야 합니다.
