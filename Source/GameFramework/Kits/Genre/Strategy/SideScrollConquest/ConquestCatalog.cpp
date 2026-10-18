#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/ConquestCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/EnumNameTable.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw
{
    namespace
    {
        struct ConquestCatalogInternal
        {
            static constexpr const utf8*                 kArrTeamName[] = { "Neutral", "Player", "Enemy" };
            static constexpr EnumNameTable<ConquestTeam> kTeamTable{ kArrTeamName };
            static_assert( kTeamTable.getCount() == static_cast<size_t>( ConquestTeam::Enemy ) + 1, "team names must cover every ConquestTeam" );

            /** @brief 자식 원소(`<Cost>` · `<Income>`)의 속성을 자원 이름과 양으로 읽습니다. 자식이 없으면 비어 있습니다. */
            static void loadStatChild( const XMLNode& node, const utf8* pChildName, StatBlock& outStats )
            {
                outStats.clear();
                if ( const XMLNode child = node.findChild( pChildName ) )
                    (void)outStats.loadFromAttributes( child ); // 반환값은 읽은 개수이다. 숫자가 아닌 속성은 건너뛰면 되므로 무시한다
            }

            /** @brief "spearman:3,archer:2" 를 읽습니다. 개수를 빼면 하나이고, 깨진 개수는 오류입니다. */
            [[nodiscard]] static bool readGarrison( string_view text, vector<ConquestGarrisonDef>& outListGarrison, string_view sourceName, const hashed_string& siteID )
            {
                outListGarrison.clear();
                bool bAllValid = true;
                GameDataXML::forEachNamedNumber( text, ",; ", ConquestGarrisonDef{}._count, GameDataXML::MissingValue::UseDefault,
                                                 [&]( string_view token, string_view unitName, int32 count, bool bValid )
                {
                    if ( bValid == false || count < 0 )
                    {
                        SW_LOG_ERROR( "%#: site '%#' has a bad garrison count '%#'", sourceName, siteID.c_str(), token );
                        bAllValid = false;
                        return;
                    }
                    ConquestGarrisonDef garrison;
                    garrison._unitID = hashed_string( unitName );
                    garrison._count  = count;
                    outListGarrison.push_back( garrison );
                } );
                return bAllValid;
            }
        };
    } // namespace

    SW_LOG_CALLER( "ConquestCatalog" );

    bool parseConquestTeam( string_view text, ConquestTeam& outTeam )
    {
        return ConquestCatalogInternal::kTeamTable.parse( text, outTeam );
    }

    ConquestCatalog::ConquestCatalog()
        : _buildingCatalog{}
        , _unitCatalog{}
        , _siteCatalog{}
        , _rules{}
    {
    }

    uint32 ConquestCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name    = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            const bool        bLoaded = loadElement( node, name, sourceName );
            const bool        bDef    = name == "Unit" || name == "Building" || name == "Site";
            if ( bLoaded && bDef )
                ++loadedCount;
        }

        // 서로 가리키는 id 는 모두 읽은 뒤에 확인한다(건물 · 거점이 병종보다 앞에 적혀도 된다).
        for ( const ConquestBuildingDef& building : _buildingCatalog.getAll() )
        {
            for ( const hashed_string& unitID : building._listTrainable )
            {
                if ( _unitCatalog.find( unitID ) == nullptr )
                    SW_LOG_ERROR( "%#: building '%#' trains an unknown unit '%#'", sourceName, building._id.c_str(), unitID.c_str() );
            }
        }
        for ( const ConquestSiteDef& site : _siteCatalog.getAll() )
        {
            for ( const ConquestGarrisonDef& garrison : site._listGarrison )
            {
                if ( _unitCatalog.find( garrison._unitID ) == nullptr )
                    SW_LOG_ERROR( "%#: site '%#' garrisons an unknown unit '%#'", sourceName, site._id.c_str(), garrison._unitID.c_str() );
            }
        }
        if ( _rules._waveUnit.empty() == false && _unitCatalog.find( _rules._waveUnit ) == nullptr )
            SW_LOG_ERROR( "%#: waveUnit '%#' is not a unit - no counter-attack waves", sourceName, _rules._waveUnit.c_str() );
        return loadedCount;
    }

    bool ConquestCatalog::loadElement( const XMLNode& node, string_view name, string_view sourceName )
    {
        if ( name == "Rules" )
        {
            ConquestRules rules;
            if ( GameDataXML::readFields( node, rules, sourceName ) == false )
                return false;
            rules._startResources = _rules._startResources; // `<Start>` 는 따로 읽는다(적힌 순서와 상관없게)
            _rules                = rules;
            return true;
        }
        if ( name == "Start" )
        {
            _rules._startResources.clear();
            (void)_rules._startResources.loadFromAttributes( node ); // 반환값은 읽은 개수이다. 숫자가 아닌 속성은 건너뛰면 되므로 무시한다
            return true;
        }
        if ( name == "Unit" )
            return loadUnit( node, sourceName );
        if ( name == "Building" )
            return loadBuilding( node, sourceName );
        if ( name == "Site" )
            return loadSite( node, sourceName );
        SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, name );
        return false;
    }

    bool ConquestCatalog::loadUnit( const XMLNode& node, string_view sourceName )
    {
        ConquestUnitDef unit;
        if ( GameDataXML::readFields( node, unit, sourceName ) == false )
            return false;
        ConquestCatalogInternal::loadStatChild( node, "Cost", unit._cost );
        (void)_unitCatalog.add( unit ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return true;
    }

    bool ConquestCatalog::loadBuilding( const XMLNode& node, string_view sourceName )
    {
        ConquestBuildingDef building;
        if ( GameDataXML::readFields( node, building, sourceName, { "trains" } ) == false )
            return false;
        GameDataXML::parseNameList( node.getAttributeText( "trains" ), ",; ", building._listTrainable );
        ConquestCatalogInternal::loadStatChild( node, "Cost", building._cost );
        (void)_buildingCatalog.add( building ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return true;
    }

    bool ConquestCatalog::loadSite( const XMLNode& node, string_view sourceName )
    {
        ConquestSiteDef site;
        bool            bValid = GameDataXML::readFields( node, site, sourceName, { "garrison" } );
        bValid                 = ConquestCatalogInternal::readGarrison( node.getAttributeText( "garrison" ), site._listGarrison, sourceName, site._id ) && bValid;
        if ( bValid == false )
            return false;
        ConquestCatalogInternal::loadStatChild( node, "Income", site._income );
        (void)_siteCatalog.add( site ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return true;
    }
} // namespace sw
