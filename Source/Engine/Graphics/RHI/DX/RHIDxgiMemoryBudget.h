/**
 * @file RHIDxgiMemoryBudget.h
 * @brief DXGI 어댑터의 GPU 메모리 사용량 · 예산 질의입니다(DX11 · DX12 공유).
 * @details `IDXGIAdapter3::QueryVideoMemoryInfo` 는 **이 프로세스**의 사용량과 OS 가 준 예산을 답합니다(Windows 10 이상). 로컬 세그먼트는 비디오 메모리,
 *          비로컬은 GPU 가 보는 시스템 메모리(업로드 힙)입니다. 사용량은 둘의 합이고 예산은 로컬 예산입니다 — 엔진 장부가 업로드 힙 버퍼도 세기 때문입니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/Support/RHIMemoryLedger.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    /**
     * @brief 이 LUID 의 DXGI 어댑터를 `IDXGIAdapter3` 으로 찾습니다(디바이스가 실제로 쓰는 어댑터 — 기본 어댑터를 가정하지 않는다).
     * @return 찾지 못하거나 런타임이 `IDXGIAdapter3`(DXGI 1.4)을 모르면 null.
     */
    inline Microsoft::WRL::ComPtr<IDXGIAdapter3> findDxgiAdapterByLuid( const LUID& luid )
    {
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        if ( FAILED( CreateDXGIFactory1( IID_PPV_ARGS( factory.GetAddressOf() ) ) ) || factory == nullptr )
            return nullptr;
        Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
        if ( FAILED( factory->EnumAdapterByLuid( luid, IID_PPV_ARGS( adapter.GetAddressOf() ) ) ) )
            return nullptr;
        return adapter;
    }

    /**
     * @brief 어댑터에게 이 프로세스의 GPU 메모리 사용량 · 예산을 묻습니다. 아무 스레드에서나 불러도 됩니다.
     * @return 로컬 세그먼트를 묻지 못하면 false 이고 `outBudget` 은 건드리지 않습니다.
     */
    inline bool queryDxgiMemoryBudget( IDXGIAdapter3* pAdapter, RHIMemoryBudget& outBudget )
    {
        if ( pAdapter == nullptr )
            return false;
        DXGI_QUERY_VIDEO_MEMORY_INFO localInfo{};
        if ( FAILED( pAdapter->QueryVideoMemoryInfo( 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &localInfo ) ) )
            return false;
        DXGI_QUERY_VIDEO_MEMORY_INFO nonLocalInfo{};
        const bool                   bNonLocal = SUCCEEDED( pAdapter->QueryVideoMemoryInfo( 0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocalInfo ) );

        outBudget._usageBytes      = localInfo.CurrentUsage + ( bNonLocal ? nonLocalInfo.CurrentUsage : 0 );
        outBudget._budgetBytes     = localInfo.Budget;
        outBudget._availableBytes  = localInfo.Budget > localInfo.CurrentUsage ? localInfo.Budget - localInfo.CurrentUsage : 0;
        outBudget._scope           = RHIMemoryScope::Process;
        outBudget._bUsageKnown     = SW_TRUE;
        outBudget._bBudgetKnown    = SW_TRUE;
        outBudget._bAvailableKnown = SW_TRUE;
        return true;
    }
} // namespace sw
#endif
