#include "pch.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Catalog/BrCatalog.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct BrCatalogInternal
        {
            /** @brief 원소 하나를 스키마 @p TDef 로 읽어 카탈로그에 더합니다. 스키마 오류면 더하지 않고 false 입니다. */
            template <typename TDef>
            [[nodiscard]] static bool readInto( const XMLNode& node, string_view sourceName, GameCatalog<TDef>& outCatalog )
            {
                TDef def;
                if ( GameDataXML::readFields( node, def, sourceName ) == false )
                    return false;
                (void)outCatalog.add( def ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
                return true;
            }

            /** @brief 설정 원소 하나를 읽어 오류가 없을 때만 바꿉니다(기본값은 스키마 초기값 — 앞의 값을 이어받지 않는다). */
            template <typename TDef>
            [[nodiscard]] static bool readSetting( const XMLNode& node, string_view sourceName, TDef& outSetting )
            {
                TDef def;
                if ( GameDataXML::readFields( node, def, sourceName ) == false )
                    return false;
                outSetting = def;
                return true;
            }
        };
    } // namespace

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

    uint32 BrCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        BrCatalogRootDef rootDef;
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

    bool BrCatalog::loadElement( const XMLNode& node, string_view name, string_view sourceName )
    {
        using Internal = BrCatalogInternal;
        const auto is  = [name]( const utf8* pName )
        { return StringUtil::equals( name, string_view( pName ), true ); };
        if ( is( "Armor" ) )
            return Internal::readInto( node, sourceName, _armorCatalog );
        if ( is( "Backpack" ) )
            return Internal::readInto( node, sourceName, _backpackCatalog );
        if ( is( "LootSpot" ) )
            return Internal::readInto( node, sourceName, _lootSpotCatalog );
        if ( is( "Player" ) )
            return Internal::readSetting( node, sourceName, _player );
        if ( is( "Flight" ) )
            return Internal::readSetting( node, sourceName, _flight );
        if ( is( "Fall" ) )
            return Internal::readSetting( node, sourceName, _fall );
        if ( is( "Zone" ) )
            return loadZone( node, sourceName );
        if ( is( "SupplyDrop" ) )
            return loadSupplyDrop( node, sourceName );
        SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, name );
        return false;
    }

    bool BrCatalog::loadZone( const XMLNode& node, string_view sourceName )
    {
        BrZoneDef zone;
        bool      bValid = GameDataXML::readFields( node, zone, sourceName );
        for ( XMLNode phaseNode = node.findChild( "Phase" ); phaseNode; phaseNode = phaseNode.findNextSibling( "Phase" ) )
        {
            BrZonePhaseDef phase;
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

    bool BrCatalog::loadSupplyDrop( const XMLNode& node, string_view sourceName )
    {
        BrSupplyDropDef supplyDrop;
        bool            bValid = GameDataXML::readFields( node, supplyDrop, sourceName, { "times" } );
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
