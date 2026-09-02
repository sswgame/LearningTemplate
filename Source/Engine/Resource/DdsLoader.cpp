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

        // Legacy FourCC codes
        constexpr uint32 kFourCC_DXT1 = 0x31545844;
        constexpr uint32 kFourCC_DXT2 = 0x32545844;
        constexpr uint32 kFourCC_DXT3 = 0x33545844;
        constexpr uint32 kFourCC_DXT4 = 0x34545844;
        constexpr uint32 kFourCC_DXT5 = 0x35545844;
        constexpr uint32 kFourCC_ATI1 = 0x31495441;
        constexpr uint32 kFourCC_BC4U = 0x55344342;
        constexpr uint32 kFourCC_ATI2 = 0x32495441;
        constexpr uint32 kFourCC_BC5U = 0x55354342;

        // DXGI formats mapped
        constexpr uint32 kDxgiFormatBC1Unorm          = 71;
        constexpr uint32 kDxgiFormatBC2Unorm          = 74;
        constexpr uint32 kDxgiFormatBC3Unorm          = 77;
        constexpr uint32 kDxgiFormatBC4Unorm          = 80;
        constexpr uint32 kDxgiFormatBC5Unorm          = 83;
        constexpr uint32 kDxgiFormatR8G8B8A8Unorm     = 28;
        constexpr uint32 kDxgiFormatB8G8R8A8Unorm     = 87;
        constexpr uint32 kDxgiFormatB8G8R8A8UnormSrgb = 91;
        constexpr uint32 kDxgiFormatB8G8R8X8Unorm     = 88;

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
            SW_LOG_ERROR( "Failed to read DDS file: %#", filePath.data() );
            return false;
        }

        return loadFromMemory( bytes.data(), bytes.size(), outImage );
    }

    bool DdsLoader::loadFromResource( string_view relativePath, DdsImageData& outImage )
    {
        vector<uint8> bytes;
        if ( ResourceUtil::readBinaryResource( relativePath, bytes ) == false || bytes.empty() )
        {
            SW_LOG_ERROR( "Failed to read DDS resource: %#", relativePath.data() );
            return false;
        }

        return loadFromMemory( bytes.data(), bytes.size(), outImage );
    }

    bool DdsLoader::loadFromMemory( const uint8* pBuffer, size_t bufferSize, DdsImageData& outImage )
    {
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

        outImage._width    = pHeader->_width;
        outImage._height   = pHeader->_height;
        outImage._depth    = ( pHeader->_depth > 0 ) ? pHeader->_depth : 1;
        outImage._mipCount = ( pHeader->_mipMapCount > 0 ) ? pHeader->_mipMapCount : 1;

        size_t dataOffset = sizeof( uint32 ) + sizeof( DdsFileHeader );

        if ( ( pHeader->_pixelFormat._flags & kDdpfFourCC ) != 0 && pHeader->_pixelFormat._fourCC == kDx10Magic )
        {
            if ( bufferSize < dataOffset + sizeof( DdsHeaderDxt10 ) )
            {
                SW_LOG_ERROR( "DDS buffer truncated before DX10 header." );
                return false;
            }

            const DdsHeaderDxt10* pDxt10 = reinterpret_cast<const DdsHeaderDxt10*>( pBuffer + dataOffset );
            outImage._dxgiFormat         = pDxt10->_dxgiFormat;
            dataOffset += sizeof( DdsHeaderDxt10 );
        }
        else if ( ( pHeader->_pixelFormat._flags & kDdpfFourCC ) != 0 )
        {
            switch ( pHeader->_pixelFormat._fourCC )
            {
                case kFourCC_DXT1:
                    outImage._dxgiFormat = kDxgiFormatBC1Unorm;
                    break;
                case kFourCC_DXT2:
                case kFourCC_DXT3:
                    outImage._dxgiFormat = kDxgiFormatBC2Unorm;
                    break;
                case kFourCC_DXT4:
                case kFourCC_DXT5:
                    outImage._dxgiFormat = kDxgiFormatBC3Unorm;
                    break;
                case kFourCC_ATI1:
                case kFourCC_BC4U:
                    outImage._dxgiFormat = kDxgiFormatBC4Unorm;
                    break;
                case kFourCC_ATI2:
                case kFourCC_BC5U:
                    outImage._dxgiFormat = kDxgiFormatBC5Unorm;
                    break;
                default:
                    SW_LOG_WARNING(
                        "Unrecognized DDS FourCC: 0x%#",
                        Fmt( pHeader->_pixelFormat._fourCC, Format( 8, Format::Padding::Zero ).hex() ) );
                    break;
            }
        }
        else if ( ( pHeader->_pixelFormat._flags & kDdpfRgb ) != 0 )
        {
            outImage._bitsPerPixel = pHeader->_pixelFormat._rgbBitCount;
            if ( pHeader->_pixelFormat._rgbBitCount == 32 )
            {
                if ( pHeader->_pixelFormat._rBitMask == 0x00FF0000 && pHeader->_pixelFormat._gBitMask == 0x0000FF00 &&
                     pHeader->_pixelFormat._bBitMask == 0x000000FF )
                {
                    outImage._dxgiFormat = ( pHeader->_pixelFormat._aBitMask != 0 ) ? kDxgiFormatB8G8R8A8Unorm : kDxgiFormatB8G8R8X8Unorm;
                    outImage._bIsBgra    = SW_TRUE;
                }
                else if ( pHeader->_pixelFormat._rBitMask == 0x000000FF && pHeader->_pixelFormat._gBitMask == 0x0000FF00 &&
                          pHeader->_pixelFormat._bBitMask == 0x00FF0000 )
                {
                    outImage._dxgiFormat = kDxgiFormatR8G8B8A8Unorm;
                    outImage._bIsBgra    = SW_FALSE;
                }
            }
        }

        const bool bIsBc1To5  = ( 70 <= outImage._dxgiFormat && outImage._dxgiFormat <= 84 );
        const bool bIsBc6Or7  = ( 94 <= outImage._dxgiFormat && outImage._dxgiFormat <= 99 );
        outImage._bCompressed = ( bIsBc1To5 || bIsBc6Or7 ) ? SW_TRUE : SW_FALSE;

        if ( outImage._dxgiFormat == kDxgiFormatB8G8R8A8Unorm || outImage._dxgiFormat == kDxgiFormatB8G8R8X8Unorm ||
             outImage._dxgiFormat == kDxgiFormatB8G8R8A8UnormSrgb )
        {
            outImage._bIsBgra = SW_TRUE;
        }

        if ( bufferSize < dataOffset )
        {
            SW_LOG_ERROR( "DDS payload offset out of bounds." );
            return false;
        }

        const size_t payloadSize = bufferSize - dataOffset;
        outImage._bytes.resize( payloadSize );
        Memory::copy( outImage._bytes.data(), pBuffer + dataOffset, payloadSize );

        return true;
    }
} // namespace sw
