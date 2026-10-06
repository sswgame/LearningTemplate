# 🔄 LiveReload & C-ABI (핫리로드 아키텍처)

SW Engine의 가장 강력한 기능 중 하나는 게임을 실행한 채로 코드를 수정하고 컴파일하면 변경 사항이 즉시 적용되는 **모듈 핫리로드(LiveReload)** 기능입니다.
이 문서는 해당 기능이 내부적으로 어떻게 동작하며, 개발 시 어떤 규칙을 지켜야 하는지 설명합니다.

---

## 1. RuntimeAPI와 C-ABI 통신

엔진의 실행 파일인 `App.exe`는 `Engine` 과 `RuntimeAPI` 만 링크하고, 게임 · 에디터 클래스는 컴파일 때 전혀 모릅니다. 게임 · 에디터 모듈(`SWGame`, `EditorModule`, `GF_*` 킷)과는
**순수 C-ABI(Extern "C")** 로 정의된 통신 규약인 `RuntimeAPI`(헤더 전용)의 함수 표를 통해서만 통신합니다(예: 게임 모듈의 `exportGameApi`).

- **장점**: 컴파일러 종속성이나 C++ RTTI, 네임맹글링(Name Mangling) 문제가 없어 DLL을 런타임에 갈아끼우기(Swap)에 매우 유리합니다.

## 2. LiveReload 동작 원리

```text
1. [개발자] C++ 코드 수정 후 빌드 (IDE 빌드 · 에디터의 빌드 · Ctrl+F7 = ReloadGame, Ctrl+F6 = ReloadEditor)
2. [App · LiveReloadManager] 파일 감시자가 DLL 변경을 감지하고, 파일이 잠잠해질 때까지 기다린다(이 프로세스가 시킨 빌드가 도는 동안은 모으기만)
3. [App] 바뀐 모듈과 그것에 의존하는 모듈을 묶어(연쇄 리로드) 위상 순서를 정한다
4. [App] 묶음의 새 DLL 을 모두 그림자 복사(Shadow Copy)해 올리고 엔진 ABI 도장(`swEngineAbiStamp:`)을 대조한다(prepare)
5. [App · ModuleHost] 모듈마다 commit — suspendModules(워커 태스크 배수 → 게임 상태를 메모리에 직렬화 → 모듈 인스턴스 파괴) 뒤
   옛 이미지를 새 이미지로 바꾼다(옛 이미지는 미뤄서 내린다)
6. [App · ModuleHost] 리플렉션 타입을 다시 등록하고, 게임 API 표를 다시 묶어 인스턴스를 세운 뒤 상태를 역직렬화해 복원한다
7. 메인 루프 재개 (변경된 로직 즉시 적용)
```

- **실패 정책**: 적용(commit) **전**에 실패하면(빌드 실패 · ABI 도장 불일치 · 이미지 검증 실패) 옛 모듈을 그대로 두고 계속 돕니다.
  적용 **뒤**의 결함(새 모듈의 생성 · 초기화 실패 등)만 리로드 그래프를 막습니다(`markGraphBroken`) — 고쳐서 다시 빌드하면 다음 리로드가 보존한 상태를 되살리고,
  그 사이에는 씬 저장이 막힙니다.
- **리로드 기계는 App 의 것입니다.** `LiveReloadManager` 는 `Source/App/Module/` 에 있고 Shipping 빌드에서는 파일째 빠집니다. Engine 은 지연 로드 훅이 묻는
  `IModuleHandleProvider` 하나만 압니다. Linux 는 그림자 복사본의 SONAME 을 세대마다 바꿔(`ModuleImagePatch`) 의존 모듈이 옛 이미지에 묶이지 않게 합니다.
- 에디터의 오브젝트 편집 Undo 는 엔진 데이터 명령으로 기록되어 에디터 모듈 리로드 뒤에도 되돌릴 수 있습니다.

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
> **3. 상태 보존은 리플렉션으로만**
> 핫리로드 전후의 상태는 직렬화로 옮깁니다. 리플렉션에 등록되지 않은 필드(`REFLECT` · `PROPERTY` 가 없는 것)와 생포인터 참조는 옮겨지지 않아 비거나 댕글링이 됩니다.
> 보존이 필요한 상태는 `PROPERTY` 로 등록하고, 다른 오브젝트는 생포인터가 아니라 `GameObjectHandle` · `ComponentHandle` 로 드세요.

---
[◀ 이전: 문서 지도](02_DocumentMap.md) | [🏠 위키 홈으로 돌아가기](../README.md) | [▶ 다음: 코딩 컨벤션](04_CodingGuidelines.md)
