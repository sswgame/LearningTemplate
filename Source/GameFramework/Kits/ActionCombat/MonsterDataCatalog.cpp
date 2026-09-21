#include "pch.h"

#include "GameFramework/Kits/ActionCombat/MonsterDataCatalog.h"

#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    SW_LOG_CALLER( "MonsterDataCatalog" );

    MonsterDataCatalog::MonsterDataCatalog()
        : _mapMonster{}
    {
    }

    MonsterDataCatalog::~MonsterDataCatalog()
    {
        clear();
    }

    void MonsterDataCatalog::seedFallback()
    {
        _mapMonster.clear();

        MonsterDef defaultMonster;
        defaultMonster._id             = "default_monster";
        defaultMonster._name           = "Default Monster";
        defaultMonster._archetype      = MonsterArchetype::MeleePatrol;
        defaultMonster._hp             = 100;
        defaultMonster._maxHp          = 100;
        defaultMonster._atk            = 10;
        defaultMonster._def            = 0;
        defaultMonster._speed          = 150.0f;
        defaultMonster._patrolRange    = 200.0f;
        defaultMonster._detectRange    = 400.0f;
        defaultMonster._attackRange    = 50.0f;
        defaultMonster._attackCoolTime = 1.5f;

        _mapMonster[hashed_string( defaultMonster._id.c_str() )] = defaultMonster;
    }

    bool MonsterDataCatalog::loadFromResource( string_view assetRelativePath )
    {
        clear();

        XmlDocument doc;
        string      absPath;
        if ( doc.loadResource( assetRelativePath, &absPath ) == false )
        {
            SW_LOG_WARNING( "Failed to read %# — using fallback monster definitions.", assetRelativePath );
            seedFallback();
            return false;
        }

        XmlNode root = doc.getRoot( "MonsterCatalog" );
        if ( root.isValid() == false )
        {
            SW_LOG_WARNING( "Missing <MonsterCatalog> root in %# — using fallback.", absPath );
            seedFallback();
            return false;
        }

        for ( XmlNode node = root.findChild( "Monster" ); node; node = node.findNextSibling( "Monster" ) )
        {
            const utf8* pIdStr = node.findAttribute( "id" );
            if ( StringUtil::isNullOrEmpty( pIdStr ) )
                continue;

            MonsterDef monsterDef;
            monsterDef._id       = pIdStr;
            const utf8* pNameStr = node.findAttribute( "name" );
            if ( pNameStr != nullptr )
                monsterDef._name = pNameStr;
            else
                monsterDef._name = monsterDef._id;

            monsterDef._archetype = parseArchetype( node.findAttribute( "archetype" ) );

            XmlNode statsNode = node.findChild( "Stats" );
            if ( statsNode.isValid() )
            {
                monsterDef._hp            = statsNode.getAttributeInt( "hp", monsterDef._hp );
                monsterDef._maxHp         = statsNode.getAttributeInt( "maxHp", monsterDef._maxHp );
                monsterDef._atk           = statsNode.getAttributeInt( "atk", monsterDef._atk );
                monsterDef._def           = statsNode.getAttributeInt( "def", monsterDef._def );
                monsterDef._speed         = statsNode.getAttributeFloat( "speed", monsterDef._speed );
                monsterDef._invincibility = statsNode.getAttributeFloat( "invincibility", monsterDef._invincibility );
            }

            XmlNode aiNode = node.findChild( "AI" );
            if ( aiNode.isValid() )
            {
                monsterDef._patrolRange    = aiNode.getAttributeFloat( "patrolRange", monsterDef._patrolRange );
                monsterDef._detectRange    = aiNode.getAttributeFloat( "detectRange", monsterDef._detectRange );
                monsterDef._attackRange    = aiNode.getAttributeFloat( "attackRange", monsterDef._attackRange );
                monsterDef._attackCoolTime = aiNode.getAttributeFloat( "coolTime", monsterDef._attackCoolTime );
                const utf8* pProj          = aiNode.findAttribute( "projectilePrefab" );
                if ( pProj != nullptr )
                    monsterDef._projectilePrefab = pProj;
            }

            XmlNode prefabNode = node.findChild( "Prefab" );
            if ( prefabNode.isValid() )
            {
                const utf8* pPath = prefabNode.findAttribute( "path" );
                if ( pPath != nullptr )
                    monsterDef._prefabPath = pPath;
            }

            XmlNode dropNode = node.findChild( "Drop" );
            if ( dropNode.isValid() )
            {
                // 속성 이름이 곧 보상 이름이다 — 코드가 보상 종류를 알 필요가 없다.
                for ( XmlAttribute attr = dropNode.getFirstAttribute(); attr.isValid(); attr = attr.getNext() )
                {
                    const utf8* pRewardId = attr.getName();
                    if ( StringUtil::isNullOrEmpty( pRewardId ) )
                        continue;
                    monsterDef._mapDrop.insert_or_assign( hashed_string( pRewardId ),
                                                          dropNode.getAttributeInt( pRewardId, 0 ) );
                }
            }

            _mapMonster[hashed_string( monsterDef._id.c_str() )] = monsterDef;
        }

        // **읽었는데 하나도 없으면 실패다.** 위의 두 실패 길(파일 없음 · 루트 없음)은 폴백을
        // 심는데 여기만 안 심어서, `<Monster>` 를 `<monster>` 로 적은 오타 하나가 "0 개 로드"
        // 라는 밝은 Info 한 줄과 **텅 빈 카탈로그**가 됐다. 그러면 모든 `findMonster` 가
        // nullptr 이고, 그 널을 다루는 쪽이 조용히 아무것도 안 한다.
        if ( _mapMonster.empty() )
        {
            SW_LOG_WARNING( "No <Monster> entries in %# — using fallback monster definitions.", absPath );
            seedFallback();
            return false;
        }

        SW_LOG_INFO( "Loaded %# monster definitions from %#", static_cast<int32>( _mapMonster.size() ), absPath );
        return true;
    }

    const MonsterDef* MonsterDataCatalog::findMonster( const hashed_string& id ) const
    {
        auto mapIter = _mapMonster.find( id );
        if ( mapIter != _mapMonster.end() )
            return &mapIter->second;
        return nullptr;
    }

    const unordered_map<hashed_string, MonsterDef>& MonsterDataCatalog::getAllMonsters() const
    {
        return _mapMonster;
    }

    void MonsterDataCatalog::clear()
    {
        _mapMonster.clear();
    }

    MonsterArchetype MonsterDataCatalog::parseArchetype( const utf8* pStr )
    {
        if ( pStr == nullptr )
            return MonsterArchetype::MeleePatrol;

        if ( StringUtil::equals( pStr, "RangedShooter", true ) )
            return MonsterArchetype::RangedShooter;
        if ( StringUtil::equals( pStr, "FlyingPursuer", true ) )
            return MonsterArchetype::FlyingPursuer;
        if ( StringUtil::equals( pStr, "ChargerRush", true ) )
            return MonsterArchetype::ChargerRush;

        return MonsterArchetype::MeleePatrol;
    }
} // namespace sw
