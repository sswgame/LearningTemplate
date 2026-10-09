#include "pch.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Catalog/BrCatalog.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXml.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "BrCatalog" );

    BrCatalog::BrCatalog()
        : _armorCatalog{}
        , _backpackCatalog{}
        , _lootSpotCatalog{}
        , _zone{}
        , _flight{}
        , _fall{}
        , _player{}
        , _supplyDrop{}
        , _mapSize{ 1000.0f }
    {
    }

    uint32 BrCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        _mapSize = MathUtil::max( 1.0f, root.getAttributeFloat( "mapSize", _mapSize ) );

        if ( const XmlNode node = root.findChild( "Player" ) )
        {
            _player._maxHealth         = MathUtil::max( 1.0f, node.getAttributeFloat( "health", _player._maxHealth ) );
            _player._downedHealth      = MathUtil::max( 0.0f, node.getAttributeFloat( "downedHealth", _player._downedHealth ) );
            _player._bleedoutRate      = MathUtil::max( 0.0f, node.getAttributeFloat( "bleedout", _player._bleedoutRate ) );
            _player._reviveTime        = MathUtil::max( 0.1f, node.getAttributeFloat( "reviveTime", _player._reviveTime ) );
            _player._reviveHealthRatio = MathUtil::clamp( node.getAttributeFloat( "reviveHealthRatio", _player._reviveHealthRatio ), 0.01f, 1.0f );
            _player._baseCarryWeight   = MathUtil::max( 0.0f, node.getAttributeFloat( "carryWeight", _player._baseCarryWeight ) );
            _player._slotCount         = MathUtil::max( 1, node.getAttributeInt( "slots", _player._slotCount ) );
            _player._maxRevivers       = MathUtil::max( 1, node.getAttributeInt( "revivers", _player._maxRevivers ) );
        }
        if ( const XmlNode node = root.findChild( "Flight" ) )
        {
            _flight._speed          = MathUtil::max( 1.0f, node.getAttributeFloat( "speed", _flight._speed ) );
            _flight._altitude       = MathUtil::max( 1.0f, node.getAttributeFloat( "altitude", _flight._altitude ) );
            _flight._jumpStartRatio = MathUtil::saturate( node.getAttributeFloat( "jumpStart", _flight._jumpStartRatio ) );
            _flight._jumpEndRatio   = MathUtil::clamp( node.getAttributeFloat( "jumpEnd", _flight._jumpEndRatio ), _flight._jumpStartRatio, 1.0f );
            _flight._offsetRatio    = MathUtil::clamp( node.getAttributeFloat( "offset", _flight._offsetRatio ), 0.0f, 0.95f );
        }
        if ( const XmlNode node = root.findChild( "Fall" ) )
        {
            _fall._freeFallSpeed            = MathUtil::max( 0.1f, node.getAttributeFloat( "freeFallSpeed", _fall._freeFallSpeed ) );
            _fall._freeFallHorizontalSpeed  = MathUtil::max( 0.0f, node.getAttributeFloat( "freeFallHorizontal", _fall._freeFallHorizontalSpeed ) );
            _fall._parachuteSpeed           = MathUtil::max( 0.1f, node.getAttributeFloat( "parachuteSpeed", _fall._parachuteSpeed ) );
            _fall._parachuteHorizontalSpeed = MathUtil::max( 0.0f, node.getAttributeFloat( "parachuteHorizontal", _fall._parachuteHorizontalSpeed ) );
            _fall._autoOpenHeight           = MathUtil::max( 0.0f, node.getAttributeFloat( "autoOpenHeight", _fall._autoOpenHeight ) );
        }
        if ( const XmlNode zoneNode = root.findChild( "Zone" ) )
        {
            _zone._startRadius    = MathUtil::max( 0.0f, zoneNode.getAttributeFloat( "startRadius", _zone._startRadius ) );
            _zone._centerAttempts = MathUtil::max( 1, zoneNode.getAttributeInt( "centerAttempts", _zone._centerAttempts ) );
            _zone._listPhase.clear();
            for ( XmlNode node = zoneNode.findChild( "Phase" ); node; node = node.findNextSibling( "Phase" ) )
            {
                BrZonePhaseDef phase;
                phase._waitTime        = MathUtil::max( 0.0f, node.getAttributeFloat( "wait", phase._waitTime ) );
                phase._shrinkTime      = MathUtil::max( 0.0f, node.getAttributeFloat( "shrink", phase._shrinkTime ) );
                phase._radiusRatio     = MathUtil::clamp( node.getAttributeFloat( "ratio", phase._radiusRatio ), 0.0f, 1.0f );
                phase._damagePerSecond = MathUtil::max( 0.0f, node.getAttributeFloat( "damage", phase._damagePerSecond ) );
                _zone._listPhase.push_back( phase );
            }
        }

        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Armor" ); node; node = node.findNextSibling( "Armor" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            BrArmorDef def;
            def._id                = hashed_string( pId );
            const string_view slot = node.getAttributeText( "slot" );
            def._slot              = hashed_string( slot.empty() ? string_view( "Vest" ) : slot );
            def._tier              = MathUtil::clamp( node.getAttributeInt( "tier", def._tier ), 1, 9 );
            def._reduction         = MathUtil::clamp( node.getAttributeFloat( "reduction", def._reduction ), 0.0f, 0.95f );
            def._durability        = MathUtil::max( 1.0f, node.getAttributeFloat( "durability", def._durability ) );
            (void)_armorCatalog.add( def );
            ++loadedCount;
        }
        for ( XmlNode node = root.findChild( "Backpack" ); node; node = node.findNextSibling( "Backpack" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            BrBackpackDef def;
            def._id       = hashed_string( pId );
            def._tier     = MathUtil::clamp( node.getAttributeInt( "tier", def._tier ), 1, 9 );
            def._capacity = MathUtil::max( 0.0f, node.getAttributeFloat( "capacity", def._capacity ) );
            (void)_backpackCatalog.add( def );
            ++loadedCount;
        }
        for ( XmlNode node = root.findChild( "LootSpot" ); node; node = node.findNextSibling( "LootSpot" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            BrLootSpotDef def;
            def._id                 = hashed_string( pId );
            const string_view table = node.getAttributeText( "table" );
            def._tableId            = table.empty() ? def._id : hashed_string( table );
            def._chance             = MathUtil::saturate( node.getAttributeFloat( "chance", def._chance ) );
            def._minRolls           = MathUtil::max( 0, node.getAttributeInt( "rolls", def._minRolls ) );
            def._maxRolls           = MathUtil::max( def._minRolls, node.getAttributeInt( "rollsMax", def._minRolls ) );
            (void)_lootSpotCatalog.add( def );
            ++loadedCount;
        }
        if ( const XmlNode node = root.findChild( "SupplyDrop" ) )
        {
            const string_view table = node.getAttributeText( "table" );
            _supplyDrop._tableId    = hashed_string( table );
            _supplyDrop._listTime.clear();
            GameDataXml::forEachToken( node.getAttributeText( "times" ), ",; ", [&]( string_view token )
            {
                float32 time = 0.0f;
                if ( StringUtil::parseFloat( token, time ) && time >= 0.0f )
                    _supplyDrop._listTime.push_back( time );
                else
                    SW_LOG_WARNING( "%#: supply drop time '%#' is not a number - skipped", sourceName, token );
            } );
            std::sort( _supplyDrop._listTime.begin(), _supplyDrop._listTime.end() );
        }

        if ( _zone._listPhase.empty() )
            SW_LOG_WARNING( "%#: no zone phases - the zone never shrinks", sourceName );
        return loadedCount + static_cast<uint32>( _zone._listPhase.size() );
    }
} // namespace sw
