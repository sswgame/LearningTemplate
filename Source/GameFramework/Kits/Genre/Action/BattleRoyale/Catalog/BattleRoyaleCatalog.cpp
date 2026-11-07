#include "pch.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Catalog/BattleRoyaleCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"

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

    uint32 BattleRoyaleCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        // 맵 크기가 깨진 글을 앞의 크기로 읽어 나가지 않는다(자기장 · 비행 경로가 말없이 다른 맵에서 돈다).
        BattleRoyaleCatalogRootDef rootDef;
        if ( XMLFields::readFields( root, rootDef, sourceName ) == false )
            return 0;
        _mapSize = rootDef._mapSize;

        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            if ( loadFromXML( node, name, sourceName ) )
                ++loadedCount;
        }
        if ( _zone._listPhase.empty() )
            SW_LOG_WARNING_ID( "BattleRoyaleCatalog.NoZonePhasesZone", "%#: no zone phases - the zone never shrinks", sourceName );
        return loadedCount;
    }

    bool BattleRoyaleCatalog::loadFromXML( const XMLNode& node, string_view name, string_view sourceName )
    {
        if ( XMLReadUtil::isElement( name, "Armor" ) )
            return loadArmor( node, sourceName );
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
        SW_LOG_ERROR_ID( "BattleRoyaleCatalog.UnknownElementSkipped", "%#: unknown element <%#> - skipped", sourceName, name );
        return false;
    }

    bool BattleRoyaleCatalog::loadArmor( const XMLNode& node, string_view sourceName )
    {
        BattleRoyaleArmorDef armor;
        if ( XMLFields::readFields( node, armor, sourceName ) == false )
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
        bool                bValid = XMLFields::readFields( node, zone, sourceName );
        for ( XMLNode phaseNode = node.findChild( "Phase" ); phaseNode; phaseNode = phaseNode.findNextSibling( "Phase" ) )
        {
            BattleRoyaleZonePhaseDef phase;
            if ( XMLFields::readFields( phaseNode, phase, sourceName ) )
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
        if ( XMLFields::readFields( node, supplyDrop, sourceName ) == false )
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
