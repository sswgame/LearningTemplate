#include "pch.h"

#include "GameFramework/Kits/Voxel/VoxelBlock.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    SW_LOG_CALLER( "VoxelBlockCatalog" );

    namespace
    {
        struct VoxelBlockInternal
        {
            /** @brief 블록 번호의 상한입니다(`VoxelBlockIndex` 가 uint8). */
            static constexpr size_t kMaxBlockCount = 255;

            /** @brief "r g b a" (쉼표도 된다)를 읽습니다. 빠진 성분은 @p fallback 의 것입니다. */
            static float4 parseColor( string_view text, const float4& fallback )
            {
                float32 arrValue[4] = { fallback._x, fallback._y, fallback._z, fallback._w };
                size_t  tokenStart  = 0;
                int32   valueIndex  = 0;
                while ( tokenStart < text.size() && valueIndex < 4 )
                {
                    size_t tokenEnd = text.find_first_of( ", ", tokenStart );
                    if ( tokenEnd == string_view::npos )
                        tokenEnd = text.size();
                    const string_view token = text.substr( tokenStart, tokenEnd - tokenStart );
                    if ( token.empty() == false )
                    {
                        float32 value = 0.0f;
                        if ( StringUtil::parseFloat( token, value ) )
                            arrValue[valueIndex] = value;
                        ++valueIndex;
                    }
                    tokenStart = tokenEnd + 1;
                }
                return float4{ arrValue[0], arrValue[1], arrValue[2], arrValue[3] };
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    VoxelCoord getVoxelFaceOffset( VoxelFace face )
    {
        switch ( face )
        {
            case VoxelFace::PositiveX:
                return VoxelCoord{ 1, 0, 0 };
            case VoxelFace::NegativeX:
                return VoxelCoord{ -1, 0, 0 };
            case VoxelFace::PositiveY:
                return VoxelCoord{ 0, 1, 0 };
            case VoxelFace::NegativeY:
                return VoxelCoord{ 0, -1, 0 };
            case VoxelFace::PositiveZ:
                return VoxelCoord{ 0, 0, 1 };
            case VoxelFace::NegativeZ:
                return VoxelCoord{ 0, 0, -1 };
        }
        return VoxelCoord{};
    }

    VoxelBlockCatalog::VoxelBlockCatalog()
        : _listBlock{}
        , _atlasColumns{ 8 }
        , _atlasRows{ 8 }
        , _tileTexels{ 16 }
    {
    }

    bool VoxelBlockCatalog::loadFromResource( string_view path )
    {
        XmlDocument doc;
        string      absPath;
        if ( doc.loadPath( path, &absPath ) == false )
        {
            SW_LOG_WARNING( "Failed to read block catalog %#", path );
            return false;
        }
        const XmlNode root = doc.getRoot( "BlockCatalog" );
        if ( root.isValid() == false )
        {
            SW_LOG_WARNING( "Missing <BlockCatalog> root in %#", absPath );
            return false;
        }
        return loadRoot( root, absPath ) > 0;
    }

    bool VoxelBlockCatalog::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        XmlDocument doc;
        if ( doc.parse( xmlText, sourceName ) == false )
        {
            SW_LOG_WARNING( "Failed to parse block catalog text %#", sourceName );
            return false;
        }
        const XmlNode root = doc.getRoot( "BlockCatalog" );
        if ( root.isValid() == false )
        {
            SW_LOG_WARNING( "Missing <BlockCatalog> root in %#", sourceName );
            return false;
        }
        return loadRoot( root, sourceName ) > 0;
    }

    VoxelBlockIndex VoxelBlockCatalog::addBlock( const VoxelBlockDef& block )
    {
        for ( VoxelBlockDef& existing : _listBlock )
        {
            if ( existing._id == block._id )
            {
                const VoxelBlockIndex index = existing._index;
                existing                    = block;
                existing._index             = index;
                return index;
            }
        }
        if ( _listBlock.size() >= VoxelBlockInternal::kMaxBlockCount )
        {
            SW_LOG_WARNING( "Block catalog is full - '%#' skipped", block._id.c_str() );
            return kVoxelAirBlock;
        }
        _listBlock.push_back( block );
        _listBlock.back()._index = static_cast<VoxelBlockIndex>( _listBlock.size() );
        return _listBlock.back()._index;
    }

    const VoxelBlockDef* VoxelBlockCatalog::findBlock( VoxelBlockIndex index ) const
    {
        if ( index == kVoxelAirBlock || index > _listBlock.size() )
            return nullptr;
        return &_listBlock[index - 1];
    }

    VoxelBlockIndex VoxelBlockCatalog::findBlockIndex( const hashed_string& blockId ) const
    {
        for ( const VoxelBlockDef& block : _listBlock )
        {
            if ( block._id == blockId )
                return block._index;
        }
        return kVoxelAirBlock;
    }

    bool VoxelBlockCatalog::isSolid( VoxelBlockIndex index ) const
    {
        const VoxelBlockDef* pBlock = findBlock( index );
        return pBlock != nullptr && pBlock->_bSolid != SW_FALSE;
    }

    bool VoxelBlockCatalog::isOpaque( VoxelBlockIndex index ) const
    {
        const VoxelBlockDef* pBlock = findBlock( index );
        return pBlock != nullptr && pBlock->_bOpaque != SW_FALSE;
    }

    void VoxelBlockCatalog::computeTileUv( int32 tile, float2& outMin, float2& outMax ) const
    {
        const int32   tileCount = _atlasColumns * _atlasRows;
        const int32   safeTile  = ( tile >= 0 && tile < tileCount ) ? tile : 0;
        const int32   column    = safeTile % _atlasColumns;
        const int32   row       = safeTile / _atlasColumns;
        const float32 cellU     = 1.0f / static_cast<float32>( _atlasColumns );
        const float32 cellV     = 1.0f / static_cast<float32>( _atlasRows );
        const float32 insetU    = cellU * 0.5f / static_cast<float32>( _tileTexels );
        const float32 insetV    = cellV * 0.5f / static_cast<float32>( _tileTexels );
        outMin                  = float2{ cellU * static_cast<float32>( column ) + insetU, cellV * static_cast<float32>( row ) + insetV };
        outMax                  = float2{ cellU * static_cast<float32>( column + 1 ) - insetU, cellV * static_cast<float32>( row + 1 ) - insetV };
    }

    uint32 VoxelBlockCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        _atlasColumns      = MathUtil::max( 1, root.getAttributeInt( "atlasColumns", _atlasColumns ) );
        _atlasRows         = MathUtil::max( 1, root.getAttributeInt( "atlasRows", _atlasRows ) );
        _tileTexels        = MathUtil::max( 1, root.getAttributeInt( "tileTexels", _tileTexels ) );
        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Block" ); node; node = node.findNextSibling( "Block" ) )
        {
            const utf8* pId = node.findAttribute( "id" );
            if ( StringUtil::isNullOrEmpty( pId ) )
            {
                SW_LOG_WARNING( "%#: <Block> without an id - skipped", sourceName );
                continue;
            }
            VoxelBlockDef block;
            block._id                                                      = hashed_string( pId );
            const utf8* pName                                              = node.findAttribute( "name" );
            block._name                                                    = pName != nullptr ? pName : pId;
            const int32 baseTile                                           = node.getAttributeInt( "tile", 0 );
            const int32 sideTile                                           = node.getAttributeInt( "side", baseTile );
            block._arrFaceTile[static_cast<int32>( VoxelFace::PositiveX )] = sideTile;
            block._arrFaceTile[static_cast<int32>( VoxelFace::NegativeX )] = sideTile;
            block._arrFaceTile[static_cast<int32>( VoxelFace::PositiveZ )] = sideTile;
            block._arrFaceTile[static_cast<int32>( VoxelFace::NegativeZ )] = sideTile;
            block._arrFaceTile[static_cast<int32>( VoxelFace::PositiveY )] = node.getAttributeInt( "top", baseTile );
            block._arrFaceTile[static_cast<int32>( VoxelFace::NegativeY )] = node.getAttributeInt( "bottom", baseTile );
            block._color                                                   = VoxelBlockInternal::parseColor( node.getAttributeText( "color" ), block._color );
            block._hardness                                                = MathUtil::max( 0.0f, node.getAttributeFloat( "hardness", block._hardness ) );
            block._bSolid                                                  = node.getAttributeBool( "solid", true ) ? SW_TRUE : SW_FALSE;
            block._bOpaque                                                 = node.getAttributeBool( "opaque", true ) ? SW_TRUE : SW_FALSE;
            block._bBreakable                                              = node.getAttributeBool( "breakable", true ) ? SW_TRUE : SW_FALSE;
            if ( addBlock( block ) != kVoxelAirBlock )
                ++loadedCount;
        }
        return loadedCount;
    }
} // namespace sw
