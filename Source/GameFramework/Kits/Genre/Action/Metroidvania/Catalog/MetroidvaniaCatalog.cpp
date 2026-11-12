#include "pch.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Catalog/MetroidvaniaCatalog.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Rule/MetroidvaniaAbilitySet.h"
#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct MetroidvaniaCatalogInternal
        {
            /** @brief 없어도 되는 원소 @p pName 을 스키마 @p TDef 로 읽습니다. 없으면 @p outDef 는 스키마 초기값이고 true 입니다. */
            template <typename TDef>
            [[nodiscard]] static bool readOptionalChild( const XMLNode& root, const utf8* pName, string_view sourceName, TDef& outDef )
            {
                const XMLNode node = root.findChild( pName );
                return node ? XMLFields::readProperties( node, outDef, sourceName ) : true;
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
        MetroidvaniaCatalogRootDef rootDef;
        MetroidvaniaRules          rules;
        MetroidvaniaHealthDef      health;
        MetroidvaniaStaminaDef     stamina;
        bool                       bValid = XMLFields::readProperties( root, rootDef, sourceName );
        bValid                            = Internal::readOptionalChild( root, "Rules", sourceName, rules ) && bValid;
        bValid                            = Internal::readOptionalChild( root, "Health", sourceName, health ) && bValid;
        bValid                            = Internal::readOptionalChild( root, "Stamina", sourceName, stamina ) && bValid;
        rules._currency                   = rootDef._currency;

        HealthSettings& healthMachine     = rules._health;
        healthMachine._maxHealth          = health._maxHealth;
        healthMachine._poiseMax           = health._poiseMax;
        healthMachine._poiseRegenDelay    = health._poiseRegenDelay;
        healthMachine._poiseRegenRate     = health._poiseRegenRate;
        healthMachine._poiseBreakDuration = health._poiseBreakDuration;
        healthMachine._healthRegenRate    = health._healthRegenRate;
        healthMachine._bDownedEnabled     = SW_FALSE; // 소울라이크는 기절이 없다. 체력이 0이 되면 죽는다
        ResourceGaugeSettings& gauge      = rules._stamina;
        gauge._max                        = stamina._max;
        gauge._regenRate                  = stamina._regenRate;
        gauge._regenDelay                 = stamina._regenDelay;
        gauge._exhaustThreshold           = stamina._exhaustThreshold;
        gauge._bOverheatMode              = SW_FALSE;

        // 패리 창은 판정의 유일한 근거다 — 코드가 대신 정한 창을 쓰지 않는다.
        const XMLNode parry = root.findChild( "Parry" );
        if ( parry && rules._parryJudge.loadFromXML( parry, sourceName ) == false )
            bValid = false;
        if ( rules._parryJudge.getWindows().empty() )
        {
            SW_LOG_ERROR_ID( "MetroidvaniaCatalog.NeedsParryLeastOne", "%#: needs <Parry> with at least one <Window grade=\"..\" early=\"..\" late=\"..\"/>", sourceName );
            bValid = false;
        }
        if ( bValid == false )
            return false;
        _rules = rules;
        return true;
    }

    uint32 MetroidvaniaCatalog::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readProperties 가 남긴다).
        if ( loadRules( root.getRawNode(), sourceName ) == false ) // 손 읽기: 파일마다 통째로 정하는 규칙 묶음(Rules · Health · Stamina · Parry)을 한 번에 조립한다 — 표는 그 이름을 known 으로 둔다
            return 0;
        // 줍는 것이 능력 · 부적을 가리킨다 — 연결은 참조 표가 보고, 읽기는 표의 순서다.
        using Element                          = XMLCatalogElement<MetroidvaniaCatalog>;
        static constexpr Element kArrElement[] = {
            Element::known( "Rules" ),
            Element::known( "Health" ),
            Element::known( "Stamina" ),
            Element::known( "Parry" ),
            Element::of<&MetroidvaniaCatalog::loadAbility>( "Ability" ),
            Element::of<&MetroidvaniaCatalog::loadCharm>( "Charm" ),
            Element::of<&MetroidvaniaCatalog::_mapCatalog>( "Map" ),
            Element::of<&MetroidvaniaCatalog::_siteCatalog>( "Site" ),
            Element::of<&MetroidvaniaCatalog::loadPickup>( "Pickup" ),
            Element::of<&MetroidvaniaCatalog::loadEnemy>( "Enemy" ),
        };
        return XMLCatalogReadResult::readInTableOrder( *this, root, sourceName, kArrElement )._loadedCount;
    }

    bool MetroidvaniaCatalog::loadAbility( const XMLNode& node, string_view sourceName )
    {
        MetroidvaniaAbilityDef ability;
        bool                   bValid = XMLFields::readProperties( node, ability, sourceName );
        const XMLNode          motor  = node.findChild( "Motor" );
        if ( motor )
            ability._motor.addAttributes( motor ); // 속성이 없으면 빈 스탯이다
        for ( const StatValue& value : ability._motor.getValues() )
        {
            if ( MetroidvaniaAbilitySet::isMotorSettingName( value._name ) == false )
            {
                SW_LOG_ERROR_ID( "MetroidvaniaCatalog.AbilityChangesUnknownMotor", "%#: ability '%#' changes an unknown motor setting '%#'", sourceName, ability._id.c_str(), value._name.c_str() );
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
        MetroidvaniaTrinketDef charm;
        if ( XMLFields::readProperties( node, charm, sourceName ) == false )
            return false;
        const XMLNode stats = node.findChild( "Stats" );
        if ( stats )
            charm._stats.addAttributes( stats ); // 속성이 없으면 빈 스탯이다
        (void)_charmCatalog.add( charm );
        return true;
    }

    bool MetroidvaniaCatalog::loadPickup( const XMLNode& node, string_view sourceName )
    {
        // 주는 능력 · 부적은 같은 파일의 id 다 — 깨지면 참조 표(`engine/settings/datacatalogreferences.xml`)가 로드 · 쿠킹을 실패시킨다(줍는 것만 빼고 서지 않는다).
        MetroidvaniaPickupDef pickup;
        if ( XMLFields::readProperties( node, pickup, sourceName ) == false )
            return false;
        (void)_pickupCatalog.add( pickup );
        return true;
    }

    bool MetroidvaniaCatalog::loadEnemy( const XMLNode& node, string_view sourceName )
    {
        MetroidvaniaEnemyDef enemy;
        bool                 bValid = XMLFields::readProperties( node, enemy, sourceName );
        if ( enemy._bBoss && enemy._flag.empty() )
        {
            SW_LOG_ERROR_ID( "MetroidvaniaCatalog.BossNeedsFlagSet", "%#: boss '%#' needs flag=\"...\" (set when it is defeated)", sourceName, enemy._id.c_str() );
            bValid = false;
        }
        if ( bValid == false )
            return false;
        (void)_enemyCatalog.add( enemy );
        return true;
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<MetroidvaniaCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( MetroidvaniaCatalog, MetroidvaniaCatalog, SW_KIT_API, MetroidvaniaCatalog::kAssetType.withLoader( AssetTypeLoader::of<MetroidvaniaCatalog>( MemoryTag::Game ) ) );
} // namespace sw
