#include "pch.h"

#include "App/RHIBackendSwitcher.h"

#include "App/EditorModuleHost.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Engine.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Profiling/FrameProfiler.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    /**
     * @brief `-gv_rhiSwapAtFrame=N -gv_rhiSwapTo=<backend>`: N 번째 프레임에 백엔드 교체를 요청합니다.
     * @details 에디터 메뉴 없이(헤드리스로) 교체를 재현 · 검증하는 창구입니다. 요청 방식은 에디터
     *          패널과 같습니다(`GlobalVariableInfo::setValueAsInt` → 변경 콜백 → 그 프레임의 `applyIfPending` 이
     *          적용). C++ 대입(`gv_rhiBackend = x`)은 콜백을 부르지 않아 아무 일도 일어나지 않습니다. 0 이면 꺼져 있습니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_rhiSwapAtFrame, 0, "이 프레임에 백엔드 교체를 요청한다 (0=사용 안 함)" );
    SW_TEST_GLOBAL_VARIABLE( RHIBackend, gv_rhiSwapTo, RHIBackend::DirectX12, "gv_rhiSwapAtFrame 에 바꿀 백엔드" );

    namespace
    {
        /** @brief 이 TU 전용 도우미 모음입니다(유니티 빌드에서 이름이 충돌하지 않도록 TU 이름을 붙입니다). */
        struct RHIBackendSwitcherInternal
        {
            /** @brief gv_rhiBackend 의 변수 정보를 찾습니다. 없거나 엔진 서비스가 묶이지 않았으면(크래시 보고 프로세스) nullptr 입니다. */
            static GlobalVariableInfo* findBackendVariable()
            {
                if ( engine::areEngineServicesBound() == false )
                    return nullptr;
                return engine::getSubsystem<GlobalVariableManager>().findVariable( "gv_rhiBackend" );
            }

            /** @brief `initialize` 한 교체기입니다(프로세스에 하나). 자동화 단계와 탐침이 찾는다. */
            static RHIBackendSwitcher* s_pActive;
        };

        RHIBackendSwitcher* RHIBackendSwitcherInternal::s_pActive = nullptr;
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "App" );

    RHIBackendSwitcher::RHIBackendSwitcher()
        : _pEngine{ nullptr }
        , _pModuleHost{ nullptr }
        , _failedSwapCount{ 0 }
        , _bEnableEditor{ SW_FALSE }
        , _bHandlingChange{ SW_FALSE }
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

    bool RHIBackendSwitcher::isSwapPending() const
    {
        const RHI* pRHI = _pEngine != nullptr ? _pEngine->getRHI() : nullptr;
        return pRHI != nullptr && pRHI->hasPendingBackendChange();
    }

    bool RHIBackendSwitcher::requestSwap( RHIBackend backend )
    {
        GlobalVariableInfo* pBackendVariable = RHIBackendSwitcherInternal::findBackendVariable();
        if ( pBackendVariable == nullptr )
            return false;
        // C++ 대입은 변경 콜백을 부르지 않는다. 메뉴 · 콘솔이 쓰는 setValueAsInt 로 가야 onBackendVariableChanged 가 받는다.
        pBackendVariable->setValueAsInt( static_cast<int32>( backend ) );
        return true;
    }

    void RHIBackendSwitcher::initialize( Engine* pEngine, EditorModuleHost* pModuleHost, bool bEnableEditor )
    {
        _pEngine                              = pEngine;
        _pModuleHost                          = pModuleHost;
        _bEnableEditor                        = bEnableEditor ? SW_TRUE : SW_FALSE;
        RHIBackendSwitcherInternal::s_pActive = this;

        GlobalVariableInfo* pBackendVariable = RHIBackendSwitcherInternal::findBackendVariable();
        if ( pBackendVariable != nullptr )
            pBackendVariable->_onValueChanged = SW_DELEGATE_METHOD( GlobalVariableChangedDelegate, &RHIBackendSwitcher::onBackendVariableChanged, this );
        // `-gv_rhiSwapAtFrame` 진단은 프레임 시작에서 본다 — 루프는 관찰자 목록만 안다.
        if ( _pEngine != nullptr )
            _pEngine->addFrameObserver( this );
    }

    void RHIBackendSwitcher::shutdown()
    {
        // **훅부터, 조건 없이 뗀다.** 훅은 `_pEngine` 와 상관없이 `initialize` 가 걸어 두므로, 루프를 받지 못한 채 초기화된
        // 경우(도구 · 부분 초기화)에도 떼지 않으면 사라진 `this` 를 가리키는 콜백이 전역 변수에 남는다.
        GlobalVariableInfo* pBackendVariable = RHIBackendSwitcherInternal::findBackendVariable();
        if ( pBackendVariable != nullptr )
            pBackendVariable->_onValueChanged = {};

        if ( RHIBackendSwitcherInternal::s_pActive == this )
            RHIBackendSwitcherInternal::s_pActive = nullptr;
        if ( _pEngine != nullptr )
            _pEngine->removeFrameObserver( this );
        _pEngine     = nullptr;
        _pModuleHost = nullptr;
    }

    RHIBackendSwitcher* RHIBackendSwitcher::findActive()
    {
        return RHIBackendSwitcherInternal::s_pActive;
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
            GlobalVariableInfo* pBackendVariable = RHIBackendSwitcherInternal::findBackendVariable();
            if ( pBackendVariable != nullptr )
                *static_cast<RHIBackend*>( pBackendVariable->_pData ) = pRHI->getCommittedBackend();
        }
    }

    void RHIBackendSwitcher::onFrameBegin( [[maybe_unused]] const EngineFrameContext& context )
    {
        // 테스트용 스위치라 Shipping 에는 없다(그 빌드에서 gv_rhiSwapAtFrame 은 등록되지 않아 늘 0 이다).
#if !defined( SW_SHIPPING )
        if ( gv_rhiSwapAtFrame <= 0 || engine::getSubsystem<FrameProfiler>().getFrameCount() != static_cast<uint64>( gv_rhiSwapAtFrame ) )
            return;
        // 프레임 수는 루프의 `tick` 이 프로파일러 프레임을 열기 전이라 그 프레임의 `tick` 첫머리와 같은 값이다.
        GlobalVariableInfo* pBackendVariable = RHIBackendSwitcherInternal::findBackendVariable();
        if ( pBackendVariable == nullptr || static_cast<RHIBackend>( pBackendVariable->getValueAsInt() ) == gv_rhiSwapTo )
            return;
        SW_LOG_INFO( "[SwapProbe] frame %# — requesting backend %#", gv_rhiSwapAtFrame, RHI::getBackendTypeName( gv_rhiSwapTo ) );
        // C++ 대입은 변경 콜백을 부르지 않는다. 메뉴 · 콘솔이 쓰는 setValueAsInt 로 가야 onBackendVariableChanged 가 받는다.
        pBackendVariable->setValueAsInt( static_cast<int32>( gv_rhiSwapTo ) );
        gv_rhiSwapAtFrame = 0; // 한 번만. 프로파일러가 프레임 수를 되돌리면(워밍업 뒤) 같은 번호가 다시 온다
#endif
    }

    int32 RHIBackendSwitcher::onFrameEnd( [[maybe_unused]] const EngineFrameContext& context )
    {
        return kRunning;
    }

    int32 RHIBackendSwitcher::onWindowClosed( [[maybe_unused]] const EngineFrameContext& context )
    {
        return kRunning;
    }

    void RHIBackendSwitcher::onBuildRenderPacket( [[maybe_unused]] EngineRenderPacketContext& inoutContext )
    {
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
        if ( _pEngine == nullptr || _pModuleHost == nullptr )
            return false;

        // 모듈 핸들은 교체 **전에** 받아 둔다. 디바이스를 다시 만드는 동안 LiveReload 가 돌지는 않지만, 재생성 경로가 "테이블이
        // 비었으면 모듈에서 다시 바인딩" 을 하려면 핸들이 필요하다.
        void* pEditorModule{ nullptr };
        void* pGameModule{ nullptr };
#if !defined( SW_SHIPPING )
        // 모듈 수명은 ModuleHost 가 안다. 여기서 리로드 내부를 직접 뒤지지 않는다.
        pEditorModule = _pModuleHost->getLoadedModuleHandle( sw::config::kTargetEditorModule );
        pGameModule   = _pModuleHost->getLoadedModuleHandle( sw::config::kTargetGameModule );
#endif

        // API 테이블은 놓지 않는다. 모듈을 언로드하지 않고 같은 테이블로 다시 만든다.
        _pModuleHost->suspendModules( ModuleScope::Both, false );

        const bool bSwapOk = _pEngine->applyPendingBackendChange();
        RHI*       pRHI    = _pEngine->getRHI();
        if ( pRHI == nullptr || pRHI->hasDevice() == false )
        {
            SW_LOG_ERROR( "applyPendingBackendChange 실패 — RHI 디바이스가 없어 모듈을 재생성하지 않습니다." );
            return false;
        }

        const bool bReinitOk = _pModuleHost->reinitializeAfterRHISwap( pEditorModule, pGameModule );
        if ( bReinitOk == false )
            SW_LOG_ERROR( "reinitializeAfterRHISwap 실패." );
        if ( bSwapOk == false )
            SW_LOG_ERROR( "applyPendingBackendChange 실패 — 이전 백엔드로 복구한 뒤 모듈을 재생성했습니다." );
        return bSwapOk && bReinitOk;
    }
} // namespace sw
