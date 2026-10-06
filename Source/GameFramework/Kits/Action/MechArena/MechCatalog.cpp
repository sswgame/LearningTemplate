#include "pch.h"

#include "GameFramework/Kits/Action/MechArena/MechCatalog.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Data/GameDataXml.h"

namespace sw
{
    namespace
    {
        struct MechCatalogInternal
        {
            static MechWeaponKind parseWeaponKind( string_view text )
            {
                if ( StringUtil::equals( text, string_view( "Melee" ), true ) )
                    return MechWeaponKind::Melee;
                if ( StringUtil::equals( text, string_view( "Special" ), true ) )
                    return MechWeaponKind::Special;
                return MechWeaponKind::Shot;
            }

            static MechSkillTrigger parseSkillTrigger( string_view text )
            {
                if ( StringUtil::equals( text, string_view( "HealthBelow" ), true ) )
                    return MechSkillTrigger::HealthBelow;
                if ( StringUtil::equals( text, string_view( "OnDown" ), true ) )
                    return MechSkillTrigger::OnDown;
                if ( StringUtil::equals( text, string_view( "OnRespawn" ), true ) )
                    return MechSkillTrigger::OnRespawn;
                return MechSkillTrigger::Manual;
            }

            static MechWeaponSlotDef readWeapon( const XmlNode& node, const utf8* pId )
            {
                MechWeaponSlotDef slot;
                slot._id                = hashed_string( pId );
                slot._kind              = parseWeaponKind( node.getAttributeText( "kind" ) );
                const utf8* pWeaponId   = node.findAttribute( "weapon" );
                slot._weaponId          = pWeaponId != nullptr ? hashed_string( pWeaponId ) : hashed_string{};
                const string_view moves = node.getAttributeText( "moves" );
                GameDataXml::forEachToken( moves, ",; ", [&]( string_view token )
                { slot._listMoveId.push_back( hashed_string( token ) ); } );
                slot._damage          = MathUtil::max( 0.0f, node.getAttributeFloat( "damage", slot._damage ) );
                slot._downValue       = MathUtil::max( 0.0f, node.getAttributeFloat( "down", slot._downValue ) );
                slot._knockback       = MathUtil::max( 0.0f, node.getAttributeFloat( "knockback", slot._knockback ) );
                slot._range           = MathUtil::max( 0.0f, node.getAttributeFloat( "range", slot._kind == MechWeaponKind::Melee ? 3.0f : 60.0f ) );
                slot._projectileSpeed = MathUtil::max( 0.0f, node.getAttributeFloat( "speed", slot._projectileSpeed ) );
                slot._homing          = MathUtil::max( 0.0f, node.getAttributeFloat( "homing", slot._homing ) );
                slot._cooldown        = MathUtil::max( 0.0f, node.getAttributeFloat( "cooldown", slot._cooldown ) );
                slot._staggerTime     = MathUtil::max( 0.0f, node.getAttributeFloat( "stagger", slot._staggerTime ) );
                return slot;
            }

            static void readWeapons( const XmlNode& parent, string_view sourceName, MechModeDef& outMode )
            {
                for ( XmlNode node = parent.findChild( "Weapon" ); node; node = node.findNextSibling( "Weapon" ) )
                {
                    const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
                    if ( pId == nullptr )
                        continue;
                    outMode._listWeapon.push_back( readWeapon( node, pId ) );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "MechCatalog" );

    int32 MechDef::computeSlotOffset( int32 mode ) const
    {
        int32 offset = 0;
        for ( int32 index = 0; index < mode && index < static_cast<int32>( _listMode.size() ); ++index )
            offset += static_cast<int32>( _listMode[static_cast<size_t>( index )]._listWeapon.size() );
        return offset;
    }

    int32 MechDef::computeSlotCount() const { return computeSlotOffset( static_cast<int32>( _listMode.size() ) ); }

    MechCatalog::MechCatalog()
        : _catalogMech{}
        , _catalogSkill{}
        , _arrClassModifier{}
        , _deckCostLimit{ 0 }
    {
    }

    const StatBlock& MechCatalog::getClassModifier( MechRangeClass rangeClass ) const
    {
        const size_t index = MathUtil::min( static_cast<size_t>( rangeClass ), static_cast<size_t>( MechRangeClass::Count ) - 1 );
        return _arrClassModifier[index];
    }

    MechRangeClass MechCatalog::parseRangeClass( string_view text, MechRangeClass fallback )
    {
        if ( StringUtil::equals( text, string_view( "Near" ), true ) )
            return MechRangeClass::Near;
        if ( StringUtil::equals( text, string_view( "Mid" ), true ) )
            return MechRangeClass::Mid;
        if ( StringUtil::equals( text, string_view( "Far" ), true ) )
            return MechRangeClass::Far;
        return fallback;
    }

    uint32 MechCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        _deckCostLimit = MathUtil::max( 0, root.getAttributeInt( "deckCostLimit", _deckCostLimit ) );
        for ( XmlNode node = root.findChild( "Class" ); node; node = node.findNextSibling( "Class" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            const MechRangeClass rangeClass = parseRangeClass( string_view( pId ), MechRangeClass::Count );
            if ( rangeClass == MechRangeClass::Count )
            {
                SW_LOG_WARNING( "%#: unknown mech class '%#' - skipped", sourceName, pId );
                continue;
            }
            StatBlock& modifier = _arrClassModifier[static_cast<size_t>( rangeClass )];
            modifier.clear();
            (void)modifier.loadFromAttributes( node, "id" ); // 읽은 속성 수만 돌려준다 — 없으면 보정이 없다
        }

        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Mech" ); node; node = node.findNextSibling( "Mech" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            MechDef def;
            def._id                = hashed_string( pId );
            const utf8* pName      = node.findAttribute( "name" );
            def._name              = pName != nullptr ? pName : pId;
            const utf8* pRank      = node.findAttribute( "rank" );
            def._rank              = pRank != nullptr ? hashed_string( pRank ) : hashed_string{};
            def._rangeClass        = parseRangeClass( node.getAttributeText( "class" ), def._rangeClass );
            def._cost              = MathUtil::max( 0, node.getAttributeInt( "cost", def._cost ) );
            def._maxHealth         = MathUtil::max( 1.0f, node.getAttributeFloat( "hp", def._maxHealth ) );
            def._radius            = MathUtil::max( 0.1f, node.getAttributeFloat( "radius", def._radius ) );
            def._dashSpeed         = MathUtil::max( 0.0f, node.getAttributeFloat( "dashSpeed", def._dashSpeed ) );
            def._dashTime          = MathUtil::max( 0.0f, node.getAttributeFloat( "dashTime", def._dashTime ) );
            def._jumpSpeed         = MathUtil::max( 0.0f, node.getAttributeFloat( "jumpSpeed", def._jumpSpeed ) );
            def._boostMax          = MathUtil::max( 1.0f, node.getAttributeFloat( "boost", def._boostMax ) );
            def._boostRegen        = MathUtil::max( 0.0f, node.getAttributeFloat( "boostRegen", def._boostRegen ) );
            def._boostRegenDelay   = MathUtil::max( 0.0f, node.getAttributeFloat( "boostRegenDelay", def._boostRegenDelay ) );
            def._overheatPenalty   = MathUtil::max( 0.0f, node.getAttributeFloat( "overheatPenalty", def._overheatPenalty ) );
            def._overheatRecover   = MathUtil::clamp( node.getAttributeFloat( "overheatRecover", def._overheatRecover ), 0.0f, def._boostMax );
            def._dashCost          = MathUtil::max( 0.0f, node.getAttributeFloat( "dashCost", def._dashCost ) );
            def._jumpCost          = MathUtil::max( 0.0f, node.getAttributeFloat( "jumpCost", def._jumpCost ) );
            def._hoverPerSecond    = MathUtil::max( 0.0f, node.getAttributeFloat( "hoverPerSecond", def._hoverPerSecond ) );
            def._downMax           = MathUtil::max( 0.0f, node.getAttributeFloat( "down", def._downMax ) );
            def._downRecovery      = MathUtil::max( 0.0f, node.getAttributeFloat( "downRecovery", def._downRecovery ) );
            def._downRecoveryDelay = MathUtil::max( 0.0f, node.getAttributeFloat( "downRecoveryDelay", def._downRecoveryDelay ) );
            def._downTime          = MathUtil::max( 0.0f, node.getAttributeFloat( "downTime", def._downTime ) );
            def._wakeInvulnerable  = MathUtil::max( 0.0f, node.getAttributeFloat( "wakeInvulnerable", def._wakeInvulnerable ) );
            def._lockOnRange       = MathUtil::max( 0.0f, node.getAttributeFloat( "lockOn", def._lockOnRange ) );
            def._lockOnAngle       = MathUtil::clamp( node.getAttributeFloat( "lockOnAngle", def._lockOnAngle ), 1.0f, 180.0f );
            def._transformTime     = MathUtil::max( 0.0f, node.getAttributeFloat( "transformTime", def._transformTime ) );
            def._skillSlots        = MathUtil::max( 0, node.getAttributeInt( "skillSlots", def._skillSlots ) );

            for ( XmlNode modeNode = node.findChild( "Mode" ); modeNode; modeNode = modeNode.findNextSibling( "Mode" ) )
            {
                const utf8* pModeId = GameDataXml::findRequiredId( modeNode, sourceName );
                if ( pModeId == nullptr )
                    continue;
                MechModeDef mode;
                mode._id             = hashed_string( pModeId );
                mode._speed          = MathUtil::max( 0.0f, modeNode.getAttributeFloat( "speed", node.getAttributeFloat( "speed", mode._speed ) ) );
                mode._boostCostScale = MathUtil::max( 0.0f, modeNode.getAttributeFloat( "boostCostScale", mode._boostCostScale ) );
                MechCatalogInternal::readWeapons( modeNode, sourceName, mode );
                def._listMode.push_back( mode );
            }
            if ( def._listMode.empty() )
            {
                MechModeDef mode;
                mode._id    = hashed_string( "default" );
                mode._speed = MathUtil::max( 0.0f, node.getAttributeFloat( "speed", mode._speed ) );
                MechCatalogInternal::readWeapons( node, sourceName, mode );
                def._listMode.push_back( mode );
            }
            (void)_catalogMech.add( def );
            ++loadedCount;
        }

        for ( XmlNode node = root.findChild( "Skill" ); node; node = node.findNextSibling( "Skill" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            MechSkillDef skill;
            skill._id        = hashed_string( pId );
            skill._trigger   = MechCatalogInternal::parseSkillTrigger( node.getAttributeText( "trigger" ) );
            skill._threshold = MathUtil::saturate( node.getAttributeFloat( "threshold", skill._threshold ) );
            skill._duration  = MathUtil::max( 0.0f, node.getAttributeFloat( "duration", skill._duration ) );
            skill._cooldown  = MathUtil::max( 0.0f, node.getAttributeFloat( "cooldown", skill._cooldown ) );
            (void)skill._modifier.loadFromAttributes( node, "id,trigger,threshold,duration,cooldown" ); // 읽은 속성 수만 돌려준다 — 없으면 보정이 없다
            (void)_catalogSkill.add( skill );
        }
        return loadedCount;
    }
} // namespace sw
