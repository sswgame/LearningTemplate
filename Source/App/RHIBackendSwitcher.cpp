#include "pch.h"

#include "App/RHIBackendSwitcher.h"

#include "App/EditorModuleManager.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Automation/Scenario/AutomationBackendSteps.h"
#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepContext.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/RHIBackendType.h"
#include "Engine/Engine.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
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

        struct RHIBackendSwitcherInternal
        {
            /** @brief 준비 함수가 시나리오가 시작할 때의 백엔드를 교체기에 적습니다(`RHIBackendSwitcher::recordStartBackend`). 교체기는 백엔드 교체 · 모듈 리로드에서 내려가지 않는 App 의 객체다. */
            [[nodiscard]] static bool prepareSwapBackend( AutomationStepContext& /*runner*/, const AutomationStep& /*step*/, string& outError )
            {
                RHIBackendSwitcher* pSwitcher = engine::findSubsystem<RHIBackendSwitcher>();
                if ( pSwitcher == nullptr || pSwitcher->recordStartBackend() == false )
                {
                    outError = "SwapBackend: backend switching is not available in this process (no RHI)";
                    return false;
                }
                return true;
            }

            /** @brief 교체를 요청합니다. 이미 그 백엔드이고 대기 중인 교체도 없으면 아무것도 하지 않습니다. 끝은 `RHI.SwapPending` 이 0 이 되는 것으로 봅니다. */
            static bool runSwapBackend( AutomationStepContext& runner, const AutomationStep& step )
            {
                RHIBackendSwitcher* pSwitcher = engine::findSubsystem<RHIBackendSwitcher>();
                RHIBackend          committed{};
                if ( pSwitcher == nullptr || pSwitcher->findCommittedBackend( committed ) == false )
                {
                    runner.recordFailure( step, "backend switching is not available in this process (no RHI)" );
                    return true;
                }
                const string& requested = *step.findAttribute( "backend" );
                RHIBackend    backend{};
                if ( requested == AutomationBackendSteps::kOtherBackend )
                    backend = committed == RHIBackend::DirectX12 ? RHIBackend::DirectX11 : RHIBackend::DirectX12;
                else if ( requested == AutomationBackendSteps::kStartBackend )
                {
                    if ( pSwitcher->findStartBackend( backend ) == false )
                    {
                        runner.recordFailure( step, "backend=\"start\" needs the start backend, which the scenario preparation did not record" );
                        return true;
                    }
                }
                else
                    (void)AutomationBackendSteps::tryParseBackend( requested, backend ); // 검사에서 확인했다
                if ( committed == backend && pSwitcher->isSwapPending() == false )
                {
                    SW_LOG_INFO( "%# -> already on %#", step.describe().c_str(), RHI::getBackendTypeName( backend ) );
                    return true;
                }
                if ( pSwitcher->requestSwap( backend ) == false )
                {
                    runner.recordFailure( step, "gv_rhiBackend is not registered" );
                    return true;
                }
                SW_LOG_INFO( "%# -> requested %#", step.describe().c_str(), RHI::getBackendTypeName( backend ) );
                return true;
            }

            /**
             * @brief 개발 명령 `rhi.removeDevice [driverWaitMs]` — 장치 제거(TDR)를 흉내 낸다. 제거 뒤 프레임 · 종료가 멈추지 않는지 보는 데 쓴다.
             * @details 대기를 주면 렌더 스레드가 다음 프레임의 펜스 대기 자리에서 그만큼 잠든 뒤 제거한다(`RHIBackendSwitcher::removeDeviceAfterDriverWaitForTest`) — 실제 TDR 이 첫 손실 결과를 돌려주기 전 드라이버 안에서 막히는 모양이다.
             */
            static bool runRemoveDevice( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                RHIBackendSwitcher* pSwitcher = engine::findSubsystem<RHIBackendSwitcher>();
                if ( listArgument.size() == 1 )
                {
                    int32 waitMilliseconds = 0;
                    if ( StringUtil::parseInt( listArgument[0], waitMilliseconds ) == false )
                        return false;
                    const bool bInRange = 1 <= waitMilliseconds && static_cast<uint32>( waitMilliseconds ) <= RHIBackendSwitcher::kMaxDeviceLossDriverWaitMilliseconds;
                    if ( bInRange == false )
                    {
                        outReply = formatToString( "driver wait must be 1..%# ms", RHIBackendSwitcher::kMaxDeviceLossDriverWaitMilliseconds );
                        return false;
                    }
                    if ( pSwitcher == nullptr )
                    {
                        outReply = "no backend switcher in this run";
                        return false;
                    }
                    pSwitcher->removeDeviceAfterDriverWaitForTest( static_cast<uint32>( waitMilliseconds ) );
                    outReply = formatToString( "next render frame waits %# ms where the fence wait is, then removes the device", waitMilliseconds );
                    return true;
                }
                if ( pSwitcher == nullptr || pSwitcher->removeDeviceForTest() == false )
                {
                    outReply = "this backend cannot remove its device on request (DirectX 12 only)";
                    return false;
                }
                outReply = "device removed";
                return true;
            }

            [[nodiscard]] static bool readCommittedBackend( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const RHIBackendSwitcher* pSwitcher = engine::findSubsystem<RHIBackendSwitcher>();
                RHIBackend                backend{};
                if ( pSwitcher == nullptr || pSwitcher->findCommittedBackend( backend ) == false )
                    return false;
                outValue = static_cast<float64>( static_cast<uint32>( backend ) );
                return true;
            }

            [[nodiscard]] static bool readSwapPending( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const RHIBackendSwitcher* pSwitcher = engine::findSubsystem<RHIBackendSwitcher>();
                if ( pSwitcher == nullptr )
                    return false;
                outValue = pSwitcher->isSwapPending() ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readFailedSwapCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const RHIBackendSwitcher* pSwitcher = engine::findSubsystem<RHIBackendSwitcher>();
                if ( pSwitcher == nullptr )
                    return false;
                outValue = static_cast<float64>( pSwitcher->getFailedSwapCount() );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "App" );

    RHIBackendSwitcher::RHIBackendSwitcher()
        : _pEngine{ nullptr }
        , _pGameModuleManager{ nullptr }
        , _failedSwapCount{ 0 }
        , _pendingDeviceLossWaitMilliseconds{ 0 }
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
        // 모듈 수명은 GameModuleManager 가 안다(배포는 정적 링크라 nullptr). 여기서 리로드 내부를 직접 뒤지지 않는다.
        void* const pEditorModule = _pGameModuleManager->getLoadedModuleHandle( sw::config::kTargetEditorModule );
        void* const pGameModule   = _pGameModuleManager->getLoadedModuleHandle( sw::config::kTargetGameModule );

        // API 테이블은 놓지 않는다. 모듈을 언로드하지 않고 같은 테이블로 다시 만든다(편집기는 리로드와 같은 길로 상태를 맡겼다 되찾는다 — `IModuleStateStore`).
        // 엔진 몫(`Backend swap took`)과 모듈 몫(게임 · 에디터를 다시 세우고 씬 상태를 되돌린다)을 함께 남긴다 - 교체 시간의 대부분이 모듈 몫이던 때 엔진 줄만 보였다.
        const Stopwatch swapWatch;
        _pGameModuleManager->suspendModules( ModuleScope::Both, false, DrainReason::BackendSwap );
        const uint64 suspendMicroseconds = swapWatch.getElapsedMicroseconds();

        const bool   bSwapOk            = _pEngine->applyPendingBackendChange();
        const uint64 engineMicroseconds = swapWatch.getElapsedMicroseconds();
        RHI*         pRHI               = _pEngine->getRHI();
        if ( pRHI == nullptr || pRHI->hasDevice() == false )
        {
            SW_LOG_ERROR( "applyPendingBackendChange failed - no RHI device, modules are not recreated." );
            return false;
        }

        const bool bReinitOk = _pGameModuleManager->reinitializeAfterRHISwap( pEditorModule, pGameModule );
        if ( bReinitOk == false )
            SW_LOG_ERROR( "reinitializeAfterRHISwap failed." );
        const uint64 totalMicroseconds = swapWatch.getElapsedMicroseconds();
        SW_LOG_INFO( "Backend swap with modules took %#ms (suspend modules %#ms, engine %#ms, recreate modules %#ms)", totalMicroseconds / 1000, suspendMicroseconds / 1000,
                     ( engineMicroseconds - suspendMicroseconds ) / 1000, ( totalMicroseconds - engineMicroseconds ) / 1000 );
        if ( bSwapOk == false )
            SW_LOG_ERROR( "applyPendingBackendChange failed - restored the previous backend and recreated the modules." );
        return bSwapOk && bReinitOk;
    }

    SW_DEV_COMMAND( rhiRemoveDevice, "rhi.removeDevice", "rhi.removeDevice [driverWaitMs]",
                    "Remove the GPU device as a TDR would (DirectX 12; Vulkan marks it lost) to check that frames and shutdown do not hang; with a wait, the render thread first blocks that long where the fence wait is",
                    &RHIBackendSwitcherInternal::runRemoveDevice );
    SW_AUTOMATION_STEP_WITH_WALL_BUDGET( swapBackend, AutomationBackendSteps::kSwapBackendKind, &RHIBackendSwitcherInternal::runSwapBackend,
                                         &AutomationBackendSteps::validateSwapBackend, &RHIBackendSwitcherInternal::prepareSwapBackend, nullptr,
                                         AutomationBackendSteps::kSwapBackendWallSeconds );
    SW_AUTOMATION_WATCH( rhiBackend, "RHI.Backend", "Backend the device runs on: 0 DirectX11, 1 DirectX12, 2 Vulkan, 3 OpenGL",
                         &RHIBackendSwitcherInternal::readCommittedBackend );
    SW_AUTOMATION_WATCH( rhiSwapPending, "RHI.SwapPending", "1 from a backend swap request (SwapBackend, gv_rhiBackend) until the end-of-frame swap has run",
                         &RHIBackendSwitcherInternal::readSwapPending );
    SW_AUTOMATION_WATCH( rhiSwapFailedCount, "RHI.SwapFailedCount", "Backend swaps that failed since the process started (each falls back to the previous backend)",
                         &RHIBackendSwitcherInternal::readFailedSwapCount );

    void RHIBackendSwitcher::removeDeviceAfterDriverWaitForTest( uint32 waitMilliseconds )
    {
        SW_ASSERT( 1u <= waitMilliseconds && waitMilliseconds <= kMaxDeviceLossDriverWaitMilliseconds );
        _pendingDeviceLossWaitMilliseconds.store( waitMilliseconds, std::memory_order_relaxed );
    }

    void RHIBackendSwitcher::onBeginFrameOnRenderThread( IRHIDevice& device )
    {
        const uint32 waitMilliseconds = _pendingDeviceLossWaitMilliseconds.exchange( 0u, std::memory_order_relaxed );
        if ( waitMilliseconds == 0u )
            return;
        SW_LOG_WARNING( "Holding the render thread %# ms before removing the device (rhi.removeDevice <ms>) - the driver wait a real TDR spends before the first device-lost result",
                        waitMilliseconds );
        MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + static_cast<int64>( waitMilliseconds ) * constant::kNanosecondsPerMillisecond );
        if ( device.debugRemoveDevice() == false )
            SW_LOG_ERROR( "rhi.removeDevice <ms>: this backend cannot remove its device; only the wait ran" );
    }
} // namespace sw
