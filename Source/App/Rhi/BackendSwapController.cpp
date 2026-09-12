#include "pch.h"

#include "App/Rhi/BackendSwapController.h"

#include "App/Module/ModuleHost.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/EngineLoop.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    namespace
    {
        /** @brief 이 TU 로컬 헬퍼 모음 (유니티 빌드 이름 충돌을 피하려 TU 이름을 붙인다). */
        struct BackendSwapControllerInternal
        {
            /** @brief gv_rhiBackend 의 변수 정보를 찾습니다. 없으면 nullptr. */
            static GlobalVariableInfo* findBackendVariable()
            {
                return engine::getGlobalVariableManager().findVariable( "gv_rhiBackend" );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "BackendSwap" );

    BackendSwapController::BackendSwapController()
        : _pEngineLoop{ nullptr }
        , _pModuleHost{ nullptr }
        , _bEnableEditor{ SW_FALSE }
        , _bHandlingChange{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    BackendSwapController::~BackendSwapController() = default;

    void BackendSwapController::initialize( EngineLoop* pEngineLoop, ModuleHost* pModuleHost, bool bEnableEditor )
    {
        _pEngineLoop   = pEngineLoop;
        _pModuleHost   = pModuleHost;
        _bEnableEditor = bEnableEditor ? SW_TRUE : SW_FALSE;

        GlobalVariableInfo* pBackendVariable = BackendSwapControllerInternal::findBackendVariable();
        if ( pBackendVariable != nullptr )
            pBackendVariable->_onValueChanged = SW_DELEGATE_METHOD( GlobalVariableChangedDelegate, &BackendSwapController::onBackendVariableChanged, this );
    }

    void BackendSwapController::shutdown()
    {
        if ( _pEngineLoop == nullptr )
            return;

        GlobalVariableInfo* pBackendVariable = BackendSwapControllerInternal::findBackendVariable();
        if ( pBackendVariable != nullptr )
            pBackendVariable->_onValueChanged = {};

        _pEngineLoop = nullptr;
        _pModuleHost = nullptr;
    }

    void BackendSwapController::applyIfPending()
    {
        if ( _pEngineLoop == nullptr )
            return;

        const RHI* pRHI = _pEngineLoop->getRHI();
        if ( pRHI == nullptr || pRHI->hasPendingBackendChange() == false )
            return;

        if ( applyPendingChange() == false )
        {
            SW_LOG_ERROR( "Backend soft-recreate failed." );
            // C++ 대입은 값만 바꾼다 — 변경 콜백(onBackendVariableChanged)은 GlobalVariableInfo 의
            // setValueAsInt/setValueFromString(콘솔·에디터 패널) 경로에서만 불린다. 되돌림이 재시도 루프가
            // 될 일은 없다.
            gv_rhiBackend = pRHI->getCommittedBackend();
        }
    }

    void BackendSwapController::onBackendVariableChanged( const GlobalVariableInfo* pInfo )
    {
        RHI* pRHI = _pEngineLoop != nullptr ? _pEngineLoop->getRHI() : nullptr;
        if ( pInfo == nullptr || pRHI == nullptr )
            return;

        // 아래의 되돌림은 C++ 대입이라 이 콜백을 다시 부르지 않는다(콜백은 GlobalVariableInfo 의 set* 경로만
        // 부른다). 재진입 가드는 되돌림을 언젠가 set* 로 바꾸더라도 무한 재귀가 되지 않도록 남겨 둔다.
        if ( _bHandlingChange == SW_TRUE )
            return;
        _bHandlingChange = SW_TRUE;

        const RHIBackend requestedBackend   = static_cast<RHIBackend>( pInfo->getValueAsInt() );
        const bool       bEditorUnsupported = _bEnableEditor == SW_TRUE && RHIAvailability::query( requestedBackend )._bEditorSupported == false;
        if ( bEditorUnsupported )
        {
            SW_LOG_WARNING( "Backend %# is not editor-supported — reverting.", RHI::getBackendTypeName( requestedBackend ) );
            gv_rhiBackend = pRHI->getCommittedBackend();
        }
        else
        {
            pRHI->schedulePendingBackendChange( requestedBackend );
        }

        _bHandlingChange = SW_FALSE;
    }

    bool BackendSwapController::applyPendingChange()
    {
        if ( _pEngineLoop == nullptr || _pModuleHost == nullptr )
            return false;

        // 모듈 핸들은 교체 **전에** 받아 둔다. 디바이스를 다시 만드는 동안 LiveReload 가 돌지는
        // 않지만, 재생성 경로가 "테이블이 비었으면 모듈에서 다시 바인딩" 을 하려면 핸들이 필요하다.
        void* pEditorModule{ nullptr };
        void* pGameModule{ nullptr };
#if !defined( SW_SHIPPING )
        // 모듈 수명은 ModuleHost 가 안다 — 여기서 리로드 내부를 직접 뒤지지 않는다.
        pEditorModule = _pModuleHost->getLoadedModuleHandle( sw::config::kTargetEditorModule );
        pGameModule   = _pModuleHost->getLoadedModuleHandle( sw::config::kTargetGameModule );
#endif

        // API 테이블은 놓지 않는다 — 모듈을 언로드하지 않고 같은 테이블로 다시 만든다.
        _pModuleHost->suspendModules( ModuleScope::Both, false );

        const bool bSwapOk = _pEngineLoop->applyPendingBackendChange();
        RHI*       pRHI    = _pEngineLoop->getRHI();
        if ( pRHI == nullptr || pRHI->hasDevice() == false )
        {
            SW_LOG_ERROR( "applyPendingBackendChange 실패 — RHI 디바이스가 없어 모듈을 재생성하지 않습니다." );
            return false;
        }

        const bool bReinitOk = _pModuleHost->reinitializeAfterRhiSwap( pEditorModule, pGameModule );
        if ( bReinitOk == false )
            SW_LOG_ERROR( "reinitializeAfterRhiSwap 실패." );
        if ( bSwapOk == false )
            SW_LOG_ERROR( "applyPendingBackendChange 실패 — 이전 백엔드로 복구한 뒤 모듈을 재생성했습니다." );
        return bSwapOk && bReinitOk;
    }
} // namespace sw
