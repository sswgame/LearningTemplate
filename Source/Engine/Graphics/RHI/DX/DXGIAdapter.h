/**
 * @file RHIDxgiAdapter.h
 * @brief 디바이스를 만들 DXGI 하드웨어 어댑터 고르기입니다(DX11, DX12 공유).
 *
 * [왜 필요한가]
 * 어댑터를 nullptr 로 넘기면 런타임이 열거 0 번(기본 어댑터)을 쓴다. 하이브리드 노트북에서 0 번은 흔히 내장 GPU 라
 * 외장 GPU 가 있어도 내장에서 돈다(RTX 3070 Ti 노트북에서 스키닝 컴퓨트가 내장 Radeon 에 걸렸다). 언리얼 · 유니티처럼
 * 고성능 순서(`DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE`)의 첫 하드웨어 어댑터를 고른다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/EnginePlatformHeaders.h"

#if defined( SW_PLATFORM_WINDOWS )

    #include <dxgi1_6.h>

namespace sw
{
    /**
     * @brief OS 의 GPU 선호 순서(@p bMinimumPower 가 아니면 고성능 — 외장 GPU, 맞으면 저전력 — 내장 GPU)의 첫 하드웨어 어댑터입니다(소프트웨어 어댑터는 건너뛴다).
     * @details 제조사를 보지 않는다 — 외장이 Radeon 이든 GeForce 든 OS 가 매긴 순서를 따른다(사용자가 Windows 그래픽 설정으로 바꾼 선호도 이 순서에 든다).
     * @return `IDXGIFactory6` 가 없거나(Windows 10 1803 이전) 하드웨어 어댑터가 없으면 nullptr 입니다 — 부르는 쪽이 오류로 멈춘다(기본 어댑터로 대신하지 않는다).
     */
    inline Microsoft::WRL::ComPtr<IDXGIAdapter1> findDXGIPreferredAdapter( IDXGIFactory1* pFactory, bool bMinimumPower )
    {
        Microsoft::WRL::ComPtr<IDXGIFactory6> factory6;
        if ( pFactory == nullptr || FAILED( pFactory->QueryInterface( IID_PPV_ARGS( factory6.GetAddressOf() ) ) ) )
            return nullptr;
        for ( UINT index = 0;; ++index )
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            const DXGI_GPU_PREFERENCE             preference = bMinimumPower ? DXGI_GPU_PREFERENCE_MINIMUM_POWER : DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
            if ( FAILED( factory6->EnumAdapterByGpuPreference( index, preference, IID_PPV_ARGS( adapter.GetAddressOf() ) ) ) )
                return nullptr;
            DXGI_ADAPTER_DESC1 desc{};
            if ( SUCCEEDED( adapter->GetDesc1( &desc ) ) && ( desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE ) == 0 )
                return adapter;
        }
    }
} // namespace sw
#endif
