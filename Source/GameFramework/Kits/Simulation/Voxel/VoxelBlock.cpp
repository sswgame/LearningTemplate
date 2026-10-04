#include "pch.h"

#include "GameFramework/Kits/Simulation/Voxel/VoxelBlock.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Data/GameDataXml.h"

namespace sw
{
    SW_LOG_CALLER( "VoxelBlockCatalog" );

    namespace
    {
        struct VoxelBlockInternal
        {
            /** @brief 블록 번호의 상한입니다(`VoxelBlockIndex` 가 uint8). */
            static constexpr size_t kMaxBlockCount = 255;
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
        : _catalog{}
        , _atlasColumns{ 8 }
        , _atlasRows{ 8 }
        , _tileTexels{ 16 }
    {
    }

    VoxelBlockIndex VoxelBlockCatalog::addBlock( const VoxelBlockDef& block )
    {
        // 번호 = 자리 + 1. 같은 id 는 그 자리를 그대로 쓴다(저장된 월드의 번호가 바뀌지 않게).
        const int32 existingIndex = _catalog.findIndex( block._id );
        if ( existingIndex < 0 && _catalog.getCount() >= VoxelBlockInternal::kMaxBlockCount )
        {
            SW_LOG_WARNING( "Block catalog is full - '%#' skipped", block._id.c_str() );
            return kVoxelAirBlock;
        }
        VoxelBlockDef stored = block;
        stored._index        = static_cast<VoxelBlockIndex>( ( existingIndex >= 0 ? existingIndex : static_cast<int32>( _catalog.getCount() ) ) + 1 );
        return _catalog.add( stored ) >= 0 ? stored._index : kVoxelAirBlock;
    }

    const VoxelBlockDef* VoxelBlockCatalog::findBlock( VoxelBlockIndex index ) const
    {
        if ( index == kVoxelAirBlock || index > _catalog.getCount() )
            return nullptr;
        return &_catalog.getAt( static_cast<size_t>( index - 1 ) );
    }

    VoxelBlockIndex VoxelBlockCatalog::findBlockIndex( const hashed_string& blockId ) const
    {
        const int32 index = _catalog.findIndex( blockId );
        return index >= 0 ? static_cast<VoxelBlockIndex>( index + 1 ) : kVoxelAirBlock;
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
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
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
            block._color                                                   = GameDataXml::parseFloat4( node.getAttributeText( "color" ), block._color );
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
