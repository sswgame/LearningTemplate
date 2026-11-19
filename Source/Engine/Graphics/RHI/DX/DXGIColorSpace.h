/**
 * @file DXGIColorSpace.h
 * @brief DXGI 스왑체인의 HDR 출력입니다(DX11, DX12 공유) — 모니터가 받는 색 공간 묻기, 색 공간 · HDR10 마스터링 값 알리기.
 * @details 묻기는 창이 놓인 출력(`IDXGISwapChain::GetContainingOutput`)의 `IDXGIOutput6::GetDesc1` 이다. 그 색 공간이 `G2084_NONE_P2020` 이면 OS 의 HDR 이 켜진 모니터다 —
 *          그때만 HDR10 · scRGB 를 받는다(`CheckColorSpaceSupport` 가 PRESENT 를 함께 답해야 한다). Microsoft D3D12HDR 예제 · 언리얼 `FD3D12Viewport::CheckHDRSupport` 와 같은 순서다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"

#include "Engine/Foundation/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"

#if defined( SW_PLATFORM_WINDOWS )

    #include <dxgi1_6.h>

namespace sw
{
    /** @brief 모니터가 받는 출력(`RHICapabilities::_displayOutputSupport` 비트)과 알린 최대 휘도(nits, 모르면 0)입니다. */
    struct DXGIDisplayOutputSupport
    {
        uint8   _mask{ makeColorSpaceBit( RHIColorSpace::SRGBNonLinear ) };
        float32 _maxLuminanceNits{ 0.0f };
    };

    /** @brief 색 공간의 DXGI 값입니다. */
    constexpr DXGI_COLOR_SPACE_TYPE toDXGIColorSpace( RHIColorSpace colorSpace )
    {
        switch ( colorSpace )
        {
            case RHIColorSpace::SRGBNonLinear:
                return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
            case RHIColorSpace::HDR10ST2084:
                return DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
            case RHIColorSpace::ExtendedSRGBLinear:
                return DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
        }
        return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    }

    /**
     * @brief @p pSwapChain 이 놓인 모니터가 받는 출력을 묻습니다. 묻지 못하면(출력 없음 · DXGI 1.6 없음 · WARP) SDR 만입니다 — 능력 표가 정한 길이지 실패가 아니다.
     * @note shortcut: 창을 다른 모니터로 옮기기만 하면(크기 그대로) 다시 묻지 않는다 — 스왑체인을 만들거나 크기 · 색 공간을 바꿀 때만 묻는다. 모니터마다 HDR 이 다른 기계가 보이면 창 이동 통보에서 다시 묻는다.
     */
    inline DXGIDisplayOutputSupport queryDXGIDisplayOutputSupport( IDXGISwapChain* pSwapChain )
    {
        DXGIDisplayOutputSupport                support{};
        Microsoft::WRL::ComPtr<IDXGIOutput>     output;
        Microsoft::WRL::ComPtr<IDXGIOutput6>    output6;
        Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain3;
        DXGI_OUTPUT_DESC1                       outputDesc{};
        const bool                              bQueried = pSwapChain != nullptr && SUCCEEDED( pSwapChain->GetContainingOutput( output.GetAddressOf() ) ) &&
                              SUCCEEDED( output.As( &output6 ) ) && SUCCEEDED( output6->GetDesc1( &outputDesc ) ) &&
                              SUCCEEDED( pSwapChain->QueryInterface( IID_PPV_ARGS( swapChain3.GetAddressOf() ) ) );
        if ( bQueried == false )
            return support;
        support._maxLuminanceNits = outputDesc.MaxLuminance;
        // OS 의 HDR 이 꺼진 모니터(출력 색 공간이 G22)는 HDR 스왑체인을 받아도 SDR 로 눌러 보인다 — 받지 않는 것으로 친다.
        if ( outputDesc.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 )
            return support;
        for ( const RHIColorSpace colorSpace : { RHIColorSpace::HDR10ST2084, RHIColorSpace::ExtendedSRGBLinear } )
        {
            UINT flags{ 0 };
            if ( SUCCEEDED( swapChain3->CheckColorSpaceSupport( toDXGIColorSpace( colorSpace ), &flags ) ) && ( flags & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT ) != 0 )
                support._mask = static_cast<uint8>( support._mask | makeColorSpaceBit( colorSpace ) );
        }
        return support;
    }

    /**
     * @brief @p pSwapChain(이미 그 색 공간의 포맷으로 `ResizeBuffers` 했다)에 색 공간을 알리고, HDR10 이면 마스터링 값을, 아니면 메타데이터 없음을 알립니다. 실패하면 오류 로그와 false 입니다.
     * @details 원색은 BT.2020 · 흰점 D65 이고 단위는 DXGI 규약이다(색도 × 50000, 마스터링 휘도 × 10000) — D3D12HDR 예제와 같다.
     */
    [[nodiscard]] inline bool applyDXGIColorSpace( IDXGISwapChain* pSwapChain, RHIColorSpace colorSpace, const RHIDisplayMetadata& metadata )
    {
        Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain4;
        if ( pSwapChain == nullptr || FAILED( pSwapChain->QueryInterface( IID_PPV_ARGS( swapChain4.GetAddressOf() ) ) ) )
        {
            SW_LOG_ERROR_ID( "DXGI.SwapChain4Missing", "IDXGISwapChain4 is not available - the color space cannot be set (DXGI 1.5+ is needed for HDR output)" );
            return false;
        }
        const HRESULT colorSpaceHr = swapChain4->SetColorSpace1( toDXGIColorSpace( colorSpace ) );
        if ( FAILED( colorSpaceHr ) )
        {
            SW_LOG_ERROR_ID( "DXGI.SetColorSpaceFailed", "IDXGISwapChain3::SetColorSpace1 failed (HRESULT 0x%#)", static_cast<uint32>( colorSpaceHr ) );
            return false;
        }
        if ( colorSpace != RHIColorSpace::HDR10ST2084 )
            return SUCCEEDED( swapChain4->SetHDRMetaData( DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr ) );

        constexpr float32       kPrimaryScale   = 50000.0f;
        constexpr float32       kLuminanceScale = 10000.0f;
        DXGI_HDR_METADATA_HDR10 hdr10{};
        hdr10.RedPrimary[0]             = static_cast<UINT16>( RHIDisplayMetadata::kRedPrimaryX * kPrimaryScale );
        hdr10.RedPrimary[1]             = static_cast<UINT16>( RHIDisplayMetadata::kRedPrimaryY * kPrimaryScale );
        hdr10.GreenPrimary[0]           = static_cast<UINT16>( RHIDisplayMetadata::kGreenPrimaryX * kPrimaryScale );
        hdr10.GreenPrimary[1]           = static_cast<UINT16>( RHIDisplayMetadata::kGreenPrimaryY * kPrimaryScale );
        hdr10.BluePrimary[0]            = static_cast<UINT16>( RHIDisplayMetadata::kBluePrimaryX * kPrimaryScale );
        hdr10.BluePrimary[1]            = static_cast<UINT16>( RHIDisplayMetadata::kBluePrimaryY * kPrimaryScale );
        hdr10.WhitePoint[0]             = static_cast<UINT16>( RHIDisplayMetadata::kWhitePointX * kPrimaryScale );
        hdr10.WhitePoint[1]             = static_cast<UINT16>( RHIDisplayMetadata::kWhitePointY * kPrimaryScale );
        hdr10.MaxMasteringLuminance     = static_cast<UINT>( metadata._maxLuminanceNits * kLuminanceScale );
        hdr10.MinMasteringLuminance     = static_cast<UINT>( metadata._minLuminanceNits * kLuminanceScale );
        hdr10.MaxContentLightLevel      = static_cast<UINT16>( metadata._maxLuminanceNits );
        hdr10.MaxFrameAverageLightLevel = static_cast<UINT16>( metadata._maxFrameAverageNits );
        const HRESULT metadataHr        = swapChain4->SetHDRMetaData( DXGI_HDR_METADATA_TYPE_HDR10, sizeof( hdr10 ), &hdr10 );
        if ( FAILED( metadataHr ) )
        {
            SW_LOG_ERROR_ID( "DXGI.SetHDRMetaDataFailed", "IDXGISwapChain4::SetHDRMetaData failed (HRESULT 0x%#)", static_cast<uint32>( metadataHr ) );
            return false;
        }
        return true;
    }
} // namespace sw
#endif
