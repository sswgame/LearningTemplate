#include "pch.h"

#include "DevTools/Client/Render/RHIBackendSwitcher.h"

#include "Core/Foundation/DynamicModules.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "DevTools/Client/Automation/Scenario/AutomationBackendSteps.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepContext.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/RHIBackendType.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"
#include "Engine/Presentation/IEngineRenderDriver.h"
#include "Engine/Renderer/RHISubsystem.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Renderer/RenderThreadSubsystem.h"

namespace sw
{
    namespace
    {
        struct RHIBackendSwitcherInternal
        {
            /** @brief gv_rhiBackend 의 변수 정보를 찾습니다. 없거나 엔진 서비스가 묶이지 않았으면(크래시 보고 프로세스) nullptr 입니다. */
            static GlobalVariableInfo* findBackendVariable()
            {
                if ( engine::areEngineServicesBound() == false )
                    return nullptr;
                return engine::getSubsystem<GlobalVariableManager>().findVariable( "gv_rhiBackend" );
            }

#if SW_WITH_DYNAMIC_MODULES
            /** @brief 준비 함수가 시나리오가 시작할 때의 백엔드를 교체기에 적습니다(`RHIBackendSwitcher::recordStartBackend`). 교체기는 백엔드 교체 · 모듈 리로드에서 내려가지 않는 기동 단계다. */
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
                    SW_LOG_INFO_ID( "RHIBackendSwitcher.Already", "%# -> already on %#", step.describe().c_str(), RHI::getBackendTypeName( backend ) );
                    return true;
                }
                if ( pSwitcher->requestSwap( backend ) == false )
                {
                    runner.recordFailure( step, "gv_rhiBackend is not registered" );
                    return true;
                }
                SW_LOG_INFO_ID( "RHIBackendSwitcher.Requested", "%# -> requested %#", step.describe().c_str(), RHI::getBackendTypeName( backend ) );
                return true;
            }
#endif // SW_WITH_DYNAMIC_MODULES — 정적 모듈 구성에는 `SwapBackend` 단계가 없다(백엔드 하나)

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
                    if ( pSwitcher == nullptr || pSwitcher->removeDeviceAfterDriverWaitForTest( static_cast<uint32>( waitMilliseconds ) ) == false )
                    {
                        outReply = "no backend switcher or render thread in this run";
                        return false;
                    }
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
                const IEngineRenderDriver* pRenderDriver = engine::findSubsystem<IEngineRenderDriver>();
                if ( pRenderDriver == nullptr )
                    return false;
                outValue = static_cast<float64>( pRenderDriver->getFailedBackendChangeCount().get() );
                return true;
            }

            [[nodiscard]] static bool readDeviceRecreateCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const IEngineRenderDriver* pRenderDriver = engine::findSubsystem<IEngineRenderDriver>();
                if ( pRenderDriver == nullptr )
                    return false;
                outValue = static_cast<float64>( pRenderDriver->getDeviceRecreateCount().get() );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    RHIBackendSwitcher::RHIBackendSwitcher( RHISubsystem& rhiSubsystem, RenderThreadSubsystem& renderThreadSubsystem )
        : _rhiSubsystem{ rhiSubsystem }
        , _renderThreadSubsystem{ renderThreadSubsystem }
        , _backendVariableSubscription{}
        , _pendingDeviceLossWaitMilliseconds{ 0 }
        , _startBackend{}
        , _bStartBackendRecorded{ SW_FALSE }
        , _bBeginFrameHookBound{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    SubsystemInitResult RHIBackendSwitcher::initializeSubsystem()
    {
        GlobalVariableInfo* const pBackendVariable = RHIBackendSwitcherInternal::findBackendVariable();
        if ( pBackendVariable == nullptr )
        {
            SW_LOG_ERROR_ID( "RHIBackendSwitcher.VariableMissing", "The backend switcher found no 'gv_rhiBackend' registration" );
            return SubsystemInitResult::Failed;
        }
        _backendVariableSubscription = pBackendVariable->bindOnValueChanged( SW_DELEGATE_METHOD( GlobalVariableChangedDelegate, &RHIBackendSwitcher::onBackendVariableChanged, this ) );
        return SubsystemInitResult::Succeeded;
    }

    void RHIBackendSwitcher::shutdownSubsystem()
    {
        _backendVariableSubscription.reset();
        // 렌더 스레드 객체는 단계를 내려도 남는다(`RenderThreadSubsystem::findRenderThread`) — 이 객체를 가리키는 훅을 떼고 사라진다.
        RenderThread* const pRenderThread = _renderThreadSubsystem.findRenderThread();
        if ( _bBeginFrameHookBound == SW_TRUE && pRenderThread != nullptr )
            pRenderThread->setBeginFrameHook( {} );
        _bBeginFrameHookBound = SW_FALSE;
    }

    SubsystemHostMask RHIBackendSwitcher::getSubsystemHost() const
    {
        return EngineSubsystemHost::kClient;
    }

    span<const utf8* const> RHIBackendSwitcher::getSubsystemDependencies() const
    {
        // RHI · 렌더 스레드 서브시스템을 참조로 든다 — 먼저 서고 나중에 진다.
        static constexpr const utf8* kArrDependency[] = { RHISubsystem::kSubsystemName, RenderThreadSubsystem::kSubsystemName };
        return kArrDependency;
    }

    RHI* RHIBackendSwitcher::findRHI() const
    {
        return _rhiSubsystem.findRHI();
    }

    bool RHIBackendSwitcher::findCommittedBackend( RHIBackend& outBackend ) const
    {
        const RHI* pRHI = findRHI();
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
        RHI* pRHI = findRHI();
        return pRHI != nullptr && pRHI->hasDevice() && pRHI->getDevice().debugRemoveDevice();
    }

    bool RHIBackendSwitcher::isSwapPending() const
    {
        const RHI* pRHI = findRHI();
        return pRHI != nullptr && pRHI->hasPendingBackendChange();
    }

#if SW_WITH_DYNAMIC_MODULES
    bool RHIBackendSwitcher::requestSwap( RHIBackend backend )
    {
        GlobalVariableInfo* pBackendVariable = RHIBackendSwitcherInternal::findBackendVariable();
        if ( pBackendVariable == nullptr )
            return false;
        // 메뉴 · 콘솔이 쓰는 setValueAsInt 로 가야 변경 콜백(onBackendVariableChanged)이 받는다.
        pBackendVariable->setValueAsInt( static_cast<int32>( backend ) );
        return true;
    }
#endif

    void RHIBackendSwitcher::onBackendVariableChanged( GlobalVariableInfo* pInfo )
    {
        RHI* pRHI = findRHI();
        if ( pInfo == nullptr || pRHI == nullptr )
            return;

        const RHIBackend requestedBackend = static_cast<RHIBackend>( pInfo->getValueAsInt() );
#if SW_WITH_DYNAMIC_MODULES
        pRHI->schedulePendingBackendChange( requestedBackend );
#else
        // 정적 모듈 구성은 백엔드 하나를 Engine 에 링크했다 — 다른 백엔드로 갈 길이 없다. 값을 되돌리고 알린다(콜백 없이 써서 이 콜백이 다시 불리지 않는다).
        if ( requestedBackend != pRHI->getCommittedBackend() )
        {
            SW_LOG_ERROR_ID( "RHIBackendSwitcher.BackendSwapStaticModules", "gv_rhiBackend: this build links one RHI backend statically (SW_STATIC_MODULES) - it cannot switch to %#; restart a desktop Dev build to change backends",
                             RHI::getBackendTypeName( requestedBackend ) );
            (void)pInfo->setValueAsIntWithoutNotify( static_cast<int32>( pRHI->getCommittedBackend() ) );
        }
#endif
    }

    SW_DEV_COMMAND( rhiRemoveDevice, "rhi.removeDevice", "rhi.removeDevice [driverWaitMs]",
                    "Remove the GPU device as a TDR would (DirectX 12; Vulkan marks it lost) to check that the engine then answers by _deviceLostPolicy (Quit: exit code DeviceLost; Recreate: a new device) without hanging; with a wait, the render thread first blocks that long where the fence wait is",
                    &RHIBackendSwitcherInternal::runRemoveDevice );
#if SW_WITH_DYNAMIC_MODULES
    SW_AUTOMATION_STEP( swapBackend, AutomationBackendSteps::kSwapBackendKind, &RHIBackendSwitcherInternal::runSwapBackend, &AutomationBackendSteps::validateSwapBackend, AutomationStepOptions{}.withPrepare( &RHIBackendSwitcherInternal::prepareSwapBackend ).withWallBudget( AutomationBackendSteps::kSwapBackendWallSeconds ) );
#endif
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

    bool RHIBackendSwitcher::removeDeviceAfterDriverWaitForTest( uint32 waitMilliseconds )
    {
        SW_ASSERT( 1u <= waitMilliseconds && waitMilliseconds <= kMaxDeviceLossDriverWaitMilliseconds );
        RenderThread* const pRenderThread = _renderThreadSubsystem.findRenderThread();
        if ( pRenderThread == nullptr )
            return false;
        // 훅은 처음 요청 때 건다 — 렌더 스레드 단계와 기동 순서를 맞출 의존을 두지 않는다(클래스 설명). 렌더 스레드 객체는 백엔드 교체를 지나 그대로라 다시 걸 일이 없다.
        if ( _bBeginFrameHookBound == SW_FALSE )
        {
            pRenderThread->setBeginFrameHook( SW_DELEGATE_METHOD( BeginFrameHookDelegate, &RHIBackendSwitcher::onBeginFrameOnRenderThread, this ) );
            _bBeginFrameHookBound = SW_TRUE;
        }
        _pendingDeviceLossWaitMilliseconds.store( waitMilliseconds, std::memory_order_relaxed );
        return true;
    }

    void RHIBackendSwitcher::onBeginFrameOnRenderThread( IRHIDevice& device )
    {
        const uint32 waitMilliseconds = _pendingDeviceLossWaitMilliseconds.exchange( 0u, std::memory_order_relaxed );
        if ( waitMilliseconds == 0u )
            return;
        SW_LOG_WARNING_ID( "RHIBackendSwitcher.HoldingRenderThreadMs", "Holding the render thread %# ms before removing the device (rhi.removeDevice <ms>) - the driver wait a real TDR spends before the first device-lost result",
                           waitMilliseconds );
        MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + static_cast<int64>( waitMilliseconds ) * constant::time::kNanosecondsPerMillisecond );
        if ( device.debugRemoveDevice() == false )
            SW_LOG_ERROR_ID( "RHIBackendSwitcher.RemoveDeviceMsBackend", "rhi.removeDevice <ms>: this backend cannot remove its device; only the wait ran" );
    }

    // 렌더러가 있는 클라이언트만 — 창 · RHI 를 두지 않는 기동(헤드리스 작업 · 단위 테스트)은 교체할 장치가 없다.
    SW_ENGINE_SUBSYSTEM( RHIBackendSwitcher, Startup, ( RHISubsystem, RenderThreadSubsystem ), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        if ( build._pStartupSetup->_bRenderer == false )
            return;
        (void)scope.emplaceSubsystem<RHIBackendSwitcher>( scope.get<RHISubsystem>(), scope.get<RenderThreadSubsystem>() );
    } );
} // namespace sw
