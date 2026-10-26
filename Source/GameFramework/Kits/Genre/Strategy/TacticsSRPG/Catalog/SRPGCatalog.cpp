#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Catalog/SRPGCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

#include <optional>

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
                    SW_LOG_ERROR( "%#: <%# %#=\"%#\"> needs four values (Ground Air Space Water)", sourceName, node.getName(), pName, text );
                    return false;
                }
                std::copy( arrValue, arrValue + kSRPGMoveTypeCount, inoutArrValue );
                return true;
            }

            /** @brief `size` 를 S · M · L · LL 또는 0..3 으로 읽습니다. 적지 않으면 Def 의 초기값이고, 모르는 값은 오류입니다. */
            [[nodiscard]] static bool readSize( const XMLNode& node, string_view sourceName, int32& inoutSize )
            {
                static constexpr const utf8* kArrSizeName[] = { "S", "M", "L", "LL" };
                const string_view            text           = node.getAttributeText( "size" );
                if ( text.empty() )
                    return true;
                for ( int32 index = 0; index < static_cast<int32>( std::size( kArrSizeName ) ); ++index )
                {
                    if ( StringUtil::equals( text, string_view( kArrSizeName[index] ), true ) )
                    {
                        inoutSize = index;
                        return true;
                    }
                }
                int32      value  = 0;
                const bool bValue = StringUtil::parseInt( text, value ) && 0 <= value && value <= 3;
                if ( bValue == false )
                {
                    SW_LOG_ERROR( "%#: <%# size=\"%#\"> needs S, M, L, LL or 0..3", sourceName, node.getName(), text );
                    return false;
                }
                inoutSize = value;
                return true;
            }

            /** @brief 파일럿 능력치 다섯 칸을 이름으로 읽습니다. 적지 않은 칸은 Def 의 초기값이고, 깨진 값은 오류입니다. */
            [[nodiscard]] static bool readPilotStats( const XMLNode& node, const utf8* const ( &arrName )[kSRPGPilotStatCount], int32 ( &inoutArrValue )[kSRPGPilotStatCount] )
            {
                bool bValid = true;
                for ( int32 stat = 0; stat < kSRPGPilotStatCount; ++stat )
                {
                    std::optional<int32> value;
                    if ( node.readOptionalAttribute( arrName[stat], value ) == false )
                        bValid = false;
                    else if ( value.has_value() )
                        inoutArrValue[stat] = *value;
                }
                return bValid;
            }
        };
    } // namespace

    SW_LOG_CALLER( "SRPGCatalog" );

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
            if ( StringUtil::equals( token, string_view( named._pName ), true ) )
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
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name    = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            const bool        bLoaded = loadElement( node, name, sourceName );
            const bool        bCurve  = name == "PilotCurve" || name == "UnitCurve";
            if ( bLoaded && bCurve == false )
                ++loadedCount;
        }

        // 다른 정의를 가리키는 id 는 모두 읽은 뒤에 확인한다.
        for ( const SRPGUnitDef& def : _unitCatalog.getAll() )
        {
            for ( const hashed_string& weaponID : def._listWeaponID )
            {
                if ( _weaponCatalog.find( weaponID ) == nullptr )
                    SW_LOG_ERROR( "%#: unit '%#' has unknown weapon '%#'", sourceName, def._id.c_str(), weaponID.c_str() );
            }
            for ( const SRPGDevelopTarget& target : def._listDevelop )
            {
                if ( _unitCatalog.find( target._unitID ) == nullptr )
                    SW_LOG_ERROR( "%#: unit '%#' develops into unknown '%#'", sourceName, def._id.c_str(), target._unitID.c_str() );
            }
        }
        return loadedCount;
    }

    bool SRPGCatalog::loadElement( const XMLNode& node, string_view name, string_view sourceName )
    {
        if ( name == "PilotCurve" )
            return _pilotCurve.loadFromNode( node, sourceName );
        if ( name == "UnitCurve" )
            return _unitCurve.loadFromNode( node, sourceName );
        if ( name == "Terrain" )
            return loadTerrain( node, sourceName );
        if ( name == "Weapon" )
            return loadWeapon( node, sourceName );
        if ( name == "Unit" )
            return loadUnit( node, sourceName );
        if ( name == "Pilot" )
            return loadPilot( node, sourceName );
        SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, name );
        return false;
    }

    bool SRPGCatalog::loadTerrain( const XMLNode& node, string_view sourceName )
    {
        SRPGTerrainDef def;
        bool           bValid = GameDataXML::readFields( node, def, sourceName, { "cost" } );
        bValid                = SRPGCatalogInternal::readFour( node, "cost", sourceName, &SRPGCatalogInternal::parseMoveCost, def._arrMoveCost ) && bValid;
        if ( bValid )
            (void)_terrainCatalog.add( def ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return bValid;
    }

    bool SRPGCatalog::loadWeapon( const XMLNode& node, string_view sourceName )
    {
        SRPGWeaponDef def;
        bool          bValid = GameDataXML::readFields( node, def, sourceName, { "pattern" } );
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
                    SW_LOG_ERROR( "%#: MAP weapon '%#' has a bad pattern cell '%#' (x,y)", sourceName, def._id.c_str(), token );
                    bValid = false;
                }
            } );
            if ( def._listMapOffset.empty() )
            {
                SW_LOG_ERROR( "%#: MAP weapon '%#' needs pattern=\"x,y ...\"", sourceName, def._id.c_str() );
                bValid = false;
            }
            def._bCounter = false; // MAP 병기로는 반격하지 않는다(규칙이다)
        }
        else if ( node.getAttributeText( "pattern" ).empty() == false )
        {
            SW_LOG_ERROR( "%#: weapon '%#' has a pattern but no map=\"Self|Target\"", sourceName, def._id.c_str() );
            bValid = false;
        }
        if ( bValid )
            (void)_weaponCatalog.add( def ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return bValid;
    }

    bool SRPGCatalog::loadUnit( const XMLNode& node, string_view sourceName )
    {
        using Internal = SRPGCatalogInternal;
        SRPGUnitDef def;
        bool        bValid = GameDataXML::readFields( node, def, sourceName, { "size", "aptitude", "weapons", "developsTo" } );
        bValid             = Internal::readSize( node, sourceName, def._size ) && bValid;
        bValid             = Internal::readFour( node, "aptitude", sourceName, &SRPGCatalog::parseAptitude, def._arrAptitude ) && bValid;
        XMLReadUtil::parseNameList( node.getAttributeText( "weapons" ), ",; \t", def._listWeaponID );
        XMLReadUtil::forEachNamedNumber( node.getAttributeText( "developsTo" ), ",; \t", SRPGDevelopTarget{}._requiredLevel, XMLReadUtil::MissingValue::UseDefault,
                                         [&]( string_view token, string_view unitName, int32 requiredLevel, bool bLevel )
        {
            if ( bLevel == false || requiredLevel < 1 )
            {
                SW_LOG_ERROR( "%#: unit '%#' has a bad develop level in '%#'", sourceName, def._id.c_str(), token );
                bValid = false;
                return;
            }
            SRPGDevelopTarget target;
            target._unitID        = hashed_string( unitName );
            target._requiredLevel = requiredLevel;
            def._listDevelop.push_back( target );
        } );
        if ( bValid )
            (void)_unitCatalog.add( def ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return bValid;
    }

    bool SRPGCatalog::loadPilot( const XMLNode& node, string_view sourceName )
    {
        using Internal = SRPGCatalogInternal;
        SRPGPilotDef def;
        bool         bValid = GameDataXML::readFields( node, def, sourceName,
                                                       { "shooting", "melee", "reaction", "awaken", "defense", "growShooting", "growMelee", "growReaction",
                                                         "growAwaken", "growDefense" } );
        bValid              = Internal::readPilotStats( node, Internal::kArrPilotStatName, def._arrStat ) && bValid;
        bValid              = Internal::readPilotStats( node, Internal::kArrPilotGrowthName, def._arrGrowth ) && bValid;
        if ( bValid )
            (void)_pilotCatalog.add( def ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return bValid;
    }
} // namespace sw
