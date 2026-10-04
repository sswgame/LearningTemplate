# SW Engine (게임 및 에디터 엔진 템플릿)

[![CI (main)](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml?query=branch%3Amain)
[![CI (develop)](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml/badge.svg?branch=develop)](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml?query=branch%3Adevelop)
[![Windows](https://img.shields.io/badge/Windows-clang--cl%20(Debug%20%7C%20Shipping)-0078D6?logo=windows&logoColor=white)](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml)
[![Linux](https://img.shields.io/badge/Linux-Clang%20(Debug%20%7C%20ASan%20%7C%20TSan%20%7C%20Shipping)-FCC624?logo=linux&logoColor=black)](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml)
[![C++17](https://img.shields.io/badge/Language-C%2B%2B17-00599C?logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![License](https://img.shields.io/badge/License-MIT-brightgreen.svg)](LICENSE)

**SW Engine**은 C++17 기반의 고성능 게임 및 에디터 엔진 프레임워크입니다.  
CMake, Ninja, LLVM Clang-cl 및 sccache를 결합하여 **초고속 증분 빌드**를 제공하며, 개발(Dev) 환경에서는 코드를 수정하면 엔진 재시작 없이 즉시 DLL이 교체되는 **모듈 핫리로드 (LiveReload)**를, 최종 배포(Shipping) 환경에서는 최고 성능과 보안을 위해 단일 실행 파일로 최적화되는 **정적 링크(Static Link)**를 지원합니다.

### 🛠️ 지원 플랫폼 및 CI 빌드 매트릭스

> 💡 모든 빌드/테스트 매트릭스의 실시간 실행 상태와 로그는 [GitHub Actions CI](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml)에서 확인하실 수 있습니다.
> CI 는 `nogpu` 레이블 시험만 돌립니다. GPU · 창 · DXC 가 필요한 `hostgpu` 시험은 GPU 가 있는 기계에서 직접 돌려야 합니다([7절](#7-자동화-테스트-스위트)).
> 지원 플랫폼은 Windows · Linux 둘입니다. **macOS 는 지원하지 않습니다** — Apple 호스트에서는 CMake 구성이 멈춥니다.

| OS / 플랫폼 | 컴파일러 / 툴체인 | 빌드 프리셋 & 검증 항목 | CI 상태 링크 |
| :--- | :--- | :--- | :---: |
| 🐧 **Linux** | Python | 린트 잡(코드 규칙 · include 순서 게이트) — 아래 빌드 잡은 이것이 통과해야 돈다 | [Lint](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml) |
| 🪟 **Windows** | `clang-cl` (LLVM) | `CI-Debug` (LiveReload DLL 모듈 + `nogpu` 시험 + `SW_ENABLE_DEADLOCK_DETECTION` 컴파일 검사) | [Windows Debug](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml) |
| 🪟 **Windows** | `clang-cl` (LLVM) | `CI-Debug-STL` (`SW_ENABLE_STL_CONTAINER` — 표준 컨테이너 구성이 컴파일되는지) | [Windows STL](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml) |
| 🪟 **Windows** | `clang-cl` (LLVM) | `CI-Shipping` (최적화 정적 단일 실행파일 + 에셋 쿠킹) | [Windows Shipping](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml) |
| 🐧 **Linux** | `clang++` (LLVM + LLD) | `CI-Debug` (LiveReload Shared 모듈 + 단위 테스트) | [Linux Debug](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml) |
| 🐧 **Linux** | `clang++` (LLVM + LLD) | `CI-Debug-ASAN` (AddressSanitizer 메모리 살균자) | [Linux ASan](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml) |
| 🐧 **Linux** | `clang++` (LLVM + LLD) | `CI-Debug-TSAN` (ThreadSanitizer 데이터 레이스 검출) | [Linux TSan](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml) |
| 🐧 **Linux** | `clang++` (LLVM + LLD) | `CI-Shipping` (최적화 정적 단일 실행파일 + 에셋 쿠킹) | [Linux Shipping](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml) |


---

## 📚 Wiki Navigation (문서 목차)

초심자이신가요? 아래의 주제별 위키 인덱스를 순서대로 읽어보시면 프로젝트의 전체 구조를 쉽게 파악할 수 있습니다.

- 🚀 **[Getting Started (시작하기)](docs/01_GettingStarted.md)**: 빌드 환경 구성(vcpkg, CMake) 및 첫 빌드/테스트 실행 가이드
- 🧩 **[Engine Subsystems (서브시스템 개요)](docs/02_EngineSubsystems.md)**: 렌더링, 오브젝트/컴포넌트, 스레드 풀, 리플렉션 등 핵심 엔진 기능 찾아보기
- 🔄 **[LiveReload & ABI (핫리로드 및 아키텍처)](docs/03_LiveReload_and_ABI.md)**: 게임을 끄지 않고 코드를 수정하는 원리와 주의사항
- 📝 **[Coding Guidelines (코딩 규칙)](docs/04_CodingGuidelines.md)**: 프로젝트에 기여할 때 지켜야 하는 C++ / CMake 네이밍 규칙
- 🎞️ **[RHI Frame Contract (프레임/렌더타깃 계약)](docs/05_RHI_FrameContract.md)**: 프레임 순서 · 백버퍼 바인딩 계약, 함정과 검증 프로토콜 — 프레임 순서를 건드리기 전에 반드시 읽을 것
- ✅ **[Backlog (작업 백로그)](docs/06_Backlog.md)**: 남은 일과 이어받기 — 여러 PC·여러 세션에서 이어서 작업할 때 먼저 읽을 것
- 🏛️ **[Engine Structure vs Commercial (상용 엔진과의 구조 대조)](docs/07_EngineStructureVsCommercial.md)**: `Source/Engine` 의 의존 방향을 언리얼 · Unity · Godot 와 견준 결과

> **심화 문서**: 전체 아키텍처 다이어그램 및 제약 사항은 **[ARCHITECTURE.md](ARCHITECTURE.md)**를 참고하세요.

---

## 📑 상세 목차 (본문)

1. [📖 핵심 아키텍처 및 빌드 모드](#1-핵심-아키텍처-및-빌드-모드)
2. [📂 디렉터리 레이아웃](#2-디렉터리-레이아웃)
3. [📦 압축 직렬화 스트림 (Compression Stream & Pluggable Codecs)](#3-압축-직렬화-스트림-compression-stream--pluggable-codecs)
   - [설계 원리 및 인터페이스 분리](#31-설계-원리-및-인터페이스-분리)
   - [바이너리 컨테이너 헤더 규격](#32-바이너리-컨테이너-헤더-규격)
   - [사용자 코덱 확장 및 등록 방법](#33-사용자-코덱-확장-및-등록-방법)
   - [C++ 실무 사용 예제 코드](#34-c-실무-사용-예제-코드)
4. [🛠️ 빌드와 실행](#4-빌드와-실행)
5. [🧩 엔진 핵심 서브시스템 실무 사용법](#5-엔진-핵심-서브시스템-실무-사용법)
   - [1. GameObject & Component 라이프사이클](#51-gameobject--component-라이프사이클)
   - [2. RHI 멀티 백엔드 렌더링 파이프라인](#52-rhi-멀티-백엔드-렌더링-파이프라인)
   - [3. 리플렉션 및 다중 포맷 직렬화](#53-리플렉션-및-다중-포맷-직렬화)
   - [4. 씬 관리 및 프리팹 (Prefab) 시스템](#54-씬-관리-및-프리팹-prefab-시스템)
   - [5. 모듈 핫리로드 (LiveReload) 시스템](#55-모듈-핫리로드-livereload-시스템)
   - [6. 공간 분할 인덱싱 (SpatialQuadTree & SpatialOctree)](#56-공간-분할-인덱싱-spatialquadtree--spatialoctree)
   - [7. 런타임 파일 감시 & 에셋 핫리로드 (FileWatchDispatcher)](#57-런타임-파일-감시--에셋-핫리로드-filewatchdispatcher)
   - [8. 멀티스레드 태스크 시스템 (Task DAG)](#58-멀티스레드-태스크-시스템-task-dag)
   - [9. 오디오 및 물리 시스템](#59-오디오-및-물리-시스템)
   - [10. 비동기 에셋 스트리밍 큐 (AssetStreamingQueue)](#510-비동기-에셋-스트리밍-큐-assetstreamingqueue)
   - [11. GPU-Driven 간접 드로우 & 컴퓨트 디스패치](#511-gpu-driven-간접-드로우--컴퓨트-디스패치)
   - [12. 바인드리스 리소스 인덱스 (IRHIResourceFactory)](#512-바인드리스-리소스-인덱스-irhiresource)
   - [13. RenderGraph 순차 쓰기/RMW 의존성 및 리소스 수명 주기 분석](#513-rendergraph-순차-쓰기rmw-의존성-및-리소스-수명-주기-분석)
   - [14. C++17 Fluent Task Continuation (TaskFuture / TaskPromise)](#514-c17-fluent-task-continuation-taskfuture--taskpromise)
   - [15. 트랜스폼 더티 루트 플러시 (SceneTransformHierarchy)](#515-트랜스폼-더티-루트-플러시-scenetransformhierarchy)
6. [✍️ 코딩 컨벤션 및 네이밍 규칙](#6-코딩-컨벤션-및-네이밍-규칙)
7. [🧪 자동화 테스트 스위트](#7-자동화-테스트-스위트)

---

## 1. 핵심 아키텍처 및 빌드 모드

```
                    ┌─────────────────────────────────────────┐
                    │               App.exe                   │
                    │  (Thin Launcher · EngineLoop · Host)    │
                    └───────────┬───────────────────┬─────────┘
                                │                   │
                                ▼                   ▼
        ┌──────────────────────────────────┐   ┌───────────────────────────┐
        │       Engine.dll (Dev)           │   │      RuntimeAPI (Header)  │
        │  (RHI, Scene, Object, Reflection)│   │  (Pure C-ABI Module ABI)  │
        └───────────────┬──────────────────┘   └─────────────┬─────────────┘
                        │                                    │
                        ▼                                    ▼
        ┌──────────────────────────────────┐   ┌───────────────────────────┐
        │       Core (Static Lib)          │   │  Game Modules / Kits      │
        │  (Log, Memory, Concurrency,      │   │  (SWGame, GF_Overworld,   │
        │   Compression, Delegate, Util)   │   │   EditorModule - LiveReload)│
        └──────────────────────────────────┘   └───────────────────────────┘
```

- **Dev (개발 모드)**:
  - `Engine`은 DLL로 분리되고, `SWGame`(게임 모듈), `GF_*`(장르 키트), `EditorModule`(에디터), `RHI_*`(백엔드)는 런타임에 동적 로드됩니다.
  - 게임 코드를 수정한 뒤 빌드하면 `LiveReloadManager`(App)가 변경된 DLL을 그림자 복사(Shadow Copy)하여 런타임 중에 갈아끼웁니다.
- **Shipping (배포 모드)**:
  - 에디터 및 핫리로드 레이어가 제거되고, 모든 서브시스템이 단일 `.exe` 바이너리로 정적 링크(STATIC)됩니다.
- **엔진 기동 · 종료**:
  - 순서는 단계 표 하나(`Source/Engine/EngineInitStepList.xxx`)가 정합니다. 단계마다 구조체 하나(`initialize` · `shutdown` · `destroy`)이고,
    `EngineInitSequence` 가 표의 의존을 위상 정렬해 세우고 역순으로 내립니다. `EngineLoop` 와 시험 하네스가 같은 부트스트랩(`EngineBootstrap`)을 씁니다.
  - 씬은 모든 타입 공급자가 등록을 끝낸 단계(`ModuleTypes`) 뒤에만 읽고, `ModuleHost` 는 게임 인스턴스를 에디터보다 먼저 세웁니다.
- **RuntimeAPI 계약**:
  - `App.exe`와 DLL 모듈 간의 통신은 순수 C-ABI 헤더(`RuntimeAPI`)의 함수 테이블을 통해 완전히 격리됩니다.
- **DLL Export / Import (API) 매크로 규칙**:
  - `SW_API`: **Engine.dll**의 심볼을 노출하거나 참조할 때 사용합니다. (`SW_EXPORTS` 매크로에 반응)
  - `SW_MODULE_API`: **동적 모듈 플러그인(EditorModule.dll, SWGame.dll, RHI 백엔드 등)**의 진입점(C-ABI Entry Point)을 노출할 때 공통으로 사용하는 매크로입니다. (`SW_MODULE_EXPORTS`에 반응)
  - `SW_GF_API`: **GameFramework.dll** 클래스 심볼을 노출하거나 참조할 때 사용합니다. (`SW_GF_EXPORTS`에 반응)
  - `SW_GAMESERVICE_API`: RuntimeAPI GameService 로케이터(`bindGameService` / `getRawService`)용입니다. GameFramework 클래스 export 매크로(`SW_GF_API`)와는 별개입니다.

---

## 2. 디렉터리 레이아웃

| 폴더 경로 | 역할 및 설명 |
| :--- | :--- |
| `Source/Core` | 기초 유틸리티 (로깅, 메모리 · 메모리 태그, 문자열, 컨테이너, 압축 코덱 인터페이스, 델리게이트, 태스크, 파일 I/O, 명령줄 · 전역 변수 등). Engine 이 OBJECT 라이브러리로 흡수합니다 |
| `Source/Engine` | 핵심 엔진 라이브러리 (기동 단계 표, RHI · 렌더러, GameObject · Component, 씬 · 프리팹, 리플렉션 · 직렬화, 물리, 오디오, 입력 등) |
| `Source/RuntimeAPI` | App ↔ Editor/Game 모듈 간의 순수 C-ABI 통신 인터페이스 (Header-Only) |
| `Source/Editor` | 개발 모드 전용 ImGui 에디터 툴셋 (`EditorModule`) |
| `Source/GameFramework` | 장르별 공통 프레임워크 및 플러그형 키트 (`GF_Overworld`, `GF_TurnBattle`, `GF_ActionCombat`) |
| `Source/Games` | 실제 게임 프로젝트 소스코드 (`Empty` 등 / `SW_ACTIVE_GAME` 변수로 빌드 대상 지정) |
| `Source/App` | 얇은 진입점 실행 파일 (프레임 순서, 모듈 호스트 · 핫리로드, 백엔드 교체) |
| `Resource/` | 런타임 에셋 — `engine/` · `common/` · `game/<게임>/` 로 나뉘고 이름은 전부 소문자입니다. 텍스처는 DDS 로만 읽고 원본 이미지는 `textures_raw/` 에 둡니다 |
| `Config/` | 엔진 · 에디터 · 게임 · 앱 설정 JSON 과 쿠킹 표(`Config/Engine/CookContract.json`) |
| `Scripts/` | 환경 설정, vcpkg 패키지 복원, 코드 생성, 린트(`Scripts/lint/{gate,fixer,report,selftest}`) 및 자동화 파이썬 도구 |
| `cmake/` | 빌드 옵션(`cmake/Config/BuildOptions.cmake`) · 타깃 규칙 · 리플렉션 코드젠 단계 등 CMake 모듈 |
| `Tools/` | ReflectionParser (libclang 기반 리플렉션 코드 생성기). LLVM · Ninja · Sccache · vcpkg 는 셋업 스크립트가 받아 두는 자리입니다 |
| `Test/` | 자동화 테스트 (`CoreTest`, `EngineTest`, `ReflectionTest`, `SmokeTest`, `EditorTest`, `EditorUiTest`, `AppTest` + `TestFramework`) |
| `docs/` | 위키 문서(위 목차) |

---

## 3. 압축 직렬화 스트림 (Compression Stream & Pluggable Codecs)

SW Engine은 세이브 파일, 네트워크 패킷, 바이너리 씬 데이터의 디스크 I/O 최적화를 위해 **플러그형 압축 직렬화 스트림**을 제공합니다.

### 3.1 설계 원리 및 인터페이스 분리

압축 알고리즘을 바꾸거나 더해도 **기존 직렬화 코드나 클라이언트 로직을 수정할 필요가 없도록** 인터페이스와 구현체가 분리되어 있습니다:

```
┌─────────────────────────────────────────────────────────────┐
│  클라이언트 코드 (SaveSlot / BinarySerializer / SceneSerializer) │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│       BinarySerializer / CompressionStream (Core)           │
│   (Magic 'SWCS' · 헤더 캡슐화 · FNV-1a 무결성 체크섬 검증)      │
└──────────────────────────────┬──────────────────────────────┘
                               │ (ICompressionCodec 포인터 디스패칭)
                               ▼
┌─────────────────────────────────────────────────────────────┐
│          CompressionCodecRegistry (엔진 서비스 레지스트리)        │
│   ┌─────────────────────┬───────────────────┬────────────┐  │
│   │ NullCompressionCodec│RleCompressionCodec│LZ4·Zstd·Zlib│ │
│   │   (Pass-through)    │  (내장 고속 RLE)   │  (Engine)  │  │
│   └─────────────────────┴───────────────────┴────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

- **`ICompressionCodec`**: 모든 압축 알고리즘이 구현해야 하는 순수 가상 인터페이스 (`compress`, `decompress`, `compressBound`, `getCodecType`, `getCodecName`).
- **`NullCompressionCodec`** · **`RleCompressionCodec`** (`Source/Core/Compression`): 무압축(Pass-through) 코덱과 외부 라이브러리 없는 바이트 런 압축기.
  레지스트리가 연결되지 않은 도구 경로(Core 만 링크하는 `ReflectionParser` 등)에서도 이 둘은 늘 쓸 수 있습니다.
- **`Lz4CompressionCodec`** · **`ZstdCompressionCodec`** · **`ZlibCompressionCodec`** (`Source/Engine/Compression`): 외부 라이브러리로 구현한 코덱.
  Core 는 압축 라이브러리에 의존하지 않으므로 Engine 이 가져와 `EngineCompressionCodecUtil::registerAll` 이 한 번에 등록합니다(목록은 그 헤더 한 곳).
- **`CompressionCodecRegistry`**: 코덱을 등록 · 조회 · 교체하는 레지스트리. 엔진 서비스(`engine::getCompressionCodecRegistry()`)로 노출됩니다.
- **`CompressionStream`**: 바이너리 헤더(`CompressionHeader`)와 FNV-1a 무결성 체크섬 검증을 캡슐화한 헬퍼.
- **`BinarySerializer`**: 리플렉션 객체의 콤팩트 바이너리 직렬화 및 압축 스트림 직렬화(`serializeCompressed`)를 통합 지원.

---

### 3.2 바이너리 컨테이너 헤더 규격

`CompressionStream`이 생성하는 바이너리 스트림은 항상 28바이트 헤더로 시작합니다(디스크 포맷이라 `static_assert` 가 크기를 고정합니다):

```cpp
#pragma pack( push, 1 )
struct SW_API CompressionHeader
{
    static constexpr uint32 kMagic        = 0x53574353; // 'SWCS' (SW Compression Stream)
    static constexpr uint8  kVersion      = 1;          // 읽을 때 이 값만 받는다
    static constexpr uint16 kFlagChecksum = 0x01;       // 해제한 뒤 _checksum 을 검증하라는 표시

    uint32               _magic{ kMagic };
    uint8                _version{ kVersion };
    CompressionCodecType _codecType{ CompressionCodecType::None }; // 1바이트 (None 0 · RLE 1 · LZ4 2 · Zstd 3 · Zlib 4 · Custom 255)
    uint16               _flags{ 0 };
    uint64               _uncompressedSize{ 0 };
    uint64               _compressedSize{ 0 };
    uint32               _checksum{ 0 };                           // 원본의 FNV-1a 32비트 체크섬
};
#pragma pack( pop )
```

`CompressionCodecType` 의 값은 디스크에 그대로 실리므로 새 코덱은 **뒤에 덧붙이기만** 합니다.

---

### 3.3 사용자 코덱 확장 및 등록 방법

LZ4 · Zstd · Zlib 은 이미 엔진에 있습니다. 그 밖의 알고리즘은 `ICompressionCodec` 을 구현해 `CompressionCodecType::Custom` 으로 등록합니다:

```cpp
#include "Core/Compression/ICompressionCodec.h"
#include "Core/Compression/CompressionCodecRegistry.h"

namespace sw
{
    class MyCompressionCodec final : public ICompressionCodec
    {
    public:
        CompressionCodecType getCodecType() const override { return CompressionCodecType::Custom; }
        const utf8*          getCodecName() const override { return "MyCodec"; }
        size_t               compressBound( size_t uncompressedSize ) const override { return uncompressedSize + 16; }

        bool compress( const void* pSrc, size_t srcSize, void* pDst, size_t dstCapacity, size_t& outCompressedSize,
                       int32 compressionLevel = 0 ) override;
        bool decompress( const void* pSrc, size_t srcSize, void* pDst, size_t dstCapacity, size_t& outUncompressedSize ) override;
    };
} // namespace sw

// 모듈 initialize 에서 등록하고, 그 모듈의 shutdown 에서 해제한다
sw::engine::getCompressionCodecRegistry().registerCodec( sw::make_unique<sw::MyCompressionCodec>() );
// ...
sw::engine::getCompressionCodecRegistry().unregisterCodec( sw::CompressionCodecType::Custom );
```

> **레지스트리의 소유자는 엔진 하나입니다.** `EngineLoop` 의 기동 단계(시험에서는 `Test/TestFramework/main.cpp` 의 하네스)가
> 만들고 `CompressionCodecRegistry::setActive` 로 Core 의 슬롯에 연결합니다. `engine::getCompressionCodecRegistry()` 와
> `CompressionCodecRegistry::getActive()` 는 같은 인스턴스라 어느 쪽으로 등록해도 같고, 등록한 코덱은 `CompressionStream`
> (즉 `Archive` · `BinarySerializer` 의 압축 섹션)이 **바로 집어 씁니다.** 슬롯이 비어 있으면(Core 만 링크하는 도구) 스트림은 내장 코덱만 씁니다.
>
> 주의할 것 둘:
>
> - **로드 가능한 모듈에서 등록했다면 그 모듈의 shutdown 에서 `unregisterCodec` 하십시오.** 레지스트리는
>   `Engine.dll` 에 살아 모듈보다 오래 갑니다 — 모듈이 내려가면 남은 코덱의 vtable 이 언맵된 주소가 됩니다.
> - **리소스 팩(`.pack`)의 압축 enum 은 따로입니다.** 팩은 자기 포맷 enum(`PackCompressionType`)을 엔트리 헤더에 박고,
>   두 enum 은 서로 다른 파일의 독립된 포맷이라 값이 다릅니다. 팩은 표 한 곳(`PackCompressionUtil::kArrCodecMapping`)에서
>   `CompressionCodecType` 으로 옮긴 뒤 같은 레지스트리에서 코덱을 찾습니다 — 두 enum 을 `static_cast` 로 오가지 마십시오.

---

### 3.4 C++ 실무 사용 예제 코드

#### 1) 메모리 버퍼 직접 압축 및 복원
```cpp
#include "Core/Compression/CompressionStream.h"

// 데이터 준비
sw::string originalText = "대규모 게임 데이터 스트림...";
sw::vector<sw::uint8> listCompressedStream;

// 1. 압축 (RLE 또는 기본 코덱)
bool bCompressed = sw::CompressionStream::compressBuffer(
    originalText.data(),
    originalText.size(),
    listCompressedStream,
    sw::CompressionCodecType::RLE
);

// 2. 역압축 (헤더를 읽어 코덱 자동 판별 및 체크섬 검증 수행)
sw::vector<sw::uint8> listRestored;
bool bDecompressed = sw::CompressionStream::decompressBuffer(
    listCompressedStream.data(),
    listCompressedStream.size(),
    listRestored
);
```

#### 2) 리플렉션 객체(세이브 데이터) 원클릭 압축 직렬화
```cpp
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Reflection/ReflectionCore.h"

// 리플렉션이 선언된 게임 플레이어 데이터 구조체
PlayerSaveData playerData{};
playerData._level = 50;
playerData._gold = 99999;
playerData._listInventory = { 101, 102, 105 };

// 1. 플레이어 데이터를 바이너리로 변환 후 즉시 압축 (REFLECT 된 타입은 StaticType() 이 TypeInfo 를 준다)
const sw::TypeInfo*   pTypeInfo = PlayerSaveData::StaticType();
sw::vector<sw::uint8> saveFileBytes;

if ( sw::BinarySerializer::serializeCompressed( &playerData, *pTypeInfo, saveFileBytes, sw::CompressionCodecType::Zstd ) == false )
    return false;

// 2. 세이브 파일 로드 및 역압축 역직렬화 (헤더가 코덱을 알려 준다)
PlayerSaveData loadedData{};
if ( sw::BinarySerializer::deserializeCompressed( &loadedData, *pTypeInfo, saveFileBytes.data(), saveFileBytes.size() ) == false )
    return false;
```

---

## 4. 빌드와 실행

자세한 환경 구성은 [Getting Started](docs/01_GettingStarted.md) 에 있습니다. 요약:

```powershell
py -3 Scripts/setup/SetupEnvironment.py       # 툴체인(LLVM · Ninja · sccache) 받기
py -3 Scripts/setup/SetupVcpkg.py --install   # vcpkg 매니페스트 복원
cmake --preset Ninja-Debug
cmake --build --preset Ninja-Debug
```

- 프리셋: `Ninja-Debug` · `Ninja-Debug-ASAN` · `Ninja-Release` · `Ninja-Shipping`(Windows clang-cl), `WSL-*`(Linux clang), `CI-*`(CI 전용).
- 산출물은 `build/<프리셋>/Bin` 입니다(`App.exe` · `Engine.dll` · 모듈 DLL). 주요 CMake 옵션은 `cmake/Config/BuildOptions.cmake` 의 `SW_*` 입니다.

### 실행 인자

명령줄 인자의 정본은 `Source/Core/Predefined/ArgumentList.xxx` 입니다. 키 앞의 `-` · `--` 는 떼고 읽으며, 값은 `-key=value` 로 줍니다
(bool 인자만 `-key` 로 켤 수 있습니다). 이름은 대소문자를 구별합니다.

| 인자 | 하는 일 |
| :--- | :--- |
| `-W=<폭>` · `-H=<높이>` (`-WIDTH` · `-HEIGHT`) | 창 크기. 주지 않으면 `EngineConfig` 의 창 설정을 씁니다 |
| `-vsync` | 수직 동기화를 켭니다 |
| `-dx11` · `-dx12` · `-vk` · `-gl` (별칭 `d3d11` · `directx11` · `d3d12` · `directx12` · `vulkan` · `spirv` · `opengl`) | RHI 백엔드. 목록 · 별칭 · 기본값(`DirectX12`)은 쿠킹 표 `Config/Engine/CookContract.json` 에서 옵니다 |
| `-EnableEditor` | 에디터 모듈을 올립니다(Dev 전용). 주지 않으면 에디터 없이 뜹니다 |
| `-lang=<코드>` · `-language=<코드>` | 시작 언어 |
| `--bake-shaders` | 창 없이 셰이더를 전부 굽고 끝냅니다(패스 종류 표 × 뷰 모드에서 요청을 모읍니다) |
| `--cook-scenes --cooked-dir=<폴더>` | 창 없이 씬 · 프리팹 · 에셋 레지스트리를 굽습니다. 소스 트리를 읽고, 모르는 컴포넌트(`MissingComponent`)가 든 씬은 실패로 셉니다 |
| `--bake-textures` · `--check-textures` | 창 없이 `textures_raw/` 의 원본을 DDS 로 굽거나, 원본 · 굽기 규칙 · DDS 가 `bake.stamp` 와 맞는지 보기만 합니다. 에디터 모듈의 일이라 Dev 빌드에서만 됩니다 |
| `-gv_<이름>=<값>` | 전역 변수(`gv_*`)를 정합니다. 예: `-gv_rhiBackend=Vulkan` · `-gv_editorStartupScene=<씬 경로>` · `-gv_benchMeshes=8000 -gv_profileFrames=600` |

헤드리스 작업(`--bake-*` · `--cook-scenes` · `--check-textures`)이 실패하면 `App.exe` 는 0 이 아닌 종료 코드로 끝납니다 — 쿠커 스크립트가 그것을 봅니다.
`SW_TEST_GLOBAL_VARIABLE_*` 로 선언한 진단 · 벤치 변수는 에디터 패널에 보이지 않고, `SW_KEEP_IN_SHIPPING` 을 준 것만 배포본에 남습니다.

---

## 5. 엔진 핵심 서브시스템 실무 사용법

### 5.1 GameObject & Component 라이프사이클

`GameObjectManager`를 기반으로 `GameObject`에 컴포넌트를 부착하고 라이프사이클을 제어합니다.

```cpp
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/CameraComponent.h"

// 1. 게임 오브젝트 생성
sw::GameObject* pPlayer = pObjectManager->createGameObject( "Hero" );

// 2. 컴포넌트 부착 (컴포넌트 틱 중에는 구조 변경이 틱 뒤로 미뤄져 nullptr 을 돌려준다 — executeOrDeferPostTick 으로 감쌀 것)
sw::SceneComponent* pScene = pPlayer->addComponent<sw::SceneComponent>();
pScene->setLocalPosition( sw::float3{ 100.0f, 0.0f, 200.0f } );

// 3. 계층 구조 구성 (Parent - Child). 틱 중에는 부모를 바꾸지 않는다.
sw::GameObject* pWeapon = pObjectManager->createGameObject( "Sword" );
if ( pWeapon->attachToParent( pPlayer ) == false ) // 플레이어의 회전/이동이 자식에 전파된다
    return;

// 4. 태그 등록 및 검색
const sw::TagID heroTag = sw::TagID::request( "Player.Hero" );
pPlayer->addTag( heroTag );
sw::vector<sw::GameObject*> listPlayer;
pObjectManager->findGameObjectsByTag( heroTag, listPlayer );
```

프레임을 넘어 들고 있을 참조는 생포인터가 아니라 `GameObjectHandle` · `ComponentHandle` 로 두고 쓸 때마다 매니저로 풉니다([AGENTS.md](AGENTS.md)).

---

### 5.2 RHI 멀티 백엔드 렌더링 파이프라인

`IRHIDevice`는 DirectX 11, DirectX 12, Vulkan(1.3 이상), OpenGL을 균일하게 추상화합니다. Dev 에서는 백엔드마다 `RHI_*` 모듈 DLL 이고,
실행 중 `gv_rhiBackend` 를 바꾸면 App 이 프레임 경계에서 디바이스를 교체합니다(`RHIBackendSwitcher`).

- **백엔드 선택**: 명령줄(`-dx11` · `-dx12` · `-vk` · `-gl`, [4절](#실행-인자)) > `EngineConfig` 의 기본값. 백엔드 목록은 `Config/Engine/CookContract.json` 한 곳입니다.
- **프레임 그래프는 데이터입니다.** 패스 순서는 `Resource/engine/pipeline/*.xml`(`RenderPipelineAsset`)이, 패스의 바인딩 틀(포맷 · 클리어)은
  `Resource/engine/renderpass/`(`RenderPassAsset`)가 정하고, `FrameRenderer` 가 그것으로 `RenderGraph` 를 지어 위상 정렬합니다(코드 예는 5.13).
- **디바이스 종료**는 `IRHIDevice::shutdown` 템플릿 메서드가 공통 단계 순서를 갖고 백엔드는 단계 훅만 구현합니다.
- **엔진 밖 모듈의 네이티브 자원**: 에디터 같은 모듈이 백엔드 네이티브 객체가 필요하면 백엔드 클래스로 캐스팅하지 않고
  판 번호 든 `RHINativeHandles` 를 디바이스에서 조회하고, 다 쓴 네이티브 자원은 `IRHIDevice::enqueueGpuRelease` 로 백엔드 해제 큐(GPU 펜스 뒤)에 넘깁니다.

---

### 5.3 리플렉션 및 다중 포맷 직렬화

C++ 헤더에 어노테이션 매크로를 작성하면 `ReflectionParser`가 빌드 타임에 타입 정보를 자동 생성합니다.

```cpp
// CharacterStats.h
#pragma once
#include "Engine/Reflection/ReflectionMacros.h"

REFLECT( Category = "Combat" )
class SW_API CharacterStats
{
public:
    REFLECT_BODY();

    FUNCTION( Category = "Combat", CallInEditor )
    void heal() { _health += 10.0f; }

    PROPERTY( Category = "Combat", Tooltip = "Current hit points", Min = 0.0, Max = 1000.0 )
    float32 _health{ 100.0f };
};
```

- 헤더에 처음 `REFLECT` · `ENUM` 을 넣었으면 **다시 configure** 해야 파서가 그 헤더를 봅니다(증상은 `X::StaticType()` 링크 오류).
- 메타 키는 파서의 필드 표가 정한 것만 받습니다 — 모르는 키는 코드젠 오류입니다(`Tools/ReflectionParser/README.md`).
  이름을 바꿀 때는 옛 이름 호환(`Alias` · `ValueAlias`)을 두지 않고 데이터를 새 이름으로 다시 씁니다 — 별칭은 다시 쓸 수 없는 데이터(배포한 세이브 등)가 생긴 뒤의 창구입니다.

- **JSON 직렬화**: `JsonSerializer::serialize(&stats, *pTypeInfo)`
- **XML 직렬화**: `XmlSerializer::serialize(&stats, *pTypeInfo)`
- **바이너리 직렬화**: `BinarySerializer::serialize(&stats, *pTypeInfo, listBuffer)`
- **압축 바이너리 직렬화**: `BinarySerializer::serializeCompressed(&stats, *pTypeInfo, listBuffer)`

---

### 5.4 씬 관리 및 프리팹 (Prefab) 시스템

```cpp
#include "Engine/Scene/SceneManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/ResourceManager.h"

// 1. 비동기 씬 로드 (TaskManager 워커에서 파싱하고, 끝나면 tickTransitions 가 활성 씬으로 바꿔 넣는다)
//    경로는 도메인을 포함한 리소스 id 다(engine/ · common/ · game/<게임>/).
sw::TaskFuture<sw::Scene*> sceneFuture = sw::engine::getSceneManager().requestLoadFuture( "game/empty/maps/editortest.scene.xml" );

// 2. 프리팹 스폰 (Dev 는 XML/JSON 저작본, Shipping 은 쿠킹된 .prefab.bin 을 읽는다)
sw::GameObject* pProp = sw::engine::getResourceManager().getPrefabCache().spawn(
    pObjectManager, "game/empty/prefabs/testprop.prefab.xml" );
```

- 씬에 놓인 프리팹 인스턴스는 **덮어쓴 값만** 저장하고, 로드가 프리팹 원형 위에 얹습니다(`PrefabOverrides`).
- 씬 · 프리팹은 지금 형식만 읽습니다(옛 판 · 별칭 읽기 없음). 모르는 컴포넌트 타입은 `MissingComponent` 로 남고, 그런 씬은 쿠킹에서 실패로 셉니다.

---

### 5.5 모듈 핫리로드 (LiveReload) 시스템

개발 모드에서 게임 코드를 수정하고 Visual Studio / CLion / CMake에서 빌드 버튼을 누르면(또는 Ctrl+F7 · 에디터의 빌드):
1. `LiveReloadManager`(App)가 DLL 변경을 파일 워처로 감지하고, 파일이 잠잠해질 때까지 기다립니다(빌드 중에는 모으기만 합니다).
2. 그 모듈에 의존하는 모듈까지 묶어(연쇄 리로드), 새 DLL 을 모두 그림자 복사본으로 올리고 엔진 ABI 도장을 대조합니다(prepare).
3. 모듈마다 실행 중인 워커 태스크를 비우고 게임 상태를 메모리 직렬화 버퍼로 덤프한 뒤 인스턴스를 내립니다(`ModuleHost::suspendModules`).
4. 옛 DLL 을 새 이미지로 바꿉니다(commit).
5. 리플렉션 타입(컴포넌트 생성 함수는 `TypeInfo` 의 칸)을 다시 등록하고 상태를 역직렬화하여 복원합니다.

적용 전에 실패하면 옛 모듈을 그대로 두고 계속 돌고, 적용 뒤의 결함만 리로드 그래프를 막습니다. 모듈 안의 static · 싱글턴은 교체 때 사라지므로
남아야 하는 상태는 `Engine` 이나 `App` 에 둡니다. 원리와 주의점은 [LiveReload & ABI](docs/03_LiveReload_and_ABI.md) 에 있습니다.

---

### 5.6 공간 분할 인덱싱 (SpatialQuadTree & SpatialOctree)

대규모 2D/3D 씬에서 $O(N)$ 전수 조사를 피하고 영역 · 점 · 구체 쿼리를 지원합니다. 같은 폴더에 `BVHTree3D` 와 `SpatialHashGrid2D` 도 있습니다.

```cpp
#include "Engine/Spatial/SpatialQuadTree.h"
#include "Engine/Spatial/SpatialOctree.h"

// 1. 2D 쿼드트리 (Top-down / 2D 월드 영역)
sw::SpatialQuadTree quadTree( sw::AABB2D{ sw::float2{ -5000.0f, -5000.0f }, sw::float2{ 5000.0f, 5000.0f } } );
quadTree.insert( pObject->getObjectId(), sw::AABB2D{ sw::float2{ 100.0f, 100.0f }, sw::float2{ 150.0f, 150.0f } }, pObject );

sw::vector<sw::SpatialElement2D> listVisible2D;
quadTree.queryRange( sw::AABB2D{ sw::float2{ 0.0f, 0.0f }, sw::float2{ 800.0f, 600.0f } }, listVisible2D );

// 2. 3D 옥트리 (3D 월드 AABB / 구체(Sphere) 반경 쿼리)
sw::SpatialOctree octree( sw::AABB{ sw::float3{ -1000.0f, -1000.0f, -1000.0f }, sw::float3{ 1000.0f, 1000.0f, 1000.0f } } );
octree.insert( 101, sw::AABB{ sw::float3{ 10.0f, 0.0f, 10.0f }, sw::float3{ 20.0f, 10.0f, 20.0f } }, pObject );

sw::vector<sw::SpatialElement3D> listVisible3D;
octree.queryRange( sw::AABB{ sw::float3{ 0.0f, -50.0f, 0.0f }, sw::float3{ 100.0f, 50.0f, 100.0f } }, listVisible3D );

// 구체 쿼리는 상자 위의 가장 가까운 점까지의 거리로 판정한다
sw::vector<sw::SpatialElement3D> listExplosionTarget;
octree.querySphere( sw::float3{ 15.0f, 5.0f, 15.0f }, 25.0f, listExplosionTarget );
```

---

### 5.7 런타임 파일 감시 & 에셋 핫리로드 (FileWatchDispatcher)

텍스처, 셰이더, XML 파일이 외부 툴에서 수정되면 실시간으로 감지하여 콜백을 실행합니다. **에디터(Dev) 기능입니다** —
`FileWatchDispatcher` 는 `Source/Editor/Common/Workspace` 에 있고 `AssetHotReload` 가 소유합니다(Windows `ReadDirectoryChangesW` · Linux `inotify`).

```cpp
#include "Editor/Common/Workspace/FileWatchDispatcher.h"

// 경로 접두사 · 확장자가 맞는 변경만 콜백으로 온다
const sw::FileWatchHandle handle = pFileWatchDispatcher->registerWatch(
    sw::ResourceUtil::getRootFolderPath(),
    { ".hlsl", ".hlsli" },
    SW_DELEGATE_LAMBDA( sw::FileWatchMatchDelegate, []( const sw::FileChangeEvent& event )
    {
        SW_LOG_INFO( "Shader modified: %#", event._filename.c_str() );
    } ) );

// 더 이상 필요 없으면 해제
pFileWatchDispatcher->unregisterWatch( handle );
```

텍스처 원본(`textures_raw/`)을 고치면 에디터가 DDS 를 다시 굽습니다.

---

### 5.8 멀티스레드 태스크 시스템 (Task DAG)

워커 풀 위에서 도는 태스크 그래프(DAG)입니다. 태스크는 `TaskHandle` 로 선후 관계(`then` · `runAfter` · `runBefore`)를 조립한 뒤 `submit` 하고,
우선순위(`TaskPriority::High` · `Normal` · `Low`)는 레인으로 갈려 렌더 기록 같은 High 일이 게임 잡에 밀리지 않습니다.

```cpp
#include "Core/Task/TaskManager.h"
#include "Engine/Common/EngineParallel.h"

sw::TaskManager& taskManager = sw::engine::getTaskManager();

// 1. 백그라운드 태스크 + 후속 태스크
sw::TaskHandle loadTask = taskManager.emplaceTask( "LoadNavMesh", SW_DELEGATE_LAMBDA( sw::TaskDelegate, []()
{
    // 백그라운드 계산 (예: 길찾기 준비)
} ) );
sw::TaskHandle doneTask = loadTask.then( SW_DELEGATE_LAMBDA( sw::TaskDelegate, []() { SW_LOG_INFO( "NavMesh ready" ); } ) );
loadTask.submit(); // 만든 태스크는 submit 해야 스케줄러에 들어간다
doneTask.submit();

// 2. 대규모 데이터 병렬 처리 — [0, count) 를 구간으로 나눠 돌리고 끝날 때까지 기다린다
//    문턱(serialThreshold)보다 작은 일은 나누지 않는다(디스패치 비용이 더 크다).
sw::engine::runParallel( count, 2048, SW_DELEGATE_LAMBDA( sw::ParallelBlockDelegate, [pData]( sw::uint32 start, sw::uint32 end )
{
    for ( sw::uint32 index = start; index < end; ++index )
        pData[index].update(); // 워커는 컨테이너를 만지지 말고 포인터만 받는다
} ) );
```

병렬 시스템 하나의 모양은 "상태를 가진 시스템 타입 + `engine::runParallel` 한 줄 + 매니저 tick 의 단계 한 줄" 입니다(`SceneTransformHierarchy` 가 첫 예).

---

### 5.9 오디오 및 물리 시스템

```cpp
#include "Engine/Audio/IAudioSystem.h"
#include "Engine/Physics/ContinuousCollision.h"
#include "Engine/Physics/PhysicsWorld.h"

// 오디오: 효과음은 비동기 재생, 배경음악은 루프 재생. 로딩 중에 preload 해 두면 첫 재생이 끊기지 않는다.
sw::IAudioSystem& audio = sw::engine::getAudioSystem();
audio.preload( "game/<게임>/audio/sfx_jump.wav" );
audio.play( "game/<게임>/audio/sfx_jump.wav" );
audio.playMusic( "game/<게임>/audio/bgm_field.ogg" );

// 물리: AABB 바디 월드 — 겹침 이벤트(step), 영역 쿼리, 스윕(연속 충돌) 쿼리
sw::SweepHit hit{};
if ( physicsWorld.sweepTest( movingBox, sw::float3{ 500.0f, 0.0f, 0.0f }, layer, hit ) )
    SW_LOG_INFO( "Hit object %# at t=%#", hit._hitObjectId, hit._time );
```

연속 충돌(ContinuousCollision)의 원리와 쓰는 법은 `Source/Engine/Physics/README.md` 에 있습니다.

---

### 5.10 비동기 에셋 스트리밍 큐 (AssetStreamingQueue)

게임플레이 중 버벅임(Frame Drop / Stuttering)을 방지하기 위해 백그라운드 워커 스레드에서 에셋을 사전 로드(Prefetch)하고 완료 콜백을 스레드-안전하게 전달합니다.

```cpp
#include "Engine/Resource/AssetStreamingQueue.h"

// 우선순위(Low · Normal · High · Immediate) 기반 비동기 프리로드 요청
sw::engine::getAssetStreamingQueue().requestAsset(
    "game/<게임>/textures/boss_dragon.dds",
    sw::StreamingPriority::High,
    SW_DELEGATE_LAMBDA( sw::OnStreamingCompleteDelegate, []( sw::string_view assetPath, bool bSuccess )
    {
        if ( bSuccess )
            SW_LOG_INFO( "Asset streaming ready" );
    } ) );

// 완료 콜백은 엔진 루프가 메인 스레드에서 프레임마다 플러시한다(EngineLoop 가 update() 를 부른다).
// TaskFuture 로 받고 싶으면 requestAssetFuture 를 쓴다.
```

큐는 호스트 전용 엔진 서비스(`HostOnly`)라 `EngineLoop` 의 기동 단계가 만들고 내립니다 — 호출부에서 `initialize` 하지 않고, 게임 모듈에는 보이지 않습니다.

---

### 5.11 GPU-Driven 간접 드로우 & 컴퓨트 디스패치

CPU 개입 없이 GPU에서 직접 컬링 결과를 기반으로 드로우 콜을 발행하는 **GPU-Driven Rendering** 파이프라인입니다.
프로덕션 컴퓨트 셰이더는 넷 — `instanceanim.hlsl`(인스턴스 애니메이션) · `meshmorph.hlsl`(메시 모프) · `gpucull.hlsl`(컬링 + 커맨드 생성) ·
`instancesort.hlsl`(투명 바이토닉 정렬) — 이고 `FrameRenderer::dispatchInstanceAnimation` · `dispatchMeshMorph` ·
`dispatchCullAndSort`(`FrameRendererCompute.cpp`)가 돌립니다.

**컴퓨트 디스패치에는 별도 래퍼 클래스가 없습니다.** 커맨드 리스트가 직접 파이프라인·리소스 바인딩·디스패치를 받습니다.
간접 인자도 전용 버퍼 클래스가 아니라 **일반 RHI 버퍼**에 담고, 인자 구조체는
`RHIDrawIndexedIndirectCommand`(`Engine/Graphics/RHI/RHITypes.h`)입니다.

```cpp
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/RHITypes.h"

// 1. 쓰기 전에 UAV 상태로 옮긴다 — 이 전이 없이 쓰면 DX12/Vulkan 에서 쓰기가 조용히 무효다.
pCmd->transitionBuffer( instanceBuffer, sw::RHIBufferState::UnorderedAccess );

// 2. 파이프라인 · 상수버퍼(b0) · UAV(u0) 를 셰이더 레지스터와 1:1 로 건다.
pCmd->setComputePipelineState( cullPso );
pCmd->bindComputeConstantBuffer( cullCbIndex, 0 );
pCmd->bindComputeUav( instanceUavIndex, 0 );
pCmd->dispatchCompute( threadGroupCount, 1, 1 );

// 3. 다음 소비자(정점 셰이더·간접 드로우)가 읽기 전에 전이를 되돌린다.
pCmd->transitionBuffer( instanceBuffer, sw::RHIBufferState::ShaderResource );

// 4. 컬링이 채운 인자 버퍼를 CPU 가 보지 않고 그대로 드로우한다.
pCmd->drawIndexedIndirect( argBuffer );
```

실제 순서와 배리어 사유는 `FrameRendererCompute.cpp` 의 세 함수에 주석으로 적혀 있습니다 — 문서 예제보다 그쪽이 정본입니다.
슬롯 번호는 숫자 리터럴로 쓰지 않고 `bindingslots.hlsli`(HLSL · C++ 가 같은 파일을 include)의 `shaderslot` 상수로 씁니다.

---

### 5.12 바인드리스 리소스 인덱스 (IRHIResourceFactory)

텍스처와 버퍼를 전역 인덱스로 셰이더에서 접근할 수 있도록 디스크립터 인덱스를 발급합니다. 백엔드마다 한계가 다릅니다 —
DX12 · Vulkan 은 디스크립터 힙 · 세트 인덱싱이고, DX11(SM5.0) · OpenGL(SPIR-V)은 버퍼로 텍스처를 넘길 수 없어 머티리얼 텍스처를 고정 슬롯으로 겁니다.

별도의 테이블 클래스는 없습니다 — **인덱스 발급은 `IRHIResourceFactory` 가 직접** 합니다
(`Engine/Graphics/RHI/IRHIResourceFactory.h`). 백엔드마다 디스크립터 힙/디스크립터 세트 구현이 달라도
호출부는 인덱스 하나만 다룹니다.

```cpp
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

sw::IRHIResourceFactory* pResource = pRhiDevice->getResourceFactory();

// 텍스처 · 버퍼 · UAV 마다 발급 함수가 따로 있다 (해제도 짝을 맞춰야 한다).
sw::RHIDescriptorIndex albedoIndex = pResource->registerBindlessTexture( textureHandle );
sw::RHIDescriptorIndex materialIndex = pResource->registerBindlessResource( materialBuffer );

// 셰이더에는 인덱스(uint32)만 넘어간다 (binding.hlsli 의 g_SwMaterials 같은 구조 버퍼를 그 인덱스로 읽는다)

pResource->unregisterBindlessTexture( albedoIndex );
pResource->unregisterBindlessResource( materialIndex );
```

> **짝을 지켜야 한다.** 텍스처 인덱스를 `unregisterBindlessResource` 에 넘기면 엉뚱한 슬롯이 풀린다
> — 헤더 주석이 그 경고를 달고 있는 이유다.

---

### 5.13 RenderGraph 순차 쓰기/RMW 의존성 및 리소스 수명 주기 분석

RenderGraph는 패스 간 자원 의존성을 DAG 위상 정렬할 때 **Read-Modify-Write (동일 리소스 읽기 및 덮어쓰기)** 및 **순차 쓰기(Sequential Multi-Write)** 체인을 추적하고,
패스마다 필요한 상태 전이(배리어)를 추론하며, 트랜지언트 앨리어싱의 바탕이 되는 리소스 수명 주기(처음 ~ 마지막 사용 패스)를 산출합니다.
엔진에서는 `FrameRenderer` 가 파이프라인 XML 로 그래프를 짓습니다 — 아래는 API 모양입니다.

```cpp
#include "Engine/Graphics/Renderer/Graph/RenderGraph.h"

sw::RenderGraph graph;
// Pass A(쓰기) -> Pass B(읽기 & 덮어쓰기) -> Pass C(읽기)
graph.addPass( "PassA_Geometry", {}, { "ColorBuffer" } );
graph.addPass( "PassB_PostProcess", { "ColorBuffer" }, { "ColorBuffer" } );
graph.addPass( "PassC_UIOverlay", { "ColorBuffer" }, { "FinalOutput" } );

if ( graph.compile() == false ) // PassA -> PassB -> PassC 순서 (사이클이면 false)
    return;

// 트랜지언트 앨리어싱을 위한 수명 주기 (compile 이 계산해 둔다)
for ( const sw::RenderGraphResourceLifetime& lifetime : graph.getResourceLifetimes() )
    SW_LOG_INFO( "Resource '%#': first pass %# ~ last pass %#", lifetime._name.c_str(), lifetime._firstPassIndex, lifetime._lastPassIndex );
```

실행은 `execute`(직렬) 또는 `executeParallel`(패스마다 커맨드 리스트를 워커에서 기록)이고, 배리어는 `setLevelPrologue` 로 받은 콜백이 기록합니다.

---

### 5.14 C++17 Fluent Task Continuation (TaskFuture / TaskPromise)

C++20 코루틴을 사용할 수 없는 C++17 환경에서도 콜백 지옥 없이 직관적인 비동기 파이프라인을 구축할 수 있도록 Monadic `.then()` 체이닝과 `whenAllFutures` · `whenAnyFuture` 콤비네이터를 제공합니다
(태스크 핸들끼리의 결합은 `TaskManager::whenAll` · `whenAny`).

#### 💡 `TaskManager (Job System)`와의 역할 차이 및 상호 보완성

| 비교 항목 | `TaskManager` (5.8절) | `TaskFuture<T> / TaskPromise<T>` (본 절) |
| :--- | :--- | :--- |
| **핵심 목적** | **CPU 코어 자원 분배 및 작업 실행 순서 관리** (Control Flow) | **비동기 연산 결과값($T$)의 타입 안전한 전달** (Data Flow) |
| **반환값 처리** | 기본적으로 `void` 실행 단위 (데이터 공유 시 `TaskArgs` 맵 필요) | 앞 작업의 반환값 `T`가 뒷 작업의 인자 `T`로 **컴파일 타임 타입 검증** |
| **스레드 점유** | 워커 스레드가 함수 루프를 직접 실행해야 함 | OS 비동기 I/O (IOCP), 소켓, GPU 펜스 등 **스레드 점유 없는 완료 알림** 지원 |
| **주요 사용처** | 씬 트랜스폼 계산, 물리 시뮬레이션, 렌더 패스 DAG, 파티클 | 에셋 스트리밍, 네트워크 패킷 파싱, 설정 파일 비동기 로드/변환 |

```cpp
#include "Core/Task/TaskFuture.h"
#include "Core/Task/TaskManager.h"

// [예제 1]: Fluent Monadic 체이닝 (.then) - 데이터 흐름 파이프라인
sw::TaskPromise<int32> promise;
sw::TaskFuture<int32>  future = promise.getFuture();

auto chained = future
    .then( []( int32 rawBytes ) { return rawBytes / 1024; } )       // int32 KB 변환
    .then( []( int32 kb )       { return std::to_string(kb) + " KB"; } ) // string 변환
    .then( []( const std::string& text ) { SW_LOG_INFO( "Loaded: %s", text.c_str() ); } );

// 비동기 작업(예: IOCP 파일 로드 완료)에서 값 공급
promise.setValue( 204800 ); // Output: "Loaded: 200 KB"
chained.wait();


// [예제 2]: TaskManager(스레드 풀)와 TaskFuture(데이터 채널)의 결합 활용
// TaskManager 워커 스레드에서 무거운 연산을 실행하고, 결과를 TaskPromise로 통지
sw::TaskPromise<std::vector<float>> computePromise;
auto computeFuture = computePromise.getFuture();

sw::TaskHandle computeTask = sw::engine::getTaskManager().emplaceTask( "AsyncCompute",
    SW_DELEGATE_LAMBDA( sw::TaskDelegate, [computePromise]() mutable
    {
        std::vector<float> heavyResults = runHeavyPathFinding();
        computePromise.setValue( std::move( heavyResults ) ); // 비동기 워커에서 결과 전달
    } ) );
computeTask.submit();

// 메인 스레드나 렌더러에서 비동기 결과를 가공
computeFuture.then( []( const std::vector<float>& path )
{
    SW_LOG_INFO( "Path finding complete: %zu nodes", path.size() );
} );
```

---

### 5.15 트랜스폼 더티 루트 플러시 (SceneTransformHierarchy)

수천 개의 정적 환경 오브젝트가 존재하는 대규모 씬에서 불필요한 순회를 피하기 위해, 트랜스폼이 바뀐 노드는 자기 **루트를 더티 루트 목록에 한 번만** 올리고
플러시는 그 목록만 돕니다(루트 서브트리끼리 겹치지 않으므로 루트 단위로 `engine::runParallel`). 상태와 알고리즘은 `SceneTransformHierarchy` 가,
단계 순서(틱 중 쓰기 적용 → 플러시)는 `GameObjectManager::tick` 이 갖습니다.

```cpp
#include "Engine/Object/GameObject/GameObjectManager.h"

// 트랜스폼이 변경되지 않은 프레임: 더티 루트 목록이 비어 있으므로 아무것도 순회하지 않는다.
if ( manager.hasDirtySceneTransforms() )
    manager.flushSceneTransforms();
```

틱 중의 세터는 값을 바로 쓰지 않고 칸의 대기 자리 · 쓰기 큐에 올려 틱 뒤에 적용합니다 — 틱 중에 다른 오브젝트가 읽는 값은 틱 전 값입니다.

---

## 6. 코딩 컨벤션 및 네이밍 규칙

SW Engine 소스코드를 작성할 때는 [AGENTS.md](AGENTS.md)(한국어판: [Coding Guidelines](docs/04_CodingGuidelines.md))의 규칙을 따릅니다. 규칙은
`Scripts/lint/` 의 게이트가 CI · 커밋 훅에서 강제합니다. 자주 만나는 것:

| 분류 | 규칙 | 예시 |
| :--- | :--- | :--- |
| **포인터 접두어** | Raw 포인터는 반드시 `p` (멤버: `_p`, 이중: `pp`) | `GameObject* pObject;`, `Transform* _pTransform;` |
| **컨테이너 접두어** | 고정 배열: `arr` / 연관 맵: `map` / 리스트: `list` / 고유 셋: `unique` (`List` 접미어 금지, `unique` 제외 단수형 강제) | `vector<int32> _listValue;`, `unordered_map<int32, string> _mapIdToName;`, `set<uint32> _uniqueIds;`, `vector<uint8> _bytes;` |
| **출력 매개변수 (Out)** | `out` 접두어 필수 (`outList...`, `outMap...`, `outUnique...`, `outArr...`), 포인터는 예외적으로 `pOut...`, `ppOut...` | `bool find( int32 id, Actor*& pOutActor, Node** ppOutNode, vector<int32>& outListId, set<uint32>& outUniqueIds, vector<uint8>& outBytes );` |
| **스마트 포인터** | `std::unique_ptr` 등은 `p` 접두어를 붙이지 않음 | `unique_ptr<Node> _rootNode;`, `shared_ptr<Material> _material;` |
| **불리언 비교** | `!` 부정 연산자 금지, 반드시 명시적 비교 작성 | `if (_bValid == false)`, `if (pPtr == nullptr)` |
| **범위(Range) 비교** | 변수를 안쪽(중간)에 배치하여 수학적 범위($min \le val \le max$)로 표기 | `if (kMin <= value && value <= kMax)` |
| **헤더 선언 순서** | 1. `public` 변수 $\rightarrow$ 2. `ctor`/`dtor` $\rightarrow$ 3. `init`/`shutdown` $\rightarrow$ 4. `process` $\rightarrow$ 5. `getter`/`setter` $\rightarrow$ 6. `private` 함수 $\rightarrow$ 7. `private` 변수 (맨 아래) | |
| **생성자 초기화** | 생성자가 있으면 헤더 기본값 대신 생성자 초기화 목록에서 선언 순서대로 `{}` 중괄호 초기화(반복자 쌍만 소괄호). 기본값 없는 스칼라 필드는 빠짐없이 | `: _memberA{ 0 }<br>, _memberB{ nullptr }` |
| **함수 이름** | 약어도 camelCase 한 낱말(`initRhi`, `bindComputeUav`), 개념 하나에 동사 하나(`initialize`/`shutdown`, `get`/`find`, `compute`), 술어는 질문형(`is`/`has`/…) | `bool isRhiValid() const;` |
| **enum switch** | 열거자를 다 다룬 `switch` 에는 `default:` 를 두지 않는다(`-Werror=switch` 가 빠진 `case` 를 잡는다) | |
| **플랫폼 · 컴파일러 판정** | 컴파일러 내장 매크로 대신 CMake 가 정의한 `SW_PLATFORM_*` · `SW_X64`/`SW_ARM64` · `SW_COMPILER_*` | `#if defined( SW_PLATFORM_WINDOWS )` |
| **힙 할당** | 맨 `new` 금지 — `sw_new` · `make_unique` · `sw_new_array` 로 sw 할당자(메모리 태그)를 지난다 | `sw_new Foo( ... )` |

---

## 7. 자동화 테스트 스위트

시험은 자체 프레임워크(`Test/TestFramework`, gtest 식 플래그를 받습니다)로 짠 실행 파일 일곱 개입니다. 자세한 규칙은 [Test/README.md](Test/README.md) 에 있습니다.

```powershell
ctest --test-dir build/Ninja-Debug -L nogpu --output-on-failure        # CI 와 같은 집합 (GPU 불필요)
ctest --test-dir build/Ninja-Shipping -L hostgpu --output-on-failure   # CI 가 못 돌리는 GPU · 창 · DXC 시험 — 끝내기 전에 직접
py -3 -m Scripts test SceneTest.*                                      # 이름으로 골라 돌리기(실행 파일 · 작업 폴더를 알아서 찾는다)
```

직접 실행할 때는 작업 폴더가 `build/<프리셋>/Bin` 이어야 합니다(테스트가 거기서 위로 올라가며 `Resource/` 를 찾습니다).

| 테스트 실행 파일 | 케이스 수 (Debug) | 검증 대상 |
| :--- | :---: | :--- |
| **`CoreTest`** | 360 | `Source/Core` 만 — 메모리 · 문자열 · 컨테이너 · 델리게이트 · 압축 스트림 · 명령줄 · 태스크 등 (엔진을 직접 쓰지 않는다) |
| **`EngineTest`** | 1041 (호스트 86) | 엔진 · GameFramework · 장르 킷 — GameObject/Component, 씬 · 프리팹, 직렬화, RHI · 렌더러(`RenderPassGpuTest` 등은 GPU 필요), 물리, 오디오, 태스크, 기동 단계 표 |
| **`ReflectionTest`** | 175 | `ReflectionParser` 코드젠과 리플렉션 런타임(타입 · 프로퍼티 · 스키마 마이그레이션 · orphan 처리) |
| **`SmokeTest`** | 47 | 모듈 동적 로드, LiveReload 반복 핫스왑, 모듈 백그라운드 컴파일 (Shipping 은 정적 경로 3 케이스) |
| **`EditorTest`** | 149 | 에디터의 UI 없는 로직(커맨드 스택 · 선택 · 뷰포트 수학 · 문서 dirty 계약) |
| **`EditorUiTest`** | 2 | ImGui 컨텍스트가 필요한 에디터 시험 |
| **`AppTest`** | 17 (호스트 11) | 런처 로직 + **진짜 `App.exe` 를 네 백엔드 × 에디터 유무로 띄우는 스모크**, 골든 이미지 비교, 에디터 자체 시험(`-gv_editorSelfTest`) |

케이스 수는 Debug 빌드의 `--test_list` 로 센 값입니다(괄호는 `hostgpu` 로 갈리는 케이스). 시험이 늘면 달라지므로 정확한 수는 `--test_list` 로 다시 세십시오. 구성마다 수가 다릅니다 — Test/README.md 의 표를 보십시오.
