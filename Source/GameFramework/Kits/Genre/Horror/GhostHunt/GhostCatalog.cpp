#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/GhostHunt/GhostCatalog.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"

namespace sw
{
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
        if ( XMLFields::readFields( root, rootDef, sourceName ) )
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
                    SW_LOG_ERROR_ID( "GhostCatalog.RoomNamesUnknownGhost", "%#: room '%#' names an unknown ghost '%#'", sourceName, room._id.c_str(), ghostID.c_str() );
            }
        }
        // 없는 가구에 숨은 부는 뒤져서 찾을 수 없다(가구가 부보다 뒤에 적혀도 된다).
        for ( const HidingGhostDef& boo : _booCatalog.getAll() )
        {
            if ( boo._furniture.empty() == false && _furnitureCatalog.find( boo._furniture ) == nullptr )
                SW_LOG_ERROR_ID( "GhostCatalog.BooNamesUnknownFurniture", "%#: boo '%#' hides in an unknown furniture '%#'", sourceName, boo._id.c_str(), boo._furniture.c_str() );
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
        SW_LOG_ERROR_ID( "GhostCatalog.UnknownElementSkipped", "%#: unknown element <%#> - skipped", sourceName, name );
        return false;
    }

    bool GhostCatalog::loadVacuum( const XMLNode& node, string_view sourceName )
    {
        GhostVacuumDef vacuum;
        if ( XMLFields::readFields( node, vacuum, sourceName ) == false )
            return false;
        // 단계가 없으면 흡입 피해가 0 이라 유령을 잡을 수 없다.
        if ( vacuum._listStagePower.empty() )
        {
            SW_LOG_ERROR_ID( "GhostCatalog.VacuumNeedsStages", "%#: <Vacuum> needs stages=\"...\"", sourceName );
            return false;
        }
        _vacuum = vacuum;
        return true;
    }

    bool GhostCatalog::loadRoom( const XMLNode& node, string_view sourceName )
    {
        GhostRoomDef room;
        if ( XMLFields::readFields( node, room, sourceName ) == false )
            return false;
        return GameDataXML::addUnique( room, sourceName, _roomCatalog );
    }

    bool GhostCatalog::loadFurniture( const XMLNode& node, string_view sourceName )
    {
        GhostFurnitureDef furniture;
        bool              bValid = XMLFields::readFields( node, furniture, sourceName, { "search" } );
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
                    SW_LOG_ERROR_ID( "GhostCatalog.FurnitureUnknownSearchMode", "%#: furniture '%#' has an unknown search mode '%#' (Vacuum, Shake)", sourceName, furniture._id.c_str(), token );
                    bValid = false;
                }
            } );
            // 구분자만 적으면 어느 방법으로도 뒤질 수 없는 가구가 된다.
            if ( bValid && furniture._bVacuum == false && furniture._bShake == false )
            {
                SW_LOG_ERROR_ID( "GhostCatalog.FurnitureNeedsSearchMode", "%#: furniture '%#' has an empty search mode (Vacuum, Shake)", sourceName, furniture._id.c_str() );
                bValid = false;
            }
        }
        return bValid && GameDataXML::addUnique( furniture, sourceName, _furnitureCatalog );
    }
} // namespace sw
