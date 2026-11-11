#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/ConquestCatalog.h"

#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct ConquestCatalogInternal
        {
            /**
             * @brief 원소의 속성을 자원 이름과 양으로 읽습니다. 양은 0 이상의 숫자입니다.
             * @details 모든 속성이 자원이다. `StatBlock::addAttributes` 처럼 숫자가 아닌 값을 건너뛰면 `gold="1O"` 가 공짜 건물이 되고, 음수 비용은 쓸수록 돈을 준다.
             */
            [[nodiscard]] static bool readResources( const XMLNode& node, string_view sourceName, StatBlock& outStats )
            {
                outStats.clear();
                bool bValid = true;
                for ( XMLAttribute attribute = node.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    float32 amount = 0.0f;
                    if ( StringUtil::parseFloat( string_view( attribute.getValue() ), amount ) && amount >= 0.0f )
                    {
                        outStats.addValue( hashed_string( attribute.getName() ), amount );
                        continue;
                    }
                    SW_LOG_ERROR_ID( "ConquestCatalog.ResourceNeedsAmount", "%#: <%# %#=\"%#\"> needs an amount of 0 or more", sourceName, node.getName(), attribute.getName(), attribute.getValue() );
                    bValid = false;
                }
                return bValid;
            }

            /** @brief 자식 원소(`<Cost>` · `<Income>`)를 자원으로 읽습니다. 자식이 없으면 비어 있습니다. */
            [[nodiscard]] static bool readResourceChild( const XMLNode& node, const utf8* pChildName, string_view sourceName, StatBlock& outStats )
            {
                outStats.clear();
                const XMLNode child = node.findChild( pChildName );
                return child ? readResources( child, sourceName, outStats ) : true;
            }

            /** @brief "spearman:3,archer:2" 를 읽습니다. 개수를 빼면 하나이고, 깨진 개수는 오류입니다. */
            [[nodiscard]] static bool readGarrison( string_view text, vector<ConquestGarrisonDef>& outListGarrison, string_view sourceName, const hashed_string& siteID )
            {
                outListGarrison.clear();
                bool bAllValid = true;
                XMLReadUtil::forEachNamedNumber( text, ",; ", ConquestGarrisonDef{}._count, XMLReadUtil::MissingValue::UseDefault,
                                                 [&]( string_view token, string_view unitName, int32 count, bool bValid )
                {
                    if ( bValid == false || count < 0 )
                    {
                        SW_LOG_ERROR_ID( "ConquestCatalog.SiteBadGarrisonCount", "%#: site '%#' has a bad garrison count '%#'", sourceName, siteID.c_str(), token );
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

    ConquestCatalog::ConquestCatalog()
        : _buildingCatalog{}
        , _unitCatalog{}
        , _siteCatalog{}
        , _rules{}
    {
    }

    uint32 ConquestCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readProperties 가 남긴다).
        using Element                          = XMLCatalogElement<ConquestCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&ConquestCatalog::loadRules>( "Rules" ).uncounted(),
            Element::of<&ConquestCatalog::loadStart>( "Start" ).uncounted(),
            Element::of<&ConquestCatalog::loadUnit>( "Unit" ),
            Element::of<&ConquestCatalog::loadBuilding>( "Building" ),
            Element::of<&ConquestCatalog::loadSite>( "Site" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );

        // 서로 가리키는 id 는 데이터 표(`engine/settings/datacatalogreferences.xml`)가 정하고 카탈로그 읽기가 이 뒤에 본다(반격 병종 `waveUnit` 포함) — 쿠커도 같은 표로 본다.
        return result._loadedCount;
    }

    bool ConquestCatalog::loadRules( const XMLNode& node, string_view sourceName )
    {
        ConquestRules rules;
        if ( XMLFields::readProperties( node, rules, sourceName ) == false )
            return false;
        rules._startResources = _rules._startResources; // `<Start>` 는 따로 읽는다(적힌 순서와 상관없게)
        _rules                = rules;
        return true;
    }

    bool ConquestCatalog::loadStart( const XMLNode& node, string_view sourceName )
    {
        return ConquestCatalogInternal::readResources( node, sourceName, _rules._startResources );
    }

    bool ConquestCatalog::loadUnit( const XMLNode& node, string_view sourceName )
    {
        ConquestUnitDef unit;
        if ( XMLFields::readProperties( node, unit, sourceName ) == false )
            return false;
        return ConquestCatalogInternal::readResourceChild( node, "Cost", sourceName, unit._cost ) && GameDataXML::addUnique( unit, sourceName, _unitCatalog );
    }

    bool ConquestCatalog::loadBuilding( const XMLNode& node, string_view sourceName )
    {
        ConquestBuildingDef building;
        if ( XMLFields::readProperties( node, building, sourceName ) == false )
            return false;
        return ConquestCatalogInternal::readResourceChild( node, "Cost", sourceName, building._cost ) && GameDataXML::addUnique( building, sourceName, _buildingCatalog );
    }

    bool ConquestCatalog::loadSite( const XMLNode& node, string_view sourceName )
    {
        ConquestSiteDef site;
        bool            bValid = XMLFields::readProperties( node, site, sourceName, { "garrison" } );
        bValid                 = ConquestCatalogInternal::readGarrison( node.getAttributeText( "garrison" ), site._listGarrison, sourceName, site._id ) && bValid;
        // 판이 중립 병사를 만들지 않는다(`spawnUnit`) — 중립 거점의 주둔군은 말없이 사라진다.
        if ( site._owner == ConquestTeam::Neutral && site._listGarrison.empty() == false )
        {
            SW_LOG_ERROR_ID( "ConquestCatalog.NeutralSiteGarrison", "%#: neutral site '%#' cannot have a garrison", sourceName, site._id.c_str() );
            bValid = false;
        }
        if ( bValid == false )
            return false;
        return ConquestCatalogInternal::readResourceChild( node, "Income", sourceName, site._income ) && GameDataXML::addUnique( site, sourceName, _siteCatalog );
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<ConquestCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( ConquestCatalog, ConquestCatalog, SW_KIT_API, ConquestCatalog::kAssetType.withLoader( AssetTypeLoader::of<ConquestCatalog>( MemoryTag::Game ) ) );
} // namespace sw
