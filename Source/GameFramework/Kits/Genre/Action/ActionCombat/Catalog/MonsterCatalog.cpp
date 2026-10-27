#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Catalog/MonsterCatalog.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

#include <optional>

namespace sw
{
    SW_LOG_CALLER( "MonsterCatalog" );

    MonsterCatalog::MonsterCatalog()
        : _mapMonster{}
    {
    }

    MonsterCatalog::~MonsterCatalog()
    {
        clear();
    }

    bool MonsterCatalog::loadFromResource( const ResourceID& resourceID )
    {
        const string_view assetRelativePath = resourceID.view();
        clear();
        if ( XMLCatalog<MonsterCatalog>::loadFromResource( resourceID ) )
            return true;
        // 파일이나 루트가 없거나 `<Monster>` 가 하나도 없는 경우다(읽는 쪽이 이미 원인을 알렸다). 코드 폴백 없이 빈 카탈로그로 둔다.
        SW_LOG_ERROR( "%# gave no monster definitions - the monster catalog stays empty", assetRelativePath );
        return false;
    }

    uint32 MonsterCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 각 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 몬스터를 넣지 않고 오류다(readFields 가 남긴다).
        uint32 rejectedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const bool bMonster = StringUtil::equals( node.getName(), "Monster" );
            if ( bMonster == false )
                SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, node.getName() );
            if ( bMonster == false || loadMonster( node, sourceName ) == false )
                ++rejectedCount; // 원인은 위 줄이나 loadMonster 가 남겼다
        }
        if ( rejectedCount > 0 )
            SW_LOG_ERROR( "%#: %# element(s) were not loaded (listed above)", sourceName, rejectedCount );

        // **읽었는데 하나도 없으면 실패로 본다**(0을 반환하면 호출한 쪽이 폴백을 채운다). 성공으로 넘기면 `<Monster>` 를 `<monster>` 로 잘못 적은 오타 하나가
        // **텅 빈 카탈로그**가 되고, 모든 `findMonster` 가 nullptr 을 반환해서 그 널을 다루는 쪽이 조용히 아무것도 하지 않는다.
        if ( _mapMonster.empty() )
        {
            SW_LOG_WARNING( "No <Monster> entries in %#", sourceName );
            return 0;
        }

        SW_LOG_INFO( "Loaded %# monster definitions from %#", static_cast<int32>( _mapMonster.size() ), sourceName );
        return static_cast<uint32>( _mapMonster.size() );
    }

    bool MonsterCatalog::loadMonster( const XMLNode& node, string_view sourceName )
    {
        MonsterDef monsterDef;
        bool       bValid = GameDataXML::readFields( node, monsterDef, sourceName );
        // 하위 원소의 오류 줄에 어느 몬스터인지 적는다(하위 원소에는 id 가 없다).
        const string where = string( sourceName ) + " <Monster id=\"" + monsterDef._id + "\">";
        for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
        {
            const string_view name = child.getName() != nullptr ? string_view( child.getName() ) : string_view{};
            if ( name == "Stats" )
                bValid = GameDataXML::readFields( child, monsterDef._stats, where ) && bValid;
            else if ( name == "AI" )
                bValid = GameDataXML::readFields( child, monsterDef._ai, where ) && bValid;
            else if ( name == "Shot" )
            {
                // 사격은 줄마다 한 발이다. 한 번에 쏘는 패턴(겨냥, 옆, 부채)을 코드 없이 적을 수 있다.
                MonsterShotDef shot;
                bValid = GameDataXML::readFields( child, shot, where ) && bValid;
                monsterDef._listShot.push_back( shot );
            }
            else if ( name == "Prefab" )
            {
                const std::optional<string> path = child.readRequiredAttribute<string>( "path" );
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
                        SW_LOG_ERROR( "%#: <Drop %#=\"%#\"> needs an integer of 0 or more", where, pRewardID, attr.getValue() );
                        bValid = false;
                        continue;
                    }
                    monsterDef._mapDrop.insert_or_assign( hashed_string( pRewardID ), amount );
                }
            }
            else
            {
                SW_LOG_ERROR( "%#: unknown child <%#>", where, name );
                bValid = false;
            }
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
