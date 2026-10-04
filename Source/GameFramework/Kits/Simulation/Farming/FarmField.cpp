#include "pch.h"

#include "GameFramework/Kits/Simulation/Farming/FarmField.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Simulation/Farming/CropCatalog.h"
#include "GameFramework/Utility/StateArchiveUtil.h"

namespace sw
{
    const utf8* toString( FarmActionResult result )
    {
        switch ( result )
        {
            case FarmActionResult::Done:
                return "Done";
            case FarmActionResult::OutOfBounds:
                return "OutOfBounds";
            case FarmActionResult::NotTilled:
                return "NotTilled";
            case FarmActionResult::AlreadyTilled:
                return "AlreadyTilled";
            case FarmActionResult::AlreadyWatered:
                return "AlreadyWatered";
            case FarmActionResult::Occupied:
                return "Occupied";
            case FarmActionResult::NoCrop:
                return "NoCrop";
            case FarmActionResult::NotReady:
                return "NotReady";
            case FarmActionResult::UnknownSeed:
                return "UnknownSeed";
            case FarmActionResult::OutOfSeason:
                return "OutOfSeason";
        }
        return "Unknown";
    }

    FarmField::FarmField()
        : _listTile{}
        , _pCatalog{ nullptr }
        , _width{ 0 }
        , _height{ 0 }
    {
    }

    void FarmField::initialize( int32 width, int32 height, const CropCatalog* pCatalog )
    {
        _width    = MathUtil::max( 0, width );
        _height   = MathUtil::max( 0, height );
        _pCatalog = pCatalog;
        _listTile.clear();
        _listTile.resize( static_cast<size_t>( _width ) * static_cast<size_t>( _height ) );
    }

    void FarmField::writeState( Archive& outArchive ) const
    {
        outArchive << _width;
        outArchive << _height;
        for ( const FarmTile& tile : _listTile )
        {
            StateArchiveUtil::writeName( outArchive, tile._cropId );
            outArchive << tile._growth;
            const uint8 flags = static_cast<uint8>( ( tile._bTilled != SW_FALSE ? 1u : 0u ) | ( tile._bWatered != SW_FALSE ? 2u : 0u ) |
                                                    ( tile._bReady != SW_FALSE ? 4u : 0u ) | ( tile._bWithered != SW_FALSE ? 8u : 0u ) );
            outArchive << flags;
        }
    }

    bool FarmField::readState( Archive& archive )
    {
        int32 width  = 0;
        int32 height = 0;
        archive >> width;
        archive >> height;
        if ( archive.isError() || width != _width || height != _height )
            return false;
        vector<FarmTile> listTile( _listTile.size() );
        for ( FarmTile& tile : listTile )
        {
            uint8 flags = 0;
            if ( StateArchiveUtil::readName( archive, tile._cropId ) == false )
                return false;
            archive >> tile._growth;
            archive >> flags;
            if ( archive.isError() || tile._growth < 0 )
                return false;
            tile._bTilled   = ( flags & 1u ) != 0 ? SW_TRUE : SW_FALSE;
            tile._bWatered  = ( flags & 2u ) != 0 ? SW_TRUE : SW_FALSE;
            tile._bReady    = ( flags & 4u ) != 0 ? SW_TRUE : SW_FALSE;
            tile._bWithered = ( flags & 8u ) != 0 ? SW_TRUE : SW_FALSE;
        }
        _listTile = std::move( listTile );
        return true;
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

    FarmActionResult FarmField::plant( int32 x, int32 y, const hashed_string& seedItem, FarmSeason season )
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
        if ( pCrop->growsIn( season ) == false )
            return FarmActionResult::OutOfSeason;

        pTile->_cropId    = pCrop->_id;
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
            pTile->_cropId    = hashed_string{};
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
            // 다시 자라는 작물은 남는다 — `_regrowDays` 만큼 다시 물을 받아 자라면 또 거둔다.
            pTile->_growth = MathUtil::max( 0, pCrop->_growthDays - pCrop->_regrowDays );
            pTile->_bReady = SW_FALSE;
        }
        else
        {
            pTile->_cropId = hashed_string{};
            pTile->_growth = 0;
            pTile->_bReady = SW_FALSE;
        }
        return FarmActionResult::Done;
    }

    void FarmField::advanceDay( FarmSeason newSeason, bool bRain )
    {
        for ( FarmTile& tile : _listTile )
        {
            const CropDef* pCrop = tile.hasCrop() ? findCropDef( tile ) : nullptr;
            if ( pCrop != nullptr && tile._bWithered == SW_FALSE )
            {
                // 1) 자람 — 어제 물을 받았으면 하루.
                if ( tile._bWatered == SW_TRUE && tile._bReady == SW_FALSE )
                {
                    ++tile._growth;
                    if ( tile._growth >= pCrop->_growthDays )
                        tile._bReady = SW_TRUE;
                }
                // 2) 시듦 — 새 계절에 자라지 않으면(다 자란 것도).
                if ( pCrop->growsIn( newSeason ) == false )
                {
                    tile._bWithered = SW_TRUE;
                    tile._bReady    = SW_FALSE;
                }
            }
            // 3) 물은 마르고, 비 오는 날은 갈아 둔 칸이 젖은 채 시작한다.
            tile._bWatered = ( bRain && tile._bTilled == SW_TRUE ) ? SW_TRUE : SW_FALSE;
        }
    }

    const FarmTile* FarmField::findTile( int32 x, int32 y ) const
    {
        const bool bInside = 0 <= x && x < _width && 0 <= y && y < _height;
        if ( bInside == false )
            return nullptr;
        return &_listTile[static_cast<size_t>( y ) * static_cast<size_t>( _width ) + static_cast<size_t>( x )];
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
        return _pCatalog != nullptr ? _pCatalog->findCrop( tile._cropId ) : nullptr;
    }
} // namespace sw
