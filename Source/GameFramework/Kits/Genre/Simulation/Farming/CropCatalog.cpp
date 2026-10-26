#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/Farming/CropCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "CropCatalog" );

    namespace
    {
        /** @brief "Spring, Summer" 를 이름 목록으로 읽습니다. 알려 둔 계절이 있으면 모르는 이름은 오류이고 false 를 반환합니다. */
        [[nodiscard]] bool parseSeasons( string_view text, const vector<hashed_string>& listKnown, string_view sourceName, const utf8* pCropID,
                                         vector<hashed_string>& outListSeason )
        {
            outListSeason.clear();
            bool bValid = true;
            StringUtil::forEachToken( text, ",;| ", [&]( string_view token )
            {
                const hashed_string season( token );
                const bool          bKnown = listKnown.empty() || VectorUtil::contains( listKnown, season );
                if ( bKnown )
                {
                    outListSeason.push_back( season );
                }
                else
                {
                    SW_LOG_ERROR( "%#: crop '%#' names an unknown season '%#'", sourceName, pCropID, season.c_str() );
                    bValid = false;
                }
            } );
            return bValid;
        }
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

    int32 CropCatalog::findSellPrice( const hashed_string& itemID ) const
    {
        const auto produceIter = _mapProduceIndex.find( itemID );
        if ( produceIter != _mapProduceIndex.end() )
            return _catalog.getAt( produceIter->second )._sellPrice;
        const auto seedIter = _mapSeedIndex.find( itemID );
        if ( seedIter != _mapSeedIndex.end() )
            return _catalog.getAt( seedIter->second )._seedPrice / 2;
        return 0;
    }

    void CropCatalog::rebuildItemIndex()
    {
        // 같은 아이템을 두 작물이 쓰면 앞 작물이 이긴다(읽은 순서).
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
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            if ( StringUtil::equals( node.getName(), "Crop" ) == false )
                SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, node.getName() );
            else if ( loadCrop( node, sourceName ) )
                ++loadedCount;
        }
        rebuildItemIndex(); // 인덱스는 한 번에 만들고, 작물마다 다시 만들지 않는다
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Crop> entries", sourceName );
        return loadedCount;
    }

    bool CropCatalog::loadCrop( const XMLNode& node, string_view sourceName )
    {
        CropDef crop;
        bool    bValid = GameDataXML::readFields( node, crop, sourceName, { "seasons" } );
        // 자라는 계절은 반드시 적는다 — 적지 않은 작물을 봄 작물로 읽지 않는다.
        const utf8* pSeasons = node.findAttribute( "seasons" );
        if ( pSeasons == nullptr )
        {
            SW_LOG_ERROR( "%#: crop '%#' needs seasons=\"...\"", sourceName, crop._id.c_str() );
            bValid = false;
        }
        else if ( parseSeasons( string_view( pSeasons ), _listKnownSeason, sourceName, crop._id.c_str(), crop._listSeason ) == false )
        {
            bValid = false;
        }
        else if ( crop._listSeason.empty() )
        {
            SW_LOG_ERROR( "%#: crop '%#' grows in no season", sourceName, crop._id.c_str() );
            bValid = false;
        }
        if ( crop._regrowDays > crop._growthDays )
            SW_LOG_WARNING( "%#: crop '%#' regrows slower (%#) than it first grows (%#)", sourceName, crop._id.c_str(), crop._regrowDays, crop._growthDays );
        if ( bValid )
            (void)_catalog.add( crop ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return bValid;
    }

    void CropCatalog::fillItemCatalog( ItemCatalog& inoutItems, int32 maxStack ) const
    {
        for ( const CropDef& crop : getCrops() )
        {
            for ( const hashed_string& itemID : { crop._seedItem, crop._produceItem } )
            {
                if ( itemID.empty() || inoutItems.find( itemID ) != nullptr )
                    continue;
                ItemDef item;
                item._id       = itemID;
                item._maxStack = maxStack;
                inoutItems.add( item );
            }
        }
    }
} // namespace sw
