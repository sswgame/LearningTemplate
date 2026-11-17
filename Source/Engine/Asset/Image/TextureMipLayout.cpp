#include "pch.h"

#include "Engine/Asset/Image/TextureMipLayout.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Image/DDSLoader.h"

namespace sw
{
    expected<TextureMipLayout> TextureMipLayout::parse( const uint8* pHeader, size_t headerSize, uint64 fileSize )
    {
        const expected<DDSHeaderInfo> header = DDSLoader::parseHeader( pHeader, headerSize );
        if ( header.has_value() == false )
            return unexpected( header.error() );
        const DDSHeaderInfo& info = header.value();
        if ( info._layerCount != 1 || info._depth != 1 )
        {
            SW_LOG_ERROR_ID( "TextureMipLayout.NotPlanar", "Mip layout is for a single 2D texture, but the DDS has %# layers and depth %#", info._layerCount, info._depth );
            return unexpected( Error{ ErrorKind::Unsupported, "mip layout needs a single 2D texture" } );
        }
        const uint64 expectedSize = static_cast<uint64>( info._dataOffset ) + info.computePayloadBytes();
        if ( fileSize != expectedSize )
        {
            SW_LOG_ERROR_ID( "TextureMipLayout.SizeMismatch", "DDS file is %# bytes but its header needs %# bytes (truncated or wrong mip count)", fileSize, expectedSize );
            return unexpected( Error{ ErrorKind::InvalidArgument, "DDS file size does not match its header" } );
        }
        TextureMipLayout layout;
        layout._dxgiFormat = info._dxgiFormat;
        uint64 offset      = info._dataOffset;
        for ( uint32 mip = 0; mip < info._mipCount; ++mip )
        {
            MipRange entry;
            entry._offset = offset;
            entry._size   = static_cast<uint32>( info.computeMipBytes( mip ) );
            entry._width  = MathUtil::max( info._width >> mip, 1u );
            entry._height = MathUtil::max( info._height >> mip, 1u );
            offset += entry._size;
            layout._listMip.push_back( entry );
        }
        return layout;
    }

    void TextureMipLayout::getRange( uint32 firstMip, uint32 mipCount, uint64& outOffset, uint64& outSize ) const
    {
        SW_ASSERT( mipCount > 0 && firstMip + mipCount <= _listMip.size() );
        const MipRange& first = _listMip[firstMip];
        const MipRange& last  = _listMip[firstMip + mipCount - 1];
        outOffset             = first._offset;
        outSize               = last._offset + last._size - first._offset;
    }
} // namespace sw
