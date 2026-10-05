#include "pch.h"

#include "GameFramework/Kits/Strategy/SideScrollConquest/ConquestCatalog.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Data/GameDataXml.h"

namespace sw
{
    SW_LOG_CALLER( "ConquestCatalog" );

    namespace
    {
        struct ConquestCatalogInternal
        {
            static void loadStatChild( const XmlNode& node, const utf8* pChildName, StatBlock& outStats )
            {
                outStats.clear();
                if ( const XmlNode child = node.findChild( pChildName ) )
                    (void)outStats.loadFromAttributes( child );
            }

            [[nodiscard]] static bool parseSiteKind( string_view text, ConquestSiteKind& outKind )
            {
                constexpr const utf8* kArrKindName[] = { "Village", "Outpost", "Fortress" };
                for ( uint32 kindIndex = 0; kindIndex < 3; ++kindIndex )
                {
                    if ( StringUtil::equals( text, string_view( kArrKindName[kindIndex] ), true ) )
                    {
                        outKind = static_cast<ConquestSiteKind>( kindIndex );
                        return true;
                    }
                }
                return false;
            }

            [[nodiscard]] static bool parseSiegeRole( string_view text, ConquestSiegeRole& outRole )
            {
                constexpr const utf8* kArrRoleName[] = { "None", "Ram", "Ladder" };
                for ( uint32 roleIndex = 0; roleIndex < 3; ++roleIndex )
                {
                    if ( StringUtil::equals( text, string_view( kArrRoleName[roleIndex] ), true ) )
                    {
                        outRole = static_cast<ConquestSiegeRole>( roleIndex );
                        return true;
                    }
                }
                return false;
            }

            /** @brief "spearman:3,archer:2" 를 읽습니다(개수를 빼면 하나). */
            static void parseGarrison( string_view text, vector<ConquestGarrisonDef>& outListGarrison, string_view sourceName, const utf8* pId )
            {
                outListGarrison.clear();
                GameDataXml::forEachToken( text, ",; ", [&]( string_view token )
                {
                    ConquestGarrisonDef garrison;
                    const size_t        colon = token.find( ':' );
                    garrison._unitId          = hashed_string( token.substr( 0, colon ) );
                    if ( colon != string_view::npos && StringUtil::parseInt( token.substr( colon + 1 ), garrison._count ) == false )
                        SW_LOG_WARNING( "%#: site '%#' has a bad garrison count '%#'", sourceName, pId, token );
                    garrison._count = MathUtil::max( 0, garrison._count );
                    outListGarrison.push_back( garrison );
                } );
            }

            static void loadRules( const XmlNode& node, ConquestRules& outRules )
            {
                outRules._waveUnit                = hashed_string( node.getAttributeText( "waveUnit" ) );
                outRules._fixedStep               = MathUtil::clamp( node.getAttributeFloat( "fixedStep", outRules._fixedStep ), 0.01f, 1.0f );
                outRules._incomeInterval          = MathUtil::max( 0.1f, node.getAttributeFloat( "incomeInterval", outRules._incomeInterval ) );
                outRules._captureTime             = MathUtil::max( 0.0f, node.getAttributeFloat( "captureTime", outRules._captureTime ) );
                outRules._moraleRadius            = MathUtil::max( 0.0f, node.getAttributeFloat( "moraleRadius", outRules._moraleRadius ) );
                outRules._moraleDamageBonus       = MathUtil::max( 0.0f, node.getAttributeFloat( "moraleDamageBonus", outRules._moraleDamageBonus ) );
                outRules._wallProtection          = MathUtil::saturate( node.getAttributeFloat( "wallProtection", outRules._wallProtection ) );
                outRules._commanderHealth         = MathUtil::max( 1.0f, node.getAttributeFloat( "commanderHealth", outRules._commanderHealth ) );
                outRules._commanderDamage         = MathUtil::max( 0.0f, node.getAttributeFloat( "commanderDamage", outRules._commanderDamage ) );
                outRules._commanderRange          = MathUtil::max( 0.1f, node.getAttributeFloat( "commanderRange", outRules._commanderRange ) );
                outRules._commanderSpeed          = MathUtil::max( 0.0f, node.getAttributeFloat( "commanderSpeed", outRules._commanderSpeed ) );
                outRules._commanderAttackInterval = MathUtil::max( 0.05f, node.getAttributeFloat( "commanderAttackInterval", outRules._commanderAttackInterval ) );
                outRules._commanderRespawnTime    = MathUtil::max( 0.0f, node.getAttributeFloat( "commanderRespawn", outRules._commanderRespawnTime ) );
                outRules._formationSpacing        = MathUtil::max( 0.1f, node.getAttributeFloat( "formationSpacing", outRules._formationSpacing ) );
                outRules._followLeash             = MathUtil::max( 0.0f, node.getAttributeFloat( "followLeash", outRules._followLeash ) );
                outRules._aggroRange              = MathUtil::max( 0.0f, node.getAttributeFloat( "aggroRange", outRules._aggroRange ) );
                outRules._waveInterval            = MathUtil::max( 0.0f, node.getAttributeFloat( "waveInterval", outRules._waveInterval ) );
                outRules._waveCountPerMinute      = MathUtil::max( 0.0f, node.getAttributeFloat( "waveCountPerMinute", outRules._waveCountPerMinute ) );
                outRules._waveBaseCount           = MathUtil::max( 0, node.getAttributeInt( "waveBaseCount", outRules._waveBaseCount ) );
                outRules._waveCountPerSite        = MathUtil::max( 0, node.getAttributeInt( "waveCountPerSite", outRules._waveCountPerSite ) );
                outRules._basePopulation          = MathUtil::max( 0, node.getAttributeInt( "basePopulation", outRules._basePopulation ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool parseConquestTeam( string_view text, ConquestTeam& outTeam )
    {
        constexpr const utf8* kArrTeamName[] = { "Neutral", "Player", "Enemy" };
        for ( uint32 teamIndex = 0; teamIndex < 3; ++teamIndex )
        {
            if ( StringUtil::equals( text, string_view( kArrTeamName[teamIndex] ), true ) )
            {
                outTeam = static_cast<ConquestTeam>( teamIndex );
                return true;
            }
        }
        return false;
    }

    const utf8* toString( ConquestTeam team )
    {
        switch ( team )
        {
            case ConquestTeam::Neutral:
                return "Neutral";
            case ConquestTeam::Player:
                return "Player";
            case ConquestTeam::Enemy:
                return "Enemy";
        }
        return "?";
    }

    ConquestCatalog::ConquestCatalog()
        : _buildingCatalog{}
        , _unitCatalog{}
        , _siteCatalog{}
        , _rules{}
    {
    }

    uint32 ConquestCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        if ( const XmlNode rulesNode = root.findChild( "Rules" ) )
            ConquestCatalogInternal::loadRules( rulesNode, _rules );
        if ( const XmlNode startNode = root.findChild( "Start" ) )
        {
            _rules._startResources.clear();
            (void)_rules._startResources.loadFromAttributes( startNode );
        }

        for ( XmlNode node = root.findChild( "Unit" ); node; node = node.findNextSibling( "Unit" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            ConquestUnitDef unit;
            unit._id                   = hashed_string( pId );
            unit._health               = MathUtil::max( 1.0f, node.getAttributeFloat( "hp", unit._health ) );
            unit._damage               = MathUtil::max( 0.0f, node.getAttributeFloat( "damage", unit._damage ) );
            unit._range                = MathUtil::max( 0.1f, node.getAttributeFloat( "range", unit._range ) );
            unit._attackInterval       = MathUtil::max( 0.05f, node.getAttributeFloat( "attackInterval", unit._attackInterval ) );
            unit._speed                = MathUtil::max( 0.0f, node.getAttributeFloat( "speed", unit._speed ) );
            unit._trainTime            = MathUtil::max( 0.0f, node.getAttributeFloat( "train", unit._trainTime ) );
            unit._structureScale       = MathUtil::max( 0.0f, node.getAttributeFloat( "structureScale", unit._structureScale ) );
            unit._population           = MathUtil::max( 0, node.getAttributeInt( "pop", unit._population ) );
            const string_view roleText = node.getAttributeText( "siege" );
            if ( roleText.empty() == false && ConquestCatalogInternal::parseSiegeRole( roleText, unit._siegeRole ) == false )
                SW_LOG_WARNING( "%#: unit '%#' has an unknown siege role '%#'", sourceName, pId, roleText );
            ConquestCatalogInternal::loadStatChild( node, "Cost", unit._cost );
            (void)_unitCatalog.add( unit );
            ++loadedCount;
        }

        for ( XmlNode node = root.findChild( "Building" ); node; node = node.findNextSibling( "Building" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            ConquestBuildingDef building;
            building._id              = hashed_string( pId );
            building._produces        = hashed_string( node.getAttributeText( "produces" ) );
            building._cycleTime       = MathUtil::max( 0.1f, node.getAttributeFloat( "cycleTime", building._cycleTime ) );
            building._amountPerWorker = MathUtil::max( 0, node.getAttributeInt( "amount", building._amountPerWorker ) );
            building._workerSlots     = MathUtil::max( 0, node.getAttributeInt( "workerSlots", building._workerSlots ) );
            building._housing         = MathUtil::max( 0, node.getAttributeInt( "housing", building._housing ) );
            GameDataXml::forEachToken( node.getAttributeText( "trains" ), ",; ", [&]( string_view token )
            {
                const hashed_string unitId( token );
                if ( _unitCatalog.find( unitId ) == nullptr )
                    SW_LOG_WARNING( "%#: building '%#' trains an unknown unit '%#'", sourceName, pId, token );
                building._listTrainable.push_back( unitId );
            } );
            ConquestCatalogInternal::loadStatChild( node, "Cost", building._cost );
            (void)_buildingCatalog.add( building );
            ++loadedCount;
        }

        for ( XmlNode node = root.findChild( "Site" ); node; node = node.findNextSibling( "Site" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            ConquestSiteDef site;
            site._id                   = hashed_string( pId );
            site._x                    = node.getAttributeFloat( "x", site._x );
            site._captureRadius        = MathUtil::max( 0.5f, node.getAttributeFloat( "captureRadius", site._captureRadius ) );
            site._gateHealth           = MathUtil::max( 0.0f, node.getAttributeFloat( "gate", site._gateHealth ) );
            site._wallHealth           = MathUtil::max( 0.0f, node.getAttributeFloat( "wall", site._wallHealth ) );
            site._workers              = MathUtil::max( 0, node.getAttributeInt( "workers", site._workers ) );
            site._housing              = MathUtil::max( 0, node.getAttributeInt( "housing", site._housing ) );
            site._buildSlots           = MathUtil::max( 0, node.getAttributeInt( "slots", site._buildSlots ) );
            const string_view kindText = node.getAttributeText( "kind" );
            if ( kindText.empty() == false && ConquestCatalogInternal::parseSiteKind( kindText, site._kind ) == false )
                SW_LOG_WARNING( "%#: site '%#' has an unknown kind '%#'", sourceName, pId, kindText );
            const string_view ownerText = node.getAttributeText( "owner" );
            if ( ownerText.empty() == false && parseConquestTeam( ownerText, site._owner ) == false )
                SW_LOG_WARNING( "%#: site '%#' has an unknown owner '%#'", sourceName, pId, ownerText );
            ConquestCatalogInternal::parseGarrison( node.getAttributeText( "garrison" ), site._listGarrison, sourceName, pId );
            for ( const ConquestGarrisonDef& garrison : site._listGarrison )
            {
                if ( _unitCatalog.find( garrison._unitId ) == nullptr )
                    SW_LOG_WARNING( "%#: site '%#' garrisons an unknown unit '%#'", sourceName, pId, garrison._unitId.c_str() );
            }
            ConquestCatalogInternal::loadStatChild( node, "Income", site._income );
            (void)_siteCatalog.add( site );
            ++loadedCount;
        }
        if ( _rules._waveUnit.empty() == false && _unitCatalog.find( _rules._waveUnit ) == nullptr )
            SW_LOG_WARNING( "%#: waveUnit '%#' is not a unit - no counter-attack waves", sourceName, _rules._waveUnit.c_str() );
        return loadedCount;
    }
} // namespace sw
