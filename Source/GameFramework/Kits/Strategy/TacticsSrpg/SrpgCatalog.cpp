#include "pch.h"

#include "GameFramework/Kits/Strategy/TacticsSrpg/SrpgCatalog.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Data/GameDataXml.h"

namespace sw
{
    namespace
    {
        struct SrpgCatalogInternal
        {
            static constexpr const utf8* kArrPilotStatName[kSrpgPilotStatCount]   = { "shooting", "melee", "reaction", "awaken", "defense" };
            static constexpr const utf8* kArrPilotGrowthName[kSrpgPilotStatCount] = { "growShooting", "growMelee", "growReaction", "growAwaken", "growDefense" };

            [[nodiscard]] static bool parseMoveType( string_view text, SrpgMoveType& outMoveType )
            {
                if ( StringUtil::equals( text, string_view( "Ground" ), true ) )
                    outMoveType = SrpgMoveType::Ground;
                else if ( StringUtil::equals( text, string_view( "Air" ), true ) )
                    outMoveType = SrpgMoveType::Air;
                else if ( StringUtil::equals( text, string_view( "Space" ), true ) )
                    outMoveType = SrpgMoveType::Space;
                else if ( StringUtil::equals( text, string_view( "Water" ), true ) )
                    outMoveType = SrpgMoveType::Water;
                else
                    return false;
                return true;
            }

            /** @brief "1 1 - 2" 처럼 이동 타입 순서의 정수 넷입니다. `-` 는 @p dashValue 입니다. */
            static void parseFourInts( string_view text, int32 dashValue, int32 ( &inoutArrValue )[kSrpgMoveTypeCount], bool bAptitude )
            {
                int32 slot = 0;
                GameDataXml::forEachToken( text, ",; \t", [&]( string_view token )
                {
                    if ( slot >= kSrpgMoveTypeCount )
                        return;
                    int32 value = inoutArrValue[slot];
                    if ( bAptitude )
                        value = SrpgCatalog::parseAptitude( token, value );
                    else if ( StringUtil::equals( token, string_view( "-" ) ) )
                        value = dashValue;
                    else if ( StringUtil::parseInt( token, value ) == false )
                        value = inoutArrValue[slot];
                    inoutArrValue[slot] = value;
                    ++slot;
                } );
            }

            static int32 parseSize( string_view text, int32 fallback )
            {
                if ( StringUtil::equals( text, string_view( "S" ), true ) )
                    return 0;
                if ( StringUtil::equals( text, string_view( "M" ), true ) )
                    return 1;
                if ( StringUtil::equals( text, string_view( "L" ), true ) )
                    return 2;
                if ( StringUtil::equals( text, string_view( "LL" ), true ) )
                    return 3;
                int32 value = fallback;
                if ( StringUtil::parseInt( text, value ) == false )
                    return fallback;
                return MathUtil::clamp( value, 0, 3 );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "SrpgCatalog" );

    int32 SrpgPilotDef::computeStat( SrpgPilotStat stat, int32 level ) const
    {
        const size_t index = static_cast<size_t>( stat );
        if ( index >= static_cast<size_t>( kSrpgPilotStatCount ) )
            return 0;
        return _arrStat[index] + _arrGrowth[index] * MathUtil::max( 0, level - 1 );
    }

    SrpgCatalog::SrpgCatalog()
        : _terrainCatalog{}
        , _weaponCatalog{}
        , _unitCatalog{}
        , _pilotCatalog{}
        , _pilotCurve{}
        , _unitCurve{}
    {
    }

    int32 SrpgCatalog::parseAptitude( string_view token, int32 fallback )
    {
        if ( StringUtil::equals( token, string_view( "S" ), true ) )
            return 120;
        if ( StringUtil::equals( token, string_view( "A" ), true ) )
            return 100;
        if ( StringUtil::equals( token, string_view( "B" ), true ) )
            return 90;
        if ( StringUtil::equals( token, string_view( "C" ), true ) )
            return 80;
        if ( StringUtil::equals( token, string_view( "D" ), true ) )
            return 60;
        if ( StringUtil::equals( token, string_view( "-" ) ) )
            return 0;
        int32 value = fallback;
        if ( StringUtil::parseInt( token, value ) == false )
            return fallback;
        return MathUtil::max( 0, value );
    }

    uint32 SrpgCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        const XmlNode pilotCurve = root.findChild( "PilotCurve" );
        if ( pilotCurve )
            _pilotCurve.loadFromNode( pilotCurve );
        const XmlNode unitCurve = root.findChild( "UnitCurve" );
        if ( unitCurve )
            _unitCurve.loadFromNode( unitCurve );

        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const utf8* pName       = node.getName();
            const bool  bTerrain    = StringUtil::equals( pName, "Terrain", true );
            const bool  bWeapon     = StringUtil::equals( pName, "Weapon", true );
            const bool  bUnit       = StringUtil::equals( pName, "Unit", true );
            const bool  bPilot      = StringUtil::equals( pName, "Pilot", true );
            const bool  bDefinition = bTerrain || bWeapon || bUnit || bPilot;
            if ( bDefinition == false )
                continue;
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            if ( bTerrain )
                loadTerrain( node, pId );
            else if ( bWeapon )
                loadWeapon( node, pId, sourceName );
            else if ( bUnit )
                loadUnit( node, pId, sourceName );
            else
                loadPilot( node, pId );
            ++loadedCount;
        }

        for ( const SrpgUnitDef& def : _unitCatalog.getAll() )
        {
            for ( const hashed_string& weaponId : def._listWeaponId )
            {
                if ( _weaponCatalog.find( weaponId ) == nullptr )
                    SW_LOG_WARNING( "%#: unit '%#' has unknown weapon '%#'", sourceName, def._id.c_str(), weaponId.c_str() );
            }
            for ( const SrpgDevelopTarget& target : def._listDevelop )
            {
                if ( _unitCatalog.find( target._unitId ) == nullptr )
                    SW_LOG_WARNING( "%#: unit '%#' develops into unknown '%#'", sourceName, def._id.c_str(), target._unitId.c_str() );
            }
        }
        return loadedCount;
    }

    void SrpgCatalog::loadTerrain( const XmlNode& node, const utf8* pId )
    {
        SrpgTerrainDef def;
        def._id           = hashed_string( pId );
        const utf8* pName = node.findAttribute( "name" );
        def._name         = pName != nullptr ? pName : pId;
        SrpgCatalogInternal::parseFourInts( node.getAttributeText( "cost" ), -1, def._arrMoveCost, false );
        def._defenseBonus = node.getAttributeInt( "defense", def._defenseBonus );
        def._evasionBonus = node.getAttributeInt( "evasion", def._evasionBonus );
        SrpgMoveType domain{ SrpgMoveType::Ground };
        if ( SrpgCatalogInternal::parseMoveType( node.getAttributeText( "domain" ), domain ) )
            def._domain = domain;
        (void)_terrainCatalog.add( def );
    }

    void SrpgCatalog::loadWeapon( const XmlNode& node, const utf8* pId, string_view sourceName )
    {
        SrpgWeaponDef def;
        def._id                = hashed_string( pId );
        const utf8* pName      = node.findAttribute( "name" );
        def._name              = pName != nullptr ? pName : pId;
        def._minRange          = MathUtil::max( 0, node.getAttributeInt( "minRange", def._minRange ) );
        def._maxRange          = MathUtil::max( def._minRange, node.getAttributeInt( "maxRange", def._maxRange ) );
        def._power             = MathUtil::max( 0, node.getAttributeInt( "power", def._power ) );
        def._hitBonus          = node.getAttributeInt( "hit", def._hitBonus );
        def._critBonus         = node.getAttributeInt( "crit", def._critBonus );
        def._enCost            = MathUtil::max( 0, node.getAttributeInt( "en", def._enCost ) );
        def._ammo              = MathUtil::max( 0, node.getAttributeInt( "ammo", def._ammo ) );
        def._moraleRequired    = MathUtil::max( 0, node.getAttributeInt( "morale", def._moraleRequired ) );
        def._bCounter          = node.getAttributeBool( "counter", true ) ? SW_TRUE : SW_FALSE;
        def._bPostMove         = node.getAttributeBool( "postMove", true ) ? SW_TRUE : SW_FALSE;
        const string_view kind = node.getAttributeText( "kind" );
        if ( StringUtil::equals( kind, string_view( "Melee" ), true ) )
            def._kind = SrpgWeaponKind::Melee;
        else if ( StringUtil::equals( kind, string_view( "Awaken" ), true ) )
            def._kind = SrpgWeaponKind::Awaken;
        else if ( kind.empty() == false && StringUtil::equals( kind, string_view( "Shooting" ), true ) == false )
            SW_LOG_WARNING( "%#: weapon '%#' has an unknown kind '%#' - read as shooting", sourceName, pId, kind );

        const string_view mapAnchor = node.getAttributeText( "map" );
        if ( StringUtil::equals( mapAnchor, string_view( "Self" ), true ) )
            def._mapAnchor = SrpgMapAnchor::Self;
        else if ( StringUtil::equals( mapAnchor, string_view( "Target" ), true ) )
            def._mapAnchor = SrpgMapAnchor::Target;
        if ( def.isMap() )
        {
            GameDataXml::forEachToken( node.getAttributeText( "pattern" ), " ;\t", [&]( string_view token )
            {
                float32 arrValue[2]{ 0.0f, 0.0f };
                if ( GameDataXml::parseFloats( token, arrValue, 2 ) == 2 )
                    def._listMapOffset.push_back( int2{ static_cast<int32>( arrValue[0] ), static_cast<int32>( arrValue[1] ) } );
            } );
            if ( def._listMapOffset.empty() )
            {
                SW_LOG_WARNING( "%#: MAP weapon '%#' has no pattern - hits only the anchor cell", sourceName, pId );
                def._listMapOffset.push_back( int2{ 0, 0 } );
            }
            def._bCounter = SW_FALSE; // MAP 병기로는 반격하지 않는다
        }
        (void)_weaponCatalog.add( def );
    }

    void SrpgCatalog::loadUnit( const XmlNode& node, const utf8* pId, string_view sourceName )
    {
        SrpgUnitDef def;
        def._id                    = hashed_string( pId );
        const utf8* pName          = node.findAttribute( "name" );
        def._name                  = pName != nullptr ? pName : pId;
        def._hp                    = MathUtil::max( 1, node.getAttributeInt( "hp", def._hp ) );
        def._en                    = MathUtil::max( 0, node.getAttributeInt( "en", def._en ) );
        def._move                  = MathUtil::max( 0, node.getAttributeInt( "move", def._move ) );
        def._armor                 = MathUtil::max( 0, node.getAttributeInt( "armor", def._armor ) );
        def._mobility              = node.getAttributeInt( "mobility", def._mobility );
        def._size                  = SrpgCatalogInternal::parseSize( node.getAttributeText( "size" ), def._size );
        const string_view moveType = node.getAttributeText( "moveType" );
        if ( moveType.empty() == false && SrpgCatalogInternal::parseMoveType( moveType, def._moveType ) == false )
            SW_LOG_WARNING( "%#: unit '%#' has an unknown moveType '%#' - read as ground", sourceName, pId, moveType );
        SrpgCatalogInternal::parseFourInts( node.getAttributeText( "aptitude" ), 0, def._arrAptitude, true );
        GameDataXml::forEachToken( node.getAttributeText( "weapons" ), ",; \t", [&]( string_view token )
        { def._listWeaponId.push_back( hashed_string( token ) ); } );
        GameDataXml::forEachToken( node.getAttributeText( "developsTo" ), ",; \t", [&]( string_view token )
        {
            SrpgDevelopTarget target;
            const size_t      colon = token.find( ':' );
            target._unitId          = hashed_string( token.substr( 0, colon ) );
            if ( colon != string_view::npos && StringUtil::parseInt( token.substr( colon + 1 ), target._requiredLevel ) == false )
                SW_LOG_WARNING( "%#: unit '%#' has a bad develop level in '%#'", sourceName, pId, token );
            target._requiredLevel = MathUtil::max( 1, target._requiredLevel );
            def._listDevelop.push_back( target );
        } );
        (void)_unitCatalog.add( def );
    }

    void SrpgCatalog::loadPilot( const XmlNode& node, const utf8* pId )
    {
        SrpgPilotDef def;
        def._id           = hashed_string( pId );
        const utf8* pName = node.findAttribute( "name" );
        def._name         = pName != nullptr ? pName : pId;
        for ( int32 stat = 0; stat < kSrpgPilotStatCount; ++stat )
        {
            def._arrStat[stat]   = node.getAttributeInt( SrpgCatalogInternal::kArrPilotStatName[stat], def._arrStat[stat] );
            def._arrGrowth[stat] = node.getAttributeInt( SrpgCatalogInternal::kArrPilotGrowthName[stat], def._arrGrowth[stat] );
        }
        (void)_pilotCatalog.add( def );
    }
} // namespace sw
