#include "pch.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Catalog/BattleRoyaleCatalog.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/GameData/GameDataXML.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "BattleRoyaleCatalog" );

    BattleRoyaleCatalog::BattleRoyaleCatalog()
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

    uint32 BattleRoyaleCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        BattleRoyaleCatalogRootDef rootDef;
        if ( GameDataXML::readFields( root, rootDef, sourceName ) )
            _mapSize = rootDef._mapSize;

        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            if ( loadElement( node, name, sourceName ) )
                ++loadedCount;
        }
        if ( _zone._listPhase.empty() )
            SW_LOG_WARNING( "%#: no zone phases - the zone never shrinks", sourceName );
        return loadedCount;
    }

    bool BattleRoyaleCatalog::loadElement( const XMLNode& node, string_view name, string_view sourceName )
    {
        if ( XMLReadUtil::isElement( name, "Armor" ) )
            return GameDataXML::readInto( node, sourceName, _armorCatalog );
        if ( XMLReadUtil::isElement( name, "Backpack" ) )
            return GameDataXML::readInto( node, sourceName, _backpackCatalog );
        if ( XMLReadUtil::isElement( name, "LootSpot" ) )
            return GameDataXML::readInto( node, sourceName, _lootSpotCatalog );
        if ( XMLReadUtil::isElement( name, "Player" ) )
            return GameDataXML::readSetting( node, sourceName, _player );
        if ( XMLReadUtil::isElement( name, "Flight" ) )
            return GameDataXML::readSetting( node, sourceName, _flight );
        if ( XMLReadUtil::isElement( name, "Fall" ) )
            return GameDataXML::readSetting( node, sourceName, _fall );
        if ( XMLReadUtil::isElement( name, "Zone" ) )
            return loadZone( node, sourceName );
        if ( XMLReadUtil::isElement( name, "SupplyDrop" ) )
            return loadSupplyDrop( node, sourceName );
        SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, name );
        return false;
    }

    bool BattleRoyaleCatalog::loadZone( const XMLNode& node, string_view sourceName )
    {
        BattleRoyaleZoneDef zone;
        bool                bValid = GameDataXML::readFields( node, zone, sourceName );
        for ( XMLNode phaseNode = node.findChild( "Phase" ); phaseNode; phaseNode = phaseNode.findNextSibling( "Phase" ) )
        {
            BattleRoyaleZonePhaseDef phase;
            if ( GameDataXML::readFields( phaseNode, phase, sourceName ) )
                zone._listPhase.push_back( phase );
            else
                bValid = false;
        }
        if ( bValid == false )
            return false;
        _zone = zone;
        return true;
    }

    bool BattleRoyaleCatalog::loadSupplyDrop( const XMLNode& node, string_view sourceName )
    {
        BattleRoyaleSupplyDropDef supplyDrop;
        bool                      bValid = GameDataXML::readFields( node, supplyDrop, sourceName, { "times" } );
        StringUtil::forEachToken( node.getAttributeText( "times" ), ",; ", [&]( string_view token )
        {
            float32 time = 0.0f;
            if ( StringUtil::parseFloat( token, time ) && time >= 0.0f )
                supplyDrop._listTime.push_back( time );
            else
            {
                SW_LOG_ERROR( "%#: <SupplyDrop times> has '%#', which is not a time of 0 or more", sourceName, token );
                bValid = false;
            }
        } );
        if ( bValid == false )
            return false;
        std::sort( supplyDrop._listTime.begin(), supplyDrop._listTime.end() );
        _supplyDrop = supplyDrop;
        return true;
    }
} // namespace sw
