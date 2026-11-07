#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Catalog/ScavengerCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"

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

    uint32 ScavengerCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        ScavengerRootDef rootDef;
        if ( XMLFields::readFields( root, rootDef, sourceName ) )
        {
            _terminalShopID = rootDef._terminalShopID;
            _currency       = rootDef._currency;
            _crewHealth     = rootDef._crewHealth;
        }
        _listBuyRate.clear();
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            if ( loadElement( node, name, sourceName ) )
                ++loadedCount;
        }

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

        // 위성이 가리키는 고철은 모두 읽은 뒤에 확인한다(위성이 고철보다 앞에 적혀도 된다).
        for ( const ScavengerMoonDef& moon : _moonCatalog.getAll() )
        {
            for ( const hashed_string& scrapID : moon._listScrap )
            {
                if ( _scrapCatalog.find( scrapID ) == nullptr )
                    SW_LOG_ERROR_ID( "ScavengerCatalog.MoonListsUnknownScrap", "%#: moon '%#' lists an unknown scrap '%#'", sourceName, moon._id.c_str(), scrapID.c_str() );
            }
        }
        if ( _moonCatalog.isEmpty() )
            SW_LOG_ERROR_ID( "ScavengerCatalog.NoMoonsNowhereLand", "%#: no moons - nowhere to land", sourceName );
        return loadedCount;
    }

    bool ScavengerCatalog::loadElement( const XMLNode& node, string_view name, string_view sourceName )
    {
        if ( XMLReadUtil::isElement( name, "Quota" ) )
            return GameDataXML::readSetting( node, sourceName, _quota );
        if ( XMLReadUtil::isElement( name, "BuyRate" ) )
        {
            ScavengerBuyRatePoint point;
            if ( XMLFields::readFields( node, point, sourceName ) == false )
                return false;
            _listBuyRate.push_back( point );
            return true;
        }
        if ( XMLReadUtil::isElement( name, "Day" ) )
            return GameDataXML::readSetting( node, sourceName, _day );
        if ( XMLReadUtil::isElement( name, "Carry" ) )
            return GameDataXML::readSetting( node, sourceName, _carry );
        if ( XMLReadUtil::isElement( name, "Penalty" ) )
            return GameDataXML::readSetting( node, sourceName, _penalty );
        if ( XMLReadUtil::isElement( name, "Facility" ) )
            return GameDataXML::readSetting( node, sourceName, _facility );
        if ( XMLReadUtil::isElement( name, "Scrap" ) )
        {
            ScavengerScrapDef def;
            if ( XMLFields::readFields( node, def, sourceName ) == false )
                return false;
            (void)_scrapCatalog.add( def );
            return true;
        }
        if ( XMLReadUtil::isElement( name, "Moon" ) )
            return loadMoon( node, sourceName );
        SW_LOG_ERROR_ID( "ScavengerCatalog.UnknownElementSkipped", "%#: unknown element <%#> - skipped", sourceName, name );
        return false;
    }

    bool ScavengerCatalog::loadMoon( const XMLNode& node, string_view sourceName )
    {
        ScavengerMoonDef def;
        if ( XMLFields::readFields( node, def, sourceName ) == false )
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
