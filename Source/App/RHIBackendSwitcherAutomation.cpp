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

#if !defined( SW_SHIPPING )

    #include "Core/Console/DevCommandRegistry.h"
    #include "Core/Container/StringUtil.h"

    #include "Engine/Probe/AutomationProbe.h"
    #include "Engine/Automation/AutomationRunner.h"
    #include "Engine/Automation/AutomationStepRegistry.h"
    #include "Engine/Common/EngineServices.h"
    #include "Engine/Config/RHIBackendType.h"
    #include "Engine/Graphics/RHI/RHI.h"

namespace sw
{
    SW_LOG_CALLER( "RHIBackendSwitcherAutomation" );

    namespace
    {
        struct RHIBackendSwitcherAutomationInternal
        {
            /** @brief 명령줄 철자(`dx11`, `dx12`, `vulkan`, `opengl`)와 열거자 이름을 대소문자 없이 받습니다. */
            [[nodiscard]] static bool tryParseBackend( string_view text, RHIBackend& outBackend )
            {
                struct NameRow
                {
                    const utf8* _pName;
                    RHIBackend  _backend;
                };
                static constexpr NameRow kArrName[] = {
                    {     "dx11", RHIBackend::DirectX11},
                    {"directx11", RHIBackend::DirectX11},
                    {     "dx12", RHIBackend::DirectX12},
                    {"directx12", RHIBackend::DirectX12},
                    {   "vulkan",    RHIBackend::Vulkan},
                    {       "vk",    RHIBackend::Vulkan},
                    {   "opengl",    RHIBackend::OpenGL},
                    {       "gl",    RHIBackend::OpenGL},
                };
                for ( const NameRow& row : kArrName )
                {
                    if ( StringUtil::equals( text, row._pName, true ) )
                    {
                        outBackend = row._backend;
                        return true;
                    }
                }
                return false;
            }

            static bool validateSwapBackend( const AutomationStep& step, string& outError )
            {
                for ( const AutomationAttribute& attribute : step._listAttribute )
                {
                    if ( string_view{ attribute._name } != "backend" )
                    {
                        outError = step.describe() + ": unknown attribute '" + attribute._name + "'";
                        return false;
                    }
                }
                const string* pBackend = step.findAttribute( "backend" );
                RHIBackend    backend{};
                const bool    bRelative = pBackend != nullptr && ( *pBackend == kOtherBackend || *pBackend == kStartBackend );
                if ( pBackend == nullptr || ( bRelative == false && tryParseBackend( *pBackend, backend ) == false ) )
                {
                    outError = step.describe() + ": needs backend=\"dx11|dx12|vulkan|opengl|other|start\"";
                    return false;
                }
                return true;
            }

            /** @brief `backend="other"` — 지금과 다른 백엔드(DirectX 12 가 아니면 DirectX 12, DirectX 12 면 DirectX 11)입니다. Windows 에서 늘 있는 둘이라 어느 백엔드로 시작해도 건넌다. */
            static constexpr const utf8* kOtherBackend = "other";
            /** @brief `backend="start"` — 시나리오가 시작할 때의 백엔드입니다. `other` 로 건넌 뒤 돌아와 체인의 다음 시나리오가 같은 백엔드에서 돌게 한다. */
            static constexpr const utf8* kStartBackend = "start";

            /** @brief 시나리오가 시작할 때의 백엔드입니다(준비 함수가 적는다). App 모듈은 백엔드 교체 · 모듈 리로드에서 내려가지 않는다. */
            static RHIBackend& getStartBackend()
            {
                static RHIBackend s_backend{ RHIBackend::DirectX12 };
                return s_backend;
            }

            static bool prepareSwapBackend( AutomationRunner& /*runner*/, const AutomationStep& /*step*/, string& outError )
            {
                const RHIBackendSwitcher* pSwitcher = engine::findSubsystem<RHIBackendSwitcher>();
                if ( pSwitcher == nullptr || pSwitcher->findCommittedBackend( getStartBackend() ) == false )
                {
                    outError = "SwapBackend: backend switching is not available in this process (no RHI)";
                    return false;
                }
                return true;
            }

            /** @brief 교체를 요청합니다. 이미 그 백엔드이고 대기 중인 교체도 없으면 아무것도 하지 않습니다. 끝은 `RHI.SwapPending` 이 0 이 되는 것으로 봅니다. */
            static bool runSwapBackend( AutomationRunner& runner, const AutomationStep& step )
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
                if ( requested == kOtherBackend )
                    backend = committed == RHIBackend::DirectX12 ? RHIBackend::DirectX11 : RHIBackend::DirectX12;
                else if ( requested == kStartBackend )
                    backend = getStartBackend();
                else
                    (void)tryParseBackend( requested, backend ); // 검사에서 확인했다
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

            /** @brief 개발 명령 `rhi.removeDevice` — 장치 제거(TDR)를 흉내 낸다. 제거 뒤 프레임 · 종료가 멈추지 않는지 보는 데 쓴다. */
            static bool runRemoveDevice( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                RHIBackendSwitcher* pSwitcher = engine::findSubsystem<RHIBackendSwitcher>();
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

    SW_DEV_COMMAND( rhiRemoveDevice, "rhi.removeDevice", "rhi.removeDevice", "Remove the GPU device as a TDR would (DirectX 12; Vulkan marks it lost) to check that frames and shutdown do not hang",
                    &RHIBackendSwitcherAutomationInternal::runRemoveDevice );
    SW_AUTOMATION_STEP_WITH_HOOKS( swapBackend, "SwapBackend", &RHIBackendSwitcherAutomationInternal::runSwapBackend,
                                   &RHIBackendSwitcherAutomationInternal::validateSwapBackend, &RHIBackendSwitcherAutomationInternal::prepareSwapBackend, nullptr, false );
    SW_AUTOMATION_PROBE( rhiBackend, "RHI.Backend", "Backend the device runs on: 0 DirectX11, 1 DirectX12, 2 Vulkan, 3 OpenGL",
                         &RHIBackendSwitcherAutomationInternal::readCommittedBackend );
    SW_AUTOMATION_PROBE( rhiSwapPending, "RHI.SwapPending", "1 from a backend swap request (SwapBackend, gv_rhiBackend) until the end-of-frame swap has run",
                         &RHIBackendSwitcherAutomationInternal::readSwapPending );
    SW_AUTOMATION_PROBE( rhiSwapFailedCount, "RHI.SwapFailedCount", "Backend swaps that failed since the process started (each falls back to the previous backend)",
                         &RHIBackendSwitcherAutomationInternal::readFailedSwapCount );
} // namespace sw

#endif
