# SW Engine (게임 및 에디터 엔진 템플릿)

[![CI](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml/badge.svg)](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml)
[![Windows](https://img.shields.io/badge/Windows-clang--cl%20(Debug%20%7C%20Shipping)-0078D6?logo=windows&logoColor=white)](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml)
[![Linux](https://img.shields.io/badge/Linux-Clang%20(Debug%20%7C%20ASan%20%7C%20Shipping)-FCC624?logo=linux&logoColor=black)](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml)
[![C++17](https://img.shields.io/badge/Language-C%2B%2B17-00599C?logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![License](https://img.shields.io/badge/License-MIT-brightgreen.svg)](LICENSE)

**SW Engine**은 C++17 기반의 고성능 게임 및 에디터 엔진 프레임워크입니다.  
CMake, Ninja, LLVM Clang-cl 및 sccache를 결합하여 **초고속 증분 빌드**를 제공하며, 개발(Dev) 환경에서는 코드를 수정하면 엔진 재시작 없이 즉시 DLL이 교체되는 **모듈 핫리로드 (LiveReload)**를, 최종 배포(Shipping) 환경에서는 최고 성능과 보안을 위해 단일 실행 파일로 최적화되는 **정적 링크(Static Link)**를 지원합니다.

### 🛠️ 지원 플랫폼 및 CI 빌드 매트릭스

| OS / 플랫폼 | 컴파일러 / 툴체인 | 빌드 프리셋 & 검증 항목 | CI 검증 |
| :--- | :--- | :--- | :---: |
| 🪟 **Windows** | `clang-cl` (LLVM) | `Debug` (LiveReload DLL 모듈) | ![Passing](https://img.shields.io/badge/build-passing-brightgreen?logo=windows) |
| 🪟 **Windows** | `clang-cl` (LLVM) | `Shipping` (최적화 정적 단일 실행파일) | ![Passing](https://img.shields.io/badge/build-passing-brightgreen?logo=windows) |
| 🐧 **Linux** | `clang++` (LLVM + LLD) | `Debug` (LiveReload Shared 모듈) | ![Passing](https://img.shields.io/badge/build-passing-brightgreen?logo=linux) |
| 🐧 **Linux** | `clang++` (LLVM + LLD) | `Debug + ASan` (메모리 주소 살균자) | ![Passing](https://img.shields.io/badge/build-passing-brightgreen?logo=linux) |
| 🐧 **Linux** | `clang++` (LLVM + LLD) | `Shipping` (최적화 정적 단일 실행파일) | ![Passing](https://img.shields.io/badge/build-passing-brightgreen?logo=linux) |

---

## 📚 Wiki Navigation (문서 목차)

초심자이신가요? 아래의 주제별 위키 인덱스를 순서대로 읽어보시면 프로젝트의 전체 구조를 쉽게 파악할 수 있습니다.

- 🚀 **[Getting Started (시작하기)](docs/01_GettingStarted.md)**: 빌드 환경 구성(vcpkg, CMake) 및 첫 빌드/테스트 실행 가이드
- 🧩 **[Engine Subsystems (서브시스템 개요)](docs/02_EngineSubsystems.md)**: 렌더링, 오브젝트/컴포넌트, 스레드 풀, 리플렉션 등 핵심 엔진 기능 찾아보기
- 🔄 **[LiveReload & ABI (핫리로드 및 아키텍처)](docs/03_LiveReload_and_ABI.md)**: 게임을 끄지 않고 코드를 수정하는 원리와 주의사항
- 📝 **[Coding Guidelines (코딩 규칙)](docs/04_CodingGuidelines.md)**: 프로젝트에 기여할 때 지켜야 하는 C++ / CMake 네이밍 규칙

> **심화 문서**: 전체 아키텍처 다이어그램 및 제약 사항은 **[ARCHITECTURE.md](ARCHITECTURE.md)**를 참고하세요.

---

## 📑 상세 목차 (본문)

1. [📖 핵심 아키텍처 및 빌드 모드](#1-핵심-아키텍처-및-빌드-모드)
2. [📂 디렉터리 레이아웃](#2-디렉터리-레이아웃)
3. [📦 압축 직렬화 스트림 (Compression Stream & Pluggable Codecs)](#3-압축-직렬화-스트림-compression-stream--pluggable-codecs)
   - [설계 원리 및 인터페이스 분리](#41-설계-원리-및-인터페이스-분리)
   - [바이너리 컨테이너 헤더 규격](#42-바이너리-컨테이너-헤더-규격)
   - [신규 코덱 확장 및 등록 방법 (LZ4 / Zstd 등)](#43-신규-코덱-확장-및-등록-방법-lz4--zstd-등)
   - [C++ 실무 사용 예제 코드](#44-c-실무-사용-예제-코드)
5. [🧩 엔진 핵심 서브시스템 실무 사용법](#5-엔진-핵심-서브시스템-실무-사용법)
   - [1. GameObject & Component 라이프사이클](#51-gameobject--component-라이프사이클)
   - [2. RHI 멀티 백엔드 렌더링 파이프라인](#52-rhi-멀티-백엔드-렌더링-파이프라인)
   - [3. 리플렉션 및 다중 포맷 직렬화](#53-리플렉션-및-다중-포맷-직렬화)
   - [4. 씬 관리 및 프리팹 (Prefab) 시스템](#54-씬-관리-및-프리팹-prefab-시스템)
   - [5. 모듈 핫리로드 (LiveReload) 시스템](#55-모듈-핫리로드-livereload-시스템)
   - [6. 공간 분할 인덱싱 (SpatialQuadTree & SpatialOctree)](#56-공간-분할-인덱싱-spatialquadtree--spatialoctree)
   - [7. 런타임 파일 감시 & 에셋 핫리로드 (ReloadFileManager)](#57-런타임-파일-감시--에셋-핫리로드-reloadfilemanager)
   - [8. 멀티스레드 태스크 시스템 (Task DAG)](#58-멀티스레드-태스크-시스템-task-dag)
   - [9. 오디오 및 2D 물리 시스템](#59-오디오-및-2d-물리-시스템)
   - [10. 비동기 에셋 스트리밍 큐 (AssetStreamingQueue)](#510-비동기-에셋-스트리밍-큐-assetstreamingqueue)
   - [11. GPU-Driven 간접 드로우 & 비동기 컴퓨트 (IndirectDrawBuffer & ComputePass)](#511-gpu-driven-간접-드로우--비동기-컴퓨트-indirectdrawbuffer--computepass)
   - [12. 티어-3 바인드리스 리소스 테이블 (BindlessTable)](#512-티어-3-바인드리스-리소스-테이블-bindlesstable)
   - [13. RenderGraph 순차 쓰기/RMW 의존성 및 리소스 수명 주기 분석](#513-rendergraph-순차-쓰기rmw-의존성-및-리소스-수명-주기-분석)
   - [14. C++17 Fluent Task Continuation & State Machine (TaskFuture / TaskPromise)](#514-c17-fluent-task-continuation--state-machine-taskfuture--taskpromise)
   - [15. 트랜스폼 세대 카운터 (Transform Dirty Generation Counter)](#515-트랜스폼-세대-카운터-transform-dirty-generation-counter)
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
  - `Engine`은 DLL로 분리되고, `SWGame`(게임 모듈), `GF_Overworld`(키트), `EditorModule`(에디터)은 런타임에 동적 로드됩니다.
  - 게임 코드를 수정한 뒤 빌드하면 `LiveReloadManager`가 변경된 DLL을 그림자 복사(Shadow Copy)하여 런타임 중에 갈아끼웁니다.
- **Shipping (배포 모드)**:
  - 에디터 및 핫리로드 레이어가 제거되고, 모든 서브시스템이 단일 `.exe` 바이너리로 정적 링크(STATIC)되어 오버헤드가 제로화됩니다.
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
| `Source/Core` | 기초 유틸리티 정적 라이브러리 (로깅, 메모리 풀링, 문자열, 압축, 델리게이트, 파일 I/O 등) |
| `Source/Engine` | 핵심 엔진 라이브러리 (RHI, GameObject, Component, 씬/프리팹, 리플렉션, 물리, 오디오, 렌더 그래프 등) |
| `Source/RuntimeAPI` | App ↔ Editor/Game 모듈 간의 순수 C-ABI 통신 인터페이스 (Header-Only) |
| `Source/Editor` | 개발 모드 전용 ImGui 에디터 툴셋 |
| `Source/GameFramework` | 장르별 공통 프레임워크 및 플러그형 키트 (`GF_Overworld`, `GF_TurnBattle`, `GF_ActionCombat`) |
| `Source/Games` | 실제 게임 프로젝트 소스코드 (`Demo`, `Empty` 등 / `SW_ACTIVE_GAME` 변수로 빌드 대상 지정) |
| `Source/App` | 얇은 진입점 실행 파일 (Main Loop, 모듈 호스트, 태스크 펜싱 관리) |
| `Resource/` | 텍스처, 셰이더, 사운드, 씬 XML, 프리팹 JSON, 데이터 XML 등의 게임 에셋 |
| `Scripts/` | 환경 설정, vcpkg 패키지 복원, 코드 린터 및 자동화 파이썬 도구 |
| `Tools/` | ReflectionParser (Clang libtooling 기반 리플렉션 코드 생성기), LLVM, Ninja, Sccache |
| `Test/` | 자동화 단위 테스트 스위트 (`CoreTest`, `ReflectionTest`, `EngineTest`, `SmokeTest`) |

---

---

## 3. 압축 직렬화 스트림 (Compression Stream & Pluggable Codecs)

SW Engine은 세이브 파일, 네트워크 패킷, 바이너리 씬 데이터의 디스크 I/O 최적화를 위해 **플러그형 압축 직렬화 스트림**을 제공합니다.

### 4.1 설계 원리 및 인터페이스 분리

압축 알고리즘(RLE, LZ4, Zstd, Snappy 등)의 변경이나 추가가 발생하더라도 **기존 직렬화 코드나 클라이언트 로직을 전혀 수정할 필요가 없도록** 인터페이스와 구현체가 엄격히 분리되어 있습니다:

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
│   │ NullCompressionCodec│RleCompressionCodec│LZ4 / Zstd  │  │
│   │   (Pass-through)    │  (내장 고속 RLE)   │ (확장 슬롯)│  │
│   └─────────────────────┴───────────────────┴────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

- **`ICompressionCodec`**: 모든 압축 알고리즘이 구현해야 하는 순수 가상 인터페이스 (`compress`, `decompress`, `compressBound`, `getCodecType`, `getCodecName`).
- **`NullCompressionCodec`**: 무압축(Pass-through) 코덱. 디버깅 및 압축 미적용 폴백용.
- **`RleCompressionCodec`**: 엔진 자체 내장 고속 바이트 런 압축기 (외부 라이브러리 없이 독립 동작).
- **`CompressionCodecRegistry`**: 런타임/빌드타임에 코덱을 등록, 조회, 교체할 수 있는 중앙 레지스트리.
- **`CompressionStream`**: 바이너리 헤더(`CompressionHeader`)와 FNV-1a 무결성 체크섬 검증을 캡슐화한 헬퍼.
- **`BinarySerializer`**: 리플렉션 객체의 콤팩트 바이너리 직렬화 및 압축 스트림 직렬화(`serializeCompressed`)를 통합 지원.

---

### 4.2 바이너리 컨테이너 헤더 규격

`CompressionStream`이 생성하는 바이너리 스트림은 항상 28바이트의 안전한 헤더로 시작합니다:

```cpp
#pragma pack(push, 1)
struct CompressionHeader
{
    uint32 _magic{ 0x53574353 }; // 'SWCS' (SW Compression Stream 매직 넘버)
    uint8  _version{ 1 };        // 스트림 포맷 버전
    uint8  _codecType{ 0 };      // CompressionCodecType (0: None, 1: RLE, 2: LZ4, 3: Zstd)
    uint16 _flags{ 0 };          // 0x01: 체크섬 포함 여부
    uint64 _uncompressedSize{0}; // 압축 전 원본 바이트 크기
    uint64 _compressedSize{ 0 }; // 압축 후 페이로드 바이트 크기
    uint32 _checksum{ 0 };       // FNV-1a 32비트 무결성 체크섬
};
#pragma pack(pop)
```

---

### 4.3 신규 코덱 확장 및 등록 방법 (LZ4 / Zstd 등)

추후 `LZ4`나 `Zstandard` 등 새로운 외부 라이브러리를 연동할 때에는 `ICompressionCodec` 인터페이스만 구현하여 레지스트리에 등록하면 끝납니다:

```cpp
#include "Core/Compression/ICompressionCodec.h"
#include "Core/Compression/CompressionCodecRegistry.h"
// #include <lz4.h>

namespace sw
{
    class Lz4CompressionCodec final : public ICompressionCodec
    {
    public:
        virtual CompressionCodecType getCodecType() const override { return CompressionCodecType::LZ4; }
        virtual const char*          getCodecName() const override { return "LZ4"; }
        virtual size_t compressBound( size_t uncompressedSize ) const override
        {
            // return LZ4_compressBound( static_cast<int32>( uncompressedSize ) );
            return uncompressedSize + ( uncompressedSize / 255 ) + 16;
        }

        virtual bool compress( const void* pSrc, size_t srcSize, void* pDst, size_t dstCapacity,
                               size_t& outCompressedSize, int32 compressionLevel = 0 ) override
        {
            // const int32 compSize = LZ4_compress_default( (const char*)pSrc, (char*)pDst, (int)srcSize, (int)dstCapacity );
            // if ( compSize <= 0 ) return false;
            // outCompressedSize = compSize;
            return true;
        }

        virtual bool decompress( const void* pSrc, size_t srcSize, void* pDst, size_t dstCapacity,
                                 size_t& outUncompressedSize ) override
        {
            // const int32 decompSize = LZ4_decompress_safe( (const char*)pSrc, (char*)pDst, (int)srcSize, (int)dstCapacity );
            // if ( decompSize < 0 ) return false;
            // outUncompressedSize = decompSize;
            return true;
        }
    };
}

// 런타임 초기화 시 레지스트리에 등록
void registerCustomCodecs()
{
    sw::engine::getCompressionCodecRegistry().registerCodec( sw::make_unique<sw::Lz4CompressionCodec>() );
    sw::engine::getCompressionCodecRegistry().setDefaultCodecType( sw::CompressionCodecType::LZ4 );
}
```

---

### 4.4 C++ 실무 사용 예제 코드

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

// 1. 플레이어 데이터를 바이너리로 변환 후 즉시 압축
const sw::TypeInfo* pTypeInfo = sw::TypeRegistry::get().findTypeByName( "PlayerSaveData" );
sw::vector<sw::uint8> listSaveFileBuffer;

sw::BinarySerializer::serializeCompressed(
    &playerData,
    *pTypeInfo,
    listSaveFileBuffer,
    sw::CompressionCodecType::RLE
);

// 디스크에 저장: FileUtil::saveFile( "Save01.sav", listSaveFileBuffer );

// 2. 세이브 파일 로드 및 역압축 역직렬화
PlayerSaveData loadedData{};
sw::BinarySerializer::deserializeCompressed(
    &loadedData,
    *pTypeInfo,
    listSaveFileBuffer.data(),
    listSaveFileBuffer.size()
);
```

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

// 2. 컴포넌트 부착
auto* pScene = pPlayer->addComponent<sw::SceneComponent>();
pScene->setLocalPosition( sw::float3{ 100.0f, 0.0f, 200.0f } );

// 3. 계층 구조 구성 (Parent - Child)
sw::GameObject* pWeapon = pObjectManager->createGameObject( "Sword" );
pWeapon->attachToParent( pPlayer ); // 플레이어의 회전/이동이 자식에 자동 전파됨

// 4. 태그 등록 및 고속 검색
pPlayer->addTag( "Player.Hero"_tag );
sw::vector<sw::GameObject*> listPlayers;
pObjectManager->findGameObjectsByTag( "Player.Hero"_tag, listPlayers );
```

---

### 5.2 RHI 멀티 백엔드 렌더링 파이프라인

`IRHIDevice`는 DirectX 11, DirectX 12, Vulkan, OpenGL을 균일하게 추상화합니다.

```cpp
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RenderPass/FrameRenderer.h"
#include "Engine/Graphics/RenderPass/RenderGraph.h"

// 1. 프레임 렌더러 초기화 (원하는 RHI 백엔드 선택)
// gv_rhiBackend = "DirectX12" 또는 "Vulkan", "DirectX11", "OpenGL"

// 2. 렌더 그래프를 통한 패스 빌드
sw::RenderGraph graph;
graph.addPass( "GBufferPass", []( sw::IRHICommandContext* pContext )
{
    // 드로우 콜 제출
    pContext->drawIndexed( 6, 1, 0, 0, 0 );
} );

graph.execute();
```

---

### 5.3 리플렉션 및 다중 포맷 직렬화

C++ 헤더에 어노테이션 매크로를 작성하면 `ReflectionParser`가 빌드 타임에 타입 정보를 자동 생성합니다.

```cpp
// Header.h
#pragma once
#include "Engine/Reflection/ReflectionMacros.h"

REFLECT_CLASS()
class SW_API CharacterStats
{
    REFLECT_BODY()
public:
    REFLECT_PROPERTY( Category="Combat", Range(0.0, 1000.0) )
    float32 _health{ 100.0f };

    REFLECT_PROPERTY( Category="Visual", AssetType="Texture" )
    sw::string _avatarPath{ "textures/hero.png" };

    REFLECT_FUNCTION()
    void heal( float32 amount ) { _health += amount; }
};
```

- **JSON 직렬화**: `JsonSerializer::serialize(&stats, *pTypeInfo)`
- **XML 직렬화**: `XmlSerializer::serialize(&stats, *pTypeInfo)`
- **바이너리 직렬화**: `BinarySerializer::serialize(&stats, *pTypeInfo, listBuffer)`
- **압축 바이너리 직렬화**: `BinarySerializer::serializeCompressed(&stats, *pTypeInfo, listBuffer)`

---

### 5.4 씬 관리 및 프리팹 (Prefab) 시스템

```cpp
#include "Engine/Scene/SceneManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"

// 1. 비동기 씬 로드 (로딩 화면 유지 중 백그라운드 워커에서 파싱)
sw::SceneManager::get().requestLoadAsync( "Resource/maps/town01.scene.xml" );

// 2. 프리팹 인스턴스화 (Prefab Delta Overrides 지원)
sw::GameObject* pMonster = sw::PrefabManager::get().instantiatePrefab(
    "Resource/prefabs/Goblin.prefab.json",
    sw::SceneManager::get().getActiveScene()
);
```

---

### 5.5 모듈 핫리로드 (LiveReload) 시스템

개발 모드에서 게임 코드를 수정하고 Visual Studio / CLion / CMake에서 빌드 버튼을 누르면:
1. `LiveReloadManager`가 DLL 변경을 파일 워처로 감지합니다.
2. 실행 중인 비동기 태스크를 펜싱(동기화 완료)합니다.
3. 게임 모듈의 상태(`State`)를 메모리 직렬화 버퍼로 덤프합니다.
4. 이전 DLL을 언로드하고, 새 DLL을 그림자 복사본으로 로드합니다.
5. 리플렉션 타입/팩토리 체인을 리바인딩하고 상태를 역직렬화하여 복원합니다.

---

### 5.6 공간 분할 인덱싱 (SpatialQuadTree & SpatialOctree)

대규모 2D/3D 씬에서 $O(N)$ 전수 조사를 방지하고 $O(\log N)$ 고속 프러스텀 컬링, 구체(Sphere) 충돌 및 영역 쿼리를 지원합니다.

```cpp
#include "Engine/Spatial/SpatialQuadTree.h"
#include "Engine/Spatial/SpatialOctree.h"

// 1. 2D 쿼드트리 (Top-down / 2D 월드 영역)
sw::SpatialQuadTree quadTree( sw::AABB2D{ -5000.0f, -5000.0f, 5000.0f, 5000.0f } );
quadTree.insert( pObject->getId(), sw::AABB2D{ 100.0f, 100.0f, 150.0f, 150.0f }, pObject );

sw::vector<sw::SpatialElement> listVisible2D;
quadTree.queryRange( sw::AABB2D{ 0.0f, 0.0f, 800.0f, 600.0f }, listVisible2D );

// 2. 3D 옥트리 (3D 월드 AABB / 구체(Sphere) 반경 쿼리)
sw::SpatialOctree octree( sw::AABB3D{ sw::float3( -1000.0f, -1000.0f, -1000.0f ), sw::float3( 1000.0f, 1000.0f, 1000.0f ) } );
octree.insert( 101, sw::AABB3D{ sw::float3( 10.0f, 0.0f, 10.0f ), sw::float3( 20.0f, 10.0f, 20.0f ) }, pObject );

// 3D 범위 및 구체 반경 쿼리
sw::vector<sw::SpatialOctreeElement> listVisible3D;
octree.queryRange( sw::AABB3D{ sw::float3( 0.0f, -50.0f, 0.0f ), sw::float3( 100.0f, 50.0f, 100.0f ) }, listVisible3D );

sw::vector<sw::SpatialOctreeElement> listExplosionTargets;
octree.querySphere( sw::float3( 15.0f, 5.0f, 15.0f ), 25.0f, listExplosionTargets );
```

---

### 5.7 런타임 파일 감시 & 에셋 핫리로드 (ReloadFileManager)

텍스처, 셰이더, XML 파일이 외부 툴에서 수정되면 실시간으로 감지하여 콜백을 실행합니다.

```cpp
#include "Engine/Utility/Module/ReloadFileManager.h"

// 셰이더 파일 수정 시 자동 리로드 콜백 등록
auto handle = sw::ReloadFileManager::get().registerWatch(
    "Resource/shaders",
    { ".hlsl", ".glsl" },
    SW_DELEGATE_LAMBDA( sw::FileWatchMatchDelegate, []( const sw::FileChangeEvent& event )
    {
        SW_LOG_INFO( "Shader modified: %# -> Recompiling...", event._filename.c_str() );
        // ShaderCompiler::get().reloadShader( event._filename );
    } )
);
```

---

### 5.8 멀티스레드 태스크 시스템 (Task DAG)

CPU 코어를 100% 활용할 수 있는 작업 훔치기(Work-stealing) 기반 비동기 태스크 시스템입니다.

```cpp
#include "Core/Task/TaskManager.h"

// 1. 백그라운드 비동기 태스크 디스패치
auto task = sw::TaskManager::get().run( sw::TaskPriority::Normal, []()
{
    // 백그라운드 계산 (예: 길찾기, 메쉬 생성 등)
    return 42;
} );

// 2. 태스크 체이닝 (.then)
task.then( []( int32 result )
{
    SW_LOG_INFO( "Task completed with result: %#", result );
} );

// 3. 대규모 데이터 병렬 처리 (Parallel For)
sw::TaskManager::get().parallelFor( 0, 10000, []( size_t index )
{
    // 각 원소 병렬 계산
} );
```

---

### 5.9 오디오 및 2D 물리 시스템

```cpp
#include "Engine/Audio/AudioSystem.h"
#include "Engine/Physics/PhysicsSystem.h"

// 오디오 사운드 재생
sw::AudioSystem::get().playSound( "Resource/audio/sfx_jump.wav", 1.0f );
sw::AudioSystem::get().playBGM( "Resource/audio/bgm_field.ogg", true );

// 2D 레이캐스트 물리 쿼리
sw::RaycastHit2D hitInfo{};
if ( sw::PhysicsSystem::get().raycast( sw::Vector2(0,0), sw::Vector2(1,0), 500.0f, hitInfo ) )
{
    SW_LOG_INFO( "Hit collider at distance: %#", hitInfo._distance );
}
```

---

### 5.10 비동기 에셋 스트리밍 큐 (AssetStreamingQueue)

게임플레이 중 버벅임(Frame Drop / Stuttering)을 방지하기 위해 백그라운드 워커 스레드에서 에셋을 사전 로드(Prefetch)하고 완료 콜백을 스레드-안전하게 전달합니다.

```cpp
#include "Engine/Resource/AssetStreamingQueue.h"

// 1. 큐 초기화 및 우선순위 기반 비동기 프리로드 요청
sw::AssetStreamingQueue::get().initialize();

sw::AssetStreamingQueue::get().requestAsset(
    "Resource/textures/boss_dragon.png",
    sw::StreamingPriority::High,
    SW_DELEGATE_LAMBDA( sw::OnStreamingCompleteDelegate, []( std::string_view assetPath, bool bSuccess )
    {
        if ( bSuccess )
            SW_LOG_INFO( "Asset streaming ready: %s", assetPath.data() );
    } )
);

// 2. 매 프레임 메인 스레드에서 완료 콜백 플러시
sw::AssetStreamingQueue::get().tick();
```

---

### 5.11 GPU-Driven 간접 드로우 & 비동기 컴퓨트 (IndirectDrawBuffer & ComputePass)

CPU 개입 없이 GPU에서 직접 컬링 결과를 기반으로 드로우 콜을 발행하는 **GPU-Driven Rendering** 파이프라인과 **비동기 컴퓨트 셰이더 디스패치**를 지원합니다.

```cpp
#include "Engine/Graphics/RenderPass/IndirectDrawBuffer.h"
#include "Engine/Graphics/RenderPass/ComputePass.h"

// 1. Indirect Draw Argument 버퍼 구성
sw::IndirectDrawBuffer indirectBuffer;
indirectBuffer.initialize( pRhiDevice, 1024 ); // 최대 1024개 간접 드로우 명령

sw::RHIDrawIndexedIndirectCommand cmd{};
cmd.indexCountPerInstance = 36;
cmd.instanceCount = 500;
indirectBuffer.setCommand( 0, cmd );
indirectBuffer.flushToGPU( pRhiDevice );

// 2. 비동기 컴퓨트 패스 (Compute Pass) 디스패치
sw::ComputePass cullingPass;
cullingPass.initialize( pRhiDevice, pComputePipeline );
cullingPass.dispatch( pRhiContext, 64, 1, 1 ); // (64 * 64 threads)
```

---

### 5.12 티어-3 바인드리스 리소스 테이블 (BindlessTable)

DirectX 12 / Vulkan의 티어-3 바인드리스(Bindless Resource Indexing)를 통해 수만 개의 텍스처와 버퍼를 전역 인덱스로 셰이더에서 즉시 접근할 수 있도록 관리합니다.

```cpp
#include "Engine/Graphics/RHI/BindlessTable.h"

sw::BindlessTable bindlessTable;
bindlessTable.initialize( 65536 ); // 64K 슬롯 예약

// 텍스처 핸들 바인딩 및 인덱스 발급
uint32 textureDescriptorIndex = bindlessTable.allocateTextureSlot( textureHandle );

// 셰이더에 단일 인덱스(uint32)만 Push Constant/Root Constant로 전달:
// MaterialData { uint32 albedoTextureId; };
```

---

---

### 5.13 RenderGraph 순차 쓰기/RMW 의존성 및 리소스 수명 주기 분석

RenderGraph는 패스 간 자원 의존성을 DAG 위상 정렬할 때 **Read-Modify-Write (동일 리소스 읽기 및 덮어쓰기)** 및 **순차 쓰기(Sequential Multi-Write)** 체인을 자동으로 추적하며, VRAM 앨리어싱(Transient Aliasing)을 위한 리소스 수명 주기(First ~ Last Pass)를 산출합니다.

```cpp
#include "Engine/Graphics/RenderPass/RenderGraph.h"

sw::RenderGraph graph;
// Pass A(쓰기) -> Pass B(읽기 & 덮어쓰기) -> Pass C(읽기)
graph.addPass( sw::hashed_string("PassA_Geometry"), {}, { sw::hashed_string("ColorBuffer") } );
graph.addPass( sw::hashed_string("PassB_PostProcess"), { sw::hashed_string("ColorBuffer") }, { sw::hashed_string("ColorBuffer") } );
graph.addPass( sw::hashed_string("PassC_UIOverlay"), { sw::hashed_string("ColorBuffer") }, { sw::hashed_string("FinalOutput") } );

graph.compile(); // PassA -> PassB -> PassC 순서 완벽 보장

// Transient Resource Aliasing을 위한 수명 주기 계산
auto listLifetimes = graph.computeResourceLifetimes();
for ( const auto& life : listLifetimes )
{
    SW_LOG_INFO( "Resource '%s': First Pass %d ~ Last Pass %d", life._name.c_str(), life._firstPassIndex, life._lastPassIndex );
}
```

---

### 5.14 C++17 Fluent Task Continuation & State Machine (TaskFuture / TaskPromise)

C++20 코루틴을 사용할 수 없는 C++17 환경에서도 콜백 지옥 없이 직관적인 비동기 파이프라인을 구축할 수 있도록 Monadic `.then()` 체이닝 및 단계별 상태 머신(`ITaskStateMachine`)을 제공합니다.

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

sw::engine::getTaskManager().emplaceTask( "AsyncCompute",
    SW_DELEGATE_LAMBDA( sw::TaskDelegate, [computePromise]() mutable
    {
        std::vector<float> heavyResults = runHeavyPathFinding();
        computePromise.setValue( std::move( heavyResults ) ); // 비동기 워커에서 결과 전달
    } )
).submit();

// 메인 스레드나 렌더러에서 비동기 결과를 가공
computeFuture.then( []( const std::vector<float>& path )
{
    SW_LOG_INFO( "Path finding complete: %zu nodes", path.size() );
} );
```

---

### 5.15 트랜스폼 세대 카운터 (Transform Dirty Generation Counter)

수천 개의 정적 환경 오브젝트가 존재하는 대규모 씬에서 불필요한 서브트리 순회를 방지하기 위해 원자적 세대 카운터(`_dirtyTransformGeneration`)를 기반으로 **$O(1)$ 조기 탈출(Early-Exit)** 최적화를 수행합니다.

```cpp
#include "Engine/Object/GameObject/GameObjectManager.h"

// 트랜스폼이 변경되지 않은 프레임:
// hasDirtySceneTransforms() 및 flushSceneTransforms()는 서브트리 순회 없이
// _dirtyTransformGeneration == _lastFlushedTransformGeneration 비교를 통해 1 클럭(O(1))에 즉시 반환됩니다.
if ( manager.hasDirtySceneTransforms() )
{
    manager.flushSceneTransforms();
}
```

---

## 6. 코딩 컨벤션 및 네이밍 규칙

SW Engine 소스코드를 작성할 때는 [AGENTS.md](AGENTS.md) 및 [GEMINI.md](GEMINI.md)의 다음 규칙을 **엄격히 준수**해야 합니다:

| 분류 | 규칙 | 예시 |
| :--- | :--- | :--- |
| **포인터 접두어** | Raw 포인터는 반드시 `p` (멤버: `_p`, 이중: `pp`) | `GameObject* pObject;`, `Transform* _pTransform;` |
| **컨테이너 접두어** | 고정 배열: `arr` / 연관 맵: `map` / 리스트: `list` / 고유 셋: `unique` (`List` 접미어 금지, `unique` 제외 단수형 강제) | `vector<int32> _listValue;`, `unordered_map<int32, string> _mapIdToName;`, `set<uint32> _uniqueIds;`, `vector<uint8> _bytes;` |
| **출력 매개변수 (Out)** | `out` 접두어 필수 (`outList...`, `outMap...`, `outUnique...`, `outArr...`), 포인터는 예외적으로 `pOut...`, `ppOut...` | `bool find( int32 id, Actor*& pOutActor, Node** ppOutNode, vector<int32>& outListId, set<uint32>& outUniqueIds, vector<uint8>& outBytes );` |
| **스마트 포인터** | `std::unique_ptr` 등은 `p` 접두어를 붙이지 않음 | `unique_ptr<Node> _rootNode;`, `shared_ptr<Material> _material;` |
| **불리언 비교** | `!` 부정 연산자 금지, 반드시 명시적 비교 작성 | `if (_bValid == false)`, `if (pPtr == nullptr)` |
| **범위(Range) 비교** | 변수를 안쪽(중간)에 배치하여 수학적 범위($min \le val \le max$)로 표기 | `if (kMin <= value && value <= kMax)` |
| **헤더 선언 순서** | 1. `public` 변수 $\rightarrow$ 2. `ctor`/`dtor` $\rightarrow$ 3. `init`/`shutdown` $\rightarrow$ 4. `process` $\rightarrow$ 5. `getter`/`setter` $\rightarrow$ 6. `private` 함수 $\rightarrow$ 7. `private` 변수 (맨 아래) |
| **생성자 초기화** | 헤더 인라인 초기화 지양, 생성자 본문에서 선언 순서대로 `{}` 중괄호 초기화 | `: _memberA{ 0 }<br>, _memberB{ nullptr }` |

---

## 7. 자동화 테스트 스위트

엔진은 모든 커밋과 빌드에서 회귀 결함을 방지하기 위해 4대 자동화 테스트 스위트를 포함하고 있습니다:

```powershell
# 모든 테스트 일괄 실행
build/Ninja-Debug/Bin/CoreTest.exe
build/Ninja-Debug/Bin/ReflectionTest.exe
build/Ninja-Debug/Bin/EngineTest.exe
build/Ninja-Debug/Bin/SmokeTest.exe
```

| 테스트 실행 파일 | 테스트 항목 수 | 검증 대상 서브시스템 |
| :--- | :---: | :--- |
| **`CoreTest.exe`** | **104개** | 메모리 풀링, 문자열 빌더, CPU 타이머, **플러그형 압축 코덱/스트림**, XML/JSON 파서 |
| **`ReflectionTest.exe`** | **66개** | C++ 리플렉션 타입/프로퍼티/메서드 동적 호출, 스키마 마이그레이션, 직렬화, 소프트 역직렬화 고아 처리 |
| **`EngineTest.exe`** | **199개** | RHI 4대 백엔드, 셰이더 컴파일러, GameObject/Component, **SpatialQuadTree/SpatialOctree (Node Collapse)**, **AssetStreamingQueue (In-Flight Multicast)**, **GPU-Driven IndirectDraw/Bindless (Double-Free 방어)**, **RenderGraph (RMW/Lifetime)**, **C++17 TaskFuture/Promise**, **Transform Dirty Generation**, 오디오, 물리(TLS 수축 가드), 태스크 DAG |
| **`SmokeTest.exe`** | **17개** | App-Editor-Game 동적 모듈 로드, **LiveReload 반복 핫스왑 사이클**, 풀 씬 전환 |
| **총계** | **386개 (100% PASS)** | **엔진 전체 서브시스템 무결성 보증** |



