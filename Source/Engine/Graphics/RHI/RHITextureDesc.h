/**
 * @file RHITextureDesc.h
 * @brief 텍스처 서술체와 업로드 서술체, 업로드 검사 · 깊이 되읽기 배치입니다.
 */
#pragma once
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHIEnums.h"

namespace sw
{
    /** @brief 큐브 텍스처의 면 수입니다. */
    inline constexpr uint32 kCubeFaceCount = 6;

    /**
     * @struct RHITextureDesc
     * @brief 텍스처를 만들 때 쓰는 서술체입니다.
     */
    struct SW_API RHITextureDesc
    {
        float4 _clearColor; ///< 초기화 색상 (16 bytes)
        uint32 _width;      ///< 너비 (4 bytes)
        uint32 _height;     ///< 높이 (4 bytes)
        uint32 _arraySize;  ///< 면 수 (4 bytes). Texture2D 는 1, TextureCube 는 6(`kCubeFaceCount`), Texture2DArray 는 1 이상
        /**
         * @brief 밉 레벨 수 (4 bytes)입니다. 1 부터 전체 밉 체인까지입니다(`isRHITextureShapeValid` — 0 은 거절한다).
         * @details 아래 `RHITextureUploadDesc::_mipLevels` 는 0 을 "텍스처가 가진 밉 전부" 로 정의하지만 이쪽에는 그런 약속이 없다.
         */
        uint32                 _mipLevels;
        RHIFormat              _format;                 ///< 텍스처 포맷 (4 bytes)
        float32                _clearDepth;             ///< 초기화 깊이 (4 bytes)
        uint8                  _clearStencil;           ///< 초기화 스텐실 (1 byte)
        uint8                  _bIsRenderTarget    : 1; ///< 렌더 타깃 지원 여부 (1 byte)
        uint8                  _bIsDepthStencil    : 1; ///< 깊이/스텐실 지원 여부
        uint8                  _bIsShaderResource  : 1; ///< 셰이더 리소스 지원 여부
        uint8                  _bIsUnorderedAccess : 1; ///< UAV 지원 여부
        uint8                  _bIsTransient       : 1; ///< 트랜지언트 첨부 풀이 만든 것이다. 만드는 방법은 같고 GPU 메모리 추적기의 항목만 구분한다(`RHIMemoryTracker::classifyTexture`).
        [[maybe_unused]] uint8 _reservedFlags      : 3;
        RHITextureDimension    _dimension;      ///< 형태 (1 byte)
        uint8                  _arrReserved[1]; ///< 4바이트 정렬 패딩 (1 byte)

        /** @brief 기본 크기, 포맷, 클리어 값으로 만듭니다(Texture2D, 면 하나). */
        RHITextureDesc() noexcept;
    };

    /**
     * @brief 서술체의 크기 · 밉 수 · 면 수가 맞는지 확인합니다. 크기는 1 이상, 밉은 1 부터 전체 체인까지, Texture2D 는 면 1, TextureCube 는 6 이면서 정사각형, Texture2DArray 는 1 이상이어야 합니다.
     * @details 생성의 공통 입구(`IRHIResourceFactory::createTexture2D`)가 이 판정 하나로 거부한다. 맞지 않는 값을 그대로 두면 백엔드마다 다르게 깨진다(밉 0 을 D3D11 은 전체 체인, GL 은 1 로 읽었다).
     */
    inline constexpr bool isRHITextureShapeValid( const RHITextureDesc& desc )
    {
        uint32 fullMipLevels = 0;
        for ( uint32 extent = desc._width > desc._height ? desc._width : desc._height; extent != 0; extent >>= 1 )
        {
            ++fullMipLevels;
        }
        const bool bSized = desc._width != 0 && desc._height != 0 && desc._mipLevels != 0 && desc._mipLevels <= fullMipLevels;
        if ( bSized == false )
            return false;
        switch ( desc._dimension )
        {
            case RHITextureDimension::Texture2D:
                return desc._arraySize == 1;
            case RHITextureDimension::Texture2DArray:
                return desc._arraySize >= 1;
            case RHITextureDimension::TextureCube:
                return desc._arraySize == kCubeFaceCount && desc._width == desc._height;
        }
        return false;
    }

    /**
     * @struct RHITextureUploadDesc
     * @brief IRHIResourceFactory::submitTextureUpload 의 입력입니다. 밉 0 부터 차례로, 각 밉의 행이 빈틈없이 이어진 바이트 블록입니다.
     * @details DDS 파일의 픽셀 배치와 같다(DDSImageData::_bytes 를 그대로 넘길 수 있다). 행 패딩은
     *          백엔드가 필요하면 스스로 맞춘다(DX12 는 256 정렬 풋프린트로 다시 배치하고, GL 은 UNPACK_ALIGNMENT 1 을 쓴다).
     */
    struct SW_API RHITextureUploadDesc
    {
        const void* _pData;      ///< 밉 0 첫 행부터 (8 bytes)
        uint32      _sizeBytes;  ///< _pData 전체 길이 (4 bytes)
        uint32      _mipLevels;  ///< 업로드할 밉 수. 0 이면 텍스처가 가진 밉 전부 (4 bytes)
        uint32      _arraySlice; ///< 업로드할 면(배열 원소, 큐브 면). 한 번에 면 하나다. (4 bytes)

        /** @brief 빈 업로드(데이터 없음, 밉 전부)로 만듭니다. */
        RHITextureUploadDesc() noexcept;
    };

    /**
     * @struct RHITextureRegionUploadDesc
     * @brief `IRHIResourceFactory::submitTextureRegionUpload` 의 입력입니다. 밉 하나와 면 하나 안의 사각형 구간이며, 행은 빈틈없이(`_width × 픽셀 바이트`) 이어집니다.
     */
    struct RHITextureRegionUploadDesc
    {
        const void* _pData{ nullptr }; ///< 구간 첫 행부터
        uint32      _sizeBytes{ 0 };   ///< _pData 전체 길이. 행 바이트 × _height 이상이어야 한다.
        uint32      _x{ 0 };           ///< 밉 안의 왼쪽 위 X (px)
        uint32      _y{ 0 };           ///< 밉 안의 왼쪽 위 Y (px)
        uint32      _width{ 0 };       ///< 구간 너비 (px)
        uint32      _height{ 0 };      ///< 구간 높이 (px)
        uint32      _mip{ 0 };         ///< 업로드할 밉
        uint32      _arraySlice{ 0 };  ///< 업로드할 면(배열 원소, 큐브 면)
    };

    /**
     * @brief 영역 업로드를 검사하고 행 바이트를 구합니다. 네 백엔드가 같은 규칙을 쓰는 유일한 지점입니다.
     * @param mipLevels 텍스처가 가진 밉 수와 면 수(`arraySize`)입니다.
     * @return 압축, 깊이, Unknown 포맷이거나, 범위 밖의 밉, 면, 사각형이거나, 빈 구간이거나, 데이터가 모자라면 false 입니다(이유는 로그에 남깁니다).
     */
    [[nodiscard]] SW_API bool validateTextureRegionUpload( RHIFormat format, uint32 textureWidth, uint32 textureHeight, uint32 mipLevels, uint32 arraySize,
                                                           const RHITextureRegionUploadDesc& desc, uint32& outRowBytes );

    /**
     * @struct RHITextureMipSpan
     * @brief resolveTextureUploadMips 가 풀어낸 밉 하나의 위치와 크기입니다.
     */
    struct RHITextureMipSpan
    {
        const uint8* _pData{ nullptr }; ///< 이 밉의 첫 바이트
        uint32       _offsetBytes{ 0 }; ///< RHITextureUploadDesc::_pData 기준 오프셋
        uint32       _sizeBytes{ 0 };   ///< _rowBytes * _height
        uint32       _rowBytes{ 0 };    ///< 빈틈없는 한 행의 바이트
        uint32       _width{ 0 };
        uint32       _height{ 0 };
        uint32       _mip{ 0 };
    };

    /**
     * @brief 깊이 텍스처를 `submitTextureReadback` 으로 읽을 때 채우는 배치의 포맷입니다. 텍셀마다 0..1 깊이 float32 하나이고 스텐실은 버립니다.
     * @details 네 백엔드가 같은 배치를 내야 읽는 쪽(에디터 렌더 타깃 창)이 백엔드를 묻지 않는다. Vulkan 이 D32 계열 깊이를 골라도 같다.
     */
    inline constexpr RHIFormat kRHIDepthReadbackFormat = RHIFormat::R32_FLOAT;

    /**
     * @brief D24 깊이 텍셀(32비트, 아래 24비트가 깊이) @p count 개를 제자리에서 0..1 float32 로 바꿉니다. 위 8비트(스텐실 또는 미정)는 버립니다.
     * @details D3D11 과 D3D12 의 깊이 평면 복사, Vulkan `D24_UNORM_S8_UINT` 의 깊이 aspect 복사가 이 배치를 낸다.
     */
    inline void convertRHIDepth24ToFloat( uint8* pTexel, uint32 count )
    {
        constexpr uint32  kDepth24Mask = 0x00FFFFFFu;
        constexpr float32 kDepth24Max  = static_cast<float32>( kDepth24Mask );
        for ( uint32 texelIndex = 0; texelIndex < count; ++texelIndex )
        {
            uint32 packed = 0;
            Memory::copy( &packed, pTexel + static_cast<size_t>( texelIndex ) * 4u, sizeof( packed ) );
            const float32 depth = static_cast<float32>( packed & kDepth24Mask ) / kDepth24Max;
            Memory::copy( pTexel + static_cast<size_t>( texelIndex ) * 4u, &depth, sizeof( depth ) );
        }
    }
} // namespace sw
