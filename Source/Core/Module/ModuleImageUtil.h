/**
 * @file ModuleImageUtil.h
 * @brief 동적 라이브러리(모듈 이미지)를 다루는 유일한 진입점입니다.
 * @details 이름 만들기, 로드, 심볼 조회, 이미지 범위 조회, 의존 이미지 고정을 제공합니다. 이미지의 코드를 가리키는 등록을 해제하고 언로드하는 일도 여기서 합니다.
 *          게임과 에디터 모듈(`ModuleHost`), RHI 백엔드 모듈(`RHIBackendRegistry`), 핫 리로드(`LiveReloadManager`), DXC 로더, 지연 로드 훅이 모두 이 구조체를 거칩니다.
 *          레지스트리 목록은 `IModuleUnloadListener` 가 보관합니다. 이 구조체는 그 목록을 순회하고 결과를 로그로 남긴 뒤 이미지를 언로드합니다.
 *          섀도 복사본의 **파일 바이트**를 고치는 `ModuleImagePatch` 는 핫 리로드만 쓰므로 `ModuleHost/ModuleImagePatch` 에 있습니다.
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
     * @brief 모듈 이미지의 이름, 로드, 조회, 코드 등록 해제, 언로드를 담당합니다.
     */
    struct SW_API ModuleImageUtil
    {
        /** @brief 지연 로드 훅을 넣은 모듈이 내보내는 미리 바인딩 함수의 이름입니다(`Engine/Module/DelayLoadNotifyHook.cpp`). */
        static constexpr const utf8* kBindDelayLoadImportsSymbol = "bindDelayLoadImports";

        // ------------------------------------------------------------------------------
        // 1) 이름. 플랫폼 접두어, 확장자, 디버그 심볼 파일.
        // ------------------------------------------------------------------------------
        /** @brief 플랫폼의 공유 라이브러리 접두어(예: lib)를 반환합니다. */
        static string_view getSharedLibraryPrefix();
        /** @brief 플랫폼의 공유 라이브러리 확장자(예: .dll)를 반환합니다. */
        static string_view getSharedLibraryExtension();
        /** @brief baseName 에 접두어와 확장자를 붙여 공유 라이브러리 이름을 만듭니다. */
        static string formatSharedLibraryName( string_view baseName );
        /**
         * @brief 라이브러리 **옆**에 있는 디버그 심볼 파일의 경로를 반환합니다(섀도 복사본의 심볼 위치).
         * @note Windows 는 `.pdb`, Linux 는 `.debug` 입니다(없으면 DWARF 가 .so 안에 들어 있는 경우가 많습니다).
         */
        static string getDebugSymbolPath( string_view libraryPath );
        /**
         * @brief 빌드가 낸 라이브러리의 디버그 심볼 파일을 찾습니다. `Bin/Symbols/<이름>` 을 먼저 보고, 없으면 라이브러리 옆을 봅니다. 둘 다 없으면 빈 문자열을 반환합니다.
         * @details Dev 빌드는 PDB 를 `Bin/Symbols` 에 낸다(`cmake/Engine/BuildLayout.cmake`).
         */
        static string findBuiltDebugSymbolPath( string_view libraryPath );

        // ------------------------------------------------------------------------------
        // 1-1) 위치. 모듈 DLL 은 `Bin/Modules`, 엔진, GameFramework, 서드파티 DLL 은 `Bin` 에 둔다.
        // ------------------------------------------------------------------------------
        /** @brief 모듈 DLL 과 매니페스트 폴더의 이름입니다(`Bin/Modules`). */
        static constexpr const utf8* kModuleFolder = "Modules";
        /** @brief 디버그 심볼 폴더 이름입니다(`Bin/Symbols`). */
        static constexpr const utf8* kSymbolFolder = "Symbols";
        /** @brief 모듈 폴더(`<Bin>/Modules`)입니다. */
        static string getModuleDirectory();
        /**
         * @brief 모듈 이름 @p baseName 의 라이브러리 경로를 찾습니다. `Bin/Modules` 에 있으면 그 경로를, 없으면 `Bin` 의 경로를 반환합니다(GameFramework 처럼 Bin 에 남는 공유 라이브러리).
         * @details 둘 다 없으면 `Bin/Modules` 의 경로를 반환한다. 부르는 쪽은 이 경로로 "없다"를 알린다.
         */
        static string findModuleLibraryPath( string_view baseName );

        // ------------------------------------------------------------------------------
        // 2) 로드, 심볼, 언로드(OS 로더). 등록을 해제하지 않는다. 엔진 코드를 가리킬 수 있는 모듈은 4)의 함수로 언로드한다.
        // ------------------------------------------------------------------------------
        /** @brief 동적 라이브러리를 로드합니다. */
        static void* loadDynamicLibrary( string_view libraryName );
        /** @brief 동적 라이브러리에서 심볼 주소를 찾습니다. */
        static void* getDynamicSymbol( void* pHandle, string_view symbolName );
        /** @brief 로드한 동적 라이브러리를 메모리에서 내립니다. */
        static void unloadDynamicLibrary( void* pHandle );
        /**
         * @brief 라이브러리 @p pHandle 이 import 하는 라이브러리 중 지금 로드된 것을 프로세스 끝까지 언로드되지 않게 고정하고, 고정한 수를 반환합니다.
         * @details 이 핸들을 언로드해도 그 라이브러리가 끌어온 의존 이미지는 남긴다. Windows 는 import 와 지연 import 테이블의 DLL 을 `GET_MODULE_HANDLE_EX_FLAG_PIN` 으로,
         *          리눅스는 `DT_NEEDED` 를 `RTLD_NODELETE | RTLD_NOLOAD` 로 고정한다. 아직 로드되지 않은 의존 이미지는 로드하지 않는다.
         */
        static uint32 pinDynamicLibraryDependencies( void* pHandle );
        /**
         * @brief 모듈 @p pHandle 의 지연 import 를 **지금 전부** 바인딩하고, 바인딩하지 못한 DLL 수를 반환합니다(모듈이 내보낸 `bindDelayLoadImports` 를 부릅니다).
         * @details 지연 import 를 첫 호출이 바인딩하게 두면 그 첫 호출의 첫 float 인자가 망가진다. lld 의 x64 지연 로드 썽크가 xmm0 을 헬퍼 호출의
         *          섀도 공간에 저장하기 때문이다(`Engine/Module/DelayLoadNotifyHook.cpp`). 그래서 모듈을 로드한 뒤, 그 의존 이미지를 등록한 뒤, 그 코드를 처음
         *          부르기 **전에** 이 함수를 부른다. 그 함수를 내보내지 않는 이미지(지연 로드 훅이 없는 모듈, Windows 이외의 플랫폼)에서는 아무것도 하지 않는다.
         */
        static uint32 bindDelayLoadImports( void* pHandle );
        /**
         * @brief 지금 로드된 모든 이미지에 `bindDelayLoadImports` 를 부릅니다. OS 로더가 함께 로드한 모듈(테스트 실행 파일이 링크한 키트)용이며, 엔진 기동이 부릅니다.
         * @details 바인딩 중에 로드된 이미지(서버 키트가 지연 로드하는 공유 키트)도 바인딩한다. 새 이미지가 없을 때까지 다시 모은다.
         */
        static uint32 bindDelayLoadImportsOfLoadedModules();
        /**
         * @brief 지금 프로세스에 로드된 이미지(실행 파일, DLL)의 핸들을 모읍니다. Windows 이외의 플랫폼에서는 아무것도 담지 않고 true 를 반환합니다.
         * @return 목록을 읽었으면 true 입니다. 다른 스레드가 DLL 을 로드하고 언로드하는 동안 시간 한도 안에 읽지 못하면 빈 목록과 false 를 반환합니다.
         */
        [[nodiscard]] static bool collectLoadedModuleHandles( vector<void*>& outListHandle );

        // ------------------------------------------------------------------------------
        // 3) 로드된 이미지 조회.
        // ------------------------------------------------------------------------------
        /**
         * @brief 주소 @p pAddressInside 를 포함한 실행 이미지(exe, DLL, SO)가 메모리에서 차지하는 범위를 찾습니다.
         * @details Windows 는 이미지 기준 주소와 `SizeOfImage` 로, 리눅스는 그 이미지의 적재 세그먼트(PT_LOAD) 전체로 범위를 정한다. 핫 리로드가
         *          "이 델리게이트나 함수 포인터가 언로드하려는 모듈의 코드를 가리키는가"를 판단하는 데 쓴다.
         * @return 찾지 못하면 false 를 반환합니다(그 밖의 플랫폼 포함).
         */
        static bool findLoadedImageRange( const void* pAddressInside, const void*& pOutBegin, const void*& pOutEnd );
        /** @brief `loadDynamicLibrary` 가 준 핸들의 이미지 범위를 찾습니다(`findLoadedImageRange` 와 같습니다). */
        static bool findDynamicLibraryRange( void* pHandle, const void*& pOutBegin, const void*& pOutEnd );
        /**
         * @brief 모듈 @p pHandle 이 DLL @p dependencyFileName(예: `GameFramework.dll`)을 **어느 이미지에 바인딩했는지** import 테이블에서 읽습니다.
         * @details 지연 로드는 디스크립터의 모듈 핸들 필드에 훅이 돌려준 핸들이 적힌다(해석되기 전에는 0). 일반 import 는 로드할 때 이미 해석되므로
         *          IAT 첫 항목이 가리키는 주소의 모듈이 바인딩된 이미지다. Windows 전용이며, 리눅스는 import 테이블에서 바인딩을 읽을 수 없어
         *          부르는 쪽이 심볼로 판단한다(`LiveReloadManager` 의 바인딩 검사).
         * @return 바인딩된 이미지의 핸들을 반환합니다. 아직 해석되지 않은 지연 로드이거나 그 DLL 을 import 하지 않으면(그 밖의 플랫폼 포함) nullptr 입니다.
         */
        static void* findBoundImportImage( void* pHandle, string_view dependencyFileName );

        // ------------------------------------------------------------------------------
        // 4) 코드 등록 해제와 언로드. 이미지의 코드를 가리키는 엔진 등록(`IModuleUnloadListener`)을 먼저 해제한다.
        // ------------------------------------------------------------------------------
        /**
         * @brief 모듈 이미지 [@p pBegin, @p pEnd) 의 코드를 가리키는 등록을 해제합니다. 이미지를 언로드하거나 언로드를 미루기 **전에** 부릅니다.
         * @details 모듈보다 오래 사는 레지스트리는 `IModuleUnloadListener` 를 상속하면 만들어질 때 스스로 목록에 오르고, 이 함수는 그 목록을 순회한다.
         *          대상은 이벤트 버스(구독과 그 모듈이 만든 채널 항목), 전역 로그 리스너, Undo 스택(들어 있으면 통째로 비운다), 창의 처리기, 에셋 캐시 레지스트리다.
         *          레지스트리를 하나 더하려면 그 레지스트리가 상속 한 줄을 추가하면 되고, 이 함수는 고치지 않는다.
         *          해제한 것은 리스너 이름과 함께 경고로 남긴다. 모듈이 스스로 해제하지 않고 남긴 것이므로 모듈 쪽 버그의 단서가 된다.
         * @param pOutKeepImageMapped 주면, 이 이미지를 **언로드하면 안 되는지** 받습니다. 이미지가 만든 이벤트 채널을 다른 코드가 아직 구독하면 true 입니다.
         *                            채널의 브로드캐스트 함수와 멀티캐스트의 해제자(`shared_ptr` 컨트롤 블록)가 그 이미지의 코드라, 언로드하면 다음
         *                            발행이나 디스패처 소멸이 언로드된 코드로 점프한다. 그런 이미지는 프로세스 끝까지 로드해 둔다(해제할 방법이 없다).
         * @return 모든 리스너에서 해제한 항목 수입니다. 모듈이 제대로 정리했으면 0 입니다.
         */
        static uint32 releaseModuleCode( string_view moduleName, const void* pBegin, const void* pEnd, bool* pOutKeepImageMapped = nullptr );
        /**
         * @brief `loadDynamicLibrary` 로 로드한 이미지 @p pHandle 의 코드를 가리키는 등록을 해제합니다(`releaseModuleCode` 를 그 이미지 범위로 부릅니다).
         * @details 언로드를 미루는 핫 리로드처럼 등록 해제와 언로드 사이가 벌어지는 곳이 쓴다. 바로 언로드하면 `unloadModuleImage` 가 이것까지 한다.
         * @return 이미지를 언로드해도 되면 true 입니다. 이미지가 만든 이벤트 채널을 다른 코드가 아직 구독하면 false 이며, 프로세스 끝까지 로드해 둡니다.
         *         범위를 찾지 못하면(핸들이 null 등) 해제할 것이 없으므로 true 입니다.
         */
        [[nodiscard]] static bool releaseImageCode( string_view moduleName, void* pHandle );

        /**
         * @brief `loadDynamicLibrary` 로 로드한 모듈 이미지 @p pHandle 을 언로드합니다.
         * @details 언로드하기 전에 그 이미지의 코드를 가리키는 등록을 해제한다(`releaseModuleCode`). 해제할 수 없는 것이 남으면 언로드하지 않고 false 를 반환한다.
         *          그 이미지가 끌어온 의존 이미지(GameFramework 같은 공유 모듈)는 **언로드하지 않는다**. 의존 이미지가 언제 함께 언로드되는지는 로더만 알아서 그 코드를 미리 해제할 수 없기 때문이다.
         *          Windows 는 지연 로드가 의존 DLL 을 프로세스 끝까지 붙잡으므로 원래 그렇다. 리눅스는 `DT_NEEDED` 참조가 함께 풀려 언로드되므로 여기서 고정한다(`pinDynamicLibraryDependencies`).
         *          핫 리로드(`LiveReloadManager`)는 등록 해제와 언로드 사이에 언로드를 미루므로 `releaseImageCode` 만 쓴다.
         * @return 이미지를 내렸으면 true 입니다.
         */
        [[nodiscard]] static bool unloadModuleImage( string_view moduleName, void* pHandle );
    };
} // namespace sw
