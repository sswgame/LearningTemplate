/**
 * @file ModuleInstanceUtil.h
 * @brief 모듈 이미지에서 API 표를 받고 인스턴스를 만들고 내리는 절차입니다. 게임(`ModuleHost`)과 에디터(`EditorModuleHost`)가 같은 절차를 씁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Module/ModuleImageUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Module/ModuleTypeRegistry.h"

#include "RuntimeAPI/ABI/ModuleAbi.h"

namespace sw
{
    class IWindow;

    /**
     * @struct ModuleApiSymbols
     * @brief 모듈 이미지에서 API 표를 받는 데 쓰는 심볼 이름입니다. 에디터 · 게임이 같은 절차를 이 이름만 바꿔 씁니다.
     */
    struct ModuleApiSymbols
    {
        const utf8* _pVersionSymbol;
        const utf8* _pStampSymbol;
        const utf8* _pExportSymbol;
        const utf8* _pModuleLabel;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ModuleInstanceUtil
     * @brief 모듈 이미지 → API 표 → 인스턴스의 공통 절차입니다. 내리는 순서(타입 등록 해제 뒤 서비스 떼기)가 에디터 · 게임에서 갈라지지 않게 한 곳에 둡니다.
     */
    struct ModuleInstanceUtil
    {
        /**
         * @brief 모듈이 호스트와 **같은 테이블 구조**로 빌드됐는지 대조합니다.
         * @details `GameAPI` · `EditorAPI` 는 함수 포인터를 순서대로 늘어놓은 구조체입니다. 모듈은 자기가 아는 자리에 채우고 호스트는
         *          자기가 아는 자리에서 읽으므로, 서로 다른 헤더로 빌드되면 **호스트가 엉뚱한 함수를 부릅니다.** `create != nullptr && destroy != nullptr`
         *          만으로는 막지 못합니다 — 그 둘은 **맨 앞**에 있어서 가운데에 끼워 넣어도 채워집니다. 핫 리로드는 모듈만 다시 빌드하는 기능이라 이런
         *          어긋남이 생기는 바로 그 상황입니다. RHI 경계의 `RHIModuleAbi.h` 와 같은 대조입니다.
         */
        static bool matchesModuleAbi( void* pLibraryModule, const ModuleApiSymbols& symbols )
        {
            const PFN_GetModuleAbiVersion pfnVersion =
                reinterpret_cast<PFN_GetModuleAbiVersion>( ModuleImageUtil::getDynamicSymbol( pLibraryModule, symbols._pVersionSymbol ) );
            if ( pfnVersion == nullptr || pfnVersion() != kModuleAbiVersion )
            {
                SW_LOG_ERROR( "%# 모듈 ABI 버전이 다릅니다 (기대 %#) — 엔진과 모듈을 함께 다시 빌드하세요.", symbols._pModuleLabel, kModuleAbiVersion );
                return false;
            }

            const PFN_GetModuleAbiStamp pfnStamp = reinterpret_cast<PFN_GetModuleAbiStamp>( ModuleImageUtil::getDynamicSymbol( pLibraryModule, symbols._pStampSymbol ) );
            if ( pfnStamp == nullptr || StringUtil::equals( pfnStamp(), kModuleAbiStamp ) == false )
            {
                SW_LOG_ERROR( "%# 모듈 ABI 스탬프가 다릅니다 (기대 '%#') — 엔진과 모듈을 함께 다시 빌드하세요.", symbols._pModuleLabel, kModuleAbiStamp );
                return false;
            }
            return true;
        }

        /**
         * @brief 모듈 이미지의 ABI 를 대조하고 API 표를 받습니다(에디터 · 게임 공통). 받지 못하면 @p outApi 는 빈 표입니다.
         * @details 리로드 전 검사(`is*ImageUsable`)와 바인딩(`bind*Api`)이 같은 절차를 씁니다.
         */
        template <typename TApi, typename TExportFn>
        [[nodiscard]] static bool exportApiFromImage( void* pLibraryModule, const ModuleApiSymbols& symbols, TApi& outApi )
        {
            outApi = {};
            if ( pLibraryModule == nullptr || matchesModuleAbi( pLibraryModule, symbols ) == false )
                return false;
            const TExportFn pfnExport = reinterpret_cast<TExportFn>( ModuleImageUtil::getDynamicSymbol( pLibraryModule, symbols._pExportSymbol ) );
            if ( pfnExport == nullptr || pfnExport( &outApi ) == false )
            {
                outApi = {};
                SW_LOG_ERROR( "The %# module does not export its API table (%#)", symbols._pModuleLabel, symbols._pExportSymbol );
                return false;
            }
            return true;
        }

        /**
         * @brief 바인딩한 API 표로 인스턴스를 만들고 초기화합니다(에디터 · 게임 공통). 실패하면 만든 것을 부수고 핸들을 비웁니다.
         * @note @p bRequireDevice 면 **디바이스가 없을 때 만들지 않습니다.** `RHI::getDevice()` 는 널 참조를 반환하므로 묻는 것 자체가 죽는
         *       길이고, 만들어 봐야 초기화가 실패할 것이 정해져 있습니다. 전용 서버의 게임은 디바이스 없이(nullptr) 만듭니다.
         */
        template <typename TApi>
        [[nodiscard]] static bool createInstance( const TApi& api, void*& pOutHandle, IWindow* pWindow, RHI* pRHI, bool bRequireDevice, const utf8* pModuleLabel )
        {
            pOutHandle            = nullptr;
            const bool bHasDevice = pRHI != nullptr && pRHI->hasDevice();
            if ( bRequireDevice && bHasDevice == false )
            {
                SW_LOG_ERROR( "No RHI device - %# instance is not created", pModuleLabel );
                return false;
            }

            pOutHandle = api.create();
            if ( pOutHandle == nullptr )
            {
                SW_LOG_ERROR( "Failed to create %# instance", pModuleLabel );
                return false;
            }

            if ( api.initialize( pOutHandle, pWindow, bHasDevice ? &pRHI->getDevice() : nullptr ) == false )
            {
                SW_LOG_ERROR( "Failed to initialize %# instance", pModuleLabel );
                if ( api.destroy != nullptr )
                    api.destroy( pOutHandle );
                pOutHandle = nullptr;
                return false;
            }
            SW_LOG_INFO( "Module instance initialized: %#", pModuleLabel );
            return true;
        }

        /**
         * @brief 인스턴스를 내립니다(에디터 · 게임 공통): shutdown → destroy → (표를 놓으면) 모듈 타입 등록 해제 → 서비스 떼기 → 핸들 비우기
         *        → (표를 놓으면) API 표 비우기.
         * @details 타입 등록 해제가 그 모듈의 살아 있는 컴포넌트를 지웁니다. 그 소멸자는 모듈 코드라 서비스가 아직 붙어 있는 동안이어야 합니다.
         */
        template <typename TApi>
        static void destroyInstance( TApi& inoutApi, void*& pInOutHandle, bool bReleaseApiTable, [[maybe_unused]] const utf8* pModuleName )
        {
            if ( pInOutHandle != nullptr && inoutApi.shutdown != nullptr )
                inoutApi.shutdown( pInOutHandle );
            if ( pInOutHandle != nullptr && inoutApi.destroy != nullptr )
                inoutApi.destroy( pInOutHandle );
            // Shipping 은 모듈을 내리지 않으므로 등록 해제 자체가 없다(Engine 에도 그 코드가 없다).
#if !defined( SW_SHIPPING )
            if ( bReleaseApiTable )
                engine::unregisterModuleTypes( pModuleName );
#endif
            if ( inoutApi.bindService != nullptr )
                inoutApi.bindService( nullptr );
            pInOutHandle = nullptr;
            if ( bReleaseApiTable )
                inoutApi = {};
        }
    };
} // namespace sw
