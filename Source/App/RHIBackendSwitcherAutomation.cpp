/**
 * @file RHIBackendSwitcherAutomation.cpp
 * @brief 시나리오 단계 `SwapBackend` 와 탐침 `RHI.*` — 한 프로세스에서 네 백엔드를 차례로 건너는 시나리오가 쓰는 창구입니다.
 * @details 단계는 메뉴 · 콘솔이 가는 길(`RHIBackendSwitcher::requestSwap` → `gv_rhiBackend` 변경 콜백 → 프레임 끝의 `applyIfPending`)로 교체를 **요청만** 한다.
 *          교체기는 App 이 호스트 범위에 걸어 두고 여기서는 `engine::findSubsystem<RHIBackendSwitcher>()` 로 엔진 범위에서 올라가 찾는다(없으면 이 프로세스에 RHI 가 없다).
 *          끝났는지는 시나리오가 `<WaitUntil probe="RHI.SwapPending" equals="0"/>` 로 기다리고, 어느 백엔드인지는 `RHI.Backend`, 실패했는지는 `RHI.SwapFailedCount` 로 본다.
 *          교체 뒤의 모듈과 씬은 `Module.BindingsOk` · `Module.GraphBroken` · `Scene.ObjectCount` 가 이미 있다. Shipping 에는 단계도 탐침도 없다.
 */
#include "pch.h"

#include "App/RHIBackendSwitcher.h"

#include "Core/Console/DevCommandsEnabled.h"

// 가드 기준은 `SW_DEV_COMMANDS_ENABLED` 한 가지다(`DevCommand` 단계와 같다) — 이 파일이 개발 명령 레지스트리를 쓰는 구성에서만 컴파일된다.
#if SW_DEV_COMMANDS_ENABLED

    #include "Core/Console/DevCommandRegistry.h"
    #include "Core/Container/StringUtil.h"

    #include "Engine/Probe/AutomationProbe.h"
    #include "Engine/Scenario/AutomationBackendSteps.h"
    #include "Engine/Scenario/AutomationScenario.h"
    #include "Engine/Scenario/AutomationStepContext.h"
    #include "Engine/Scenario/AutomationStepRegistry.h"
    #include "Engine/Common/EngineServices.h"
    #include "Engine/Config/RHIBackendType.h"
    #include "Engine/Graphics/RHI/RHI.h"
    #include "Engine/Renderer/RenderThread.h"

namespace sw
{
    SW_LOG_CALLER( "RHIBackendSwitcherAutomation" );

    namespace
    {
        struct RHIBackendSwitcherAutomationInternal
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
             * @details 대기를 주면 렌더 스레드가 다음 프레임의 펜스 대기 자리에서 그만큼 잠든 뒤 제거한다(`RenderThread::requestDeviceLossAfterDriverWait`) — 실제 TDR 이 첫 손실 결과를 돌려주기 전 드라이버 안에서 막히는 모양이다.
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
                    const bool bInRange = 1 <= waitMilliseconds && static_cast<uint32>( waitMilliseconds ) <= RenderThread::kMaxDeviceLossDriverWaitMilliseconds;
                    if ( bInRange == false )
                    {
                        outReply = "driver wait must be 1.." + to_string( RenderThread::kMaxDeviceLossDriverWaitMilliseconds ) + " ms";
                        return false;
                    }
                    if ( pSwitcher == nullptr || pSwitcher->removeDeviceAfterDriverWaitForTest( static_cast<uint32>( waitMilliseconds ) ) == false )
                    {
                        outReply = "no render thread to hold";
                        return false;
                    }
                    outReply = "next render frame waits " + to_string( waitMilliseconds ) + " ms where the fence wait is, then removes the device";
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

    SW_DEV_COMMAND( rhiRemoveDevice, "rhi.removeDevice", "rhi.removeDevice [driverWaitMs]",
                    "Remove the GPU device as a TDR would (DirectX 12; Vulkan marks it lost) to check that frames and shutdown do not hang; with a wait, the render thread first blocks that long where the fence wait is",
                    &RHIBackendSwitcherAutomationInternal::runRemoveDevice );
    SW_AUTOMATION_STEP_WITH_HOOKS( swapBackend, AutomationBackendSteps::kSwapBackendKind, &RHIBackendSwitcherAutomationInternal::runSwapBackend,
                                   &AutomationBackendSteps::validateSwapBackend, &RHIBackendSwitcherAutomationInternal::prepareSwapBackend, nullptr, false );
    SW_AUTOMATION_PROBE( rhiBackend, "RHI.Backend", "Backend the device runs on: 0 DirectX11, 1 DirectX12, 2 Vulkan, 3 OpenGL",
                         &RHIBackendSwitcherAutomationInternal::readCommittedBackend );
    SW_AUTOMATION_PROBE( rhiSwapPending, "RHI.SwapPending", "1 from a backend swap request (SwapBackend, gv_rhiBackend) until the end-of-frame swap has run",
                         &RHIBackendSwitcherAutomationInternal::readSwapPending );
    SW_AUTOMATION_PROBE( rhiSwapFailedCount, "RHI.SwapFailedCount", "Backend swaps that failed since the process started (each falls back to the previous backend)",
                         &RHIBackendSwitcherAutomationInternal::readFailedSwapCount );
} // namespace sw

#endif
