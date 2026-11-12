#include "pch.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Catalog/BattleRoyaleCatalog.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

#include <algorithm>

namespace sw
{
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

    const hashed_string& BattleRoyaleCatalog::getHelmetSlotName()
    {
        static const hashed_string name( "Helmet" );
        return name;
    }

    uint32 BattleRoyaleCatalog::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readProperties 가 남긴다).
        // 맵 크기가 깨진 글을 앞의 크기로 읽어 나가지 않는다(자기장 · 비행 경로가 말없이 다른 맵에서 돈다).
        BattleRoyaleCatalogRootDef rootDef;
        if ( root.readProperties( rootDef, sourceName ) == false )
            return 0;
        _mapSize = rootDef._mapSize;

        using Element                          = XMLCatalogElement<BattleRoyaleCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&BattleRoyaleCatalog::loadArmor>( "Armor" ),
            Element::of<&BattleRoyaleCatalog::_backpackCatalog>( "Backpack" ),
            Element::of<&BattleRoyaleCatalog::_lootSpotCatalog>( "LootSpot" ),
            Element::of<&BattleRoyaleCatalog::_player>( "Player" ),
            Element::of<&BattleRoyaleCatalog::_flight>( "Flight" ),
            Element::of<&BattleRoyaleCatalog::_fall>( "Fall" ),
            Element::of<&BattleRoyaleCatalog::loadZone>( "Zone" ),
            Element::of<&BattleRoyaleCatalog::loadSupplyDrop>( "SupplyDrop" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        if ( _zone._listPhase.empty() )
            SW_LOG_WARNING_ID( "BattleRoyaleCatalog.NoZonePhasesZone", "%#: no zone phases - the zone never shrinks", sourceName );
        return result._loadedCount;
    }

    bool BattleRoyaleCatalog::loadArmor( const XMLNode& node, string_view sourceName )
    {
        BattleRoyaleArmorDef armor;
        if ( XMLFields::readProperties( node, armor, sourceName ) == false )
            return false;
        // 장비는 헬멧이 아니면 조끼로 입는다. 슬롯 오타가 말없이 조끼가 되지 않게 여기서 막는다.
        if ( armor._slot != BattleRoyaleCatalog::getHelmetSlotName() && armor._slot != hashed_string( "Vest" ) )
        {
            SW_LOG_ERROR_ID( "BattleRoyaleCatalog.ArmorUnknownSlot", "%#: <Armor id=\"%#\"> slot '%#' is neither Helmet nor Vest", sourceName, armor._id.c_str(), armor._slot.c_str() );
            return false;
        }
        (void)_armorCatalog.add( armor );
        return true;
    }

    bool BattleRoyaleCatalog::loadZone( const XMLNode& node, string_view sourceName )
    {
        BattleRoyaleZoneDef zone;
        bool                bValid = XMLFields::readProperties( node, zone, sourceName );
        for ( XMLNode phaseNode = node.findChild( "Phase" ); phaseNode; phaseNode = phaseNode.findNextSibling( "Phase" ) )
        {
            BattleRoyaleZonePhaseDef phase;
            if ( XMLFields::readProperties( phaseNode, phase, sourceName ) )
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
        if ( XMLFields::readProperties( node, supplyDrop, sourceName ) == false )
            return false;
        VectorUtil::sort( supplyDrop._listTime );
        _supplyDrop = supplyDrop;
        return true;
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<BattleRoyaleCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( BattleRoyaleCatalog, BattleRoyaleCatalog, SW_KIT_API, BattleRoyaleCatalog::kAssetType.withLoader( AssetTypeLoader::of<BattleRoyaleCatalog>( MemoryTag::Game ) ) );
} // namespace sw
