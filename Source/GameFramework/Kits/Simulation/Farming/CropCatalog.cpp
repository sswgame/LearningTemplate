#include "pch.h"

#include "GameFramework/Kits/Simulation/Farming/CropCatalog.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Data/GameDataXml.h"

namespace sw
{
    SW_LOG_CALLER( "CropCatalog" );

    namespace
    {
        struct CropCatalogInternal
        {
            /** @brief "Spring,Fall" 같은 목록을 계절 마스크로 읽습니다. 모르는 이름은 경고하고 건너뜁니다. */
            static uint8 parseSeasonMask( string_view text, string_view sourceName, const utf8* pCropId )
            {
                (void)sourceName;
                (void)pCropId;
                uint8 mask = 0;
                GameDataXml::forEachToken( text, ",;| ", [&]( string_view token )
                {
                    FarmSeason season{ FarmSeason::Spring };
                    if ( parseFarmSeason( token, season ) )
                        mask = static_cast<uint8>( mask | makeFarmSeasonBit( season ) );
                    else
                        SW_LOG_WARNING( "%#: crop '%#' has an unknown season in '%#'", sourceName, pCropId, text );
                } );
                return mask;
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
    {
    }

    bool CropCatalog::loadFromResource( string_view path )
    {
        return GameDataXml::loadFile( *this, &CropCatalog::loadRoot, path, "CropCatalog" );
    }

    bool CropCatalog::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        return GameDataXml::loadText( *this, &CropCatalog::loadRoot, xmlText, sourceName, "CropCatalog" );
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

    uint32 CropCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Crop" ); node; node = node.findNextSibling( "Crop" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
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
            crop._seasonMask     = pSeasons != nullptr ? CropCatalogInternal::parseSeasonMask( string_view( pSeasons ), sourceName, pId )
                                                       : makeFarmSeasonBit( FarmSeason::Spring );
            if ( crop._seasonMask == 0 )
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
} // namespace sw
