#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/Farming/FarmField.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Simulation/Farming/CropCatalog.h"

namespace sw
{
    FarmField::FarmField()
        : _listTile{}
        , _pCatalog{ nullptr }
        , _topology{}
        , _land{}
    {
    }

    void FarmField::initialize( int32 width, int32 height, const CropCatalog* pCatalog )
    {
        _topology = GridTopology{ width, height }; // 음수는 0 타일로 처리한다(GridTopology 생성자)
        _pCatalog = pCatalog;
        _listTile.clear();
        _listTile.resize( static_cast<size_t>( _topology.getCellCount() ) );
    }

    bool FarmField::bindLand( LandRegistry* pLand, const int2& origin )
    {
        LandBinding land;
        land.bind( pLand, origin, hashed_string( "Farming" ) );
        if ( _topology.getCellCount() > 0 && land.claimRect( 0, 0, _topology._width - 1, _topology._height - 1, false ) == false )
            return false;
        _land = land;
        return true;
    }

    void FarmField::serializeState( StateIO& io )
    {
        // 밭 크기는 설정이 정한다 — 저장된 크기로 읽어 같은 좌표에 옮기고, 줄어 밖에 남은 밭이 있으면 거절한다(감사 A-6). 칸마다 이름(4) + 자람(4) + 깃발(1)
        const GridTopology savedTopology = _topology.serializeSize( io );
        _topology.serializeCells( io, savedTopology, _listTile, kTileStateMinBytes, [this]( StateIO& tileIO, FarmTile& tile )
        { serializeTile( tileIO, tile ); },
                                  []( const FarmTile& tile )
        { return tile.hasCrop() == false && tile._bTilled == SW_FALSE; } );
    }

    void FarmField::serializeTile( StateIO& io, FarmTile& tile ) const
    {
        if ( io.isReading() )
        {
            tile = FarmTile{};
        }
        io.value( tile._cropID );
        io.value( tile._growth );
        uint8 flags = static_cast<uint8>( ( tile._bTilled != SW_FALSE ? 1u : 0u ) | ( tile._bWatered != SW_FALSE ? 2u : 0u ) |
                                          ( tile._bReady != SW_FALSE ? 4u : 0u ) | ( tile._bWithered != SW_FALSE ? 8u : 0u ) );
        io.value( flags );
        if ( io.check( flags <= 15u && 0 <= tile._growth ) == false )
            return;
        if ( io.isReading() )
        {
            tile._bTilled   = ( flags & 1u ) != 0 ? SW_TRUE : SW_FALSE;
            tile._bWatered  = ( flags & 2u ) != 0 ? SW_TRUE : SW_FALSE;
            tile._bReady    = ( flags & 4u ) != 0 ? SW_TRUE : SW_FALSE;
            tile._bWithered = ( flags & 8u ) != 0 ? SW_TRUE : SW_FALSE;
            if ( io.check( isTileValid( tile ) ) == false )
                return;
        }
    }

    bool FarmField::isTileValid( const FarmTile& tile ) const
    {
        // 물은 간 땅에만 준다(비도 같다).
        if ( tile._bWatered == SW_TRUE && tile._bTilled == SW_FALSE )
            return false;
        if ( tile.hasCrop() == false )
            return tile._growth == 0 && tile._bReady == SW_FALSE && tile._bWithered == SW_FALSE;
        // 모르는 작물은 `advanceDay` 가 건너뛰고 `harvest` 가 거절해 타일이 영영 막힌다. 다 자란 표시는 자란 날로 번다.
        const CropDef* pCrop = findCropDef( tile );
        if ( pCrop == nullptr || tile._bTilled == SW_FALSE || tile._growth > pCrop->_growthDays )
            return false;
        return tile._bReady == SW_FALSE || ( tile._bWithered == SW_FALSE && tile._growth >= pCrop->_growthDays );
    }

    FarmActionResult FarmField::till( int32 x, int32 y )
    {
        FarmTile* pTile = findTileMutable( x, y );
        if ( pTile == nullptr )
            return FarmActionResult::OutOfBounds;
        if ( pTile->hasCrop() )
            return FarmActionResult::Occupied;
        if ( pTile->_bTilled == SW_TRUE )
            return FarmActionResult::AlreadyTilled;
        pTile->_bTilled = SW_TRUE;
        return FarmActionResult::Done;
    }

    FarmActionResult FarmField::water( int32 x, int32 y )
    {
        FarmTile* pTile = findTileMutable( x, y );
        if ( pTile == nullptr )
            return FarmActionResult::OutOfBounds;
        if ( pTile->_bTilled == SW_FALSE )
            return FarmActionResult::NotTilled;
        if ( pTile->_bWatered == SW_TRUE )
            return FarmActionResult::AlreadyWatered;
        pTile->_bWatered = SW_TRUE;
        return FarmActionResult::Done;
    }

    FarmActionResult FarmField::plant( int32 x, int32 y, const hashed_string& seedItem, const hashed_string& season )
    {
        FarmTile* pTile = findTileMutable( x, y );
        if ( pTile == nullptr )
            return FarmActionResult::OutOfBounds;
        if ( pTile->_bTilled == SW_FALSE )
            return FarmActionResult::NotTilled;
        if ( pTile->hasCrop() )
            return FarmActionResult::Occupied;
        const CropDef* pCrop = _pCatalog != nullptr ? _pCatalog->findCropBySeed( seedItem ) : nullptr;
        if ( pCrop == nullptr )
            return FarmActionResult::UnknownSeed;
        if ( pCrop->canGrowIn( season ) == false )
            return FarmActionResult::OutOfSeason;

        pTile->_cropID    = pCrop->_id;
        pTile->_growth    = 0;
        pTile->_bReady    = SW_FALSE;
        pTile->_bWithered = SW_FALSE;
        return FarmActionResult::Done;
    }

    FarmActionResult FarmField::harvest( int32 x, int32 y, hashed_string& outProduceItem, int32& outCount )
    {
        outProduceItem  = hashed_string{};
        outCount        = 0;
        FarmTile* pTile = findTileMutable( x, y );
        if ( pTile == nullptr )
            return FarmActionResult::OutOfBounds;
        if ( pTile->hasCrop() == false )
            return FarmActionResult::NoCrop;

        // 시든 작물은 치우기만 한다.
        if ( pTile->_bWithered == SW_TRUE )
        {
            pTile->_cropID    = hashed_string{};
            pTile->_growth    = 0;
            pTile->_bReady    = SW_FALSE;
            pTile->_bWithered = SW_FALSE;
            return FarmActionResult::Done;
        }
        if ( pTile->_bReady == SW_FALSE )
            return FarmActionResult::NotReady;

        const CropDef* pCrop = findCropDef( *pTile );
        if ( pCrop == nullptr )
            return FarmActionResult::UnknownSeed;
        outProduceItem = pCrop->_produceItem;
        outCount       = pCrop->_harvestCount;

        if ( pCrop->_regrowDays > 0 )
        {
            // 다시 자라는 작물은 남는다. `_regrowDays` 만큼 다시 물을 받아 자라면 또 거둘 수 있다.
            pTile->_growth = MathUtil::max( 0, pCrop->_growthDays - pCrop->_regrowDays );
            pTile->_bReady = SW_FALSE;
        }
        else
        {
            pTile->_cropID = hashed_string{};
            pTile->_growth = 0;
            pTile->_bReady = SW_FALSE;
        }
        return FarmActionResult::Done;
    }

    void FarmField::advanceDay( const hashed_string& newSeason, bool bRain )
    {
        for ( FarmTile& tile : _listTile )
        {
            const CropDef* pCrop = tile.hasCrop() ? findCropDef( tile ) : nullptr;
            if ( pCrop != nullptr && tile._bWithered == SW_FALSE )
            {
                // 1) 자람: 어제 물을 받았으면 하루 자란다.
                if ( tile._bWatered == SW_TRUE && tile._bReady == SW_FALSE )
                {
                    ++tile._growth;
                    if ( tile._growth >= pCrop->_growthDays )
                        tile._bReady = SW_TRUE;
                }
                // 2) 시듦: 새 계절에 자라지 않으면 시든다(다 자란 것도 마찬가지이다).
                if ( pCrop->canGrowIn( newSeason ) == false )
                {
                    tile._bWithered = SW_TRUE;
                    tile._bReady    = SW_FALSE;
                }
            }
            // 3) 물은 마르고, 비 오는 날은 갈아 둔 타일이 젖은 채 시작한다.
            tile._bWatered = ( bRain && tile._bTilled == SW_TRUE ) ? SW_TRUE : SW_FALSE;
        }
    }

    const FarmTile* FarmField::findTile( int32 x, int32 y ) const
    {
        if ( _topology.isInside( x, y ) == false )
            return nullptr;
        return &_listTile[static_cast<size_t>( _topology.toIndex( x, y ) )];
    }

    uint32 FarmField::getCropCount() const
    {
        uint32 cropCount = 0;
        for ( const FarmTile& tile : _listTile )
        {
            if ( tile.hasCrop() )
                ++cropCount;
        }
        return cropCount;
    }

    uint32 FarmField::getReadyCount() const
    {
        uint32 readyCount = 0;
        for ( const FarmTile& tile : _listTile )
        {
            if ( tile._bReady == SW_TRUE )
                ++readyCount;
        }
        return readyCount;
    }

    float32 FarmField::computeGrowthRatio( int32 x, int32 y ) const
    {
        const FarmTile* pTile = findTile( x, y );
        const CropDef*  pCrop = pTile != nullptr && pTile->hasCrop() ? findCropDef( *pTile ) : nullptr;
        if ( pCrop == nullptr )
            return 0.0f;
        if ( pTile->_bReady == SW_TRUE )
            return 1.0f;
        return MathUtil::clamp( static_cast<float32>( pTile->_growth ) / static_cast<float32>( MathUtil::max( 1, pCrop->_growthDays ) ), 0.0f, 1.0f );
    }

    FarmTile* FarmField::findTileMutable( int32 x, int32 y )
    {
        return const_cast<FarmTile*>( findTile( x, y ) );
    }

    const CropDef* FarmField::findCropDef( const FarmTile& tile ) const
    {
        return _pCatalog != nullptr ? _pCatalog->findCrop( tile._cropID ) : nullptr;
    }
} // namespace sw
