/**
 * @file RHIDxgiTearing.h
 * @brief DXGI 티어링 허용 여부 질의 (DX11/DX12 공유)
 *
 * [왜 필요한가]
 * `Present( 0, 0 )` 만으로는 VSync 가 꺼지지 않는다. 플립 모델 스왑체인이라도 창이 DWM 합성을 거치는
 * 동안에는 런타임이 vblank 에 맞춰 프레임을 넘겨 주고, 그래서 동기화 간격 0 으로도 화면 주사율에
 * **정확히** 붙는다(이 저장소에서도 165Hz = 6061us 로 붙어 있었고, 포그라운드 여부에 따라 붙었다
 * 안 붙었다 해서 재현이 들쭉날쭉했다).
 *
 * 실제로 끄려면 두 가지가 **같이** 필요하다:
 *   1) 스왑체인을 `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` 으로 만들고(ResizeBuffers 에도 같은 플래그),
 *   2) `Present( 0, DXGI_PRESENT_ALLOW_TEARING )` 으로 표시한다.
 * 둘 중 하나만 있으면 DXGI 가 `DXGI_ERROR_INVALID_CALL` 을 돌려준다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Common/EnginePlatformHeaders.h"

#if defined( SW_PLATFORM_WINDOWS )

    #include <dxgi1_5.h>

namespace sw
{
    /**
     * @brief 이 시스템이 티어링 허용 표시를 지원하는지 묻습니다.
     * @details `IDXGIFactory5` 가 없거나(구형 런타임) 질의가 실패하면 false 다 — 그때는 VSync 를 끄라는
     *          요청이 와도 스왑체인 플래그를 붙이지 않는다. 지원하지 않는데 붙이면 생성 자체가 실패한다.
     */
    inline bool queryDxgiAllowTearing()
    {
        Microsoft::WRL::ComPtr<IDXGIFactory5> factory;
        if ( FAILED( CreateDXGIFactory1( IID_PPV_ARGS( factory.GetAddressOf() ) ) ) || factory == nullptr )
            return false;

        BOOL bAllowTearing{ FALSE };
        if ( FAILED( factory->CheckFeatureSupport( DXGI_FEATURE_PRESENT_ALLOW_TEARING, &bAllowTearing, sizeof( bAllowTearing ) ) ) )
            return false;

        return bAllowTearing != FALSE;
    }
} // namespace sw
#endif
