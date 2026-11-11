#include "pch.h"

#include "Games/Velmora/VelmoraRegionCatalog.h"

#include "Core/Log/Logger.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct VelmoraRegionCatalogInternal
        {
            /** @brief 자리 @p point 가 지역 경계 안인지 봅니다. 밖이면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool checkInside( const VelmoraRegionDef& region, const float2& point, const utf8* pWhat, string_view where )
            {
                if ( region.contains( point ) )
                    return true;
                SW_LOG_ERROR_ID( "Velmora.RegionPointOutside", "%#: %# (%#, %#) is outside the region bounds", where, pWhat, point._x, point._y );
                return false;
            }

            /** @brief `<Camp id at/>` · `<Beacon id at/>` 를 읽습니다. 한 지역에 하나씩입니다. */
            [[nodiscard]] static bool readSite( const XMLNode& node, string_view where, VelmoraSiteDef& outSite )
            {
                if ( outSite.isSet() )
                {
                    SW_LOG_ERROR_ID( "Velmora.RegionSiteRepeated", "%#: <%#> is written twice - a region has one", where, node.getName() );
                    return false;
                }
                VelmoraSiteAttributeDef def;
                if ( XMLFields::readProperties( node, def, where ) == false )
                    return false;
                outSite._id       = def._id;
                outSite._position = def._at;
                return true;
            }

            /** @brief `<Hall monster at/>` · `<Boss monster at/>` 한 줄을 읽습니다. */
            [[nodiscard]] static bool readSpawn( const XMLNode& node, string_view where, vector<ActionRoomSpawnDef>& outListSpawn )
            {
                VelmoraSpawnAttributeDef def;
                if ( XMLFields::readProperties( node, def, where ) == false )
                    return false;
                ActionRoomSpawnDef spawn;
                spawn._monsterID = def._monster;
                spawn._position  = def._at;
                outListSpawn.push_back( spawn );
                return true;
            }
        };
    } // namespace

    ActionRoomLayout VelmoraRegionDef::makeLayout() const
    {
        ActionRoomLayout layout;
        layout._listHallSpawn = _listHallSpawn;
        layout._listBossSpawn = _listBossSpawn;
        return layout;
    }

    const VelmoraRegionDef* VelmoraRegionCatalog::findAt( const float2& position ) const
    {
        return findIf( [&position]( const VelmoraRegionDef& region )
        { return region.contains( position ); } );
    }

    uint32 VelmoraRegionCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        using Element                          = XMLCatalogElement<VelmoraRegionCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&VelmoraRegionCatalog::loadRegion>( "Region" ),
        };
        return XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement )._loadedCount;
    }

    bool VelmoraRegionCatalog::loadRegion( const XMLNode& node, string_view sourceName )
    {
        using Internal  = VelmoraRegionCatalogInternal;
        const utf8* pID = XMLReadUtil::findRequiredID( node, sourceName );
        if ( pID == nullptr )
            return false;
        const string where = string( sourceName ) + " <Region id=\"" + pID + "\">";
        // 속성은 리플렉션 칸이다 — 빠진 경계 · 일어설 자리는 XMLFields.RequiredFieldMissing, 깨진 `x,z` 는 XMLFields.ValueUnreadable 로 이름과 함께 알린다.
        VelmoraRegionAttributeDef attribute;
        bool                      bValid = XMLFields::readProperties( node, attribute, where );
        VelmoraRegionDef          region;
        region._id      = attribute._id.empty() ? hashed_string( pID ) : attribute._id;
        region._name    = attribute._name.empty() ? string( pID ) : attribute._name;
        region._min     = attribute._min;
        region._max     = attribute._max;
        region._respawn = attribute._respawn;
        if ( bValid && ( region._min._x >= region._max._x || region._min._y >= region._max._y ) )
        {
            SW_LOG_ERROR_ID( "Velmora.RegionEmptyBounds", "%#: min must be below max on both axes", where );
            bValid = false;
        }
        bValid = bValid && Internal::checkInside( region, region._respawn, "respawn", where );
        if ( attribute._startQuest.empty() == false )
            XMLReadUtil::parseNameList( attribute._startQuest.c_str(), ", ", region._listStartQuest );

        for ( XMLNode child = node.findChild(); bValid && child; child = child.findNextSibling() )
        {
            const string_view name = child.getName() != nullptr ? string_view( child.getName() ) : string_view{};
            if ( name == "Camp" )
                bValid = Internal::readSite( child, where, region._camp ) && Internal::checkInside( region, region._camp._position, "camp", where );
            else if ( name == "Beacon" )
                bValid = Internal::readSite( child, where, region._beacon ) && Internal::checkInside( region, region._beacon._position, "beacon", where );
            else if ( name == "Hall" )
                bValid = Internal::readSpawn( child, where, region._listHallSpawn ) && Internal::checkInside( region, region._listHallSpawn.back()._position, "hall spawn", where );
            else if ( name == "Boss" )
                bValid = Internal::readSpawn( child, where, region._listBossSpawn ) && Internal::checkInside( region, region._listBossSpawn.back()._position, "boss spawn", where );
            else
            {
                SW_LOG_ERROR_ID( "Velmora.RegionUnknownChild", "%#: unknown child <%#>", where, name );
                bValid = false;
            }
        }
        // 한 자리는 한 지역이다 — 겹치면 어느 지역에 들어섰는지가 표 순서에 기댄다.
        const VelmoraRegionDef* pOverlap = findIf( [&region]( const VelmoraRegionDef& other )
        { return region._min._x < other._max._x && other._min._x < region._max._x && region._min._y < other._max._y && other._min._y < region._max._y; } );
        if ( bValid && pOverlap != nullptr )
        {
            SW_LOG_ERROR_ID( "Velmora.RegionOverlap", "%#: overlaps region '%#' - regions do not overlap", where, pOverlap->_id.c_str() );
            bValid = false;
        }
        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "Velmora.RegionSkipped", "%#: invalid fields - the region is skipped", where );
            return false;
        }
        return add( region ) >= 0;
    }
} // namespace sw

namespace sw
{
    /** @brief 벨모라 지역 표의 데이터 종류입니다. 쓰는 쪽은 공유 캐시의 한 벌을 받는다(게임 인스턴스가 게임 서비스로 건다). */
    SW_MODULE_SHARED_ASSET_CACHE( VelmoraRegions, VelmoraRegionCatalog, , VelmoraRegionCatalog::kAssetType.withLoader( AssetTypeLoader::of<VelmoraRegionCatalog>( MemoryTag::Game ) ) );
} // namespace sw
