/**
 * @file RHIDxgiTearing.h
 * @brief DXGI 티어링 허용 여부 질의입니다(DX11, DX12 공유).
 *
 * [왜 필요한가]
 * `Present( 0, 0 )` 만으로는 VSync 가 꺼지지 않습니다. 플립 모델 스왑체인이라도 창이 DWM 합성을 거치는
 * 동안에는 런타임이 vblank 에 맞춰 프레임을 넘겨 주고, 그래서 동기화 간격 0 으로도 화면 주사율에
 * 정확히 붙습니다(165Hz 면 6061us). 주의: 창이 포그라운드인지에 따라 붙기도 하고 안 붙기도 해서
 * 프레임 시간 측정이 들쭉날쭉해집니다.
 *
 * 실제로 끄려면 두 가지가 같이 필요합니다:
 *   1) 스왑체인을 `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` 으로 만들고(ResizeBuffers 에도 같은 플래그),
 *   2) `Present( 0, DXGI_PRESENT_ALLOW_TEARING )` 으로 표시합니다.
 * 둘 중 하나만 있으면 DXGI 가 `DXGI_ERROR_INVALID_CALL` 을 반환합니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/EnginePlatformHeaders.h"

#if defined( SW_PLATFORM_WINDOWS )

    #include <dxgi1_5.h>

namespace sw
{
    /**
     * @brief 이 시스템이 티어링 허용 표시를 지원하는지 묻습니다.
     * @details `IDXGIFactory5` 가 없거나(구형 런타임) 질의가 실패하면 false 입니다. 그때는 VSync 를 끄라는
     *          요청이 와도 스왑체인 플래그를 붙이지 않습니다. 지원하지 않는데 붙이면 생성 자체가 실패합니다.
     */
    inline bool queryDXGIAllowTearing()
    {
        Microsoft::WRL::ComPtr<IDXGIFactory5> factory;
        if ( FAILED( CreateDXGIFactory1( IID_PPV_ARGS( factory.GetAddressOf() ) ) ) || factory == nullptr )
            return false;

        BOOL bAllowTearing{ FALSE };
        if ( FAILED( factory->CheckFeatureSupport( DXGI_FEATURE_PRESENT_ALLOW_TEARING, &bAllowTearing, sizeof( bAllowTearing ) ) ) )
            return false;

        return bAllowTearing != FALSE;
    }

    /**
     * @brief @p pSwapChain 을 만든 팩터리에 "@p hWnd 의 창 메시지를 보지 말라" 고 알립니다(`DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER`). 실패하면 false 입니다.
     * @details 이 연결이 없으면 DXGI 가 Alt+Enter 를 가로채 배타 전체 화면으로 바꾸고, 그 상태의 티어링 Present 는 `DXGI_ERROR_INVALID_CALL` 이다(R-1).
     *          전체 화면은 엔진의 테두리 없는 창 하나다. 문서대로 스왑체인의 `GetParent` 로 팩터리를 찾는다(다른 팩터리에 걸면 효과가 없다).
     *          세션 0 은 `DXGI_ERROR_NOT_CURRENTLY_AVAILABLE` 인데 그때는 DXGI 가 창 메시지를 보지 않으므로 성공으로 친다. 언리얼 D3D12RHI 뷰포트와 같은 플래그다.
     */
    [[nodiscard]] inline bool disableDXGIWindowChanges( IDXGISwapChain* pSwapChain, HWND hWnd )
    {
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        if ( pSwapChain == nullptr || FAILED( pSwapChain->GetParent( IID_PPV_ARGS( factory.GetAddressOf() ) ) ) )
            return false;
        const HRESULT hr = factory->MakeWindowAssociation( hWnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER );
        return SUCCEEDED( hr ) || hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    }
} // namespace sw
#endif
