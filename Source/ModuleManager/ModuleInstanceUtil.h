/**
 * @file ModuleManager/ModuleInstanceUtil.h
 * @brief 모듈 라이브러리에서 API 표를 받고 인스턴스를 만들고 내리는 절차입니다. 게임(`GameModuleManager`)과 에디터(`ClientModuleManager`)가 같은 절차를 씁니다.
 */
#pragma once
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"

#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Module/EngineABIStamp.h"
#include "Engine/Module/ModuleTypeRegistry.h"

#include "ModuleAPI/ABI/EditorAPI.h"
#include "ModuleAPI/ABI/ModuleABI.h"

#include "ModuleManager/ModuleLibraryHandle.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    class GenericWindow;

    /**
     * @struct ModuleAPISymbols
     * @brief 모듈 라이브러리에서 API 표를 받는 데 쓰는 심볼 이름입니다. 에디터 · 게임이 같은 절차를 이 이름만 바꿔 씁니다.
     */
    struct ModuleAPISymbols
    {
        const utf8* _pModuleName; ///< 모듈 이름 — 빌드가 박은 엔진 ABI 스탬프 심볼(`sw_moduleEngineABIStamp_<이름>`)을 고른다
        const utf8* _pExportSymbol;
        const utf8* _pModuleLabel;
    };

    /** @brief 에디터 모듈 라이브러리의 ABI · API 표 심볼 이름입니다. 인스턴스 경로(`ClientModuleManager`)와 헤드리스 작업(`DevTools/Host/EditorModuleHeadlessTasks`)이 같이 씁니다. */
    inline constexpr ModuleAPISymbols kEditorModuleSymbols{ config::kTargetEditorModule, "exportEditorAPI", "Editor" };
} // namespace sw

namespace sw
{
    /**
     * @struct ModuleInstanceUtil
     * @brief 모듈 라이브러리 → API 표 → 인스턴스의 공통 절차입니다. 내리는 순서(타입 등록 해제 뒤 서비스 떼기)가 에디터 · 게임에서 갈라지지 않게 한 곳에 둡니다.
     */
    struct ModuleInstanceUtil
    {
        /** @brief API 표 @p TAPI 가 만드는 인스턴스 핸들 형식입니다(`create` 의 반환 — `GameHandle` · `EditorHandle`). 라이브러리 핸들(`ModuleLibraryHandle`)과 다른 형식이다. */
        template <typename TAPI>
        using InstanceHandleOf = decltype( std::declval<const TAPI&>().create() );

        /**
         * @brief 모듈이 이 호스트와 같은 헤더(표 구조 · 경계 구조체)로 빌드됐는지 대조합니다 — 모든 동적 모듈과 같은 엔진 ABI 스탬프 대조다(`engine::isModuleBuiltWithThisEngine`).
         * @details `create != nullptr && destroy != nullptr` 만으로는 막지 못합니다 — 그 둘은 표 **맨 앞**이라 가운데에 끼워도 채워진다. 핫 리로드는 모듈만 다시 빌드해 이 어긋남이 생기는 바로 그 상황이다.
         */
        static bool matchesModuleABI( ModuleLibraryHandle library, const ModuleAPISymbols& symbols )
        {
            return engine::isModuleBuiltWithThisEngine( library.getOSHandle(), symbols._pModuleName );
        }

        /**
         * @brief 모듈 라이브러리의 ABI 를 대조하고 API 표를 받습니다(에디터 · 게임 공통). 받지 못하면 @p outAPI 는 빈 표입니다.
         * @details 리로드 전 검사(`is*LibraryUsable`)와 바인딩(`bind*API`)이 같은 절차를 씁니다.
         */
        template <typename TAPI, typename TExportFn>
        [[nodiscard]] static bool exportAPIFromLibrary( ModuleLibraryHandle library, const ModuleAPISymbols& symbols, TAPI& outAPI )
        {
            outAPI = {};
            if ( library.isValid() == false || matchesModuleABI( library, symbols ) == false )
                return false;
            const TExportFn pfnExport = reinterpret_cast<TExportFn>( library.findSymbol( symbols._pExportSymbol ) );
            if ( pfnExport == nullptr || pfnExport( &outAPI ) == false )
            {
                outAPI = {};
                SW_LOG_ERROR_ID( "ModuleInstanceUtil.ModuleDoesNotExport", "The %# module does not export its API table (%#)", symbols._pModuleLabel, symbols._pExportSymbol );
                return false;
            }
            return true;
        }

        /** @brief 새 라이브러리로 리로드해도 되는지(ABI 가 맞고 API 표에 만들기 · 부수기가 있는지) 봅니다 — 리로드 전 검사(에디터 · 게임 공통). */
        template <typename TAPI, typename TExportFn>
        [[nodiscard]] static bool isLibraryUsable( ModuleLibraryHandle library, const ModuleAPISymbols& symbols )
        {
            TAPI api{};
            return exportAPIFromLibrary<TAPI, TExportFn>( library, symbols, api ) && api.create != nullptr && api.destroy != nullptr;
        }

        /**
         * @brief 바인딩한 API 표로 인스턴스를 만들고 초기화합니다(에디터 · 게임 공통). 실패하면 만든 것을 부수고 핸들을 비웁니다.
         * @p initialize 는 핸들 하나를 받아 그 모듈 종류의 초기화를 부릅니다(인자가 종류마다 다르다). @note @p bRequireDevice 면 **디바이스가 없을 때 만들지 않습니다.** `RHI::getDevice()` 는 널 참조를 반환하므로 묻는 것 자체가 죽는
         *       길이고, 만들어 봐야 초기화가 실패할 것이 정해져 있습니다. 전용 서버의 게임은 디바이스 없이(nullptr) 만듭니다.
         */
        template <typename TAPI, typename TInitialize>
        [[nodiscard]] static bool createInstance( const TAPI& api, InstanceHandleOf<TAPI>& pOutHandle, const TInitialize& initialize, RHI* pRHI, bool bRequireDevice, const utf8* pModuleLabel )
        {
            pOutHandle            = nullptr;
            const bool bHasDevice = pRHI != nullptr && pRHI->hasDevice();
            if ( bRequireDevice && bHasDevice == false )
            {
                SW_LOG_ERROR_ID( "ModuleInstanceUtil.NoRHIDeviceInstance", "No RHI device - %# instance is not created", pModuleLabel );
                return false;
            }

            pOutHandle = api.create();
            if ( pOutHandle == nullptr )
            {
                SW_LOG_ERROR_ID( "ModuleInstanceUtil.FailedCreateInstance", "Failed to create %# instance", pModuleLabel );
                return false;
            }

            if ( initialize( pOutHandle ) == false ) // 초기화 인자는 모듈 종류마다 다르다(게임은 창만, 에디터는 창과 디바이스) — 부르는 쪽이 정한다
            {
                SW_LOG_ERROR_ID( "ModuleInstanceUtil.FailedInitializeInstance", "Failed to initialize %# instance", pModuleLabel );
                if ( api.destroy != nullptr )
                    api.destroy( pOutHandle );
                pOutHandle = nullptr;
                return false;
            }
            SW_LOG_INFO_ID( "ModuleInstanceUtil.ModuleInstanceInitialized", "Module instance initialized: %#", pModuleLabel );
            return true;
        }

        /**
         * @brief 인스턴스를 내립니다(에디터 · 게임 공통): shutdown → destroy → (표를 놓으면) 모듈 타입 등록 해제 → 서비스 떼기 → 핸들 비우기
         *        → (표를 놓으면) API 표 비우기.
         * @details 타입 등록 해제가 그 모듈의 살아 있는 컴포넌트를 지웁니다. 그 소멸자는 모듈 코드라 서비스가 아직 붙어 있는 동안이어야 합니다.
         */
        template <typename TAPI>
        static void destroyInstance( TAPI& inoutAPI, InstanceHandleOf<TAPI>& pInOutHandle, bool bReleaseAPITable, const utf8* pModuleName )
        {
            if ( pInOutHandle != nullptr && inoutAPI.shutdown != nullptr )
                inoutAPI.shutdown( pInOutHandle );
            if ( pInOutHandle != nullptr && inoutAPI.destroy != nullptr )
                inoutAPI.destroy( pInOutHandle );
            if ( bReleaseAPITable )
                engine::unregisterModuleTypes( pModuleName );
            if ( inoutAPI.bindService != nullptr )
                inoutAPI.bindService( nullptr );
            pInOutHandle = nullptr;
            if ( bReleaseAPITable )
                inoutAPI = {};
        }
    };
} // namespace sw
