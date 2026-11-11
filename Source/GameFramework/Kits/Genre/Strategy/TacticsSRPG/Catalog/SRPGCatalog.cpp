#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Catalog/SRPGCatalog.h"

#include "Core/Foundation/Expected.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct SRPGCatalogInternal
        {
            static constexpr const utf8* kArrPilotStatName[kSRPGPilotStatCount]   = { "shooting", "melee", "reaction", "awaken", "defense" };
            static constexpr const utf8* kArrPilotGrowthName[kSRPGPilotStatCount] = { "growShooting", "growMelee", "growReaction", "growAwaken", "growDefense" };

            /** @brief 토큰 하나를 이동 비용으로 읽습니다. `-` 는 들어갈 수 없음(-1)이고, 숫자는 0 이상이어야 합니다. */
            [[nodiscard]] static bool parseMoveCost( string_view token, int32& outCost )
            {
                if ( StringUtil::equals( token, string_view( "-" ) ) )
                {
                    outCost = -1;
                    return true;
                }
                int32 value = 0;
                if ( StringUtil::parseInt( token, value ) == false || value < 0 )
                    return false;
                outCost = value;
                return true;
            }

            /**
             * @brief "1 1 - 2" 처럼 이동 타입 순서(지상 · 공중 · 우주 · 수중)의 값 네 개를 읽습니다. 적지 않으면 Def 의 초기값입니다.
             * @details 적었으면 네 값이 모두 읽혀야 합니다. 모자라거나 깨진 값은 오류이고 표를 바꾸지 않습니다.
             */
            template <typename TParse>
            [[nodiscard]] static bool readFour( const XMLNode& node, const utf8* pName, string_view sourceName, TParse parse,
                                                int32 ( &inoutArrValue )[kSRPGMoveTypeCount] )
            {
                const string_view text = node.getAttributeText( pName );
                if ( text.empty() )
                    return true;
                int32 arrValue[kSRPGMoveTypeCount] = {};
                int32 count                        = 0;
                bool  bValid                       = true;
                StringUtil::forEachToken( text, ",; \t", [&]( string_view token )
                {
                    const bool bRoom = count < kSRPGMoveTypeCount;
                    if ( bRoom == false || parse( token, arrValue[count] ) == false )
                        bValid = false;
                    ++count;
                } );
                if ( bValid == false || count != kSRPGMoveTypeCount )
                {
                    SW_LOG_ERROR_ID( "SRPGCatalog.NeedsFourValuesGround", "%#: <%# %#=\"%#\"> needs four values (Ground Air Space Water)", sourceName, node.getName(), pName, text );
                    return false;
                }
                std::copy( arrValue, arrValue + kSRPGMoveTypeCount, inoutArrValue );
                return true;
            }

            /** @brief 파일럿 능력치 다섯 칸을 이름으로 읽습니다. 적지 않은 칸은 Def 의 초기값이고, 깨진 값은 오류입니다. */
            [[nodiscard]] static bool readPilotStats( const XMLNode& node, const utf8* const ( &arrName )[kSRPGPilotStatCount], int32 ( &inoutArrValue )[kSRPGPilotStatCount] )
            {
                bool bValid = true;
                for ( int32 stat = 0; stat < kSRPGPilotStatCount; ++stat )
                {
                    optional<int32> value;
                    if ( node.readOptionalAttribute( arrName[stat], value ) == false )
                        bValid = false;
                    else if ( value.has_value() )
                        inoutArrValue[stat] = *value;
                }
                return bValid;
            }
        };
    } // namespace

    int32 SRPGPilotDef::computeStat( SRPGPilotStat stat, int32 level ) const
    {
        const size_t index = static_cast<size_t>( stat );
        if ( index >= static_cast<size_t>( kSRPGPilotStatCount ) )
            return 0;
        return _arrStat[index] + _arrGrowth[index] * MathUtil::max( 0, level - 1 );
    }

    SRPGCatalog::SRPGCatalog()
        : _terrainCatalog{}
        , _weaponCatalog{}
        , _unitCatalog{}
        , _pilotCatalog{}
        , _pilotCurve{}
        , _unitCurve{}
    {
    }

    bool SRPGCatalog::parseAptitude( string_view token, int32& outAptitude )
    {
        struct NamedAptitude
        {
            const utf8* _pName;
            int32       _percent;
        };
        static constexpr NamedAptitude kArrNamed[] = {
            {"S", 120},
            {"A", 100},
            {"B",  90},
            {"C",  80},
            {"D",  60},
            {"-",   0},
        };
        for ( const NamedAptitude& named : kArrNamed )
        {
            if ( StringUtil::equals( token, string_view( named._pName ) ) )
            {
                outAptitude = named._percent;
                return true;
            }
        }
        int32 value = 0;
        if ( StringUtil::parseInt( token, value ) == false || value < 0 )
            return false;
        outAptitude = value;
        return true;
    }

    uint32 SRPGCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readProperties 가 남긴다).
        using Element                          = XMLCatalogElement<SRPGCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&SRPGCatalog::_pilotCurve>( "PilotCurve" ).uncounted(),
            Element::of<&SRPGCatalog::_unitCurve>( "UnitCurve" ).uncounted(),
            Element::of<&SRPGCatalog::loadTerrain>( "Terrain" ),
            Element::of<&SRPGCatalog::loadWeapon>( "Weapon" ),
            Element::of<&SRPGCatalog::loadUnit>( "Unit" ),
            Element::of<&SRPGCatalog::loadPilot>( "Pilot" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );

        // 서로 가리키는 id 는 데이터 표(`engine/settings/datacatalogreferences.xml`)가 정하고 카탈로그 읽기가 이 뒤에 본다 — 쿠커도 같은 표로 본다.
        return result._loadedCount;
    }

    bool SRPGCatalog::loadTerrain( const XMLNode& node, string_view sourceName )
    {
        SRPGTerrainDef def;
        bool           bValid = XMLFields::readProperties( node, def, sourceName, { "cost" } );
        bValid                = SRPGCatalogInternal::readFour( node, "cost", sourceName, &SRPGCatalogInternal::parseMoveCost, def._arrMoveCost ) && bValid;
        if ( bValid )
            (void)_terrainCatalog.add( def );
        return bValid;
    }

    bool SRPGCatalog::loadWeapon( const XMLNode& node, string_view sourceName )
    {
        SRPGWeaponDef def;
        bool          bValid = XMLFields::readProperties( node, def, sourceName, { "pattern" } );
        if ( def.isMap() )
        {
            // MAP 병기의 범위는 "x,y x,y ..." 이고 반드시 적는다(기준 셀 하나로 물러나지 않는다).
            StringUtil::forEachToken( node.getAttributeText( "pattern" ), " ;\t", [&]( string_view token )
            {
                float2 cell{};
                if ( XMLReadUtil::parseFloat2( token, cell ) )
                    def._listMapOffset.push_back( int2{ static_cast<int32>( cell._x ), static_cast<int32>( cell._y ) } );
                else
                {
                    SW_LOG_ERROR_ID( "SRPGCatalog.MAPWeaponBadPattern", "%#: MAP weapon '%#' has a bad pattern cell '%#' (x,y)", sourceName, def._id.c_str(), token );
                    bValid = false;
                }
            } );
            if ( def._listMapOffset.empty() )
            {
                SW_LOG_ERROR_ID( "SRPGCatalog.MAPWeaponNeedsPattern", "%#: MAP weapon '%#' needs pattern=\"x,y ...\"", sourceName, def._id.c_str() );
                bValid = false;
            }
            def._bCounter = false; // MAP 병기로는 반격하지 않는다(규칙이다)
        }
        else if ( node.getAttributeText( "pattern" ).empty() == false )
        {
            SW_LOG_ERROR_ID( "SRPGCatalog.WeaponPatternButNo", "%#: weapon '%#' has a pattern but no map=\"Self|Target\"", sourceName, def._id.c_str() );
            bValid = false;
        }
        if ( bValid )
            (void)_weaponCatalog.add( def );
        return bValid;
    }

    bool SRPGCatalog::loadUnit( const XMLNode& node, string_view sourceName )
    {
        using Internal = SRPGCatalogInternal;
        SRPGUnitDef def;
        bool        bValid = XMLFields::readProperties( node, def, sourceName, { "aptitude", "developsTo" } );
        bValid             = Internal::readFour( node, "aptitude", sourceName, &SRPGCatalog::parseAptitude, def._arrAptitude ) && bValid;
        XMLReadUtil::forEachNamedNumber( node.getAttributeText( "developsTo" ), ",; \t", SRPGDevelopTarget{}._requiredLevel, XMLReadUtil::MissingValue::UseDefault,
                                         [&]( string_view token, string_view unitName, int32 requiredLevel, bool bLevel )
        {
            if ( bLevel == false || requiredLevel < 1 )
            {
                SW_LOG_ERROR_ID( "SRPGCatalog.UnitBadDevelopLevel", "%#: unit '%#' has a bad develop level in '%#'", sourceName, def._id.c_str(), token );
                bValid = false;
                return;
            }
            SRPGDevelopTarget target;
            target._unitID        = hashed_string( unitName );
            target._requiredLevel = requiredLevel;
            def._listDevelop.push_back( target );
        } );
        if ( bValid )
            (void)_unitCatalog.add( def );
        return bValid;
    }

    bool SRPGCatalog::loadPilot( const XMLNode& node, string_view sourceName )
    {
        using Internal = SRPGCatalogInternal;
        SRPGPilotDef def;
        bool         bValid = XMLFields::readProperties( node, def, sourceName,
                                                         { "shooting", "melee", "reaction", "awaken", "defense", "growShooting", "growMelee", "growReaction",
                                                           "growAwaken", "growDefense" } );
        bValid              = Internal::readPilotStats( node, Internal::kArrPilotStatName, def._arrStat ) && bValid;
        bValid              = Internal::readPilotStats( node, Internal::kArrPilotGrowthName, def._arrGrowth ) && bValid;
        if ( bValid )
            (void)_pilotCatalog.add( def );
        return bValid;
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<SRPGCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( SRPGCatalog, SRPGCatalog, SW_KIT_API, SRPGCatalog::kAssetType.withLoader( AssetTypeLoader::of<SRPGCatalog>( MemoryTag::Game ) ) );
} // namespace sw
