/**
 * @file ModuleLibraryUtil.h
 * @brief 동적 라이브러리(모듈 라이브러리)를 다루는 유일한 진입점입니다.
 * @details 이름 만들기, 로드, 심볼 조회, 라이브러리 범위 조회, 의존 라이브러리 고정을 제공합니다. 라이브러리의 코드를 가리키는 등록을 해제하고 언로드하는 일도 여기서 합니다.
 *          게임과 에디터 모듈(`GameModuleManager`), RHI 백엔드 모듈(`RHIBackendRegistry`), 핫 리로드(`LiveReloadManager`), DXC 로더가 모두 이 구조체를 거칩니다.
 *          레지스트리 목록은 `ModuleLibraryUtil::registerUnloadHandler` 가 보관합니다. 이 구조체는 그 목록을 순회하고 결과를 로그로 남긴 뒤 라이브러리를 언로드합니다.
 *          섀도 복사본의 **파일 바이트**를 고치는 `ModuleLibraryPatch` 는 핫 리로드만 쓰므로 `GameModuleManager/ModuleLibraryPatch` 에 있습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"

namespace sw
{
    /**
     * @brief 모듈 언로드 처리기입니다(`ModuleLibraryUtil::registerUnloadHandler`). [@p pBegin, @p pEnd) 안의 코드를 가리키는 등록을 떼고 뗀 수를 돌려준다.
     *        떼어 낼 수 없어 라이브러리를 내리면 안 되면 @p outKeepLibraryMapped 를 true 로 둔다.
     */
    using ModuleUnloadFunction = Delegate<uint32( const void* pBegin, const void* pEnd, bool& outKeepLibraryMapped )>;
} // namespace sw

namespace sw
{
    /** @struct ModuleUnloadHandlerDesc @brief 모듈 언로드 처리기 하나의 이름과 성격입니다. */
    struct ModuleUnloadHandlerDesc
    {
        const utf8* _pName{ nullptr };          ///< 로그에 쓰는 이름("event subscriptions" 처럼 무엇을 들고 있는지). 정적 문자열이어야 한다
        bool        _bReleaseExpected{ false }; ///< 떼는 것이 정상 경로다(다른 모듈의 등록 줄로 인스턴스를 만들어 두는 쪽) — 경고가 아니라 정보로 남긴다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ModuleLibraryUtil
     * @brief 모듈 라이브러리의 이름, 로드, 조회, 코드 등록 해제, 언로드를 담당합니다.
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
         * @brief 주소 @p pAddressInside 를 포함한 실행 라이브러리(exe, DLL, SO)가 메모리에서 차지하는 범위를 찾습니다.
         * @details Windows 는 라이브러리 기준 주소와 `SizeOfImage` 로, 리눅스는 그 라이브러리의 적재 세그먼트(PT_LOAD) 전체로 범위를 정한다. 핫 리로드가
         *          "이 델리게이트나 함수 포인터가 언로드하려는 모듈의 코드를 가리키는가"를 판단하는 데 쓴다.
         * @return 찾지 못하면 false 를 반환합니다(그 밖의 플랫폼 포함).
         */
        [[nodiscard]] static bool findLoadedLibraryRange( const void* pAddressInside, const void*& pOutBegin, const void*& pOutEnd );
        /** @brief `loadDynamicLibrary` 가 준 핸들의 라이브러리 범위를 찾습니다(`findLoadedLibraryRange` 와 같습니다). */
        [[nodiscard]] static bool findDynamicLibraryRange( void* pHandle, const void*& pOutBegin, const void*& pOutEnd );
        /**
         * @brief 모듈 @p pHandle 이 DLL @p dependencyFileName(예: `GameFramework.dll`)을 **어느 라이브러리에 바인딩했는지** import 테이블에서 읽습니다.
         * @details import 는 로드할 때 해석되므로 IAT 첫 항목이 가리키는 주소의 모듈이 바인딩된 라이브러리다(모듈 라이브러리에는 지연 import 가 없다.
         *          `LiveReloadManager` 가 그런 라이브러리를 거절한다). Windows 전용이며, 리눅스는 import 테이블에서 바인딩을 읽을 수 없어
         *          부르는 쪽이 심볼로 판단한다(`LiveReloadManager` 의 바인딩 검사).
         * @return 바인딩된 라이브러리의 핸들을 반환합니다. 그 DLL 을 import 하지 않으면(그 밖의 플랫폼 포함) nullptr 입니다.
         */
        static void* findBoundImportLibrary( void* pHandle, string_view dependencyFileName );

        // ------------------------------------------------------------------------------
        // 4) 코드 등록 해제와 언로드. 라이브러리의 코드를 가리키는 엔진 등록(5 절의 언로드 처리기)을 먼저 해제한다.
        // ------------------------------------------------------------------------------
        /**
         * @brief 모듈 라이브러리 [@p pBegin, @p pEnd) 의 코드를 가리키는 등록을 해제합니다. 라이브러리를 언로드하거나 언로드를 미루기 **전에** 부릅니다.
         * @details 모듈보다 오래 사는 레지스트리는 언로드 처리기(`registerUnloadHandler`)를 올리고, 이 함수는 그 처리기를 모두 부른다.
         *          대상은 이벤트 버스(구독과 그 모듈이 만든 채널 항목), 전역 로그 리스너, Undo 스택(들어 있으면 통째로 비운다), 창의 처리기, 에셋 캐시 레지스트리다.
         *          레지스트리를 하나 더하려면 처리기 구독 멤버 한 줄을 더하면 되고, 이 함수는 고치지 않는다.
         *          이 훑기는 탐지기다. 등록은 구독(`Subscription`)이 소유해 모듈 객체와 함께 풀리므로, 예상 밖으로 해제한 것(`_bReleaseExpected` 가 아닌 처리기)은
         *          처리기 이름과 함께 **오류**로 남긴다(`Module.ListenerLeftBehind`). 모듈이 스스로 해제하지 않고 남긴 것이므로 모듈 쪽 버그다.
         * @param pOutKeepLibraryMapped 주면, 이 라이브러리를 **언로드하면 안 되는지** 받습니다. 라이브러리가 만든 이벤트 채널을 다른 코드가 아직 구독하면 true 입니다.
         *                            채널의 브로드캐스트 함수와 멀티캐스트의 해제자(`shared_ptr` 컨트롤 블록)가 그 라이브러리의 코드라, 언로드하면 다음
         *                            발행이나 디스패처 소멸이 언로드된 코드로 점프한다. 그런 라이브러리는 프로세스 끝까지 로드해 둔다(해제할 방법이 없다).
         * @return 모든 처리기에서 해제한 항목 수입니다. 모듈이 제대로 정리했으면 0 입니다.
         */
        static uint32 releaseModuleCode( string_view moduleName, const void* pBegin, const void* pEnd, bool* pOutKeepLibraryMapped = nullptr );
        /**
         * @brief `loadDynamicLibrary` 로 로드한 라이브러리 @p pHandle 의 코드를 가리키는 등록을 해제합니다(`releaseModuleCode` 를 그 라이브러리 범위로 부릅니다).
         * @details 언로드를 미루는 핫 리로드처럼 등록 해제와 언로드 사이가 벌어지는 곳이 쓴다. 바로 언로드하면 `unloadModuleLibrary` 가 이것까지 한다.
         * @return 라이브러리를 언로드해도 되면 true 입니다. 라이브러리가 만든 이벤트 채널을 다른 코드가 아직 구독하면 false 이며, 프로세스 끝까지 로드해 둡니다.
         *         범위를 찾지 못하면(핸들이 null 등) 해제할 것이 없으므로 true 입니다.
         */
        [[nodiscard]] static bool releaseLibraryCode( string_view moduleName, void* pHandle );

        /**
         * @brief `loadDynamicLibrary` 로 로드한 모듈 라이브러리 @p pHandle 을 언로드합니다.
         * @details 언로드하기 전에 그 라이브러리의 코드를 가리키는 등록을 해제한다(`releaseModuleCode`). 해제할 수 없는 것이 남으면 언로드하지 않고 false 를 반환한다.
         *          그 라이브러리가 끌어온 의존 라이브러리(GameFramework 같은 공유 모듈)는 **언로드하지 않는다**. 의존 라이브러리가 언제 함께 언로드되는지는 로더만 알아서 그 코드를 미리 해제할 수 없기 때문이다.
         *          Windows 는 지연 로드가 의존 DLL 을 프로세스 끝까지 붙잡으므로 원래 그렇다. 리눅스는 `DT_NEEDED` 참조가 함께 풀려 언로드되므로 여기서 고정한다(`pinDynamicLibraryDependencies`).
         *          핫 리로드(`LiveReloadManager`)는 등록 해제와 언로드 사이에 언로드를 미루므로 `releaseLibraryCode` 만 쓴다.
         * @return 라이브러리를 내렸으면 true 입니다.
         */
        [[nodiscard]] static bool unloadModuleLibrary( string_view moduleName, void* pHandle );

        // ------------------------------------------------------------------------------
        // 5) 언로드 처리기. 모듈 DLL 보다 오래 사는 레지스트리(이벤트 버스 · 로그 리스너 · Undo 스택 · 창 처리기 · 에셋 캐시 …)는 처리기를 올리고
        //    구독을 멤버로 든다(언리얼 `FModuleManager::OnModulesChanged` 와 같은 모양 — 상속하지 않는다). `releaseModuleCode` 가 처리기를 모두 부른다.
        //    처리기는 엔진(또는 App) 쪽 코드여야 한다 — 모듈 코드의 처리기는 그 모듈보다 오래 살면 훑기가 내려간 코드로 뛴다.
        //
        //        Subscription _moduleUnloadSubscription{ ModuleLibraryUtil::registerUnloadHandler( { "input callbacks" },
        //                                                    ModuleUnloadFunction::create<&InputManager::onModuleUnloading>( this ) ) };
        // ------------------------------------------------------------------------------
        /** @brief 처리기를 올립니다. 돌려준 구독이 사라지면 빠진다. 훑기 안에서 올리거나 풀어도 된다(훑기는 시작 때의 사본을 돌고 풀린 것은 건너뛴다). */
        [[nodiscard]] static Subscription registerUnloadHandler( const ModuleUnloadHandlerDesc& desc, const ModuleUnloadFunction& function );
        /** @brief 주소 @p pCode 가 [@p pBegin, @p pEnd) 안인지 봅니다. nullptr 은 늘 밖입니다. */
        static bool isAddressWithin( const void* pCode, const void* pBegin, const void* pEnd );
        /**
         * @brief 다형 객체 @p pObject 의 vtable 주소입니다. 객체가 어느 라이브러리의 코드인지 묻는 데 씁니다.
         * @details MSVC · Itanium ABI 모두 다형 (부분)객체의 첫 포인터가 vptr 입니다. 가상 함수를 가진 기반 포인터를 넘깁니다.
         */
        static const void* findVtableAddress( const void* pObject );
    };
} // namespace sw
