#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/GhostHunt/GhostCatalog.h"

#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

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

    uint32 GhostCatalog::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readProperties 가 남긴다).
        GhostHuntRootDef rootDef;
        if ( root.readProperties( rootDef, sourceName ) )
            _currency = rootDef._currency;
        using Element                          = XMLCatalogElement<GhostCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&GhostCatalog::_flashlight>( "Flashlight" ),
            Element::of<&GhostCatalog::loadVacuum>( "Vacuum" ),
            Element::of<&GhostCatalog::_ghostCatalog>( "Ghost" ),
            Element::of<&GhostCatalog::_roomCatalog>( "Room" ),
            Element::of<&GhostCatalog::_doorCatalog>( "Door" ),
            Element::of<&GhostCatalog::loadFurniture>( "Furniture" ),
            Element::of<&GhostCatalog::_booCatalog>( "Boo" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        return result._loadedCount;
    }

    bool GhostCatalog::loadVacuum( const XMLNode& node, string_view sourceName )
    {
        GhostVacuumDef vacuum;
        if ( XMLFields::readProperties( node, vacuum, sourceName ) == false )
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

    bool GhostCatalog::loadFurniture( const XMLNode& node, string_view sourceName )
    {
        GhostFurnitureDef furniture;
        bool              bValid = XMLFields::readProperties( node, furniture, sourceName, { "search" } );
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

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<GhostCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( GhostCatalog, GhostCatalog, SW_KIT_API, GhostCatalog::kAssetType.withLoader( AssetTypeLoader::of<GhostCatalog>( MemoryTag::Game ) ) );
} // namespace sw
