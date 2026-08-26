# Architecture (엔진 아키텍처 및 시스템 가이드)

> **[🏠 위키 홈으로 돌아가기](README.md)** | **[🚀 시작하기](docs/01_GettingStarted.md)**
> ---

이 문서는 SW Engine의 각 서브시스템(Subsystem)이 어떻게 구성되어 있고, 어떤 개념으로 동작하는지 설명합니다. 개발 중 꼭 지켜야 할 주요 **주의사항(Gotchas)** 도 이곳에서 다룹니다.

엔진을 처음 사용하시거나 빌드 방법이 궁금하시다면 먼저 [README.md](README.md)를 참고해 주세요.

---

## 🏗 타겟 그래프 (Target Graph)

엔진은 여러 모듈(DLL)과 정적 라이브러리로 분리되어 있습니다.
```text
App (exe)  — Engine + RuntimeAPI만 링크. GameFramework는 링크하지 않음.
 ├─ Engine        (Object, RHI, Scene 등 — Core_objects 심볼을 DLL에 포함)
 │   └─ RuntimeAPI / Delegate / Event로 게임·에디터와 통신
 ├─ EditorModule  (Dev 전용, delay-load)
 ├─ GameFramework + Kits  (Dev: delay-load / Shipping: SWGame과 함께 App에 정적 링크)
 └─ SWGame        (활성 게임 팩)

Core (STATIC)     — 로그·파일·문자열·메모리. OBJECT를 Engine과 공유 컴파일.
                    ReflectionParser는 Core만 링크 (Engine.dll 순환 방지).
```
- **Dev 모드**: `Engine` SHARED, `Editor`/`SWGame`/키트는 MODULE. App을 끄지 않고 핫리로드할 수 있습니다.
- **Shipping 모드**: `Editor` 제외. `Engine`/`SWGame` STATIC. `SW_RHI_AS_MODULES`는 CACHE FORCE로 OFF.

### Engine 내부 레이어
순환(Reflection ↔ Graphics 등) 때문에 단일 라이브러리로 링크하되, include 방향은 아래로만 허용합니다. 상세·린트는 [Source/Engine/README.md](Source/Engine/README.md)와 `Scripts/lint/CheckEngineLayers.py`를 참고하세요.

### 리소스 도메인
- `Resource/engine/` — 엔진 기본 셰이더, 기본 텍스처, 파이프라인 에셋
- `Resource/common/` — 공유 공통 에셋
- `Resource/game/` — 활성 게임별 프로젝트 에셋 (`demo`, `empty` 등)

### 외부 의존성 (ThirdParty & Vcpkg)
프로젝트의 의존성은 주로 `vcpkg` 매니페스트(`vcpkg.json`)를 통해 통합 관리됩니다. `Scripts/setup/SetupVcpkg.py`가 필요한 의존성을 설치하며, 커스텀 패키지(예: `imgui-node-editor`)나 직접 소스 포함이 필요한 일부 라이브러리들은 `ThirdParty/` 디렉터리에 위치합니다.

---

## ⚙️ 하위 시스템 개념 및 기능 설명

### 1. 핫리로드 (LiveReload)와 C-ABI
App은 게임이나 에디터 클래스를 직접 알지 못하며 오직 C-ABI(Extern "C") 모듈 진입점(Entry Point)만 동적으로 불러다 씁니다.
- **동작 원리**: 코드를 수정해 DLL을 다시 빌드하면, 런타임이 이를 감지하여 새로운 DLL을 섀도우(shadow) 복사 후 로드합니다.
- **주의사항**: `FreeLibrary`가 안전하게 불리려면 기존에 할당한 메모리나 렌더 자원을 정확한 시점에 반환해야 합니다.

### 2. 리소스 경로 (Resource Paths)
`Resource/` 폴더는 실제 게임이 바라보는 가장 높은 위치입니다. 리소스를 찾을 때는 항상 상대 경로를 사용합니다.
- **전역 ID**: `engine/pipeline/forward.xml` 같이 도메인(engine, game 등)을 포함한 명확한 식별자.
- **경로 대소문자**: 리소스를 검색할 때 엔진 내부에서는 **모든 경로를 소문자로 정규화**(`normalizePath`)하여 매칭합니다. 따라서 리소스 파일 이름은 가급적 소문자를 사용하는 것이 혼동을 줄이는 길입니다.

### 3. RHI (Render Hardware Interface)
DirectX 11/12, OpenGL, Vulkan 등을 추상화하는 그래픽스 백엔드입니다.
- **동작 원리**: `IRHIDevice` 인터페이스를 통해 RHI 백엔드를 DLL 형태로 동적으로 불러옵니다(`SW_RHI_AS_MODULES=ON`).
- **Caps**: 현재 모든 백엔드는 Bindless(바인드리스) 텍스처 접근과 Compute Root Constants(작은 UBO/CB)를 에뮬레이션 또는 네이티브로 지원합니다.
- **명령 기록 (Command List)**: 렌더 스레드 또는 메인 스레드에서 GPU 명령을 모은 뒤, 한 번에 큐에 제출(Submit)하는 지연(Deferred) 방식을 씁니다.

### 4. RenderPass vs Render Pipeline
프레임이 그려지는 과정은 패스(Pass)와 파이프라인(Pipeline)으로 철저히 나뉩니다.
- **RenderPass (`RenderPassResource`)**: "어떤 포맷의 텍스처에 그릴 것인가?", "그리기 전에 화면을 지울(Clear) 것인가?" 등 바인딩 템플릿 역할. (`renderpass/` 경로에 저장)
- **Render Pipeline (`RenderPipelineResource`)**: "이번 프레임은 [그림자 패스] → [메인 패스] → [포스트 프로세스 패스] 순서로 그린다"를 정의하는 전체 프레임 그래프. (`pipeline/` 경로에 저장)
- **RenderGraph**: 파이프라인 파일을 읽어들여 렌더링 순서와 자원 의존성(Read/Write)을 런타임에 자동으로 정렬해주는 시스템입니다.

### 5. 리플렉션과 직렬화 (Reflection & Serialization)
- **리플렉션 생성**: C++ 소스 코드에 `REFLECT`, `PROPERTY` 매크로를 달아두면 `Tools/ReflectionParser`가 코드를 읽어서 `*.gen.cpp`(메타데이터)를 만들어줍니다.
- **직렬화**: 이렇게 만들어진 데이터를 통해 JSON, XML, Binary 등으로 오브젝트의 상태를 저장하고 불러옵니다(Scene 로딩). `ObjectDiffSerializer`를 통해 바뀐 값만 따로 델타(Delta) 저장도 가능합니다.

### 6. Scene (씬)과 Prefab (프리팹)
- **GameObject**: 씬을 구성하는 기본 단위.
- **Component**: 게임 오브젝트에 붙어 동작하는 로직(예: `MeshComponent`, `CameraComponent`). C++ RTTI 대신 리플렉션 타입(`TypeInfo`)을 통해 관리됩니다.
- **Prefab**: 미리 구성해 둔 오브젝트의 템플릿. 인스턴스화(`spawn`) 할 때 변경된 데이터(Diff)만 덮어씌웁니다.

---

## ⚠️ 반드시 지켜야 할 주의사항 (Gotchas)

엔진 구조상 다음과 같은 행위를 하면 크래시나 버그가 발생할 수 있습니다. 코드를 짤 때 항상 유의해 주세요.

> [!CAUTION]
> **병렬 틱(Tick) 도중 계층 구조 변경 금지**
> `GameObjectManager::tick` 구간에서는 여러 오브젝트가 멀티스레드로 동시에 `onTick()`을 돕니다. 이때 **자신이나 다른 오브젝트의 부모/자식(Parent/Child) 관계를 수정(`attachToParent`, `detach`)해서는 절대 안 됩니다.**
>
> **구조 변경은 자동 지연됩니다.** `Registry::emplace` / `remove`는 tick 중 CommandBuffer로, `GameObject::addComponent`는 `deferPostTick`으로 미룹니다. tick 중 `addComponent`는 `nullptr`을 반환하므로, 생성 직후 필드를 채워야 하면 `GameObjectManager::executeOrDeferPostTick`으로 스폰+초기화를 한 블록에 묶으세요.

> [!WARNING]
> **RHI DLL 스탬프 불일치 방지**
> 렌더링 백엔드(예: `RHI_DX11.dll`)를 핫스왑할 수 있지만, ABI 인터페이스(`RHIModuleAbi.h`)가 변경된 경우 **엔진 전체와 RHI DLL들을 동시에 재빌드**해야 합니다. 오래된 RHI DLL이 남아있으면 함수 포인터가 어긋나 즉시 크래시납니다.

> [!IMPORTANT]
> **모듈 핫리로드 시 정적 변수(Static Variable) 주의**
> 핫리로드 기능은 DLL을 내리고 다시 올립니다. 클래스 내의 **static 변수나 싱글톤 메모리**는 DLL이 언로드될 때 증발하거나 주소가 바뀔 수 있습니다. 꼭 유지되어야 하는 전역 상태는 `Engine` 모듈이나 `App` 쪽 저장소에 둡니다.

> [!NOTE]
> **헤더 주석**
> 함수는 선언 위 `/** @brief */` 한글. 관련 API는 CMakeLists처럼 구간 배너로 묶습니다. 멤버 필드만 옆 `/**<` / `///<`를 씁니다. 로그/assert 문자열은 번역하지 않습니다.

---

## 🧪 테스트 아키텍처

CTest 타겟은 `CoreTest`, `EngineTest`(`EngineTest_NoGPU`), `ReflectionTest`, `SmokeTest`입니다.
GPU가 없는 CI는 `nogpu` 라벨만 돌립니다. 루트는 추가로 `CheckEngineLayers`·`CheckSourceGlob` lint를 `nogpu;lint`로 등록합니다.
