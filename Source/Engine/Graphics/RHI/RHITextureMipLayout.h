/**
 * @file RHITextureMipLayout.h
 * @brief 텍스처 밉 배치 계산입니다: 밉 하나의 크기 · 행 바이트(`computeRHITextureMipLayout`)와 업로드 서술체를 밉 배열로 풀기(`resolveTextureUploadMips`).
 * @details 포맷 블록 표(`RHIFormatInfo.h`)를 읽으므로 서술체 묶음(`RHIDescriptors.h`)에서 떼었다 — 업로드 · 되읽기를 하는 곳만 포함한다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/RHIEnums.h"
#include "Engine/Graphics/RHI/RHIFormatInfo.h"

namespace sw
{
    /**
     * @brief 텍스처 밉 하나의 크기와 행 바이트를 계산합니다. 업로드(빈틈없는 행)와 읽기(readback)가 같은 배치를 씁니다.
     * @details BC 는 행 하나가 블록 한 줄(ceil(w/4) 블록)이고, 밉 크기가 4 미만이어도 블록 하나를 차지한다.
     * @return 포맷이 대상이 아니거나 크기가 0 이면 false 입니다.
     */
    inline bool computeRHITextureMipLayout( RHIFormat format, uint32 width, uint32 height, uint32 mip, RHITextureMipSpan& outSpan )
    {
        const RHIFormatBlockInfo info                = RHIFormatUtil::getBlockInfo( format );
        const bool               bInvalidBlockLayout = info._blockBytes == 0 || width == 0 || height == 0;
        if ( bInvalidBlockLayout )
            return false;
        const uint32 mipWidth  = ( width >> mip ) > 0 ? ( width >> mip ) : 1u;
        const uint32 mipHeight = ( height >> mip ) > 0 ? ( height >> mip ) : 1u;
        const uint32 blocksX   = ( mipWidth + info._blockWidth - 1 ) / info._blockWidth;
        const uint32 blocksY   = ( mipHeight + info._blockHeight - 1 ) / info._blockHeight;
        outSpan._pData         = nullptr;
        outSpan._offsetBytes   = 0;
        outSpan._rowBytes      = blocksX * info._blockBytes;
        outSpan._sizeBytes     = outSpan._rowBytes * blocksY;
        outSpan._width         = mipWidth;
        outSpan._height        = mipHeight;
        outSpan._mip           = mip;
        return true;
    }

    /**
     * @brief 업로드 서술체를 밉 배열로 풀어냅니다. 네 백엔드가 같은 규칙으로 밉 크기와 오프셋을 계산해야 하므로 여기 한 곳에 둡니다.
     * @return 채운 밉 수입니다. 포맷이 업로드할 수 없거나, 요청한 밉이 텍스처의 밉보다 많거나, 데이터가 모자라면 0 입니다.
     */
    inline uint32 resolveTextureUploadMips( const RHITextureUploadDesc& desc, RHIFormat format, uint32 width, uint32 height,
                                            uint32 textureMipCount, RHITextureMipSpan* pOutSpan, uint32 outCapacity )
    {
        const bool bNoUploadData = desc._pData == nullptr || desc._sizeBytes == 0 || pOutSpan == nullptr;
        if ( bNoUploadData )
            return 0;

        const uint32 mipCount         = ( desc._mipLevels == 0 ) ? textureMipCount : desc._mipLevels;
        const bool   bInvalidMipCount = mipCount == 0 || mipCount > textureMipCount || mipCount > outCapacity;
        if ( bInvalidMipCount )
            return 0;

        const uint8* pBase  = static_cast<const uint8*>( desc._pData );
        uint32       offset = 0;
        for ( uint32 mip = 0; mip < mipCount; ++mip )
        {
            RHITextureMipSpan& span = pOutSpan[mip];
            if ( computeRHITextureMipLayout( format, width, height, mip, span ) == false )
                return 0;
            if ( offset + span._sizeBytes > desc._sizeBytes )
                return 0;
            span._pData       = pBase + offset;
            span._offsetBytes = offset;
            offset += span._sizeBytes;
        }
        return mipCount;
    }
} // namespace sw
