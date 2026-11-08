#include "pch.h"

#include "GameFramework/Kits/Feature/World/Voxel/Catalog/VoxelBlock.h"

#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct VoxelBlockInternal
        {
            /** @brief 블록 번호의 상한입니다(`VoxelBlockIndex` 가 uint8). */
            static constexpr size_t kMaxBlockCount = 255;

            /** @brief 면 칸 속성 @p pName 을 정수로 읽습니다. 없으면 false 이고, 있는데 정수가 아니면 오류를 남기고 @p inoutbValid 를 false 로 바꿉니다. */
            [[nodiscard]] static bool readTile( const XMLNode& node, const utf8* pName, string_view sourceName, int32& outTile, bool& inoutbValid )
            {
                const utf8* pText = node.findAttribute( pName );
                if ( pText == nullptr )
                    return false;
                if ( StringUtil::parseInt( pText, outTile ) == false )
                {
                    SW_LOG_ERROR_ID( "VoxelBlockCatalog.BlockNotInteger", "%#: block '%#' has %#=\"%#\" that is not an integer", sourceName, node.findAttribute( "id" ), pName, pText );
                    inoutbValid = false;
                }
                return true;
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
        : _catalog{}
        , _atlasColumns{ VoxelBlockCatalogRootDef{}._atlasColumns }
        , _atlasRows{ VoxelBlockCatalogRootDef{}._atlasRows }
        , _tileTexels{ VoxelBlockCatalogRootDef{}._tileTexels }
    {
    }

    VoxelBlockIndex VoxelBlockCatalog::addBlock( const VoxelBlockDef& block )
    {
        // 블록 번호는 인덱스 + 1 이다. 저장된 월드가 이 번호를 쓰므로 겹친 id 는 카탈로그가 거절한다(앞 블록의 번호와 값을 지킨다).
        if ( _catalog.getCount() >= VoxelBlockInternal::kMaxBlockCount )
        {
            SW_LOG_WARNING_ID( "VoxelBlockCatalog.BlockCatalogFullSkipped", "Block catalog is full - '%#' skipped", block._id.c_str() );
            return kVoxelAirBlock;
        }
        VoxelBlockDef stored = block;
        stored._index        = static_cast<VoxelBlockIndex>( _catalog.getCount() + 1 );
        return _catalog.add( stored ) >= 0 ? stored._index : kVoxelAirBlock;
    }

    const VoxelBlockDef* VoxelBlockCatalog::findBlock( VoxelBlockIndex index ) const
    {
        if ( index == kVoxelAirBlock || index > _catalog.getCount() )
            return nullptr;
        return &_catalog.getAt( static_cast<size_t>( index - 1 ) );
    }

    VoxelBlockIndex VoxelBlockCatalog::findBlockIndex( const hashed_string& blockID ) const
    {
        const optional<int32> index = _catalog.findIndex( blockID );
        return index.has_value() ? static_cast<VoxelBlockIndex>( *index + 1 ) : kVoxelAirBlock;
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

    void VoxelBlockCatalog::computeTileUV( int32 tile, float2& outMin, float2& outMax ) const
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

    uint32 VoxelBlockCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        VoxelBlockCatalogRootDef rootDef;
        if ( XMLFields::readFields( root, rootDef, sourceName ) == false )
            return 0;
        _atlasColumns                          = rootDef._atlasColumns;
        _atlasRows                             = rootDef._atlasRows;
        _tileTexels                            = rootDef._tileTexels;
        using Element                          = XMLCatalogElement<VoxelBlockCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&VoxelBlockCatalog::loadBlock>( "Block" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        // 얻는 블록은 같은 카탈로그의 블록이어야 한다 — 모든 블록을 읽은 뒤에 본다(앞 블록이 뒤 블록을 가리킬 수 있다).
        bool bDropsKnown = true;
        for ( const VoxelBlockDef& block : getBlocks() )
        {
            if ( block._dropID.empty() || findBlockIndex( block._dropID ) != kVoxelAirBlock )
                continue;
            SW_LOG_ERROR_ID( "VoxelBlockCatalog.BlockDropsWhichNot", "%#: block '%#' drops '%#', which is not a block in this catalog", sourceName, block._id.c_str(), block._dropID.c_str() );
            bDropsKnown = false;
        }
        return bDropsKnown ? result._loadedCount : 0;
    }

    bool VoxelBlockCatalog::loadBlock( const XMLNode& node, string_view sourceName )
    {
        using Internal = VoxelBlockInternal;
        VoxelBlockDef block;
        bool          bValid = XMLFields::readFields( node, block, sourceName, { "tile", "top", "bottom", "side" } );
        // 면은 `tile` 하나 또는 `top` · `bottom` · `side` 셋이다. 빠진 면을 다른 면 값으로 채우지 않는다.
        int32      tile    = 0;
        int32      top     = 0;
        int32      bottom  = 0;
        int32      side    = 0;
        const bool bTile   = Internal::readTile( node, "tile", sourceName, tile, bValid );
        const bool bTop    = Internal::readTile( node, "top", sourceName, top, bValid );
        const bool bBottom = Internal::readTile( node, "bottom", sourceName, bottom, bValid );
        const bool bSide   = Internal::readTile( node, "side", sourceName, side, bValid );
        if ( bTile && ( bTop || bBottom || bSide ) == false )
        {
            top    = tile;
            bottom = tile;
            side   = tile;
        }
        else if ( bTile || ( bTop && bBottom && bSide ) == false )
        {
            SW_LOG_ERROR_ID( "VoxelBlockCatalog.BlockNeedsEitherTile", "%#: block '%#' needs either tile=\"...\" alone or all of top, bottom and side", sourceName, block._id.c_str() );
            bValid = false;
        }
        const int32 tileCount = _atlasColumns * _atlasRows;
        for ( const int32 faceTile : { top, bottom, side } )
        {
            if ( faceTile < 0 || faceTile >= tileCount )
            {
                SW_LOG_ERROR_ID( "VoxelBlockCatalog.BlockUsesTileOutside", "%#: block '%#' uses tile %# outside the %#x%# atlas", sourceName, block._id.c_str(), faceTile, _atlasColumns, _atlasRows );
                bValid = false;
            }
        }
        block._arrFaceTile[static_cast<int32>( VoxelFace::PositiveX )] = side;
        block._arrFaceTile[static_cast<int32>( VoxelFace::NegativeX )] = side;
        block._arrFaceTile[static_cast<int32>( VoxelFace::PositiveZ )] = side;
        block._arrFaceTile[static_cast<int32>( VoxelFace::NegativeZ )] = side;
        block._arrFaceTile[static_cast<int32>( VoxelFace::PositiveY )] = top;
        block._arrFaceTile[static_cast<int32>( VoxelFace::NegativeY )] = bottom;
        return bValid && addBlock( block ) != kVoxelAirBlock;
    }
} // namespace sw

namespace sw
{
    /** @brief `VoxelBlockCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_MODULE_SHARED_ASSET_CACHE( VoxelBlocks, VoxelBlockCatalog, SW_KIT_API, AssetTypeDef::makeDataFile( VoxelBlockCatalog::kDataKind, AssetPathRule{ "game/", {}, "/data/blocks.xml" }, AssetTypeLoader::of<VoxelBlockCatalog>( MemoryTag::Game ) ) );
} // namespace sw
