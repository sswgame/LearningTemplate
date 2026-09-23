#include "pch.h"

#include "Engine/Resource/DdsLoader.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    namespace
    {
        constexpr uint32 kDdsMagic      = 0x20534444; // "DDS "
        constexpr uint32 kDdsHeaderSize = 124;
        constexpr uint32 kDx10Magic     = 0x30315844; // "DX10"

        constexpr uint32 kDdpfFourCC = 0x00000004;
        constexpr uint32 kDdpfRgb    = 0x00000040;

        // 옛 FourCC 코드
        constexpr uint32 kFourCC_DXT1 = 0x31545844;
        constexpr uint32 kFourCC_DXT2 = 0x32545844;
        constexpr uint32 kFourCC_DXT3 = 0x33545844;
        constexpr uint32 kFourCC_DXT4 = 0x34545844;
        constexpr uint32 kFourCC_DXT5 = 0x35545844;
        constexpr uint32 kFourCC_ATI1 = 0x31495441;
        constexpr uint32 kFourCC_BC4U = 0x55344342;
        constexpr uint32 kFourCC_ATI2 = 0x32495441;
        constexpr uint32 kFourCC_BC5U = 0x55354342;

        // D3DFMT 열거값이 그대로 들어앉은 FourCC — **네 글자 코드가 아니다.**
        // D3D9 시절 DDS 라이터는 부동소수점 포맷에 네 글자 이름을 주지 않고 `D3DFORMAT` 의 정수를
        // dwFourCC 에 밀어 넣었다. 그래서 값이 0x71 같은 작은 수로 보인다. 이 저장소의 DDS 다섯 개
        // (`engine/textures/perlin.dds` · `skybox/env*.dds`)가 전부 이 모양이고, 예전에는 전부
        // "Unrecognized DDS FourCC" 경고 한 줄만 남기고 **성공으로 처리**되고 있었다.
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
        struct DdsPixelFormatHeader
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

        struct DdsFileHeader
        {
            uint32               _size;
            uint32               _flags;
            uint32               _height;
            uint32               _width;
            uint32               _pitchOrLinearSize;
            uint32               _depth;
            uint32               _mipMapCount;
            uint32               _arrReserved1[11];
            DdsPixelFormatHeader _pixelFormat;
            uint32               _caps;
            uint32               _caps2;
            uint32               _caps3;
            uint32               _caps4;
            uint32               _reserved2;
        };

        struct DdsHeaderDxt10
        {
            uint32 _dxgiFormat;
            uint32 _resourceDimension;
            uint32 _miscFlag;
            uint32 _arraySize;
            uint32 _miscFlags2;
        };
#pragma pack( pop )
    } // namespace

    SW_LOG_CALLER( "DdsLoader" );

    bool DdsLoader::loadFromFile( string_view filePath, DdsImageData& outImage )
    {
        vector<uint8> bytes;
        if ( FileUtil::readFile( filePath, bytes ) == false || bytes.empty() )
        {
            SW_LOG_ERROR( "Failed to read DDS file: %#", filePath );
            return false;
        }

        return loadFromMemory( bytes.data(), bytes.size(), outImage );
    }

    bool DdsLoader::loadFromResource( string_view relativePath, DdsImageData& outImage )
    {
        vector<uint8> bytes;
        if ( ResourceUtil::readBinaryResource( relativePath, bytes ) == false || bytes.empty() )
        {
            SW_LOG_ERROR( "Failed to read DDS resource: %#", relativePath );
            return false;
        }

        return loadFromMemory( bytes.data(), bytes.size(), outImage );
    }

    bool DdsLoader::loadFromMemory( const uint8* pBuffer, size_t bufferSize, DdsImageData& outImage )
    {
        // **실패는 출력에 아무것도 남기지 않는다.** 나가는 길이 여섯 군데인데 그중 넷은 크기를
        // 이미 채운 뒤에 있다. 그래서 여기서 비우고, 파싱은 지역 변수에 한 뒤 **성공했을 때만**
        // 옮긴다. "실패 경로마다 잊지 말고 비우기" 를 사람이 지키는 대신 구조로 못 박는다.
        outImage = DdsImageData{};

        DdsImageData image;

        if ( pBuffer == nullptr || bufferSize < sizeof( uint32 ) + sizeof( DdsFileHeader ) )
        {
            SW_LOG_ERROR( "DDS buffer is null or smaller than minimum header size." );
            return false;
        }

        const uint32 magic = *reinterpret_cast<const uint32*>( pBuffer );
        if ( magic != kDdsMagic )
        {
            SW_LOG_ERROR(
                "Invalid DDS magic: 0x%# (expected 0x%#).",
                Fmt( magic, Format( 8, Format::Padding::Zero ).hex() ),
                Fmt( static_cast<uint32>( kDdsMagic ), Format( 8, Format::Padding::Zero ).hex() ) );
            return false;
        }

        const DdsFileHeader* pHeader = reinterpret_cast<const DdsFileHeader*>( pBuffer + sizeof( uint32 ) );
        if ( pHeader->_size != kDdsHeaderSize || pHeader->_pixelFormat._size != sizeof( DdsPixelFormatHeader ) )
        {
            SW_LOG_ERROR( "Corrupted DDS header size (%#, expected %#).", pHeader->_size, kDdsHeaderSize );
            return false;
        }

        image._width    = pHeader->_width;
        image._height   = pHeader->_height;
        image._depth    = ( pHeader->_depth > 0 ) ? pHeader->_depth : 1;
        image._mipCount = ( pHeader->_mipMapCount > 0 ) ? pHeader->_mipMapCount : 1;

        size_t dataOffset = sizeof( uint32 ) + sizeof( DdsFileHeader );

        if ( ( pHeader->_pixelFormat._flags & kDdpfFourCC ) != 0 && pHeader->_pixelFormat._fourCC == kDx10Magic )
        {
            if ( bufferSize < dataOffset + sizeof( DdsHeaderDxt10 ) )
            {
                SW_LOG_ERROR( "DDS buffer truncated before DX10 header." );
                return false;
            }

            const DdsHeaderDxt10* pDxt10 = reinterpret_cast<const DdsHeaderDxt10*>( pBuffer + dataOffset );
            image._dxgiFormat            = pDxt10->_dxgiFormat;
            dataOffset += sizeof( DdsHeaderDxt10 );
        }
        else if ( ( pHeader->_pixelFormat._flags & kDdpfFourCC ) != 0 )
        {
            switch ( pHeader->_pixelFormat._fourCC )
            {
                case kFourCC_DXT1:
                {
                    image._dxgiFormat = kDxgiFormatBC1Unorm;
                    break;
                }
                case kFourCC_DXT2:
                case kFourCC_DXT3:
                {
                    image._dxgiFormat = kDxgiFormatBC2Unorm;
                    break;
                }
                case kFourCC_DXT4:
                case kFourCC_DXT5:
                {
                    image._dxgiFormat = kDxgiFormatBC3Unorm;
                    break;
                }
                case kFourCC_ATI1:
                case kFourCC_BC4U:
                {
                    image._dxgiFormat = kDxgiFormatBC4Unorm;
                    break;
                }
                case kFourCC_ATI2:
                case kFourCC_BC5U:
                {
                    image._dxgiFormat = kDxgiFormatBC5Unorm;
                    break;
                }
                case kD3dFmt_R16F:
                {
                    image._dxgiFormat = kDxgiFormatR16Float;
                    break;
                }
                case kD3dFmt_G16R16F:
                {
                    image._dxgiFormat = kDxgiFormatR16G16Float;
                    break;
                }
                case kD3dFmt_A16B16G16R16F:
                {
                    image._dxgiFormat = kDxgiFormatR16G16B16A16Float;
                    break;
                }
                case kD3dFmt_R32F:
                {
                    image._dxgiFormat = kDxgiFormatR32Float;
                    break;
                }
                case kD3dFmt_G32R32F:
                {
                    image._dxgiFormat = kDxgiFormatR32G32Float;
                    break;
                }
                case kD3dFmt_A32B32G32R32F:
                {
                    image._dxgiFormat = kDxgiFormatR32G32B32A32Float;
                    break;
                }
                default:
                {
                    // 포맷을 정하지 않고 빠진다. 아래 `kDxgiFormatUnknown` 검사가 실패로 끝낸다.
                    break;
                }
            }
        }
        else if ( ( pHeader->_pixelFormat._flags & kDdpfRgb ) != 0 )
        {
            image._bitsPerPixel = pHeader->_pixelFormat._rgbBitCount;
            if ( pHeader->_pixelFormat._rgbBitCount == 32 )
            {
                if ( pHeader->_pixelFormat._rBitMask == 0x00FF0000 && pHeader->_pixelFormat._gBitMask == 0x0000FF00 &&
                     pHeader->_pixelFormat._bBitMask == 0x000000FF )
                {
                    image._dxgiFormat = ( pHeader->_pixelFormat._aBitMask != 0 ) ? kDxgiFormatB8G8R8A8Unorm : kDxgiFormatB8G8R8X8Unorm;
                    image._bIsBgra    = SW_TRUE;
                }
                else if ( pHeader->_pixelFormat._rBitMask == 0x000000FF && pHeader->_pixelFormat._gBitMask == 0x0000FF00 &&
                          pHeader->_pixelFormat._bBitMask == 0x00FF0000 )
                {
                    image._dxgiFormat = kDxgiFormatR8G8B8A8Unorm;
                    image._bIsBgra    = SW_FALSE;
                }
            }
        }

        const bool bIsBc1To5 = ( 70 <= image._dxgiFormat && image._dxgiFormat <= 84 );
        const bool bIsBc6Or7 = ( 94 <= image._dxgiFormat && image._dxgiFormat <= 99 );
        image._bCompressed   = ( bIsBc1To5 || bIsBc6Or7 ) ? SW_TRUE : SW_FALSE;

        if ( image._dxgiFormat == kDxgiFormatB8G8R8A8Unorm || image._dxgiFormat == kDxgiFormatB8G8R8X8Unorm ||
             image._dxgiFormat == kDxgiFormatB8G8R8A8UnormSrgb )
            image._bIsBgra = SW_TRUE;

        // **못 알아본 포맷은 실패다.** 예전에는 여기까지 흘러와 `_dxgiFormat == 0` 인 채로 true 를
        // 반환했다. 그때는 `isValid()` 도 포맷을 보지 않아서(바이트 · 가로 · 세로만 봤다) 부르는 쪽에서도
        // 걸러지지 않았고, 알아보지 못한 이미지가 "성공적으로 로드된 이미지" 로 흘러 나갔다.
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
} // namespace sw
