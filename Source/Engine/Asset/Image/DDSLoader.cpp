#include "pch.h"

#include "Engine/Asset/Image/DDSLoader.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/Image/DDSFormat.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    namespace
    {

        // DXT 계열 FourCC 코드
        constexpr uint32 kFourCC_DXT1 = FourCCUtil::make( "DXT1" );
        constexpr uint32 kFourCC_DXT2 = FourCCUtil::make( "DXT2" );
        constexpr uint32 kFourCC_DXT3 = FourCCUtil::make( "DXT3" );
        constexpr uint32 kFourCC_DXT4 = FourCCUtil::make( "DXT4" );
        constexpr uint32 kFourCC_DXT5 = FourCCUtil::make( "DXT5" );
        constexpr uint32 kFourCC_ATI1 = FourCCUtil::make( "ATI1" );
        constexpr uint32 kFourCC_BC4U = FourCCUtil::make( "BC4U" );
        constexpr uint32 kFourCC_ATI2 = FourCCUtil::make( "ATI2" );
        constexpr uint32 kFourCC_BC5U = FourCCUtil::make( "BC5U" );

        // D3DFMT 열거값이 그대로 들어앉은 FourCC — **네 글자 코드가 아니다.**
        // D3D9 시절 DDS 라이터는 부동소수점 포맷에 네 글자 이름을 주지 않고 `D3DFORMAT` 의 정수를
        // dwFourCC 에 밀어 넣었다. 그래서 값이 0x71 같은 작은 수로 보인다. 이 저장소의 임포터는 DX10 머리를 쓰므로
        // 이 모양은 밖에서 들여온 DDS 에만 있다(`DDSLoaderTest.LegacyHeadersMapToDXGIFormats`).
        constexpr uint32 kD3dFmt_R16F          = 111;
        constexpr uint32 kD3dFmt_G16R16F       = 112;
        constexpr uint32 kD3dFmt_A16B16G16R16F = 113;
        constexpr uint32 kD3dFmt_R32F          = 114;
        constexpr uint32 kD3dFmt_G32R32F       = 115;
        constexpr uint32 kD3dFmt_A32B32G32R32F = 116;

        // 매핑하는 DXGI 포맷
        constexpr uint32 kDxgiFormatBC1Unorm          = 71;
        constexpr uint32 kDxgiFormatBC2Unorm          = 74;
        constexpr uint32 kDxgiFormatBC3Unorm          = 77;
        constexpr uint32 kDxgiFormatBC4Unorm          = 80;
        constexpr uint32 kDxgiFormatBC5Unorm          = 83;
        constexpr uint32 kDxgiFormatR8G8B8A8Unorm     = 28;
        constexpr uint32 kDxgiFormatB8G8R8A8Unorm     = 87;
        constexpr uint32 kDxgiFormatB8G8R8A8UnormSRGB = 91;
        constexpr uint32 kDxgiFormatB8G8R8X8Unorm     = 88;
        constexpr uint32 kDxgiFormatR32G32B32A32Float = 2;
        constexpr uint32 kDxgiFormatR16G16B16A16Float = 10;
        constexpr uint32 kDxgiFormatR32G32Float       = 16;
        constexpr uint32 kDxgiFormatR16G16Float       = 34;
        constexpr uint32 kDxgiFormatR32Float          = 41;
        constexpr uint32 kDxgiFormatR16Float          = 54;

        // DXGI_FORMAT_UNKNOWN. 어떤 이미지도 이 포맷일 수 없으므로 "못 알아봤다" 의 표시로 쓴다.
        constexpr uint32 kDxgiFormatUnknown = 0;

#pragma pack( push, 1 )
        struct DDSPixelFormatHeader
        {
            uint32 _size;
            uint32 _flags;
            uint32 _fourCC;
            uint32 _rgbBitCount;
            uint32 _rBitMask;
            uint32 _gBitMask;
            uint32 _bBitMask;
            uint32 _aBitMask;
        };

        struct DDSFileHeader
        {
            uint32               _size;
            uint32               _flags;
            uint32               _height;
            uint32               _width;
            uint32               _pitchOrLinearSize;
            uint32               _depth;
            uint32               _mipMapCount;
            uint32               _arrReserved1[11];
            DDSPixelFormatHeader _pixelFormat;
            uint32               _caps;
            uint32               _caps2;
            uint32               _caps3;
            uint32               _caps4;
            uint32               _reserved2;
        };

        struct DDSHeaderDxt10
        {
            uint32 _dxgiFormat;
            uint32 _resourceDimension;
            uint32 _miscFlag;
            uint32 _arraySize;
            uint32 _miscFlags2;
        };
#pragma pack( pop )

        struct DDSLoaderInternal
        {
            /** @brief FourCC 코드 하나가 가리키는 DXGI 포맷의 표입니다. 이름의 정본은 `Config/Editor/DdsFormats.json` 이고 테스트가 이 표와 견준다. */
            static constexpr DDSLegacyFourCC kArrFourCCRow[] = {
                {         kFourCC_DXT1,          kDxgiFormatBC1Unorm},
                {         kFourCC_DXT2,          kDxgiFormatBC2Unorm},
                {         kFourCC_DXT3,          kDxgiFormatBC2Unorm},
                {         kFourCC_DXT4,          kDxgiFormatBC3Unorm},
                {         kFourCC_DXT5,          kDxgiFormatBC3Unorm},
                {         kFourCC_ATI1,          kDxgiFormatBC4Unorm},
                {         kFourCC_BC4U,          kDxgiFormatBC4Unorm},
                {         kFourCC_ATI2,          kDxgiFormatBC5Unorm},
                {         kFourCC_BC5U,          kDxgiFormatBC5Unorm},
                {         kD3dFmt_R16F,          kDxgiFormatR16Float},
                {      kD3dFmt_G16R16F,       kDxgiFormatR16G16Float},
                {kD3dFmt_A16B16G16R16F, kDxgiFormatR16G16B16A16Float},
                {         kD3dFmt_R32F,          kDxgiFormatR32Float},
                {      kD3dFmt_G32R32F,       kDxgiFormatR32G32Float},
                {kD3dFmt_A32B32G32R32F, kDxgiFormatR32G32B32A32Float},
            };

            /** @brief 압축 계열 FourCC 와 D3DFMT 정수 코드를 DXGI 포맷으로 바꿉니다. 모르는 코드는 `kDxgiFormatUnknown`(호출한 쪽의 포맷 검사가 실패로 끝낸다)입니다. */
            static uint32 findDXGIFormatOfFourCC( uint32 fourCC )
            {
                for ( const DDSLegacyFourCC& row : kArrFourCCRow )
                {
                    if ( row._fourCC == fourCC )
                        return row._dxgiFormat;
                }
                return kDxgiFormatUnknown;
            }

            /**
             * @brief 옛 머리의 비트 수 · 채널 마스크가 가리키는 DXGI 포맷의 표입니다. 마스크는 다섯 칸이 모두 같아야 맞는다(비슷한 모양을 어림하지 않는다).
             * @details 이름의 정본은 `Config/Editor/DdsFormats.json` 의 `legacy_rgb_masks` 이고 테스트가 이 표와 견준다. 알파 마스크가 0 인 RGB 순서는 X 채널 형식이 DXGI 에 없어
             *          R8G8B8A8 로 읽는다(알파 바이트는 그 파일이 쓴 값이다).
             */
            static constexpr DDSLegacyRGBMask kArrRGBMaskRow[] = {
                {32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000, kDxgiFormatR8G8B8A8Unorm},
                {32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0x00000000, kDxgiFormatR8G8B8A8Unorm},
                {32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000, kDxgiFormatB8G8R8A8Unorm},
                {32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0x00000000, kDxgiFormatB8G8R8X8Unorm},
            };

            /**
             * @brief 옛 머리의 RGB 마스크를 DXGI 포맷으로 바꿉니다. 표에 없는 모양은 `kDxgiFormatUnknown` 입니다.
             * @details BGRA 계열이면 호출한 쪽이 포맷으로 `_bIsBgra` 를 켠다(`bBgraFormat`) — 여기서 따로 정하지 않는다.
             */
            static uint32 findDxgiFormatOfRGBMasks( const DDSPixelFormatHeader& pixelFormat )
            {
                for ( const DDSLegacyRGBMask& row : kArrRGBMaskRow )
                {
                    const bool bSame = row._bitCount == pixelFormat._rgbBitCount && row._rMask == pixelFormat._rBitMask && row._gMask == pixelFormat._gBitMask &&
                                       row._bMask == pixelFormat._bBitMask && row._aMask == pixelFormat._aBitMask;
                    if ( bSame )
                        return row._dxgiFormat;
                }
                return kDxgiFormatUnknown;
            }

            /**
             * @brief 읽는 DXGI 형식의 블록 크기 표입니다. 형식 목록의 정본은 `Config/Editor/DdsFormats.json` 의 `dxgi_formats` 이고 테스트가 이 표와 견준다.
             * @details 압축 여부도 이 표가 정한다(블록 너비 > 1) — 번호 범위로 어림하지 않는다. DirectXTex `BitsPerPixel` · `ComputePitch` 와 같은 값이다.
             */
            static constexpr DDSFormatSize kArrFormatSizeRow[] = {
                {                              2, 1, 1, 16}, // R32G32B32A32_FLOAT
                {                              6, 1, 1, 12}, // R32G32B32_FLOAT
                {                             10, 1, 1,  8}, // R16G16B16A16_FLOAT
                {                             16, 1, 1,  8}, // R32G32_FLOAT
                {                             24, 1, 1,  4}, // R10G10B10A2_UNORM
                {                             28, 1, 1,  4}, // R8G8B8A8_UNORM
                {                             29, 1, 1,  4}, // R8G8B8A8_UNORM_SRGB
                {                             34, 1, 1,  4}, // R16G16_FLOAT
                {                             41, 1, 1,  4}, // R32_FLOAT
                {                             54, 1, 1,  2}, // R16_FLOAT
                {                             61, 1, 1,  1}, // R8_UNORM
                {                             71, 4, 4,  8}, // BC1_UNORM
                {                             72, 4, 4,  8}, // BC1_UNORM_SRGB
                {                             74, 4, 4, 16}, // BC2_UNORM
                {                             75, 4, 4, 16}, // BC2_UNORM_SRGB
                {                             77, 4, 4, 16}, // BC3_UNORM
                {                             78, 4, 4, 16}, // BC3_UNORM_SRGB
                {                             80, 4, 4,  8}, // BC4_UNORM
                {                             83, 4, 4, 16}, // BC5_UNORM
                {                             87, 1, 1,  4}, // B8G8R8A8_UNORM
                {                             88, 1, 1,  4}, // B8G8R8X8_UNORM
                {                             91, 1, 1,  4}, // B8G8R8A8_UNORM_SRGB
                {                             95, 4, 4, 16}, // BC6H_UF16
                {                             98, 4, 4, 16}, // BC7_UNORM
                {                             99, 4, 4, 16}, // BC7_UNORM_SRGB
                {   DDSFormat::kDxgiASTC4x4Unorm, 4, 4, 16},
                {DDSFormat::kEngineDxgiETC2RGBA8, 4, 4, 16},
                {   DDSFormat::kEngineDxgiEACR11, 4, 4,  8},
                {  DDSFormat::kEngineDxgiEACRG11, 4, 4, 16},
            };

            /** @brief @p dxgiFormat 의 블록 크기 줄입니다. 표에 없으면 nullptr 입니다(읽기가 거절한다). */
            static const DDSFormatSize* findFormatSize( uint32 dxgiFormat )
            {
                for ( const DDSFormatSize& row : kArrFormatSizeRow )
                {
                    if ( row._dxgiFormat == dxgiFormat )
                        return &row;
                }
                return nullptr;
            }

            /** @brief 너비 · 높이 · 깊이 중 가장 큰 변을 1 까지 반씩 줄인 밉 수(온전한 밉 사슬의 길이)입니다. */
            static uint32 computeFullMipCount( uint32 width, uint32 height, uint32 depth )
            {
                uint32 extent   = width > height ? width : height;
                extent          = extent > depth ? extent : depth;
                uint32 mipCount = 1;
                while ( extent > 1 )
                {
                    extent >>= 1;
                    ++mipCount;
                }
                return mipCount;
            }

            /** @brief 층 하나의 밉 사슬이 차지하는 바이트입니다(밉마다 블록 수 × 블록 바이트 × 그 밉의 깊이). */
            static uint64 computeLayerBytes( const DDSFormatSize& size, uint32 width, uint32 height, uint32 depth, uint32 mipCount )
            {
                uint64 total = 0;
                for ( uint32 mip = 0; mip < mipCount; ++mip )
                {
                    const uint64 mipWidth  = ( width >> mip ) > 0 ? ( width >> mip ) : 1;
                    const uint64 mipHeight = ( height >> mip ) > 0 ? ( height >> mip ) : 1;
                    const uint64 mipDepth  = ( depth >> mip ) > 0 ? ( depth >> mip ) : 1;
                    const uint64 blocksX   = ( mipWidth + size._blockWidth - 1 ) / size._blockWidth;
                    const uint64 blocksY   = ( mipHeight + size._blockHeight - 1 ) / size._blockHeight;
                    total += blocksX * blocksY * size._blockBytes * mipDepth;
                }
                return total;
            }
        };
    } // namespace

    bool DDSLoader::loadFromAsset( const AssetID& assetID, DDSImageData& outImage )
    {
        const string_view relativePath = assetID.view();
        // 이 호스트(전용 서버)의 패키지에 없는 종류다 — 없는 것으로 치고 오류를 남기지 않는다(AssetPaths::setHostTarget).
        if ( engine::getSubsystem<AssetPaths>().isExcludedForHost( relativePath ) )
            return false;
        vector<uint8> bytes;
        if ( AssetIO::readBinaryAsset( assetID, bytes ) == false || bytes.empty() )
        {
            SW_LOG_ERROR_ID( "DDS.ReadFailed", "Failed to read DDS resource: %#", relativePath );
            return false;
        }

        return loadFromMemory( bytes.data(), bytes.size(), outImage );
    }

    bool DDSLoader::loadFromMemory( const uint8* pBuffer, size_t bufferSize, DDSImageData& outImage )
    {
        // **실패는 출력에 아무것도 남기지 않는다.** 나가는 길이 여섯 군데인데 그중 넷은 크기를
        // 이미 채운 뒤에 있다. 그래서 여기서 비우고, 파싱은 지역 변수에 한 뒤 **성공했을 때만**
        // 옮긴다. "실패 경로마다 잊지 말고 비우기" 를 사람이 지키는 대신 구조로 못 박는다.
        outImage = DDSImageData{};

        DDSImageData image;

        if ( pBuffer == nullptr || bufferSize < sizeof( uint32 ) + sizeof( DDSFileHeader ) )
        {
            SW_LOG_ERROR_ID( "DDS.Truncated", "DDS buffer is null or smaller than minimum header size." );
            return false;
        }

        const uint32 magic = *reinterpret_cast<const uint32*>( pBuffer );
        if ( magic != DDSFormat::kMagic )
        {
            SW_LOG_ERROR_ID(
                "DDS.InvalidMagic", "Invalid DDS magic: 0x%# (expected 0x%#).",
                Fmt( magic, Format( 8, Format::Padding::Zero ).hex() ),
                Fmt( static_cast<uint32>( DDSFormat::kMagic ), Format( 8, Format::Padding::Zero ).hex() ) );
            return false;
        }

        const DDSFileHeader* pHeader = reinterpret_cast<const DDSFileHeader*>( pBuffer + sizeof( uint32 ) );
        if ( pHeader->_size != DDSFormat::kHeaderSize || pHeader->_pixelFormat._size != sizeof( DDSPixelFormatHeader ) )
        {
            SW_LOG_ERROR_ID( "DDS.CorruptHeader", "Corrupted DDS header size (%#, expected %#).", pHeader->_size, DDSFormat::kHeaderSize );
            return false;
        }
        if ( pHeader->_width == 0 || pHeader->_height == 0 )
        {
            SW_LOG_ERROR_ID( "DDS.CorruptHeader", "DDS header has an empty size (%# x %#).", pHeader->_width, pHeader->_height );
            return false;
        }

        image._width    = pHeader->_width;
        image._height   = pHeader->_height;
        image._depth    = ( pHeader->_depth > 0 ) ? pHeader->_depth : 1;
        image._mipCount = ( pHeader->_mipMapCount > 0 ) ? pHeader->_mipMapCount : 1;

        size_t dataOffset = sizeof( uint32 ) + sizeof( DDSFileHeader );

        if ( ( pHeader->_pixelFormat._flags & DDSFormat::kPixelFormatFourCCFlag ) != 0 && pHeader->_pixelFormat._fourCC == DDSFormat::kDx10FourCC )
        {
            if ( bufferSize < dataOffset + sizeof( DDSHeaderDxt10 ) )
            {
                SW_LOG_ERROR_ID( "DDS.Truncated", "DDS buffer truncated before DX10 header." );
                return false;
            }

            const DDSHeaderDxt10* pDxt10 = reinterpret_cast<const DDSHeaderDxt10*>( pBuffer + dataOffset );
            if ( pDxt10->_arraySize == 0 )
            {
                SW_LOG_ERROR_ID( "DDS.CorruptHeader", "DDS DX10 header has an array size of 0." );
                return false;
            }
            image._dxgiFormat = pDxt10->_dxgiFormat;
            image._layerCount = pDxt10->_arraySize * ( ( pDxt10->_miscFlag & DDSFormat::kDx10MiscTextureCubeFlag ) != 0 ? 6u : 1u );
            dataOffset += sizeof( DDSHeaderDxt10 );
        }
        else if ( ( pHeader->_pixelFormat._flags & DDSFormat::kPixelFormatFourCCFlag ) != 0 )
        {
            // 모르는 코드는 포맷을 정하지 않는다. 아래 `kDxgiFormatUnknown` 검사가 실패로 끝낸다.
            image._dxgiFormat = DDSLoaderInternal::findDXGIFormatOfFourCC( pHeader->_pixelFormat._fourCC );
        }
        else if ( ( pHeader->_pixelFormat._flags & DDSFormat::kPixelFormatRGBFlag ) != 0 )
        {
            image._dxgiFormat = DDSLoaderInternal::findDxgiFormatOfRGBMasks( pHeader->_pixelFormat );
        }
        // 옛 머리의 큐브 맵은 caps2 가 면 여섯을 말한다(DX10 머리는 위에서 misc 플래그로 정했다).
        if ( dataOffset == sizeof( uint32 ) + sizeof( DDSFileHeader ) && ( pHeader->_caps2 & DDSFormat::kCaps2CubemapFlag ) != 0 )
            image._layerCount = 6;

        const bool bBgraFormat = image._dxgiFormat == kDxgiFormatB8G8R8A8Unorm || image._dxgiFormat == kDxgiFormatB8G8R8X8Unorm ||
                                 image._dxgiFormat == kDxgiFormatB8G8R8A8UnormSRGB;
        if ( bBgraFormat )
            image._bIsBgra = SW_TRUE;

        // **못 알아본 포맷은 실패다.** `_dxgiFormat == 0` 인 채로 true 를 돌려주면 알아보지 못한 이미지가
        // "성공적으로 로드된 이미지" 로 흘러 나간다(`isValid()` 도 포맷을 본다).
        if ( image._dxgiFormat == kDxgiFormatUnknown )
        {
            SW_LOG_ERROR_ID(
                "DDS.UnsupportedFormat", "Unsupported DDS pixel format (pfFlags=0x%#, fourCC=0x%#, rgbBits=%#) — cannot determine a DXGI format.",
                Fmt( pHeader->_pixelFormat._flags, Format( 8, Format::Padding::Zero ).hex() ),
                Fmt( pHeader->_pixelFormat._fourCC, Format( 8, Format::Padding::Zero ).hex() ),
                pHeader->_pixelFormat._rgbBitCount );
            return false;
        }
        // 형식은 알지만 크기 표에 없으면(DX10 머리가 적은 아무 번호) 바이트 수를 셀 수 없다 — 읽지 않는다.
        const DDSFormatSize* pSize = DDSLoaderInternal::findFormatSize( image._dxgiFormat );
        if ( pSize == nullptr )
        {
            SW_LOG_ERROR_ID( "DDS.UnsupportedFormat", "Unsupported DXGI format %# in the DDS header — the loader has no block size for it.", image._dxgiFormat );
            return false;
        }
        image._bCompressed = pSize->_blockWidth > 1 ? SW_TRUE : SW_FALSE;

        const uint32 fullMipCount = DDSLoaderInternal::computeFullMipCount( image._width, image._height, image._depth );
        if ( image._mipCount > fullMipCount )
        {
            SW_LOG_ERROR_ID( "DDS.CorruptHeader", "DDS header lists %# mips but a %# x %# x %# image has at most %#.", image._mipCount, image._width, image._height, image._depth,
                             fullMipCount );
            return false;
        }

        // 머리만 있고 픽셀이 없으면 실패다 — 성공은 곧 `isValid()` 여야 한다(빈 바이트로 텍스처를 만들지 않게).
        if ( bufferSize <= dataOffset )
        {
            SW_LOG_ERROR_ID( "DDS.Truncated", "DDS has no pixel data after its header." );
            return false;
        }

        // 픽셀 바이트는 형식 · 크기 · 밉 · 층으로 정해진다. 짧으면 업로드가 버퍼 밖을 읽고, 길면 형식이나 밉 수를 잘못 읽은 것이다(DirectXTex 도 크기를 셈해 견준다).
        const size_t payloadSize   = bufferSize - dataOffset;
        const uint64 expectedBytes = DDSLoaderInternal::computeLayerBytes( *pSize, image._width, image._height, image._depth, image._mipCount ) * image._layerCount;
        if ( payloadSize != expectedBytes )
        {
            SW_LOG_ERROR_ID( "DDS.PayloadSizeMismatch", "DDS pixel data is %# bytes but DXGI format %# at %# x %# x %#, %# mips, %# layers needs %# bytes.", payloadSize,
                             image._dxgiFormat, image._width, image._height, image._depth, image._mipCount, image._layerCount, expectedBytes );
            return false;
        }
        image._bytes.resize( payloadSize );
        Memory::copy( image._bytes.data(), pBuffer + dataOffset, payloadSize );

        outImage = std::move( image );
        return true;
    }

    vector<DDSLegacyFourCC> DDSLoader::getLegacyFourCCTable()
    {
        vector<DDSLegacyFourCC> listRow;
        for ( const DDSLegacyFourCC& row : DDSLoaderInternal::kArrFourCCRow )
        {
            listRow.push_back( row );
        }
        return listRow;
    }

    vector<DDSFormatSize> DDSLoader::getFormatSizeTable()
    {
        vector<DDSFormatSize> listRow;
        for ( const DDSFormatSize& row : DDSLoaderInternal::kArrFormatSizeRow )
        {
            listRow.push_back( row );
        }
        return listRow;
    }

    vector<DDSLegacyRGBMask> DDSLoader::getLegacyRGBMaskTable()
    {
        vector<DDSLegacyRGBMask> listRow;
        for ( const DDSLegacyRGBMask& row : DDSLoaderInternal::kArrRGBMaskRow )
        {
            listRow.push_back( row );
        }
        return listRow;
    }
} // namespace sw
