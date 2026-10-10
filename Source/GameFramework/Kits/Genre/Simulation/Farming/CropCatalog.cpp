#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/Farming/CropCatalog.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "CropCatalog" );

    namespace
    {
        struct CropCatalogInternal
        {
            /** @brief "Spring, Summer" 를 이름 목록으로 읽습니다. 알려 둔 계절이 있으면 모르는 이름은 알리고 뺀다. */
            static void parseSeasons( string_view text, const vector<hashed_string>& listKnown, string_view sourceName, const utf8* pCropId,
                                      vector<hashed_string>& outListSeason )
            {
                outListSeason.clear();
                GameDataXML::forEachToken( text, ",;| ", [&]( string_view token )
                {
                    const hashed_string season( token );
                    const bool          bKnown = listKnown.empty() || std::find( listKnown.begin(), listKnown.end(), season ) != listKnown.end();
                    if ( bKnown )
                        outListSeason.push_back( season );
                    else
                        SW_LOG_WARNING( "%#: crop '%#' names an unknown season '%#'", sourceName, pCropId, season.c_str() );
                } );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    CropCatalog::CropCatalog()
        : _catalog{}
        , _mapSeedIndex{}
        , _mapProduceIndex{}
        , _listKnownSeason{}
    {
    }

    void CropCatalog::addCrop( const CropDef& crop )
    {
        if ( _catalog.add( crop ) >= 0 )
            rebuildItemIndex();
    }

    const CropDef* CropCatalog::findCropBySeed( const hashed_string& seedItem ) const
    {
        const auto mapIter = _mapSeedIndex.find( seedItem );
        return mapIter != _mapSeedIndex.end() ? &_catalog.getAt( mapIter->second ) : nullptr;
    }

    int32 CropCatalog::findSellPrice( const hashed_string& itemId ) const
    {
        const auto produceIter = _mapProduceIndex.find( itemId );
        if ( produceIter != _mapProduceIndex.end() )
            return _catalog.getAt( produceIter->second )._sellPrice;
        const auto seedIter = _mapSeedIndex.find( itemId );
        if ( seedIter != _mapSeedIndex.end() )
            return _catalog.getAt( seedIter->second )._seedPrice / 2;
        return 0;
    }

    void CropCatalog::rebuildItemIndex()
    {
        // 같은 아이템을 두 작물이 쓰면 앞 작물이 이긴다(읽은 순서) — 예전 선형 조회와 같은 답.
        _mapSeedIndex.clear();
        _mapProduceIndex.clear();
        for ( uint32 cropIndex = 0; cropIndex < static_cast<uint32>( _catalog.getCount() ); ++cropIndex )
        {
            const CropDef& crop = _catalog.getAt( cropIndex );
            if ( _mapSeedIndex.find( crop._seedItem ) == _mapSeedIndex.end() )
                _mapSeedIndex[crop._seedItem] = cropIndex;
            if ( _mapProduceIndex.find( crop._produceItem ) == _mapProduceIndex.end() )
                _mapProduceIndex[crop._produceItem] = cropIndex;
        }
    }

    uint32 CropCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild( "Crop" ); node; node = node.findNextSibling( "Crop" ) )
        {
            const utf8* pId = GameDataXML::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            CropDef crop;
            crop._id             = hashed_string( pId );
            const utf8* pName    = node.findAttribute( "name" );
            crop._name           = pName != nullptr ? pName : pId;
            const utf8* pSeed    = node.findAttribute( "seed" );
            const utf8* pProduce = node.findAttribute( "produce" );
            crop._seedItem       = hashed_string( pSeed != nullptr ? pSeed : pId );
            crop._produceItem    = hashed_string( pProduce != nullptr ? pProduce : pId );
            crop._growthDays     = MathUtil::max( 1, node.getAttributeInt( "days", crop._growthDays ) );
            crop._regrowDays     = MathUtil::max( 0, node.getAttributeInt( "regrow", crop._regrowDays ) );
            crop._seedPrice      = MathUtil::max( 0, node.getAttributeInt( "seedPrice", crop._seedPrice ) );
            crop._sellPrice      = MathUtil::max( 0, node.getAttributeInt( "sellPrice", crop._sellPrice ) );
            crop._harvestCount   = MathUtil::max( 1, node.getAttributeInt( "harvest", crop._harvestCount ) );
            const utf8* pSeasons = node.findAttribute( "seasons" );
            if ( pSeasons != nullptr )
                CropCatalogInternal::parseSeasons( string_view( pSeasons ), _listKnownSeason, sourceName, pId, crop._listSeason );
            else
                crop._listSeason.push_back( hashed_string( "Spring" ) );
            if ( crop._listSeason.empty() )
            {
                SW_LOG_WARNING( "%#: crop '%#' grows in no season - skipped", sourceName, pId );
                continue;
            }
            if ( crop._regrowDays > crop._growthDays )
                SW_LOG_WARNING( "%#: crop '%#' regrows slower (%#) than it first grows (%#)", sourceName, pId, crop._regrowDays, crop._growthDays );
            (void)_catalog.add( crop ); // 색인은 한 번에 — 작물마다 다시 짓지 않는다
            ++loadedCount;
        }
        rebuildItemIndex();
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Crop> entries", sourceName );
        return loadedCount;
    }

    void CropCatalog::fillItemCatalog( ItemCatalog& inoutItems, int32 maxStack ) const
    {
        for ( const CropDef& crop : getCrops() )
        {
            for ( const hashed_string& itemId : { crop._seedItem, crop._produceItem } )
            {
                if ( itemId.empty() || inoutItems.findItem( itemId ) != nullptr )
                    continue;
                ItemDef item;
                item._id       = itemId;
                item._maxStack = maxStack;
                inoutItems.addItem( item );
            }
        }
    }
} // namespace sw
