#include "pch.h"

#include "GameFramework/Kits/Action/Metroidvania/MetroidvaniaCatalog.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXml.h"
#include "GameFramework/Kits/Action/Metroidvania/MetroAbilitySet.h"

namespace sw
{
    SW_LOG_CALLER( "MetroidvaniaCatalog" );

    namespace
    {
        struct MetroidvaniaCatalogInternal
        {
            static hashed_string readName( const XmlNode& node, const utf8* pName )
            {
                const utf8* pValue = node.findAttribute( pName );
                return pValue != nullptr ? hashed_string( pValue ) : hashed_string{};
            }

            static string readText( const XmlNode& node, const utf8* pName, const utf8* pFallback )
            {
                const utf8* pValue = node.findAttribute( pName );
                return pValue != nullptr ? string( pValue ) : string( pFallback );
            }

            static uint8 readFlag( const XmlNode& node, const utf8* pName, uint8 fallback )
            {
                return node.getAttributeBool( pName, fallback != SW_FALSE ) ? SW_TRUE : SW_FALSE;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    MetroidvaniaCatalog::MetroidvaniaCatalog()
        : _abilityCatalog{}
        , _charmCatalog{}
        , _mapCatalog{}
        , _siteCatalog{}
        , _pickupCatalog{}
        , _enemyCatalog{}
        , _rules{}
    {
    }

    void MetroidvaniaCatalog::loadRules( const XmlNode& root )
    {
        const utf8* pCurrency = root.findAttribute( "currency" );
        if ( pCurrency != nullptr )
            _rules._currency = hashed_string( pCurrency );

        const XmlNode rules = root.findChild( "Rules" );
        if ( rules )
        {
            _rules._flaskCharges              = MathUtil::max( 0, rules.getAttributeInt( "flaskCharges", _rules._flaskCharges ) );
            _rules._flaskMaxCharges           = MathUtil::max( _rules._flaskCharges, rules.getAttributeInt( "flaskMaxCharges", _rules._flaskMaxCharges ) );
            _rules._flaskHeal                 = MathUtil::max( 0.0f, rules.getAttributeFloat( "flaskHeal", _rules._flaskHeal ) );
            _rules._flaskHealPerUpgrade       = MathUtil::max( 0.0f, rules.getAttributeFloat( "flaskHealPerUpgrade", _rules._flaskHealPerUpgrade ) );
            _rules._riposteMultiplier         = MathUtil::max( 1.0f, rules.getAttributeFloat( "riposteMultiplier", _rules._riposteMultiplier ) );
            _rules._riposteTime               = MathUtil::max( 0.0f, rules.getAttributeFloat( "riposteTime", _rules._riposteTime ) );
            _rules._attackStaminaCost         = MathUtil::max( 0.0f, rules.getAttributeFloat( "attackStamina", _rules._attackStaminaCost ) );
            _rules._dodgeStaminaCost          = MathUtil::max( 0.0f, rules.getAttributeFloat( "dodgeStamina", _rules._dodgeStaminaCost ) );
            _rules._guardStaminaPerDamage     = MathUtil::max( 0.0f, rules.getAttributeFloat( "guardStaminaPerDamage", _rules._guardStaminaPerDamage ) );
            _rules._guardChipRatio            = MathUtil::saturate( rules.getAttributeFloat( "guardChip", _rules._guardChipRatio ) );
            _rules._corpseRecoverRadius       = MathUtil::max( 0.0f, rules.getAttributeFloat( "corpseRadius", _rules._corpseRecoverRadius ) );
            _rules._overcharmDamageTakenScale = MathUtil::max( 1.0f, rules.getAttributeFloat( "overcharmDamageScale", _rules._overcharmDamageTakenScale ) );
            _rules._charmNotches              = MathUtil::max( 0, rules.getAttributeInt( "charmNotches", _rules._charmNotches ) );
            _rules._bAllowOvercharm           = MetroidvaniaCatalogInternal::readFlag( rules, "overcharm", _rules._bAllowOvercharm );
        }

        const XmlNode health = root.findChild( "Health" );
        if ( health )
        {
            VitalitySettings& settings   = _rules._health;
            settings._maxHealth          = MathUtil::max( 1.0f, health.getAttributeFloat( "max", settings._maxHealth ) );
            settings._poiseMax           = MathUtil::max( 0.0f, health.getAttributeFloat( "poise", settings._poiseMax ) );
            settings._poiseRegenDelay    = MathUtil::max( 0.0f, health.getAttributeFloat( "poiseRegenDelay", settings._poiseRegenDelay ) );
            settings._poiseRegenRate     = MathUtil::max( 0.0f, health.getAttributeFloat( "poiseRegenRate", settings._poiseRegenRate ) );
            settings._poiseBreakDuration = MathUtil::max( 0.0f, health.getAttributeFloat( "poiseBreakDuration", settings._poiseBreakDuration ) );
            settings._healthRegenRate    = MathUtil::max( 0.0f, health.getAttributeFloat( "regenRate", settings._healthRegenRate ) );
        }
        _rules._health._bDownedEnabled = SW_FALSE; // 소울라이크는 기절이 없다 — 0 이면 죽는다

        const XmlNode stamina = root.findChild( "Stamina" );
        if ( stamina )
        {
            ResourceGaugeSettings& settings = _rules._stamina;
            settings._max                   = MathUtil::max( 1.0f, stamina.getAttributeFloat( "max", settings._max ) );
            settings._regenRate             = MathUtil::max( 0.0f, stamina.getAttributeFloat( "regenRate", settings._regenRate ) );
            settings._regenDelay            = MathUtil::max( 0.0f, stamina.getAttributeFloat( "regenDelay", settings._regenDelay ) );
            settings._exhaustThreshold      = MathUtil::max( 0.0f, stamina.getAttributeFloat( "exhaustThreshold", settings._exhaustThreshold ) );
            settings._bOverheatMode         = SW_FALSE;
        }

        const XmlNode parry = root.findChild( "Parry" );
        if ( parry )
            _rules._parryJudge.loadFromNode( parry );
        if ( _rules._parryJudge.getWindows().empty() )
        {
            TimingWindow window;
            window._grade      = hashed_string( "Parry" );
            window._earlyWidth = 0.1f;
            window._lateWidth  = 0.03f;
            _rules._parryJudge.setWindows( vector<TimingWindow>{ window } );
        }
    }

    uint32 MetroidvaniaCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        loadRules( root );
        uint32 loadedCount = 0;

        for ( XmlNode node = root.findChild( "Ability" ); node; node = node.findNextSibling( "Ability" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            MetroAbilityDef ability;
            ability._id         = hashed_string( pId );
            ability._name       = MetroidvaniaCatalogInternal::readText( node, "name", pId );
            ability._flag       = MetroidvaniaCatalogInternal::readName( node, "flag" );
            const XmlNode motor = node.findChild( "Motor" );
            if ( motor )
                (void)ability._motor.loadFromAttributes( motor ); // 읽은 속성 수만 돌려준다 — 없으면 빈 스탯이다
            for ( const StatValue& value : ability._motor.getValues() )
            {
                if ( MetroAbilitySet::isMotorSettingName( value._name ) == false )
                    SW_LOG_WARNING( "%#: ability '%#' changes an unknown motor setting '%#' - ignored", sourceName, pId, value._name.c_str() );
            }
            (void)_abilityCatalog.add( ability );
            ++loadedCount;
        }

        for ( XmlNode node = root.findChild( "Charm" ); node; node = node.findNextSibling( "Charm" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            MetroCharmDef charm;
            charm._id           = hashed_string( pId );
            charm._name         = MetroidvaniaCatalogInternal::readText( node, "name", pId );
            charm._cost         = MathUtil::max( 0, node.getAttributeInt( "cost", charm._cost ) );
            const XmlNode stats = node.findChild( "Stats" );
            if ( stats )
                (void)charm._stats.loadFromAttributes( stats ); // 읽은 속성 수만 돌려준다 — 없으면 빈 스탯이다
            (void)_charmCatalog.add( charm );
            ++loadedCount;
        }

        for ( XmlNode node = root.findChild( "Map" ); node; node = node.findNextSibling( "Map" ) )
        {
            MetroRegionMapDef regionMap;
            regionMap._id = MetroidvaniaCatalogInternal::readName( node, "region" );
            if ( regionMap._id.empty() )
            {
                SW_LOG_WARNING( "%#: <Map> without a region - skipped", sourceName );
                continue;
            }
            regionMap._price = MathUtil::max( 0, node.getAttributeInt( "price", regionMap._price ) );
            (void)_mapCatalog.add( regionMap );
            ++loadedCount;
        }

        for ( XmlNode node = root.findChild( "Site" ); node; node = node.findNextSibling( "Site" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            MetroSiteDef site;
            site._id          = hashed_string( pId );
            site._area        = MetroidvaniaCatalogInternal::readName( node, "area" );
            site._bRest       = MetroidvaniaCatalogInternal::readFlag( node, "rest", SW_FALSE );
            site._bFastTravel = MetroidvaniaCatalogInternal::readFlag( node, "fastTravel", SW_FALSE );
            if ( site._area.empty() )
                SW_LOG_WARNING( "%#: site '%#' has no area - it never shows on the map", sourceName, pId );
            (void)_siteCatalog.add( site );
            ++loadedCount;
        }

        for ( XmlNode node = root.findChild( "Pickup" ); node; node = node.findNextSibling( "Pickup" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            MetroPickupDef pickup;
            pickup._id      = hashed_string( pId );
            pickup._area    = MetroidvaniaCatalogInternal::readName( node, "area" );
            pickup._ability = MetroidvaniaCatalogInternal::readName( node, "ability" );
            pickup._charm   = MetroidvaniaCatalogInternal::readName( node, "charm" );
            if ( pickup._ability.empty() == false && _abilityCatalog.find( pickup._ability ) == nullptr )
                SW_LOG_WARNING( "%#: pickup '%#' grants an unknown ability '%#'", sourceName, pId, pickup._ability.c_str() );
            if ( pickup._charm.empty() == false && _charmCatalog.find( pickup._charm ) == nullptr )
                SW_LOG_WARNING( "%#: pickup '%#' grants an unknown charm '%#'", sourceName, pId, pickup._charm.c_str() );
            (void)_pickupCatalog.add( pickup );
            ++loadedCount;
        }

        for ( XmlNode node = root.findChild( "Enemy" ); node; node = node.findNextSibling( "Enemy" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            MetroEnemyDef enemy;
            enemy._id        = hashed_string( pId );
            enemy._currency  = MathUtil::max( 0, node.getAttributeInt( "currency", enemy._currency ) );
            enemy._lootTable = MetroidvaniaCatalogInternal::readName( node, "loot" );
            enemy._bBoss     = MetroidvaniaCatalogInternal::readFlag( node, "boss", SW_FALSE );
            enemy._flag      = MetroidvaniaCatalogInternal::readName( node, "flag" );
            if ( enemy._bBoss == SW_TRUE && enemy._flag.empty() )
                enemy._flag = hashed_string( string( "boss." ) + pId );
            (void)_enemyCatalog.add( enemy );
            ++loadedCount;
        }
        return loadedCount;
    }
} // namespace sw
