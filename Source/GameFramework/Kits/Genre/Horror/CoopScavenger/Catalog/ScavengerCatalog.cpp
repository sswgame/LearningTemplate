#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Catalog/ScavengerCatalog.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

#include <algorithm>

namespace sw
{
    ScavengerCatalog::ScavengerCatalog()
        : _moonCatalog{}
        , _scrapCatalog{}
        , _listBuyRate{}
        , _quota{}
        , _day{}
        , _carry{}
        , _penalty{}
        , _facility{}
        , _terminalShopID{ ScavengerRootDef{}._terminalShopID }
        , _currency{ ScavengerRootDef{}._currency }
        , _crewHealth{ ScavengerRootDef{}._crewHealth }
    {
    }

    float32 ScavengerCatalog::computeBuyRate( int32 daysLeft ) const
    {
        if ( _listBuyRate.empty() )
            return 1.0f;
        if ( daysLeft <= _listBuyRate.front()._daysLeft )
            return _listBuyRate.front()._rate;
        for ( size_t index = 1; index < _listBuyRate.size(); ++index )
        {
            const ScavengerBuyRatePoint& upper = _listBuyRate[index];
            if ( daysLeft > upper._daysLeft )
                continue;
            const ScavengerBuyRatePoint& lower = _listBuyRate[index - 1];
            const float32                ratio = static_cast<float32>( daysLeft - lower._daysLeft ) / static_cast<float32>( upper._daysLeft - lower._daysLeft );
            return MathUtil::lerp( lower._rate, upper._rate, ratio );
        }
        return _listBuyRate.back()._rate;
    }

    uint32 ScavengerCatalog::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readProperties 가 남긴다).
        ScavengerRootDef rootDef;
        if ( root.readProperties( rootDef, sourceName ) )
        {
            _terminalShopID = rootDef._terminalShopID;
            _currency       = rootDef._currency;
            _crewHealth     = rootDef._crewHealth;
        }
        _listBuyRate.clear();
        using Element                          = XMLCatalogElement<ScavengerCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&ScavengerCatalog::_quota>( "Quota" ),
            Element::of<&ScavengerCatalog::_listBuyRate>( "BuyRate" ),
            Element::of<&ScavengerCatalog::_day>( "Day" ),
            Element::of<&ScavengerCatalog::_carry>( "Carry" ),
            Element::of<&ScavengerCatalog::_penalty>( "Penalty" ),
            Element::of<&ScavengerCatalog::_facility>( "Facility" ),
            Element::of<&ScavengerCatalog::_scrapCatalog>( "Scrap" ),
            Element::of<&ScavengerCatalog::loadMoon>( "Moon" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );

        // 매입률 점은 남은 날 순서로 둔다. 같은 날이 두 번이면 어느 값인지 모르므로 오류이고 뒤의 것을 버린다.
        VectorUtil::stableSort( _listBuyRate, []( const ScavengerBuyRatePoint& lhs, const ScavengerBuyRatePoint& rhs )
        { return lhs._daysLeft < rhs._daysLeft; } );
        for ( size_t index = 1; index < _listBuyRate.size(); ++index )
        {
            if ( _listBuyRate[index]._daysLeft == _listBuyRate[index - 1]._daysLeft )
                SW_LOG_ERROR_ID( "ScavengerCatalog.BuyRateDaysLeft", "%#: buy rate for %# days left is given twice - the later one is dropped", sourceName, _listBuyRate[index]._daysLeft );
        }
        _listBuyRate.erase( std::unique( _listBuyRate.begin(), _listBuyRate.end(),
                                         []( const ScavengerBuyRatePoint& lhs, const ScavengerBuyRatePoint& rhs )
        { return lhs._daysLeft == rhs._daysLeft; } ),
                            _listBuyRate.end() );

        if ( _moonCatalog.isEmpty() )
            SW_LOG_ERROR_ID( "ScavengerCatalog.NoMoonsNowhereLand", "%#: no moons - nowhere to land", sourceName );
        return result._loadedCount;
    }

    bool ScavengerCatalog::loadMoon( const XMLNode& node, string_view sourceName )
    {
        ScavengerMoonDef def;
        if ( XMLFields::readProperties( node, def, sourceName ) == false )
            return false;
        (void)_moonCatalog.add( def );
        return true;
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<ScavengerCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( ScavengerCatalog, ScavengerCatalog, SW_KIT_API, ScavengerCatalog::kAssetType.withLoader( AssetTypeLoader::of<ScavengerCatalog>( MemoryTag::Game ) ) );
} // namespace sw
