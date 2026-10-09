/**
 * @file ModuleImageUtil.h
 * @brief 동적 라이브러리(모듈 이미지)를 다루는 단일 자리입니다 — 이름 · 올리기 · 심볼 · 이미지 범위 · 의존 고정, 그리고 그 이미지의 코드를 쥔 등록을 떼고 내리기.
 *
 * 게임 · 에디터 모듈(`ModuleHost`), RHI 백엔드 모듈(`RHIBackendRegistry`), 핫 리로드(`LiveReloadManager`), DXC 로더, 지연 로드 훅이 모두 이 자리를 지난다.
 * 등록부 목록은 `IModuleUnloadListener` 가 들고, 여기서는 그 목록을 훑고 결과를 로그로 남긴 뒤 이미지를 내린다.
 * 섀도 복사본 **파일 바이트**를 고치는 것(`ModuleImagePatch`)은 핫 리로드만 쓰므로 `App/Module/ModuleImagePatch` 에 있다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @struct ModuleImageUtil
     * @brief 모듈 이미지의 이름 · 올리기 · 조회 · 코드 떼기 · 내리기입니다.
     */
    struct SW_API ModuleImageUtil
    {
        /** @brief 지연 로드 훅을 넣은 모듈이 내보내는 미리 묶기 함수의 이름입니다(`Engine/Module/DelayLoadNotifyHook.cpp`). */
        static constexpr const utf8* kBindDelayLoadImportsSymbol = "bindDelayLoadImports";

        // ------------------------------------------------------------------------------
        // 1) 이름 — 플랫폼 접두어 · 확장자 · 디버그 심볼 파일
        // ------------------------------------------------------------------------------
        /** @brief 플랫폼의 공유 라이브러리 접두어(예: lib)를 반환합니다. */
        static string_view getSharedLibraryPrefix();
        /** @brief 플랫폼의 공유 라이브러리 확장자(예: .dll)를 반환합니다. */
        static string_view getSharedLibraryExtension();
        /** @brief baseName 에 접두어와 확장자를 붙여 공유 라이브러리 이름을 만듭니다. */
        static string formatSharedLibraryName( string_view baseName );
        /**
         * @brief 라이브러리 **옆** 의 디버그 심볼 파일 경로를 반환합니다(섀도 복사본의 심볼 자리).
         * @note Windows: `.pdb` / Linux: `.debug`(없으면 DWARF 가 .so 안에 들어 있는 경우가 많습니다)
         */
        static string getDebugSymbolPath( string_view libraryPath );
        /**
         * @brief 빌드가 낸 라이브러리의 디버그 심볼 파일을 찾습니다 — `Bin/Symbols/<이름>` 을 먼저, 없으면 라이브러리 옆입니다. 둘 다 없으면 빈 문자열입니다.
         * @details Dev 빌드는 PDB 를 `Bin/Symbols` 에 냅니다(`cmake/Engine/BuildLayout.cmake`).
         */
        static string findBuiltDebugSymbolPath( string_view libraryPath );

        // ------------------------------------------------------------------------------
        // 1-1) 자리 — 모듈 DLL 은 `Bin/Modules`, 엔진 · GameFramework · 서드파티 DLL 은 `Bin`
        // ------------------------------------------------------------------------------
        /** @brief 모듈 DLL · 매니페스트 폴더 이름입니다(`Bin/Modules`). */
        static constexpr const utf8* kModuleFolder = "Modules";
        /** @brief 디버그 심볼 폴더 이름입니다(`Bin/Symbols`). */
        static constexpr const utf8* kSymbolFolder = "Symbols";
        /** @brief 모듈 폴더(`<Bin>/Modules`)입니다. */
        static string getModuleDirectory();
        /**
         * @brief 모듈 이름 @p baseName 의 라이브러리 경로를 찾습니다 — `Bin/Modules` 에 있으면 그것, 없으면 `Bin` 의 것(GameFramework 처럼 Bin 에 남는 공유 라이브러리).
         * @details 둘 다 없으면 `Bin/Modules` 의 경로를 돌려준다(부르는 쪽이 "없다" 를 그 경로로 알린다).
         */
        static string findModuleLibraryPath( string_view baseName );

        // ------------------------------------------------------------------------------
        // 2) 올리기 · 심볼 · 내리기(OS 로더) — 등록을 떼지 않는다. 엔진 코드를 쥘 수 있는 모듈은 4) 로 내린다
        // ------------------------------------------------------------------------------
        /** @brief 동적 라이브러리를 로드합니다. */
        static void* loadDynamicLibrary( string_view libraryName );
        /** @brief 동적 라이브러리에서 심볼 주소를 찾습니다. */
        static void* getDynamicSymbol( void* pHandle, string_view symbolName );
        /** @brief 로드한 동적 라이브러리를 메모리에서 내립니다. */
        static void unloadDynamicLibrary( void* pHandle );
        /**
         * @brief 라이브러리 @p pHandle 이 import 하는 라이브러리 가운데 지금 올라와 있는 것을 프로세스 끝까지 내려가지 않게 고정하고, 고정한 수를 반환합니다.
         * @details 이 핸들을 내려도 그것이 끌어온 의존 이미지는 남깁니다. Windows 는 import · 지연 import 표의 DLL 을 `GET_MODULE_HANDLE_EX_FLAG_PIN`
         *          으로, 리눅스는 `DT_NEEDED` 를 `RTLD_NODELETE | RTLD_NOLOAD` 로 고정합니다. 아직 올라오지 않은 의존은 올리지 않습니다.
         */
        static uint32 pinDynamicLibraryDependencies( void* pHandle );
        /**
         * @brief 모듈 @p pHandle 의 지연 import 를 **지금 전부** 묶고, 묶지 못한 DLL 수를 반환합니다(모듈이 내보낸 `bindDelayLoadImports` 를 부른다).
         * @details 지연 import 를 첫 호출이 묶게 두면 그 첫 호출의 첫 float 인자가 망가진다 — lld 의 x64 지연 로드 썽크가 xmm0 을 헬퍼 호출의
         *          홈(shadow) 공간에 저장한다(`Engine/Module/DelayLoadNotifyHook.cpp`). 모듈을 올린 뒤, 그 의존 이미지가 등록된 뒤, 그 코드를 처음
         *          부르기 **전**에 부릅니다. 그 함수를 내보내지 않는 이미지(지연 로드 훅이 없는 모듈 · Windows 밖)는 아무것도 하지 않습니다.
         */
        static uint32 bindDelayLoadImports( void* pHandle );
        /**
         * @brief 지금 올라와 있는 모든 이미지에 `bindDelayLoadImports` 를 합니다 — OS 로더가 함께 올린 모듈(시험 실행 파일이 링크한 키트)용, 엔진 기동이 부른다.
         * @details 묶기가 올린 이미지(서버 키트가 지연 로드하는 공유 키트)도 묶는다 — 새 이미지가 없을 때까지 다시 모은다.
         */
        static uint32 bindDelayLoadImportsOfLoadedModules();
        /** @brief 지금 프로세스에 올라와 있는 이미지(실행 파일 · DLL)의 핸들을 모읍니다. Windows 밖에서는 아무것도 담지 않습니다. */
        static void collectLoadedModuleHandles( vector<void*>& outListHandle );

        // ------------------------------------------------------------------------------
        // 3) 올라온 이미지 조회
        // ------------------------------------------------------------------------------
        /**
         * @brief 주소 @p pAddressInside 를 담은 실행 이미지(exe · DLL · SO)가 메모리에서 차지하는 범위를 찾습니다.
         * @details Windows 는 이미지 기준 주소 + `SizeOfImage`, 리눅스는 그 이미지의 적재 세그먼트(PT_LOAD) 전체입니다. 핫 리로드가
         *          "이 델리게이트 · 함수 포인터가 내리려는 모듈의 코드인가" 를 가리는 데 씁니다.
         * @return 찾지 못하면 false 입니다(그 외 플랫폼 포함).
         */
        static bool findLoadedImageRange( const void* pAddressInside, const void*& pOutBegin, const void*& pOutEnd );
        /** @brief `loadDynamicLibrary` 가 준 핸들의 이미지 범위를 찾습니다(`findLoadedImageRange` 와 같다). */
        static bool findDynamicLibraryRange( void* pHandle, const void*& pOutBegin, const void*& pOutEnd );
        /**
         * @brief 모듈 @p pHandle 이 DLL @p dependencyFileName(예: `GameFramework.dll`)을 **어느 이미지에 묶었는지** import 표에서 읽습니다.
         * @details 지연 로드는 서술자의 모듈 핸들 칸에 훅이 돌려준 핸들이 적힙니다(풀리기 전에는 0). 일반 import 는 로드할 때 이미 풀리므로
         *          IAT 첫 칸이 가리키는 주소의 모듈이 묶인 이미지입니다. Windows 전용입니다 — 리눅스는 import 표에서 결속을 읽을 수 없어
         *          부르는 쪽이 심볼로 가립니다(`LiveReloadManager` 의 결속 검사).
         * @return 묶인 이미지의 핸들입니다. 아직 풀리지 않은 지연 로드이거나 그 DLL 을 import 하지 않으면(그 외 플랫폼 포함) nullptr 입니다.
         */
        static void* findBoundImportImage( void* pHandle, string_view dependencyFileName );

        // ------------------------------------------------------------------------------
        // 4) 코드 떼기 · 내리기 — 이미지의 코드를 쥔 엔진 등록(`IModuleUnloadListener`)을 먼저 뗀다
        // ------------------------------------------------------------------------------
        /**
         * @brief 모듈 이미지 [@p pBegin, @p pEnd) 의 코드를 가리키는 등록을 뗍니다. 이미지를 내리거나 언로드를 미루기 **전에** 부릅니다.
         * @details 모듈보다 오래 사는 등록부는 `IModuleUnloadListener` 를 상속해 만들어질 때 스스로 목록에 오르고, 여기서는 그 목록을 훑습니다
         *          — 이벤트 버스(구독과 그 모듈이 만든 채널 항목) · 전역 로그 리스너 · Undo 스택(들어 있으면 통째로 비운다) · 창의 처리기 ·
         *          에셋 캐시 등록부. 등록부를 하나 더하는 자리는 그 등록부의 상속 한 줄이고 이 함수는 고치지 않습니다. 뗀 것은 리스너 이름과
         *          함께 경고로 남깁니다 — 모듈이 스스로 떼지 않고 남긴 것이라 모듈 쪽 버그의 실마리입니다.
         * @param pOutKeepImageMapped 주면, 이 이미지를 **내리면 안 되는지** 받습니다. 이미지가 만든 이벤트 채널을 다른 코드가 아직 구독하면 true —
         *                            채널의 브로드캐스트 함수와 멀티캐스트의 해제자(`shared_ptr` 제어 블록)가 그 이미지의 코드라, 내리면 다음
         *                            발행 · 디스패처 소멸이 내려간 코드로 뛴다. 그런 이미지는 프로세스 끝까지 올려 둔다(떼어 낼 방법이 없다).
         * @return 모든 리스너에서 뗀 것의 수입니다. 모듈이 제대로 정리했으면 0 입니다.
         */
        static uint32 releaseModuleCode( string_view moduleName, const void* pBegin, const void* pEnd, bool* pOutKeepImageMapped = nullptr );
        /**
         * @brief `loadDynamicLibrary` 로 올린 이미지 @p pHandle 의 코드를 가리키는 등록을 뗍니다(`releaseModuleCode` 를 그 이미지 범위로).
         * @details 언로드를 미루는 핫 리로드처럼 떼기와 내리기 사이가 벌어지는 곳이 씁니다. 바로 내리면 `unloadModuleImage` 가 이것까지 합니다.
         * @return 이미지를 내려도 되면 true 입니다. 이미지가 만든 이벤트 채널을 다른 코드가 아직 구독하면 false — 프로세스 끝까지 올려 둡니다.
         *         범위를 찾지 못하면(핸들이 null 등) 뗄 것이 없으므로 true 입니다.
         */
        [[nodiscard]] static bool releaseImageCode( string_view moduleName, void* pHandle );

        /**
         * @brief `loadDynamicLibrary` 로 올린 모듈 이미지 @p pHandle 을 내립니다.
         * @details 내리기 전에 그 이미지의 코드를 쥔 등록을 뗍니다(`releaseModuleCode`). 떼어 낼 수 없는 것이 남으면 내리지 않고 false 를
         *          돌려줍니다. 그 이미지가 끌어온 의존 이미지(GameFramework 같은 공유 모듈)는 **내리지 않습니다** — 의존 이미지가 언제 함께
         *          내려가는지는 로더만 알아 그 코드를 미리 뗄 수 없기 때문입니다. Windows 는 지연 로드가 의존 DLL 을 프로세스 끝까지 잡아 원래
         *          그렇고, 리눅스는 `DT_NEEDED` 참조가 함께 풀려 내려가므로 여기서 고정합니다(`pinDynamicLibraryDependencies`).
         *          핫 리로드(`LiveReloadManager`)는 떼기와 내리기 사이에 언로드를 미루므로 `releaseImageCode` 만 씁니다.
         * @return 이미지를 내렸으면 true 입니다.
         */
        [[nodiscard]] static bool unloadModuleImage( string_view moduleName, void* pHandle );
    };
} // namespace sw
