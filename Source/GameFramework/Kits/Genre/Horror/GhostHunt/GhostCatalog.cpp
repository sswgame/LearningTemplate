#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/GhostHunt/GhostCatalog.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/GameData/GameDataXML.h"

namespace sw
{
    SW_LOG_CALLER( "GhostCatalog" );

    GhostCatalog::GhostCatalog()
        : _flashlight{}
        , _vacuum{}
        , _ghostCatalog{}
        , _roomCatalog{}
        , _doorCatalog{}
        , _furnitureCatalog{}
        , _booCatalog{}
        , _currency{ GhostHuntRootDef{}._currency }
    {
    }

    uint32 GhostCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        GhostHuntRootDef rootDef;
        if ( GameDataXML::readFields( root, rootDef, sourceName ) )
            _currency = rootDef._currency;
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            if ( loadElement( node, name, sourceName ) )
                ++loadedCount;
        }
        // 방이 가리키는 유령은 모두 읽은 뒤에 확인한다(방이 유령보다 앞에 적혀도 된다).
        for ( const GhostRoomDef& room : _roomCatalog.getAll() )
        {
            for ( const hashed_string& ghostID : room._listGhost )
            {
                if ( _ghostCatalog.find( ghostID ) == nullptr )
                    SW_LOG_ERROR( "%#: room '%#' names an unknown ghost '%#'", sourceName, room._id.c_str(), ghostID.c_str() );
            }
        }
        return loadedCount;
    }

    bool GhostCatalog::loadElement( const XMLNode& node, string_view name, string_view sourceName )
    {
        if ( XMLReadUtil::isElement( name, "Flashlight" ) )
            return GameDataXML::readSetting( node, sourceName, _flashlight );
        if ( XMLReadUtil::isElement( name, "Vacuum" ) )
            return loadVacuum( node, sourceName );
        if ( XMLReadUtil::isElement( name, "Ghost" ) )
            return GameDataXML::readInto( node, sourceName, _ghostCatalog );
        if ( XMLReadUtil::isElement( name, "Room" ) )
            return loadRoom( node, sourceName );
        if ( XMLReadUtil::isElement( name, "Door" ) )
            return GameDataXML::readInto( node, sourceName, _doorCatalog );
        if ( XMLReadUtil::isElement( name, "Furniture" ) )
            return loadFurniture( node, sourceName );
        if ( XMLReadUtil::isElement( name, "Boo" ) )
            return GameDataXML::readInto( node, sourceName, _booCatalog );
        SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, name );
        return false;
    }

    bool GhostCatalog::loadVacuum( const XMLNode& node, string_view sourceName )
    {
        GhostVacuumDef vacuum;
        bool           bValid = GameDataXML::readFields( node, vacuum, sourceName, { "stages" } );
        // 단계 위력은 적었으면 모두 0 이상의 수여야 한다(적지 않으면 스키마 초기값 한 단계).
        const string_view stages = node.getAttributeText( "stages" );
        if ( stages.empty() == false )
        {
            vacuum._listStagePower.clear();
            StringUtil::forEachToken( stages, ",; ", [&]( string_view token )
            {
                float32    power  = 0.0f;
                const bool bPower = StringUtil::parseFloat( token, power ) && power >= 0.0f;
                if ( bPower )
                    vacuum._listStagePower.push_back( power );
                else
                {
                    SW_LOG_ERROR( "%#: <Vacuum stages> has '%#' - needs numbers of 0 or more", sourceName, token );
                    bValid = false;
                }
            } );
        }
        if ( bValid )
            _vacuum = vacuum;
        return bValid;
    }

    bool GhostCatalog::loadRoom( const XMLNode& node, string_view sourceName )
    {
        GhostRoomDef room;
        if ( GameDataXML::readFields( node, room, sourceName, { "ghosts" } ) == false )
            return false;
        XMLReadUtil::parseNameList( node.getAttributeText( "ghosts" ), ",; ", room._listGhost );
        (void)_roomCatalog.add( room ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return true;
    }

    bool GhostCatalog::loadFurniture( const XMLNode& node, string_view sourceName )
    {
        GhostFurnitureDef furniture;
        bool              bValid = GameDataXML::readFields( node, furniture, sourceName, { "search" } );
        // 뒤지는 방법은 적었으면 그것만 된다(적지 않으면 둘 다). 모르는 이름은 오류다.
        const string_view search = node.getAttributeText( "search" );
        if ( search.empty() == false )
        {
            furniture._bVacuum = false;
            furniture._bShake  = false;
            StringUtil::forEachToken( search, ",; ", [&]( string_view token )
            {
                if ( StringUtil::equals( token, string_view( "Vacuum" ) ) )
                    furniture._bVacuum = true;
                else if ( StringUtil::equals( token, string_view( "Shake" ) ) )
                    furniture._bShake = true;
                else
                {
                    SW_LOG_ERROR( "%#: furniture '%#' has an unknown search mode '%#' (Vacuum, Shake)", sourceName, furniture._id.c_str(), token );
                    bValid = false;
                }
            } );
        }
        if ( bValid )
            (void)_furnitureCatalog.add( furniture ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return bValid;
    }
} // namespace sw
