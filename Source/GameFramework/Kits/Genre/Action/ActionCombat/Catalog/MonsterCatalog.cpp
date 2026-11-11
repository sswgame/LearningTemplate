#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Catalog/MonsterCatalog.h"

#include "Core/Foundation/Expected.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    MonsterCatalog::MonsterCatalog()
        : _mapMonster{}
    {
    }

    MonsterCatalog::~MonsterCatalog()
    {
        clear();
    }

    bool MonsterCatalog::loadFromAsset( const AssetID& assetID )
    {
        const string_view assetRelativePath = assetID.view();
        clear();
        if ( XMLCatalog<MonsterCatalog>::loadFromAsset( assetID ) )
            return true;
        // 파일이나 루트가 없거나 `<Monster>` 가 하나도 없는 경우다(읽는 쪽이 이미 원인을 알렸다). 코드 폴백 없이 빈 카탈로그로 둔다.
        SW_LOG_ERROR_ID( "MonsterCatalog.GaveNoMonsterDefinitions", "%# gave no monster definitions - the monster catalog stays empty", assetRelativePath );
        return false;
    }

    uint32 MonsterCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 각 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 몬스터를 넣지 않고 오류다(readProperties 가 남긴다).
        using Element                          = XMLCatalogElement<MonsterCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&MonsterCatalog::loadMonster>( "Monster" ),
        };
        // **읽었는데 하나도 없으면 실패로 본다.** 성공으로 넘기면 `<Monster>` 를 `<monster>` 로 잘못 적은 오타 하나가
        // **텅 빈 카탈로그**가 되고, 모든 `findMonster` 가 nullptr 을 반환해서 그 널을 다루는 쪽이 조용히 아무것도 하지 않는다.
        const uint32 loadedCount = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement )._loadedCount;
        if ( loadedCount > 0 )
            SW_LOG_INFO_ID( "MonsterCatalog.LoadedMonsterDefinitions", "Loaded %# monster definitions from %#", static_cast<int32>( loadedCount ), sourceName );
        return loadedCount;
    }

    bool MonsterCatalog::loadMonster( const XMLNode& node, string_view sourceName )
    {
        MonsterDef monsterDef;
        bool       bValid = XMLFields::readProperties( node, monsterDef, sourceName );
        // 하위 원소의 오류 줄에 어느 몬스터인지 적는다(하위 원소에는 id 가 없다).
        const string where = string( sourceName ) + " <Monster id=\"" + monsterDef._id + "\">";
        for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
        {
            const string_view name = child.getName() != nullptr ? string_view( child.getName() ) : string_view{};
            if ( name == "Stats" )
                bValid = XMLFields::readProperties( child, monsterDef._stats, where ) && bValid;
            else if ( name == "AI" )
                bValid = XMLFields::readProperties( child, monsterDef._ai, where ) && bValid;
            else if ( name == "Shot" )
            {
                // 사격은 줄마다 한 발이다. 한 번에 쏘는 패턴(겨냥, 옆, 부채)을 코드 없이 적을 수 있다.
                MonsterShotDef shot;
                bValid = XMLFields::readProperties( child, shot, where ) && bValid;
                monsterDef._listShot.push_back( shot );
            }
            else if ( name == "Prefab" )
            {
                const auto path = child.readRequiredAttribute<string>( "path" );
                if ( path.has_value() )
                    monsterDef._prefabPath = *path;
                else
                    bValid = false;
            }
            else if ( name == "Drop" )
            {
                // 속성 이름이 곧 보상 이름이다. 코드가 보상 종류를 알 필요가 없다. 수량은 0 이상의 정수여야 한다.
                for ( XMLAttribute attr = child.getFirstAttribute(); attr.isValid(); attr = attr.getNext() )
                {
                    const utf8* const pRewardID = attr.getName();
                    int32             amount    = 0;
                    const bool        bAmount   = StringUtil::parseInt( attr.getValue() != nullptr ? attr.getValue() : "", amount ) && amount >= 0;
                    if ( bAmount == false )
                    {
                        SW_LOG_ERROR_ID( "MonsterCatalog.DropNeedsIntegerMore", "%#: <Drop %#=\"%#\"> needs an integer of 0 or more", where, pRewardID, attr.getValue() );
                        bValid = false;
                        continue;
                    }
                    monsterDef._mapDrop.insert_or_assign( hashed_string( pRewardID ), amount );
                }
            }
            else
            {
                SW_LOG_ERROR_ID( "MonsterCatalog.UnknownChild", "%#: unknown child <%#>", where, name );
                bValid = false;
            }
        }
        // 쏘는 종의 공격 간격이 0 이면 프레임마다 쏜다(발사 빈도가 fps 에 묶인다).
        if ( monsterDef._listShot.empty() == false && monsterDef._ai._attackCoolTime <= 0.0f )
        {
            SW_LOG_ERROR_ID( "MonsterCatalog.ShooterNeedsCoolTime", "%#: a monster with <Shot> needs <AI coolTime=\"...\"> above 0", where );
            bValid = false;
        }
        if ( bValid )
            _mapMonster[hashed_string( monsterDef._id.c_str() )] = monsterDef;
        return bValid;
    }

    const MonsterDef* MonsterCatalog::findMonster( const hashed_string& id ) const
    {
        auto mapIter = _mapMonster.find( id );
        if ( mapIter != _mapMonster.end() )
            return &mapIter->second;
        return nullptr;
    }

    const unordered_map<hashed_string, MonsterDef>& MonsterCatalog::getAllMonsters() const
    {
        return _mapMonster;
    }

    void MonsterCatalog::clear()
    {
        _mapMonster.clear();
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<MonsterCatalog>::acquire`). 복사 · 이동을 막은 형식이라 다시 읽으면 떼어 낸다. 팩의 `data/monsters.xml` 은 경로 규칙, 그 밖은 접미사로 가른다. */
    SW_MODULE_SHARED_ASSET_CACHE( MonsterCatalog, MonsterCatalog, SW_KIT_API, MonsterCatalog::kAssetType.withLoader( AssetTypeLoader::of<MonsterCatalog>( MemoryTag::Game, SharedAssetReloadMode::Detach ) ) );
} // namespace sw
