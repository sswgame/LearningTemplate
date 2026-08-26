# 🔄 LiveReload & C-ABI (핫리로드 아키텍처)

SW Engine의 가장 강력한 기능 중 하나는 게임을 실행한 채로 코드를 수정하고 컴파일하면 변경 사항이 즉시 적용되는 **모듈 핫리로드(LiveReload)** 기능입니다.
이 문서는 해당 기능이 내부적으로 어떻게 동작하며, 개발 시 어떤 규칙을 지켜야 하는지 설명합니다.

---

## 1. RuntimeAPI와 C-ABI 통신

엔진의 실행 파일인 `App.exe`는 내부적으로 C++ 클래스(예: `GameObject`, `Component`)에 대해 아무것도 모릅니다. 대신 **순수 C-ABI(Extern "C")** 로 정의된 통신 규약인 `RuntimeAPI`를 통해서만 DLL 모듈(`Engine.dll`, `SWGame.dll`)과 통신합니다.

- **장점**: 컴파일러 종속성이나 C++ RTTI, 네임맹글링(Name Mangling) 문제가 없어 DLL을 런타임에 갈아끼우기(Swap)에 매우 유리합니다.

## 2. LiveReload 동작 원리

```text
1. [개발자] C++ 코드 수정 후 빌드 (Ctrl+Shift+B)
2. [App.exe] 파일 시스템 감시자(File Watcher)가 DLL 변경을 감지
3. [App.exe] 다음 프레임 진입 시 메인 루프를 잠시 일시 정지(Fencing)
4. [엔진] 기존 DLL에서 직렬화(Serialization)를 통해 현재 게임 상태를 메모리에 백업
5. [엔진] `FreeLibrary` 호출로 기존 DLL 언로드
6. [엔진] 새 DLL을 그림자 복사(Shadow Copy) 후 `LoadLibrary`
7. [엔진] 백업해둔 게임 상태를 역직렬화(Deserialization)하여 복원
8. [App.exe] 메인 루프 재개 (변경된 로직 즉시 적용!)
```

## 3. ⚠️ 개발 시 필수 주의사항 (Gotchas)

핫리로드가 안전하게 작동하려면 개발자가 다음 규칙들을 엄격히 지켜야 합니다.

> [!WARNING]
> **1. 정적(Static) 변수 주의**
> 핫리로드 시 기존 DLL이 메모리에서 통째로 내려가기 때문에, DLL 내부에 선언된 `static` 변수나 싱글톤 데이터는 **모두 날아가거나 주소가 변경**됩니다.
> 반드시 유지되어야 하는 전역 상태는 `Engine` 전용 레지스트리를 통해 할당/접근해야 합니다.

> [!CAUTION]
> **2. 리소스 해제 타이밍**
> RHI 자원이나 백그라운드 태스크(스레드)는 DLL이 언로드(`FreeLibrary`)되기 전에 확실히 멈추고 해제해야 합니다. 이전 DLL의 함수 포인터를 참조하는 비동기 태스크가 살아있으면 즉시 크래시가 발생합니다.

> [!NOTE]
> **3. 메모리 누수 방지**
> 핫리로드 전 상태를 직렬화하고 복원하는 과정(`ObjectDiffSerializer`)에서 제대로 처리되지 않은 객체 레퍼런스는 메모리 누수나 댕글링 포인터를 유발할 수 있습니다. 상태 보존이 필요한 객체는 항상 `REFLECT()` 매크로를 통해 리플렉션 시스템에 등록하세요.

---
[◀ 이전: 서브시스템 개요](02_EngineSubsystems.md) | [🏠 위키 홈으로 돌아가기](../README.md) | [▶ 다음: 코딩 컨벤션](04_CodingGuidelines.md)
