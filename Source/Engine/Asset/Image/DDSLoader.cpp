#include "pch.h"

#include "Engine/Asset/Image/DDSLoader.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetIO.h"
#include "Engine/Asset/AssetPaths.h"
#include "Engine/Asset/Image/DDSFormat.h"

namespace sw
{
    namespace
    {

        // DXT 계열 FourCC 코드
        constexpr uint32 kFourCC_DXT1 = FourCcUtil::make( "DXT1" );
        constexpr uint32 kFourCC_DXT2 = FourCcUtil::make( "DXT2" );
        constexpr uint32 kFourCC_DXT3 = FourCcUtil::make( "DXT3" );
        constexpr uint32 kFourCC_DXT4 = FourCcUtil::make( "DXT4" );
        constexpr uint32 kFourCC_DXT5 = FourCcUtil::make( "DXT5" );
        constexpr uint32 kFourCC_ATI1 = FourCcUtil::make( "ATI1" );
        constexpr uint32 kFourCC_BC4U = FourCcUtil::make( "BC4U" );
        constexpr uint32 kFourCC_ATI2 = FourCcUtil::make( "ATI2" );
        constexpr uint32 kFourCC_BC5U = FourCcUtil::make( "BC5U" );

        // D3DFMT 열거값이 그대로 들어앉은 FourCC — **네 글자 코드가 아니다.**
        // D3D9 시절 DDS 라이터는 부동소수점 포맷에 네 글자 이름을 주지 않고 `D3DFORMAT` 의 정수를
        // dwFourCC 에 밀어 넣었다. 그래서 값이 0x71 같은 작은 수로 보인다. 이 저장소의 임포터는 DX10 머리를 쓰므로
        // 이 모양은 밖에서 들여온 DDS 에만 있다(`ResourceTest.DDSLoaderReadsD3dFormatIntegerFourCc`).
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
        constexpr uint32 kDxgiFormatB8G8R8A8UnormSrgb = 91;
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
            static constexpr DDSLegacyFourCc kArrFourCcRow[] = {
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
            static uint32 findDxgiFormatOfFourCc( uint32 fourCC )
            {
                for ( const DDSLegacyFourCc& row : kArrFourCcRow )
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
            static constexpr DDSLegacyRgbMask kArrRgbMaskRow[] = {
                {32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000, kDxgiFormatR8G8B8A8Unorm},
                {32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0x00000000, kDxgiFormatR8G8B8A8Unorm},
                {32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000, kDxgiFormatB8G8R8A8Unorm},
                {32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0x00000000, kDxgiFormatB8G8R8X8Unorm},
            };

            /**
             * @brief 옛 머리의 RGB 마스크를 DXGI 포맷으로 바꿉니다. 표에 없는 모양은 `kDxgiFormatUnknown` 입니다.
             * @details BGRA 계열이면 호출한 쪽이 포맷으로 `_bIsBgra` 를 켠다(`bBgraFormat`) — 여기서 따로 정하지 않는다.
             */
            static uint32 findDxgiFormatOfRgbMasks( const DDSPixelFormatHeader& pixelFormat )
            {
                for ( const DDSLegacyRgbMask& row : kArrRgbMaskRow )
                {
                    const bool bSame = row._bitCount == pixelFormat._rgbBitCount && row._rMask == pixelFormat._rBitMask && row._gMask == pixelFormat._gBitMask &&
                                       row._bMask == pixelFormat._bBitMask && row._aMask == pixelFormat._aBitMask;
                    if ( bSame )
                        return row._dxgiFormat;
                }
                return kDxgiFormatUnknown;
            }
        };
    } // namespace

    SW_LOG_CALLER( "DDSLoader" );

    bool DDSLoader::loadFromDiskFile( string_view filePath, DDSImageData& outImage )
    {
        vector<uint8> bytes;
        if ( FileUtil::readFile( filePath, bytes ) == false || bytes.empty() )
        {
            SW_LOG_ERROR( "Failed to read DDS file: %#", filePath );
            return false;
        }

        return loadFromMemory( bytes.data(), bytes.size(), outImage );
    }

    bool DDSLoader::loadFromAsset( const AssetID& assetID, DDSImageData& outImage )
    {
        const string_view relativePath = assetID.view();
        // 이 호스트(전용 서버)의 패키지에 없는 종류다 — 없는 것으로 치고 오류를 남기지 않는다(AssetPaths::setHostTarget).
        if ( AssetPaths::isExcludedForHost( relativePath ) )
            return false;
        vector<uint8> bytes;
        if ( AssetIO::readBinaryAsset( assetID, bytes ) == false || bytes.empty() )
        {
            SW_LOG_ERROR( "Failed to read DDS resource: %#", relativePath );
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
            SW_LOG_ERROR( "DDS buffer is null or smaller than minimum header size." );
            return false;
        }

        const uint32 magic = *reinterpret_cast<const uint32*>( pBuffer );
        if ( magic != DDSFormat::kMagic )
        {
            SW_LOG_ERROR(
                "Invalid DDS magic: 0x%# (expected 0x%#).",
                Fmt( magic, Format( 8, Format::Padding::Zero ).hex() ),
                Fmt( static_cast<uint32>( DDSFormat::kMagic ), Format( 8, Format::Padding::Zero ).hex() ) );
            return false;
        }

        const DDSFileHeader* pHeader = reinterpret_cast<const DDSFileHeader*>( pBuffer + sizeof( uint32 ) );
        if ( pHeader->_size != DDSFormat::kHeaderSize || pHeader->_pixelFormat._size != sizeof( DDSPixelFormatHeader ) )
        {
            SW_LOG_ERROR( "Corrupted DDS header size (%#, expected %#).", pHeader->_size, DDSFormat::kHeaderSize );
            return false;
        }

        image._width    = pHeader->_width;
        image._height   = pHeader->_height;
        image._depth    = ( pHeader->_depth > 0 ) ? pHeader->_depth : 1;
        image._mipCount = ( pHeader->_mipMapCount > 0 ) ? pHeader->_mipMapCount : 1;

        size_t dataOffset = sizeof( uint32 ) + sizeof( DDSFileHeader );

        if ( ( pHeader->_pixelFormat._flags & DDSFormat::kPixelFormatFourCcFlag ) != 0 && pHeader->_pixelFormat._fourCC == DDSFormat::kDx10FourCc )
        {
            if ( bufferSize < dataOffset + sizeof( DDSHeaderDxt10 ) )
            {
                SW_LOG_ERROR( "DDS buffer truncated before DX10 header." );
                return false;
            }

            const DDSHeaderDxt10* pDxt10 = reinterpret_cast<const DDSHeaderDxt10*>( pBuffer + dataOffset );
            image._dxgiFormat            = pDxt10->_dxgiFormat;
            dataOffset += sizeof( DDSHeaderDxt10 );
        }
        else if ( ( pHeader->_pixelFormat._flags & DDSFormat::kPixelFormatFourCcFlag ) != 0 )
        {
            // 모르는 코드는 포맷을 정하지 않는다. 아래 `kDxgiFormatUnknown` 검사가 실패로 끝낸다.
            image._dxgiFormat = DDSLoaderInternal::findDxgiFormatOfFourCc( pHeader->_pixelFormat._fourCC );
        }
        else if ( ( pHeader->_pixelFormat._flags & DDSFormat::kPixelFormatRgbFlag ) != 0 )
        {
            image._dxgiFormat = DDSLoaderInternal::findDxgiFormatOfRgbMasks( pHeader->_pixelFormat );
        }

        const bool bIsBc1To5 = ( 70 <= image._dxgiFormat && image._dxgiFormat <= 84 );
        const bool bIsBc6Or7 = ( 94 <= image._dxgiFormat && image._dxgiFormat <= 99 );
        image._bCompressed   = ( bIsBc1To5 || bIsBc6Or7 ) ? SW_TRUE : SW_FALSE;

        const bool bBgraFormat = image._dxgiFormat == kDxgiFormatB8G8R8A8Unorm || image._dxgiFormat == kDxgiFormatB8G8R8X8Unorm ||
                                 image._dxgiFormat == kDxgiFormatB8G8R8A8UnormSrgb;
        if ( bBgraFormat )
            image._bIsBgra = SW_TRUE;

        // **못 알아본 포맷은 실패다.** `_dxgiFormat == 0` 인 채로 true 를 돌려주면 알아보지 못한 이미지가
        // "성공적으로 로드된 이미지" 로 흘러 나간다(`isValid()` 도 포맷을 본다).
        if ( image._dxgiFormat == kDxgiFormatUnknown )
        {
            SW_LOG_ERROR(
                "Unsupported DDS pixel format (pfFlags=0x%#, fourCC=0x%#, rgbBits=%#) — cannot determine a DXGI format.",
                Fmt( pHeader->_pixelFormat._flags, Format( 8, Format::Padding::Zero ).hex() ),
                Fmt( pHeader->_pixelFormat._fourCC, Format( 8, Format::Padding::Zero ).hex() ),
                pHeader->_pixelFormat._rgbBitCount );
            return false;
        }

        if ( bufferSize < dataOffset )
        {
            SW_LOG_ERROR( "DDS payload offset out of bounds." );
            return false;
        }

        const size_t payloadSize = bufferSize - dataOffset;
        image._bytes.resize( payloadSize );
        Memory::copy( image._bytes.data(), pBuffer + dataOffset, payloadSize );

        outImage = std::move( image );
        return true;
    }

    vector<DDSLegacyFourCc> DDSLoader::getLegacyFourCcTable()
    {
        vector<DDSLegacyFourCc> listRow;
        for ( const DDSLegacyFourCc& row : DDSLoaderInternal::kArrFourCcRow )
        {
            listRow.push_back( row );
        }
        return listRow;
    }

    vector<DDSLegacyRgbMask> DDSLoader::getLegacyRgbMaskTable()
    {
        vector<DDSLegacyRgbMask> listRow;
        for ( const DDSLegacyRgbMask& row : DDSLoaderInternal::kArrRgbMaskRow )
        {
            listRow.push_back( row );
        }
        return listRow;
    }
} // namespace sw
