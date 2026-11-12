#include "pch.h"

#include "App/Dev/RHIBackendSwitcher.h"

#include "App/EditorModuleManager.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "DevTools/Client/Automation/Scenario/AutomationBackendSteps.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepContext.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/RHIBackendType.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Engine.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"
#include "Engine/Presentation/IEngineRenderDriver.h"
#include "Engine/Renderer/RenderThread.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    /** @brief 시나리오가 장치 분실 정책을 설정 파일 대신 고르는 입구입니다(0 = 설정 값, 1 = Quit, 2 = Recreate). 교체기가 기동 때 렌더 드라이버에 덮는다(`overrideDeviceLostPolicy`). */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_deviceLostPolicy, 0, "장치 분실 정책 덮기 (0=EngineConfig 값, 1=Quit, 2=Recreate — 시나리오가 Recreate 길을 끝까지 볼 때)" );

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
            [[nodiscard]] static expected<void> prepareSwapBackend( AutomationStepContext& /*runner*/, const AutomationStep& /*step*/ )
            {
                RHIBackendSwitcher* pSwitcher = engine::findSubsystem<RHIBackendSwitcher>();
                if ( pSwitcher == nullptr || pSwitcher->recordStartBackend() == false )
                    return unexpected( Error{ ErrorKind::Unsupported, "backend switching is not available in this process (no RHI)" } );
                return {};
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
                {
                    if ( RHIAvailability::findOtherBackend( committed, backend ) == false )
                    {
                        runner.recordFailure( step, "backend=\"other\" needs a second backend, and this build has only one" );
                        return true;
                    }
                }
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
                    SW_LOG_INFO_ID( "App.Already", "%# -> already on %#", step.describe().c_str(), RHI::getBackendTypeName( backend ) );
                    return true;
                }
                if ( pSwitcher->requestSwap( backend ) == false )
                {
                    runner.recordFailure( step, "gv_rhiBackend is not registered" );
                    return true;
                }
                SW_LOG_INFO_ID( "App.Requested", "%# -> requested %#", step.describe().c_str(), RHI::getBackendTypeName( backend ) );
                return true;
            }

            /**
             * @brief 개발 명령 `rhi.removeDevice [driverWaitMs]` — 장치 제거(TDR)를 흉내 낸다. 제거 뒤 엔진이 Config 정책(`EngineConfig::_deviceLostPolicy`)대로 끝나는지 — 멈추지 않고 정한 종료 코드로 — 보는 데 쓴다.
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
                        StringBuilder<constant::buffer::kMax256> replyBuilder;
                        replyBuilder.appendFormat( "driver wait must be 1..%# ms", RHIBackendSwitcher::kMaxDeviceLossDriverWaitMilliseconds );
                        outReply = string( replyBuilder.view() );
                        return false;
                    }
                    if ( pSwitcher == nullptr )
                    {
                        outReply = "no backend switcher in this run";
                        return false;
                    }
                    pSwitcher->removeDeviceAfterDriverWaitForTest( static_cast<uint32>( waitMilliseconds ) );
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "next render frame waits %# ms where the fence wait is, then removes the device", waitMilliseconds );
                    outReply = string( replyBuilder.view() );
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

            [[nodiscard]] static bool readDeviceRecreateCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const IEngineRenderDriver* pRenderDriver = engine::findSubsystem<IEngineRenderDriver>();
                if ( pRenderDriver == nullptr )
                    return false;
                outValue = static_cast<float64>( pRenderDriver->getDeviceRecreateCount() );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
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

        // 시나리오가 장치 분실 정책을 고르는 입구(`<Launch variable="gv_deviceLostPolicy" value="2"/>`) — 설정 파일을 고치지 않고 Recreate 길을 끝까지 본다.
        IEngineRenderDriver* const pRenderDriver = pEngine != nullptr ? pEngine->getRenderDriver() : nullptr;
        if ( gv_deviceLostPolicy != 0 && pRenderDriver != nullptr )
        {
            const bool bKnownPolicy = gv_deviceLostPolicy == static_cast<int32>( RHIDeviceLostPolicy::Quit ) || gv_deviceLostPolicy == static_cast<int32>( RHIDeviceLostPolicy::Recreate );
            if ( bKnownPolicy )
                pRenderDriver->overrideDeviceLostPolicy( static_cast<RHIDeviceLostPolicy>( gv_deviceLostPolicy ) );
            else
                SW_LOG_ERROR_ID( "App.DeviceLostPolicyOverrideUnknown", "gv_deviceLostPolicy %# is not a policy (1 = Quit, 2 = Recreate) - the EngineConfig value stays", gv_deviceLostPolicy );
        }

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
            SW_LOG_ERROR_ID( "App.BackendSoftRecreateFailed", "Backend soft-recreate failed." );
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
        // 순서(모듈 내리기 → 장치 → 모듈 세우기)는 배포도 쓰는 모듈 호스트 하나가 안다 — 장치 분실 재생성(`RHIDeviceLostPolicy::Recreate`)이 같은 길이다.
        return _pGameModuleManager->applyPendingBackendChange( *_pEngine );
    }

    SW_DEV_COMMAND( rhiRemoveDevice, "rhi.removeDevice", "rhi.removeDevice [driverWaitMs]",
                    "Remove the GPU device as a TDR would (DirectX 12; Vulkan marks it lost) to check that the engine then answers by _deviceLostPolicy (Quit: exit code DeviceLost; Recreate: a new device) without hanging; with a wait, the render thread first blocks that long where the fence wait is",
                    &RHIBackendSwitcherInternal::runRemoveDevice );
    SW_AUTOMATION_STEP( swapBackend, AutomationBackendSteps::kSwapBackendKind, &RHIBackendSwitcherInternal::runSwapBackend, &AutomationBackendSteps::validateSwapBackend, AutomationStepOptions{}.withPrepare( &RHIBackendSwitcherInternal::prepareSwapBackend ).withWallBudget( AutomationBackendSteps::kSwapBackendWallSeconds ) );
    // 설명의 백엔드 목록은 쿠킹 표에서 만든다(줄 순서 = 열거값) — 손으로 적은 목록이 WebGPU 를 빠뜨렸다.
#define SW_RHI_BACKEND_WATCH_NAME_ROW( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) " " #Backend
    SW_AUTOMATION_WATCH( rhiBackend, "RHI.Backend", "Backend the device runs on, as the RHIBackend value counted from 0 in this order:" SW_RHI_BACKEND_TABLE( SW_RHI_BACKEND_WATCH_NAME_ROW ),
                         &RHIBackendSwitcherInternal::readCommittedBackend );
#undef SW_RHI_BACKEND_WATCH_NAME_ROW
    SW_AUTOMATION_WATCH( rhiSwapPending, "RHI.SwapPending", "1 from a backend swap request (SwapBackend, gv_rhiBackend) until the end-of-frame swap has run",
                         &RHIBackendSwitcherInternal::readSwapPending );
    SW_AUTOMATION_WATCH( renderDeviceRecreateCount, "Render.DeviceRecreateCount", "Lost devices recreated with the scene rebound (EngineConfig _deviceLostPolicy = Recreate) since the process started",
                         &RHIBackendSwitcherInternal::readDeviceRecreateCount );
    SW_AUTOMATION_WATCH( rhiSwapFailedCount, "RHI.SwapFailedCount", "Backend swaps that failed since the process started (each falls back to the previous backend)",
                         &RHIBackendSwitcherInternal::readFailedSwapCount );

    void RHIBackendSwitcher::removeDeviceAfterDriverWaitForTest( uint32 waitMilliseconds )
    {
        SW_ASSERT( 1u <= waitMilliseconds && waitMilliseconds <= kMaxDeviceLossDriverWaitMilliseconds );
        _pendingDeviceLossWaitMilliseconds.store( waitMilliseconds, std::memory_order_relaxed );
    }

    void RHIBackendSwitcher::bindRenderThreadHook( RenderThread& renderThread )
    {
        renderThread.setBeginFrameHook( SW_DELEGATE_METHOD( BeginFrameHookDelegate, &RHIBackendSwitcher::onBeginFrameOnRenderThread, this ) ); // 개발 명령 `rhi.removeDevice <ms>`
    }

    void RHIBackendSwitcher::onBeginFrameOnRenderThread( IRHIDevice& device )
    {
        const uint32 waitMilliseconds = _pendingDeviceLossWaitMilliseconds.exchange( 0u, std::memory_order_relaxed );
        if ( waitMilliseconds == 0u )
            return;
        SW_LOG_WARNING_ID( "App.HoldingRenderThreadMs", "Holding the render thread %# ms before removing the device (rhi.removeDevice <ms>) - the driver wait a real TDR spends before the first device-lost result",
                           waitMilliseconds );
        MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + static_cast<int64>( waitMilliseconds ) * constant::time::kNanosecondsPerMillisecond );
        if ( device.debugRemoveDevice() == false )
            SW_LOG_ERROR_ID( "App.RhiRemoveDeviceMsBackend", "rhi.removeDevice <ms>: this backend cannot remove its device; only the wait ran" );
    }
} // namespace sw
