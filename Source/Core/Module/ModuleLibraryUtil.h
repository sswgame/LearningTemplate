/**
 * @file ModuleLibraryUtil.h
 * @brief 동적 라이브러리(모듈 라이브러리)를 다루는 유일한 진입점입니다.
 * @details 이름 만들기, 로드, 심볼 조회, import 바인딩 조회, 의존 라이브러리 고정, 언로드를 제공합니다.
 *          게임과 에디터 모듈(`GameModuleManager`), RHI 백엔드 모듈(`RHIBackendRegistry`), 핫 리로드(`LiveReloadManager`), DXC 로더가 모두 이 구조체를 거칩니다.
 *          라이브러리의 코드를 가리키는 등록은 여기서 주소로 훑어 떼지 않는다 — 등록은 구독(`Subscription`)이나 모듈 등록자(`ModuleObjectRegistrar`)가 소유하고,
 *          모듈 타입의 인스턴스는 타입의 모듈 이름으로 고른다(`engine::unregisterModuleTypes`).
 *          섀도 복사본의 **파일 바이트**를 고치는 `ModuleLibraryPatch` 는 핫 리로드만 쓰므로 `GameModuleManager/ModuleLibraryPatch` 에 있습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/DynamicModules.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct ModuleLibraryUtil
     * @brief 모듈 라이브러리의 이름, 로드, 조회, 언로드를 담당합니다.
     */
    struct SW_API ModuleLibraryUtil
    {
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
         * @details Dev 빌드는 PDB 를 `Bin/Symbols` 에 낸다(`cmake/Engine/BuildLayout.cmake`). `setModuleSubdirectory` 로 옮긴 모듈은 `Bin/Symbols/<그 폴더>` 다 —
         *          게임 팩마다 PDB 이름이 같아(`SWGame.pdb`) 폴더로 가른다.
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
        /**
         * @brief 모듈 @p baseName 을 `Bin/Modules` 대신 `Bin/<binRelativeDirectory>` 에서 찾게 합니다. 디버그 심볼은 `Bin/Symbols/<binRelativeDirectory>` 입니다.
         * @details 함께 지은 게임 팩(`Bin/Games/<게임>/SWGame`)을 `-game=` 으로 고를 때 기동이 모듈을 올리기 전에 한 번 부른다. 모듈 하나만 둔다 —
         *          다시 부르면 바꾸고, 빈 @p binRelativeDirectory 는 되돌린다.
         * @note 기동 스레드에서만 부른다. 읽는 쪽(핫 리로드 감시)은 그 뒤에 돈다.
         */
        static void setModuleSubdirectory( string_view baseName, string_view binRelativeDirectory );
        /** @brief `setModuleSubdirectory` 로 옮긴 모듈이 있으면 그 이름과 `Bin` 상대 폴더를 채우고 true 를 반환합니다. */
        static bool getModuleSubdirectory( string& outBaseName, string& outBinRelativeDirectory );

        // ------------------------------------------------------------------------------
        // 2) 로드, 심볼, 언로드(OS 로더). 등록을 해제하지 않는다. 엔진 코드를 가리킬 수 있는 모듈은 4)의 함수로 언로드한다.
        // ------------------------------------------------------------------------------
        /**
         * @brief 동적 라이브러리를 로드합니다.
         * @details **연 쪽이 핸들을 소유하고 내린다**(`LiveReloadManager` · `RHIBackendRegistry` · 헤드리스 에디터 모듈 · ALSA 백엔드). 프로세스 끝까지 두는 곳은 그 자리에
         *          이유를 적는다(DXC 의 함수 정적 · OnlineLoadBot 의 서버 키트). `unloadModuleLibrary` 가 고정하는 의존 라이브러리도 같은 규칙의 예외다.
         */
        static void* loadDynamicLibrary( string_view libraryName );
        /** @brief 동적 라이브러리에서 심볼 주소를 찾습니다. */
        static void* getDynamicSymbol( void* pHandle, string_view symbolName );
        /** @brief 로드한 동적 라이브러리를 메모리에서 내립니다. */
        static void unloadDynamicLibrary( void* pHandle );
        /**
         * @brief 라이브러리 @p pHandle 이 import 하는 라이브러리 중 지금 로드된 것을 프로세스 끝까지 언로드되지 않게 고정하고, 고정한 수를 반환합니다.
         * @details 이 핸들을 언로드해도 그 라이브러리가 끌어온 의존 라이브러리는 남긴다. Windows 는 import 테이블의 DLL 을 `GET_MODULE_HANDLE_EX_FLAG_PIN` 으로,
         *          리눅스는 `DT_NEEDED` 를 `RTLD_NODELETE | RTLD_NOLOAD` 로 고정한다. 아직 로드되지 않은 의존 라이브러리는 로드하지 않는다.
         */
        static uint32 pinDynamicLibraryDependencies( void* pHandle );

        // ------------------------------------------------------------------------------
        // 3) 로드된 라이브러리 조회.
        // ------------------------------------------------------------------------------
        /**
         * @brief 모듈 @p pHandle 이 DLL @p dependencyFileName(예: `GameFramework.dll`)을 **어느 라이브러리에 바인딩했는지** import 테이블에서 읽습니다.
         * @details import 는 로드할 때 해석되므로 IAT 첫 항목이 가리키는 주소의 모듈이 바인딩된 라이브러리다(모듈 라이브러리에는 지연 import 가 없다.
         *          `LiveReloadManager` 가 그런 라이브러리를 거절한다). Windows 전용이며, 리눅스는 import 테이블에서 바인딩을 읽을 수 없어
         *          부르는 쪽이 심볼로 판단한다(`LiveReloadManager` 의 바인딩 검사).
         * @return 바인딩된 라이브러리의 핸들을 반환합니다. 그 DLL 을 import 하지 않으면(그 밖의 플랫폼 포함) nullptr 입니다.
         */
        static void* findBoundImportLibrary( void* pHandle, string_view dependencyFileName );

#if SW_WITH_DYNAMIC_MODULES
        // ------------------------------------------------------------------------------
        // 4) 언로드.
        // ------------------------------------------------------------------------------
        /**
         * @brief `loadDynamicLibrary` 로 로드한 모듈 라이브러리 @p pHandle 을 언로드합니다.
         * @details 그 라이브러리를 가리키는 등록은 부르는 쪽(모듈 경로)이 먼저 뗐다 — 구독은 소유자가, 모듈 등록자 · 타입은 `engine::unregisterModuleTypes` 가.
         *          그 라이브러리가 끌어온 의존 라이브러리(GameFramework 같은 공유 모듈)는 **언로드하지 않는다**. 의존 라이브러리가 언제 함께 언로드되는지는 로더만 알아서 그 코드를 미리 해제할 수 없기 때문이다.
         *          Windows 는 지연 로드가 의존 DLL 을 프로세스 끝까지 붙잡으므로 원래 그렇다. 리눅스는 `DT_NEEDED` 참조가 함께 풀려 언로드되므로 여기서 고정한다(`pinDynamicLibraryDependencies`).
         *          핫 리로드(`LiveReloadManager`)는 언로드를 미룬다(`DeferredLibraryUnloadQueue`).
         * @return 라이브러리를 내렸으면 true 입니다.
         */
        [[nodiscard]] static bool unloadModuleLibrary( string_view moduleName, void* pHandle );

#endif // SW_WITH_DYNAMIC_MODULES
    };
} // namespace sw
