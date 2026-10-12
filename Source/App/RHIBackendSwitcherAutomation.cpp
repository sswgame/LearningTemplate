/**
 * @file RHIBackendSwitcherAutomation.cpp
 * @brief 시나리오 단계 `SwapBackend` 와 탐침 `RHI.*` — 한 프로세스에서 네 백엔드를 차례로 건너는 시나리오가 쓰는 창구입니다.
 * @details 단계는 `-gv_rhiSwapAtFrame` 과 메뉴 · 콘솔이 가는 길(`RHIBackendSwitcher::requestSwap` → `gv_rhiBackend` 변경 콜백 → 프레임 끝의 `applyIfPending`)로 교체를 **요청만** 한다.
 *          끝났는지는 시나리오가 `<WaitUntil probe="RHI.SwapPending" equals="0"/>` 로 기다리고, 어느 백엔드인지는 `RHI.Backend`, 실패했는지는 `RHI.SwapFailedCount` 로 본다.
 *          교체 뒤의 모듈과 씬은 `Module.BindingsOk` · `Module.GraphBroken` · `Scene.ObjectCount` 가 이미 있다. Shipping 에는 단계도 탐침도 없다.
 */
#include "pch.h"

#include "App/RHIBackendSwitcher.h"

#if !defined( SW_SHIPPING )

    #include "Core/Container/StringUtil.h"

    #include "Engine/Automation/AutomationProbe.h"
    #include "Engine/Automation/AutomationRunner.h"
    #include "Engine/Automation/AutomationStepRegistry.h"
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
                if ( pBackend == nullptr || tryParseBackend( *pBackend, backend ) == false )
                {
                    outError = step.describe() + ": needs backend=\"dx11|dx12|vulkan|opengl\"";
                    return false;
                }
                return true;
            }

            /** @brief 교체를 요청합니다. 이미 그 백엔드이고 대기 중인 교체도 없으면 아무것도 하지 않습니다. 끝은 `RHI.SwapPending` 이 0 이 되는 것으로 봅니다. */
            static bool runSwapBackend( AutomationRunner& runner, const AutomationStep& step )
            {
                RHIBackend backend{};
                (void)tryParseBackend( *step.findAttribute( "backend" ), backend ); // 검사에서 확인했다
                RHIBackendSwitcher* pSwitcher = RHIBackendSwitcher::findActive();
                RHIBackend          committed{};
                if ( pSwitcher == nullptr || pSwitcher->findCommittedBackend( committed ) == false )
                {
                    runner.recordFailure( step, "backend switching is not available in this process (no RHI)" );
                    return true;
                }
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

            [[nodiscard]] static bool readCommittedBackend( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const RHIBackendSwitcher* pSwitcher = RHIBackendSwitcher::findActive();
                RHIBackend                backend{};
                if ( pSwitcher == nullptr || pSwitcher->findCommittedBackend( backend ) == false )
                    return false;
                outValue = static_cast<float64>( static_cast<uint32>( backend ) );
                return true;
            }

            [[nodiscard]] static bool readSwapPending( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const RHIBackendSwitcher* pSwitcher = RHIBackendSwitcher::findActive();
                if ( pSwitcher == nullptr )
                    return false;
                outValue = pSwitcher->isSwapPending() ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readFailedSwapCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const RHIBackendSwitcher* pSwitcher = RHIBackendSwitcher::findActive();
                if ( pSwitcher == nullptr )
                    return false;
                outValue = static_cast<float64>( pSwitcher->getFailedSwapCount() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_STEP( swapBackend, "SwapBackend", &RHIBackendSwitcherAutomationInternal::runSwapBackend, &RHIBackendSwitcherAutomationInternal::validateSwapBackend, false );
    SW_AUTOMATION_PROBE( rhiBackend, "RHI.Backend", "Backend the device runs on: 0 DirectX11, 1 DirectX12, 2 Vulkan, 3 OpenGL",
                         &RHIBackendSwitcherAutomationInternal::readCommittedBackend );
    SW_AUTOMATION_PROBE( rhiSwapPending, "RHI.SwapPending", "1 from a backend swap request (SwapBackend, gv_rhiBackend) until the end-of-frame swap has run",
                         &RHIBackendSwitcherAutomationInternal::readSwapPending );
    SW_AUTOMATION_PROBE( rhiSwapFailedCount, "RHI.SwapFailedCount", "Backend swaps that failed since the process started (each falls back to the previous backend)",
                         &RHIBackendSwitcherAutomationInternal::readFailedSwapCount );
} // namespace sw

#endif
