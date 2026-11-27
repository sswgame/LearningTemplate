/**
 * @file ModuleLibraryUtil.h
 * @brief 동적 라이브러리(모듈 라이브러리)의 이름 · 위치 규칙과 이미지 조회입니다. 라이브러리를 열고 쥐고 내리는 것은 `DynamicLibrary`(Core/Module) 입니다.
 * @details 게임과 에디터 모듈(`GameModuleManager`), RHI 백엔드 모듈(`RHIBackendRegistry`), 핫 리로드(`ModuleHotReloadManager`), DXC 로더가 모두 이 이름 규칙으로 경로를 찾는다.
 *          라이브러리의 코드를 가리키는 등록은 여기서 주소로 훑어 떼지 않는다 — 등록은 구독(`Subscription`)이나 모듈 등록자(`ModuleObjectRegistrar`)가 소유하고,
 *          모듈 타입의 인스턴스는 타입의 모듈 이름으로 고른다(`engine::unregisterModuleTypes`).
 *          섀도 복사본의 파일 바이트를 고치는 `ModuleLibraryPatch` 는 핫 리로드만 쓰므로 `GameModuleManager/ModuleLibraryPatch` 에 있습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

namespace sw
{
    /**
     * @struct ModuleLibraryUtil
     * @brief 모듈 라이브러리의 이름 · 위치 · 이미지 조회를 담당합니다.
     */
    struct SW_API ModuleLibraryUtil
    {
        // ------------------------------------------------------------------------------
        // 1) 이름. 플랫폼 접두어, 확장자.
        // ------------------------------------------------------------------------------
        /** @brief 플랫폼의 공유 라이브러리 접두어(예: lib)를 반환합니다. */
        static string_view getSharedLibraryPrefix();
        /** @brief 플랫폼의 공유 라이브러리 확장자(예: .dll)를 반환합니다. */
        static string_view getSharedLibraryExtension();
        /** @brief baseName 에 접두어와 확장자를 붙여 공유 라이브러리 이름을 만듭니다. */
        static string formatSharedLibraryName( string_view baseName );

        // ------------------------------------------------------------------------------
        // 1-1) 위치. 모듈 DLL 은 `Bin/Modules`, 엔진, GameFramework, 서드파티 DLL 은 `Bin` 에 둔다.
        // ------------------------------------------------------------------------------
        /** @brief 모듈 DLL 과 매니페스트 폴더의 이름입니다(`Bin/Modules`). */
        static constexpr const utf8* kModuleFolder = "Modules";
        /** @brief 디버그 심볼 폴더 이름입니다(`Bin/Symbols`). */
        static constexpr const utf8* kSymbolFolder = "Symbols";
        /** @brief 모듈 폴더(`<Bin>/Modules`)입니다. */
        static AbsolutePath getModuleDirectory();
        /**
         * @brief 모듈 이름 @p baseName 의 라이브러리 경로를 찾습니다. `Bin/Modules` 에 있으면 그 경로를, 없으면 `Bin` 의 경로를 반환합니다(GameFramework 처럼 Bin 에 남는 공유 라이브러리).
         * @details 둘 다 없으면 `Bin/Modules` 의 경로를 반환한다. 부르는 쪽은 이 경로로 "없다"를 알린다.
         */
        static AbsolutePath findModuleLibraryPath( string_view baseName );
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
        // 2) 이미지 고정 · 이름. 라이브러리를 열고 내리는 일은 소유 형식 `DynamicLibrary` 가 맡는다.
        // ------------------------------------------------------------------------------
        /**
         * @brief 주소 @p pAddress 를 담은 라이브러리를 프로세스 끝까지 내려가지 않게 고정합니다. 고정했으면 true 입니다.
         * @details 떼어 낸 스레드가 아직 그 라이브러리의 코드를 돌 수 있을 때 쓴다(언로드 뒤 남은 몇 명령이 빈 주소를 돈다). 리로드는 새 사본을 따로 싣으므로 옛 사본만 남는다.
         */
        static bool pinLibraryContaining( const void* pAddress );
        /** @brief 주소 @p pAddress 를 담은 이미지의 모듈 이름(접두어 · 확장자 없는 파일 이름)입니다. 실행 파일이면 그 이름이고, 찾지 못하면 비어 있습니다. */
        [[nodiscard]] static string findImageNameContaining( const void* pAddress );

        // ------------------------------------------------------------------------------
        // 3) 로드된 라이브러리 조회.
        // ------------------------------------------------------------------------------
        /**
         * @brief 로드된 이미지의 기준 주소 @p pBase 가 가리키는 PE NT 머리(`IMAGE_NT_HEADERS`)를 반환합니다. DOS · NT 서명이 맞지 않거나 Windows 가 아니면 nullptr 입니다.
         * @details PE 머리를 읽는 식은 이 함수 한 곳에만 둔다. 부르는 쪽이 `IMAGE_NT_HEADERS` 로 형변환해 import · 디버그 디렉터리를 읽는다.
         */
        [[nodiscard]] static const void* findImageNtHeaders( const void* pBase );
    };
} // namespace sw
