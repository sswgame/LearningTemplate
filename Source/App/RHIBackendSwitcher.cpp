#include "pch.h"

#include "App/RHIBackendSwitcher.h"

#include "App/EditorModuleManager.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Engine.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Module/EditSessionState.h"
#include "Engine/Renderer/RenderThread.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    namespace
    {
        /** @brief gv_rhiBackend 의 변수 정보를 찾습니다. 없거나 엔진 서비스가 묶이지 않았으면(크래시 보고 프로세스) nullptr 입니다. */
        GlobalVariableInfo* findRHIBackendSwitcherVariable()
        {
            if ( engine::areEngineServicesBound() == false )
                return nullptr;
            return engine::getSubsystem<GlobalVariableManager>().findVariable( "gv_rhiBackend" );
        }
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "App" );

    RHIBackendSwitcher::RHIBackendSwitcher()
        : _pEngine{ nullptr }
        , _pGameModuleManager{ nullptr }
        , _failedSwapCount{ 0 }
        , _startBackend{}
        , _bEnableEditor{ SW_FALSE }
        , _bHandlingChange{ SW_FALSE }
        , _bStartBackendRecorded{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    bool RHIBackendSwitcher::findCommittedBackend( RHIBackend& outBackend ) const
    {
        const RHI* pRHI = _pEngine != nullptr ? _pEngine->getRHI() : nullptr;
        if ( pRHI == nullptr )
            return false;
        outBackend = pRHI->getCommittedBackend();
        return true;
    }

    bool RHIBackendSwitcher::recordStartBackend()
    {
        RHIBackend committed{};
        if ( findCommittedBackend( committed ) == false )
            return false;
        _startBackend          = committed;
        _bStartBackendRecorded = SW_TRUE;
        return true;
    }

    bool RHIBackendSwitcher::findStartBackend( RHIBackend& outBackend ) const
    {
        if ( _bStartBackendRecorded == SW_FALSE )
            return false;
        outBackend = _startBackend;
        return true;
    }

    bool RHIBackendSwitcher::removeDeviceForTest()
    {
        RHI* pRHI = _pEngine != nullptr ? _pEngine->getRHI() : nullptr;
        return pRHI != nullptr && pRHI->getDevice().debugRemoveDevice();
    }

#if SW_DEV_COMMANDS_ENABLED
    bool RHIBackendSwitcher::removeDeviceAfterDriverWaitForTest( uint32 waitMilliseconds )
    {
        RenderThread* pRenderThread = _pEngine != nullptr ? _pEngine->getRenderThread() : nullptr;
        if ( pRenderThread == nullptr )
            return false;
        pRenderThread->requestDeviceLossAfterDriverWait( waitMilliseconds );
        return true;
    }
#endif

    bool RHIBackendSwitcher::isSwapPending() const
    {
        const RHI* pRHI = _pEngine != nullptr ? _pEngine->getRHI() : nullptr;
        return pRHI != nullptr && pRHI->hasPendingBackendChange();
    }

    bool RHIBackendSwitcher::requestSwap( RHIBackend backend )
    {
        GlobalVariableInfo* pBackendVariable = findRHIBackendSwitcherVariable();
        if ( pBackendVariable == nullptr )
            return false;
        // C++ 대입은 변경 콜백을 부르지 않는다. 메뉴 · 콘솔이 쓰는 setValueAsInt 로 가야 onBackendVariableChanged 가 받는다.
        pBackendVariable->setValueAsInt( static_cast<int32>( backend ) );
        return true;
    }

    void RHIBackendSwitcher::initialize( Engine* pEngine, EditorModuleManager* pGameModuleManager, bool bEnableEditor )
    {
        _pEngine            = pEngine;
        _pGameModuleManager = pGameModuleManager;
        _bEnableEditor      = bEnableEditor ? SW_TRUE : SW_FALSE;

        GlobalVariableInfo* pBackendVariable = findRHIBackendSwitcherVariable();
        if ( pBackendVariable != nullptr )
            pBackendVariable->_onValueChanged = SW_DELEGATE_METHOD( GlobalVariableChangedDelegate, &RHIBackendSwitcher::onBackendVariableChanged, this );
    }

    void RHIBackendSwitcher::shutdown()
    {
        // **훅부터, 조건 없이 뗀다.** 훅은 `_pEngine` 와 상관없이 `initialize` 가 걸어 두므로, 루프를 받지 못한 채 초기화된
        // 경우(도구 · 부분 초기화)에도 떼지 않으면 사라진 `this` 를 가리키는 콜백이 전역 변수에 남는다.
        GlobalVariableInfo* pBackendVariable = findRHIBackendSwitcherVariable();
        if ( pBackendVariable != nullptr )
            pBackendVariable->_onValueChanged = {};

        _pEngine            = nullptr;
        _pGameModuleManager = nullptr;
    }

    void RHIBackendSwitcher::applyIfPending()
    {
        if ( _pEngine == nullptr )
            return;

        const RHI* pRHI = _pEngine->getRHI();
        if ( pRHI == nullptr || pRHI->hasPendingBackendChange() == false )
            return;

        if ( applyPendingChange() == false )
        {
            SW_LOG_ERROR( "Backend soft-recreate failed." );
            ++_failedSwapCount;
            // 값만 되돌린다. 변경 콜백(onBackendVariableChanged)은 GlobalVariableInfo 의 setValueAsInt/setValueFromString
            // (콘솔 · 에디터 패널) 경로에서만 불린다. 그래서 되돌림이 재시도 루프가 될 일은 없다. 심볼이 아니라 매니저가 든 주소로
            // 쓰므로 App 이 Engine.dll 의 변수를 import 할 필요가 없다.
            GlobalVariableInfo* pBackendVariable = findRHIBackendSwitcherVariable();
            if ( pBackendVariable != nullptr )
                *static_cast<RHIBackend*>( pBackendVariable->_pData ) = pRHI->getCommittedBackend();
        }
    }

    void RHIBackendSwitcher::onBackendVariableChanged( const GlobalVariableInfo* pInfo )
    {
        RHI* pRHI = _pEngine != nullptr ? _pEngine->getRHI() : nullptr;
        if ( pInfo == nullptr || pRHI == nullptr )
            return;

        // 아래의 되돌림은 C++ 대입이라 이 콜백을 다시 부르지 않는다(콜백은 GlobalVariableInfo 의 set* 경로만 부른다). 재진입
        // 가드는 되돌림을 언젠가 set* 로 바꾸더라도 무한 재귀가 되지 않도록 남겨 둔다.
        if ( _bHandlingChange == SW_TRUE )
            return;
        _bHandlingChange = SW_TRUE;

        const RHIBackend requestedBackend = static_cast<RHIBackend>( pInfo->getValueAsInt() );
        pRHI->schedulePendingBackendChange( requestedBackend );

        _bHandlingChange = SW_FALSE;
    }

    bool RHIBackendSwitcher::applyPendingChange()
    {
        if ( _pEngine == nullptr || _pGameModuleManager == nullptr )
            return false;

        // 모듈 핸들은 교체 **전에** 받아 둔다. 디바이스를 다시 만드는 동안 LiveReload 가 돌지는 않지만, 재생성 경로가 "테이블이
        // 비었으면 모듈에서 다시 바인딩" 을 하려면 핸들이 필요하다.
        void* pEditorModule{ nullptr };
        void* pGameModule{ nullptr };
#if !defined( SW_SHIPPING )
        // 모듈 수명은 GameModuleManager 가 안다. 여기서 리로드 내부를 직접 뒤지지 않는다.
        pEditorModule = _pGameModuleManager->getLoadedModuleHandle( sw::config::kTargetEditorModule );
        pGameModule   = _pGameModuleManager->getLoadedModuleHandle( sw::config::kTargetGameModule );
#endif

        // API 테이블은 놓지 않는다. 모듈을 언로드하지 않고 같은 테이블로 다시 만든다.
        // 모듈 이미지는 내리지 않고 인스턴스만 다시 만든다 — 에디터가 자기 코드를 가진 되돌리기 명령(지형 붓질 등)을 스택에 남기게 알린다.
#if !defined( SW_SHIPPING )
        EditSessionState* pEditSession = engine::findSubsystem<EditSessionState>();
        if ( pEditSession != nullptr )
            pEditSession->setModuleImageKept( true );
#endif
        _pGameModuleManager->suspendModules( ModuleScope::Both, false, DrainReason::BackendSwap );
#if !defined( SW_SHIPPING )
        if ( pEditSession != nullptr )
            pEditSession->setModuleImageKept( false );
#endif

        const bool bSwapOk = _pEngine->applyPendingBackendChange();
        RHI*       pRHI    = _pEngine->getRHI();
        if ( pRHI == nullptr || pRHI->hasDevice() == false )
        {
            SW_LOG_ERROR( "applyPendingBackendChange failed - no RHI device, modules are not recreated." );
            return false;
        }

        const bool bReinitOk = _pGameModuleManager->reinitializeAfterRHISwap( pEditorModule, pGameModule );
        if ( bReinitOk == false )
            SW_LOG_ERROR( "reinitializeAfterRHISwap failed." );
        if ( bSwapOk == false )
            SW_LOG_ERROR( "applyPendingBackendChange failed - restored the previous backend and recreated the modules." );
        return bSwapOk && bReinitOk;
    }
} // namespace sw
