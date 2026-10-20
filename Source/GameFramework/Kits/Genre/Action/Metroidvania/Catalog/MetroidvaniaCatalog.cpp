#include "pch.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Catalog/MetroidvaniaCatalog.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Kits/Genre/Action/Metroidvania/Rule/MetroAbilitySet.h"

namespace sw
{
    SW_LOG_CALLER( "MetroidvaniaCatalog" );

    namespace
    {
        struct MetroidvaniaCatalogInternal
        {
            /** @brief 없어도 되는 원소 @p pName 을 스키마 @p TDef 로 읽습니다. 없으면 @p outDef 는 스키마 초기값이고 true 입니다. */
            template <typename TDef>
            [[nodiscard]] static bool readOptionalChild( const XMLNode& root, const utf8* pName, string_view sourceName, TDef& outDef )
            {
                const XMLNode node = root.findChild( pName );
                return node ? GameDataXML::readFields( node, outDef, sourceName ) : true;
            }
        };
    } // namespace

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

    bool MetroidvaniaCatalog::loadRules( const XMLNode& root, string_view sourceName )
    {
        using Internal = MetroidvaniaCatalogInternal;
        // 규칙은 파일마다 통째로 정한다 — 칸의 기본값은 스키마 초기값이고 앞의 값을 이어받지 않는다.
        MetroCatalogRootDef rootDef;
        MetroRules          rules;
        MetroHealthDef      health;
        MetroStaminaDef     stamina;
        bool                bValid = GameDataXML::readFields( root, rootDef, sourceName );
        bValid                     = Internal::readOptionalChild( root, "Rules", sourceName, rules ) && bValid;
        bValid                     = Internal::readOptionalChild( root, "Health", sourceName, health ) && bValid;
        bValid                     = Internal::readOptionalChild( root, "Stamina", sourceName, stamina ) && bValid;
        rules._currency            = rootDef._currency;

        VitalitySettings& vitality   = rules._health;
        vitality._maxHealth          = health._maxHealth;
        vitality._poiseMax           = health._poiseMax;
        vitality._poiseRegenDelay    = health._poiseRegenDelay;
        vitality._poiseRegenRate     = health._poiseRegenRate;
        vitality._poiseBreakDuration = health._poiseBreakDuration;
        vitality._healthRegenRate    = health._healthRegenRate;
        vitality._bDownedEnabled     = SW_FALSE; // 소울라이크는 기절이 없다. 체력이 0이 되면 죽는다
        ResourceGaugeSettings& gauge = rules._stamina;
        gauge._max                   = stamina._max;
        gauge._regenRate             = stamina._regenRate;
        gauge._regenDelay            = stamina._regenDelay;
        gauge._exhaustThreshold      = stamina._exhaustThreshold;
        gauge._bOverheatMode         = SW_FALSE;

        // 패리 창은 판정의 유일한 근거다 — 코드가 대신 정한 창을 쓰지 않는다.
        const XMLNode parry = root.findChild( "Parry" );
        if ( parry && rules._parryJudge.loadFromNode( parry, sourceName ) == false )
            bValid = false;
        if ( rules._parryJudge.getWindows().empty() )
        {
            SW_LOG_ERROR( "%#: needs <Parry> with at least one <Window grade=\"..\" early=\"..\" late=\"..\"/>", sourceName );
            bValid = false;
        }
        if ( bValid == false )
            return false;
        _rules = rules;
        return true;
    }

    uint32 MetroidvaniaCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        if ( loadRules( root, sourceName ) == false )
            return 0;
        uint32 loadedCount = 0;
        // 줍는 것이 능력 · 부적을 가리키므로 종류 순서대로 읽는다.
        for ( XMLNode node = root.findChild( "Ability" ); node; node = node.findNextSibling( "Ability" ) )
        {
            loadedCount += loadAbility( node, sourceName ) ? 1 : 0;
        }
        for ( XMLNode node = root.findChild( "Charm" ); node; node = node.findNextSibling( "Charm" ) )
        {
            loadedCount += loadCharm( node, sourceName ) ? 1 : 0;
        }
        for ( XMLNode node = root.findChild( "Map" ); node; node = node.findNextSibling( "Map" ) )
        {
            loadedCount += GameDataXML::readInto( node, sourceName, _mapCatalog ) ? 1 : 0;
        }
        for ( XMLNode node = root.findChild( "Site" ); node; node = node.findNextSibling( "Site" ) )
        {
            loadedCount += GameDataXML::readInto( node, sourceName, _siteCatalog ) ? 1 : 0;
        }
        for ( XMLNode node = root.findChild( "Pickup" ); node; node = node.findNextSibling( "Pickup" ) )
        {
            loadedCount += loadPickup( node, sourceName ) ? 1 : 0;
        }
        for ( XMLNode node = root.findChild( "Enemy" ); node; node = node.findNextSibling( "Enemy" ) )
        {
            loadedCount += loadEnemy( node, sourceName ) ? 1 : 0;
        }
        return loadedCount;
    }

    bool MetroidvaniaCatalog::loadAbility( const XMLNode& node, string_view sourceName )
    {
        MetroAbilityDef ability;
        bool            bValid = GameDataXML::readFields( node, ability, sourceName );
        const XMLNode   motor  = node.findChild( "Motor" );
        if ( motor )
            ability._motor.loadFromAttributes( motor ); // 속성이 없으면 빈 스탯이다
        for ( const StatValue& value : ability._motor.getValues() )
        {
            if ( MetroAbilitySet::isMotorSettingName( value._name ) == false )
            {
                SW_LOG_ERROR( "%#: ability '%#' changes an unknown motor setting '%#'", sourceName, ability._id.c_str(), value._name.c_str() );
                bValid = false;
            }
        }
        if ( bValid == false )
            return false;
        (void)_abilityCatalog.add( ability );
        return true;
    }

    bool MetroidvaniaCatalog::loadCharm( const XMLNode& node, string_view sourceName )
    {
        MetroCharmDef charm;
        if ( GameDataXML::readFields( node, charm, sourceName ) == false )
            return false;
        const XMLNode stats = node.findChild( "Stats" );
        if ( stats )
            charm._stats.loadFromAttributes( stats ); // 속성이 없으면 빈 스탯이다
        (void)_charmCatalog.add( charm );
        return true;
    }

    bool MetroidvaniaCatalog::loadPickup( const XMLNode& node, string_view sourceName )
    {
        MetroPickupDef pickup;
        bool           bValid = GameDataXML::readFields( node, pickup, sourceName );
        if ( pickup._ability.empty() == false && _abilityCatalog.find( pickup._ability ) == nullptr )
        {
            SW_LOG_ERROR( "%#: pickup '%#' grants an unknown ability '%#'", sourceName, pickup._id.c_str(), pickup._ability.c_str() );
            bValid = false;
        }
        if ( pickup._charm.empty() == false && _charmCatalog.find( pickup._charm ) == nullptr )
        {
            SW_LOG_ERROR( "%#: pickup '%#' grants an unknown charm '%#'", sourceName, pickup._id.c_str(), pickup._charm.c_str() );
            bValid = false;
        }
        if ( bValid == false )
            return false;
        (void)_pickupCatalog.add( pickup );
        return true;
    }

    bool MetroidvaniaCatalog::loadEnemy( const XMLNode& node, string_view sourceName )
    {
        MetroEnemyDef enemy;
        bool          bValid = GameDataXML::readFields( node, enemy, sourceName );
        if ( enemy._bBoss && enemy._flag.empty() )
        {
            SW_LOG_ERROR( "%#: boss '%#' needs flag=\"...\" (set when it is defeated)", sourceName, enemy._id.c_str() );
            bValid = false;
        }
        if ( bValid == false )
            return false;
        (void)_enemyCatalog.add( enemy );
        return true;
    }
} // namespace sw
