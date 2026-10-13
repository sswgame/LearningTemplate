# 저장소 소유 정리 — 종료 전용 해제 함수 없애기

이 문서는 작업 계획입니다. 표의 '다음 단위' 가 모두 옮겨지면 지웁니다.

## 왜

`AppSmokeTest.ShutdownReturnsEveryTagToTheBaseline` 은 엔진 기동 뒤 기준선과 종료 보고를 견줘, 자란 메모리 태그가 없어야 통과합니다.
이 테스트를 통과시키려고 저장소마다 종료 때만 부르는 해제 함수(`releaseTextStorage`, `releaseActive`, `releaseStorage`, `releaseUnusedStorage`, `releaseScratch`, `releaseTitleStorage` …)가 늘었습니다.
저장소가 하나 늘 때마다 함수 하나와 `StaticStorageSubsystem::shutdownSubsystem` 의 호출 한 줄이 늘어나는 구조입니다.

방향은 소유 관계입니다. 저장소는 소유자만 선언하고, 소유 범위(엔진 수명 · 모듈 · 씬 · 스레드)가 끝나면 저절로 사라집니다.
초기화도 같습니다. 처음 쓸 때 만들어지고(지연 생성), 별도 init 함수를 두지 않습니다.

## 쓰는 도구

`Core/Memory/LifetimeScope.h` 의 두 타입입니다.

- `LifetimeScope` — 소유 범위입니다. 등록된 값을 `close()` 가 등록의 역순으로 소멸시킵니다. 전역 범위(`LifetimeScope::getGlobal()`)는 엔진 종료(`StaticStorageSubsystem::shutdownSubsystem`)가 닫습니다.
- `ScopeOwned<T>` — 처음 `getOrCreate()` 할 때 만들어져 범위에 등록되는 값입니다. 범위가 닫힌 뒤의 접근은 nullptr 이고 Debug 에서 단언합니다. 상수 초기화되므로 익명 네임스페이스의 정적 변수로 둡니다.

```cpp
namespace
{
    ScopeOwned<LocalizedTextArena> s_localizedTextArena; // 엔진 수명이 소유한다
}
// 쓰는 자리
LocalizedTextArena* const pArena = s_localizedTextArena.getOrCreate();
```

## 분류

| 종류 | 뜻 | 푸는 방법 |
|---|---|---|
| ① 소유자 없는 정적 저장소 | 함수 지역 · 파일 정적 컨테이너. 프로세스 끝까지 살아 종료 보고에 남는다 | `ScopeOwned<T>` 로 소유자를 선언한다 |
| ② 스레드 지역 | `thread_local` 컨테이너. 스레드가 종료 보고보다 오래 산다 | 스크래치를 그리는 쪽(시스템 · 스레드 슬롯)이 소유한다 |
| ③ 기준선 · 보고 시점 문제 | 소유자는 있으나 수명이 엔진 범위 밖이거나, 정상 초기화가 기준선 뒤에 온다 | 소유자를 엔진 범위 안으로 옮기거나 첫 생성을 기동으로 당긴다 |
| ④ 진짜 결함 | 조회가 등록을 만들거나, 해제가 빠졌다 | 결함을 고친다(해제 함수로 가리지 않는다) |

## 표

`부르는 곳` 은 지금 해제를 부르는 자리입니다. 옮긴 항목은 함수와 호출을 지웠습니다.

| 이름 | 파일 | 종류 | 소유해야 하는 쪽 | 부르는 곳 | 상태 |
|---|---|---|---|---|---|
| `StringTable::releaseTextStorage`(`LocalizedTextArena`) | `Engine/Localization/StringTable.cpp` | ① | 엔진 수명 범위 | `StaticStorageSubsystem::shutdownSubsystem` | 옮김 — `ScopeOwned`, 함수 삭제 |
| `Render2DSettings::releaseActive`(활성 표) | `Engine/Graphics/2D/Render2DSettings.cpp` | ① | 엔진 수명 범위 | `StaticStorageSubsystem::shutdownSubsystem` | 옮김 — `ScopeOwned`, 함수 삭제 |
| `AssetLoadProfiler::releaseStorage` | `Engine/Resource/AssetLoadProfiler.cpp` | ① | 엔진 수명 범위 | `StaticStorageSubsystem::shutdownSubsystem` | 옮김 — `ScopeOwned`, 함수 삭제 |
| `IModuleUnloadListener::releaseUnusedStorage` | `Core/Module/ModuleUnloadListener.cpp` | ① | 등록한 모듈(빈 목록이 되면 용량 반납) | `StaticStorageSubsystem::shutdownSubsystem` | 다음 단위 |
| `AutomationStepRegistry::releaseUnusedStorage` | `Engine/Automation/AutomationStepRegistry.cpp` | ① | 등록한 모듈 | `StaticStorageSubsystem::shutdownSubsystem` | 다음 단위 |
| `AutomationProbes::releaseUnusedStorage` | `Engine/Automation/AutomationProbe.cpp` | ① | 등록한 모듈 | `StaticStorageSubsystem::shutdownSubsystem` | 다음 단위 |
| `DevCommandRegistry::releaseUnusedStorage` | `Core/Console/DevCommandRegistry.cpp` | ① | 등록한 모듈 | `StaticStorageSubsystem::shutdownSubsystem` | 다음 단위 |
| `GameAutoplay::unregisterAutoplay` 의 `shrinkToFit` | `Engine/Utility/GameAutoplay.cpp` | ① | 등록한 모듈 | 등록 해제 때(목록이 비면) | 이미 반응형 — 위 넷의 본보기 |
| `SceneTransformStorage::releaseStorage` | `Engine/Object/Component/SceneTransformStorage.cpp` | ① + ④ 검출 | 엔진 수명 범위(소멸자에서 남은 칸 경고) | `StaticStorageSubsystem::shutdownSubsystem` | 다음 단위 |
| `RHIRenderResource::releaseRegistryStorage` | `Engine/Graphics/RHI/RHIRenderResource.cpp` | ① + ④ 검출 | 엔진 수명 범위(소멸자에서 남은 등록 경고) | `StaticStorageSubsystem::shutdownSubsystem` | 다음 단위 |
| `ResourcePaths::clearPathCache` | `Engine/Resource/ResourcePaths.cpp` | ① | 엔진 수명 범위(`initialize` 의 비우기는 남는다) | `StaticStorageSubsystem::shutdownSubsystem`, `ResourcePaths::initialize` | 다음 단위 |
| `ShaderReflectionLibrary::clearCache`(매니페스트) | `Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.cpp` | ① | 엔진 수명 범위 또는 `ShaderCache` | `ShaderCache` 종료, 재컴파일, 쿠킹 | 다음 단위(재컴파일 · 쿠킹의 비우기는 남는다) |
| `MeshCache::clear` 의 경고 경로 집합 | `Engine/Graphics/Mesh/MeshCache.cpp` | ① | 엔진 수명 범위 | `MeshCache::clear` | 다음 단위 |
| `PhysicsMeshShapeCache::clear` | `Engine/Physics/Collision/PhysicsMeshShapeCache.cpp` | ① | 엔진 수명 범위 | `PhysicsMeshShapeCache::clear` | 다음 단위 |
| `HashedStringPool::shutdown` | `Core/String/hashed_string.cpp` | ① | 부트스트랩(모든 `hashed_string` 뒤) | `NamePoolSubsystem` 소멸자 | 유지 — 순서가 계약이다(범위가 닫힌 뒤 내린다) |
| `UIPaintPass::releaseScratch`(`t_listPaintOrderByDepth`) | `Engine/UI/Render/UIPaintPass.cpp` | ② | `UISystem`(칠하기 문맥으로 스크래치를 넘긴다) | `UISystem::shutdown` | 다음 단위 — `ded0a6fd6` 의 이동 대입 수정은 그대로 둔다 |
| `BinarySerializer` 의 `thread_local` 버퍼 넷 | `Engine/Serialization/Format/BinarySerializer.cpp` | ② | 스레드 슬롯별 스크래치 | 없음 | 다음 단위 — 기준선 뒤에 자라는지 먼저 잰다 |
| `ShaderParameterBinder` 의 `s_cbBytes` | `Engine/Renderer/Frame/ShaderParameterBinder.cpp` | ② | 바인더(렌더 스레드) | 없음 | 다음 단위 — 기준선 뒤에 자라는지 먼저 잰다 |
| `IWindow::releaseTitleStorage` | `Engine/Window/IWindow.h` | ③ | App 의 창(엔진보다 오래 산다) | `App::shutdown` | 다음 단위 — 보고를 창 파괴 뒤로 옮기거나 창 수명을 엔진 범위 안으로 |
| `LiveReloadManager::clearContexts` 의 표 용량 | `ModuleHost/LiveReloadManager.cpp` | ③ | `LiveReloadManager`(소멸자가 용량을 돌려준다) | 모듈 호스트 종료 | `ded0a6fd6` 에서 이동 대입으로 고침 — 소유자 소멸 시점이 보고 뒤인지 다음 단위에서 본다 |
| `ModuleDependencyGraph::clear` 의 표 용량 | `ModuleHost/ModuleDependencyGraph.cpp` | ③ | `ModuleDependencyGraph` | 모듈 호스트 종료 | `ded0a6fd6` 에서 고침 — 위와 같이 본다 |
| `GimmickNodeRegistry::getDefault` | `GameFramework/Base/Gameplay/Gimmick/GimmickNodeRegistry.cpp` | ③ | 엔진 수명 범위(`ScopeOwned` 면 기동 단계 등록이 필요 없다) | `ded0a6fd6` 가 기동 단계로 당김 | 다음 단위 — 병합 뒤 `ScopeOwned` 로 대체할 수 있는지 본다 |
| `EditorSiblingOrder::isFolder` | `Editor/Common/Commands/EditorSiblingOrder.cpp` | ④ | — | — | `ded0a6fd6` 에서 고침(조회가 태그를 등록하던 결함) |
| `ObjectCopyBuffer` 의 함수 안 정적 목록 | `Engine/Object/GameObject/ObjectCopyBuffer.cpp` | ① | 엔진 수명 범위 | 없음 | 옮김 — `ScopeOwned` |
| `RenderThread` 스크린샷 우편함 | `Engine/Renderer/RenderThread.cpp` | ① | 엔진 수명 범위 | 없음 | 옮김 — `ScopeOwned` |
| `TagID` 등록표 | `Core/String/TagID.cpp` | ① | 엔진 수명 범위(이름 풀보다 먼저 닫힌다) | 없음 | 옮김 — `ScopeOwned` |
| `ValidationIssueLog::get` | `Engine/Reflection/ReflectionValidation.cpp` | ① | 엔진 수명 범위 | 없음 | 옮김 — `ScopeOwned` |
| `ReflectValue` 기본 타입 이름 표 | `Engine/Reflection/ReflectValue.cpp` | ③ | — | 없음 | 저장소를 없앰 — 고정 `array` |
| `AutomationScenarioChain::getCurrentPath` 의 빈 경로 | `Engine/Automation/AutomationScenarioChain.cpp` | ③ | 이미지 적재 | 없음 | 정적 멤버로 옮김 |
| `DeferredImageUnloadQueue` 목록 용량 | `ModuleHost/DeferredImageUnloadQueue.cpp` | ③ | `LiveReloadManager`(보고 뒤에 사라진다) | `unloadAll` | 다 비우면 빈 객체 대입 |
| `Win32SplashWindow::releasePaintCache` | `Engine/Window/Windows/Win32SplashWindow.cpp` | 해당 없음 | — | 창 크기 변경 · 파괴 | GDI 자원 정리 도우미다. 종료 누수와 무관하다 |

종류별 개수(해당 없음 제외 30): ① 19 · ② 3 · ③ 7 · ④ 1(`SceneTransformStorage` · `RHIRenderResource` 의 검출 몫은 ①로 셌다).

## 다음 단위

1. 레지스트리 용량 넷(`releaseUnusedStorage`)을 `GameAutoplay` 처럼 등록 해제에서 목록이 비면 반납하게 바꾸거나, `RegistrationList` 가 비면 스스로 반납하게 한다. 그러면 함수 넷과 호출 넷이 사라진다.
2. `SceneTransformStorage` · `RHIRenderResource` 레지스트리를 `ScopeOwned` 로 옮기고, 남은 칸 · 등록의 경고를 소멸자로 옮긴다.
3. `ResourcePaths` 경로 캐시 · 셰이더 리플렉션 매니페스트 · 메시 · 물리 캐시를 `ScopeOwned` 로 옮긴다.
4. `UIPaintPass` 스크래치를 `UISystem` 이 소유하게 한다. `thread_local` 이 사라진다.
5. 창 제목(③)은 종료 보고 시점과 창 수명 중 어느 쪽을 옮길지 정한다.
6. 모듈 범위가 필요해지면 `ModuleHost` 가 모듈마다 `LifetimeScope` 를 두고 언로드 때 닫는다. 지금은 모듈 DLL 의 `ScopeOwned` 가 DLL 언로드 때 스스로 소멸해 같은 효과다.
